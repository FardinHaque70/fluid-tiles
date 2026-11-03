// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect.h"
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <core/backendoutput.h>
#include <core/output.h>
#include <core/renderloop.h>
#include <input_event.h>
#include <window.h>
#include <workspace.h>
#include <virtualdesktops.h>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <QAction>
#include <QDBusConnection>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLoggingCategory>
#include <linux/input-event-codes.h>
#include <dlfcn.h>
#include <QStandardPaths>
#include <QFileInfo>
#include <core/renderviewport.h>

using namespace KWin;
Q_LOGGING_CATEGORY(fluidLog, "fluidtiles")
namespace {
QString idOf(EffectWindow *w) { return w->internalId().toString(QUuid::WithoutBraces); }
QJsonArray jsonRect(const QRectF &r) { return {r.x(), r.y(), r.width(), r.height()}; }
bool closeRect(const QRectF &a, const QRectF &b, double epsilon = .2) {
    return std::abs(a.x()-b.x()) < epsilon && std::abs(a.y()-b.y()) < epsilon
        && std::abs(a.width()-b.width()) < epsilon && std::abs(a.height()-b.height()) < epsilon;
}
QJsonObject distribution(const std::vector<double> &samples) {
    if (samples.empty()) return {{"samples", 0}};
    auto s = samples; std::sort(s.begin(), s.end());
    auto percentile = [&](double p) { return s[std::min(s.size()-1, size_t(p * (s.size()-1)))]; };
    return {{"samples", qint64(s.size())}, {"p50_ms", percentile(.5)}, {"p95_ms", percentile(.95)}, {"p99_ms", percentile(.99)}, {"max_ms", s.back()}};
}
void sample(std::vector<double> &out, double ms) {
    if (out.size() == 8192) out.erase(out.begin(), out.begin()+4096);
    out.push_back(ms);
}
}

class FluidInputFilter : public InputEventFilter {
public:
    explicit FluidInputFilter(FluidTiles *owner) : InputEventFilter(InputFilterOrder::Effects), m_owner(owner) {}
    bool pointerMotion(PointerMotionEvent *e) override { return m_owner->filterMotion(e); }
    bool pointerButton(PointerButtonEvent *e) override { return m_owner->filterButton(e); }
    bool keyboardKey(KeyboardKeyEvent *e) override { return m_owner->filterKey(e); }
private:
    FluidTiles *m_owner;
};
FluidTiles::FluidTiles() {
    m_test = qEnvironmentVariableIntValue("FLUIDTILES_TEST_MODE") == 1;
    m_clock.start();
    m_appearance=std::make_unique<Fluid::Appearance>();
    connect(m_appearance.get(),&Fluid::Appearance::changed,this,[this](bool geometry) {
        if (geometry && m_drag) endDrag(false);
        scheduleReconcile();
    });
    reconfigure(ReconfigureAll);
    m_enabled = KConfigGroup(effects->config(), "Effect-fluidtiles").readEntry("TilingEnabled", true);
    m_reconcile.setSingleShot(true);
    connect(&m_reconcile, &QTimer::timeout, this, &FluidTiles::reconcile);
    m_configReload.setSingleShot(true);
    m_configReload.setInterval(60);
    connect(&m_configReload,&QTimer::timeout,this,[this] {
        effects->config()->reparseConfiguration();
        reconfigure(ReconfigureAll);
        const auto file=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/kwinrc";
        if (QFileInfo::exists(file) && !m_configFiles.files().contains(file)) m_configFiles.addPath(file);
    });
    const auto configDirectory=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    m_configFiles.addPath(configDirectory);
    if (QFileInfo::exists(configDirectory+"/kwinrc")) m_configFiles.addPath(configDirectory+"/kwinrc");
    connect(&m_configFiles,&QFileSystemWatcher::fileChanged,this,[this] { m_configReload.start(); });
    connect(&m_configFiles,&QFileSystemWatcher::directoryChanged,this,[this] { m_configReload.start(); });
    connect(effects, &EffectsHandler::windowAdded, this, [this](EffectWindow *w) { watch(w,true); });
    connect(effects, &EffectsHandler::windowActivated, this, [this](EffectWindow *w) {
        m_appearance->focusChanged();
        if (!w) return;
        const auto it=m_windows.find(idOf(w));
        if (it!=m_windows.end() && it->second.tiled) m_lastFocus[it->second.group]=it->first;
    });
    connect(effects, &EffectsHandler::windowClosed, this, [this](EffectWindow *w) {
        const auto id = idOf(w);
        bool managed=false;
        if (auto it = m_windows.find(id); it != m_windows.end()) {
            managed=it->second.tiled;
            if (m_drag && (managed || m_drag->id==id)) endDrag(false);
            detach(it->second, false); m_windows.erase(it);
        }
        // Unmanaged docks can still change the work area when they close.
        // Reconcile geometry, but never cancel input for an unrelated popup.
        m_appearance->forget(w);m_drawDepth.remove(w);
        m_floating.remove(id); scheduleReconcile();
    });
    connect(effects, &EffectsHandler::windowDeleted, this, [this](EffectWindow *w) {
        const auto id = idOf(w);
        bool managed=false;
        if (auto it = m_windows.find(id); it != m_windows.end()) {
            // OffscreenEffect handles unredirecting a deleted window itself.
            managed=it->second.tiled;
            if (m_drag && (managed || m_drag->id==id)) endDrag(false);
            if (!it->second.group.isEmpty() && m_trees[it->second.group].remove(id)) m_pendingPairChecks.insert(it->second.group);
            m_windows.erase(it);
        }
        m_appearance->forget(w);m_drawDepth.remove(w);
        m_floating.remove(id); scheduleReconcile();
    });
    connect(effects, &EffectsHandler::desktopChanged, this, [this] { if (m_drag) endDrag(false); scheduleReconcile(); });
    connect(effects, &EffectsHandler::currentActivityChanged, this, [this] { if (m_drag) endDrag(false); scheduleReconcile(); });
    connect(effects, &EffectsHandler::screenAdded, this, &FluidTiles::watchOutput);
    connect(effects, &EffectsHandler::screenRemoved, this, [this] { if (m_drag) endDrag(false); scheduleReconcile(); });
    connect(workspace(), &Workspace::configChanged, this, [this] { reconfigure(ReconfigureAll); scheduleReconcile(); });
    connect(workspace(), &Workspace::outputsChanged, this, [this] { if (m_drag) endDrag(false); scheduleReconcile(); });
    connect(effects, &EffectsHandler::activeFullScreenEffectChanged, this, [this] {
        if (effects->activeFullScreenEffect() && m_drag) endDrag(false);
        effects->addRepaintFull();
    });
    for (auto output : effects->screens()) watchOutput(output);
    for (auto w : effects->stackingOrder()) watch(w);
    m_filter = std::make_unique<FluidInputFilter>(this);
    input()->installInputEventFilter(m_filter.get());
    const bool registered = QDBusConnection::sessionBus().registerObject("/org/kde/FluidTiles", this, QDBusConnection::ExportAllSlots);
    if (!registered) qCWarning(fluidLog) << "Could not register diagnostics D-Bus object";
    shortcut("toggle", "Toggle tiling", QKeySequence("Meta+Ctrl+T"));
    shortcut("disable", "Emergency disable and restore windows", QKeySequence("Meta+Ctrl+Shift+F12"));
    shortcut("float", "Toggle floating", QKeySequence("Meta+Shift+Space"));
    shortcut("split", "Rotate split", QKeySequence("Meta+Ctrl+Return"));
    const QStringList directions{"left", "down", "up", "right"};
    const QStringList keys{"H", "J", "K", "L"};
    for (int i=0; i<4; ++i) {
        shortcut("focus-"+directions[i], "Focus "+directions[i], QKeySequence("Meta+"+keys[i]));
        shortcut("swap-"+directions[i], "Swap "+directions[i], QKeySequence("Meta+Shift+"+keys[i]));
    }
    scheduleReconcile();
    qCInfo(fluidLog) << "Loaded native prototype; enabled:" << m_enabled << "test mode:" << m_test;
}
FluidTiles::~FluidTiles() {
    m_configReload.stop();
    m_reconcile.stop();
    m_filter.reset();
    QDBusConnection::sessionBus().unregisterObject("/org/kde/FluidTiles");
    restoreAll();
    effects->makeOpenGLContextCurrent();
    m_appearance.reset();
}
void FluidTiles::reconfigure(ReconfigureFlags) {
    KConfigGroup config(effects->config(), "Script-fluidtiles-controller");
    const double gap=std::clamp(config.readEntry("InnerGap",10.0),0.0,80.0);
    const double outer=std::clamp(config.readEntry("OuterGap",10.0),0.0,80.0);
    const double omega=std::clamp(config.readEntry("SpringFrequency",24.0),8.0,60.0);
    const auto applications=config.readEntry("IgnoreApplications",Fluid::defaultIgnoredApplications);
    const auto titles=config.readEntry("IgnoreTitles",QString{});
    const auto roles=config.readEntry("IgnoreRoles",Fluid::defaultIgnoredRoles);
    const bool changed=gap!=m_gap || outer!=m_outerGap || omega!=m_omega || applications!=m_ignoreApplicationText
        || titles!=m_ignoreTitleText || roles!=m_ignoreRoleText;
    const bool appearanceGeometry=m_appearance->configure(config);
    if (!changed && !appearanceGeometry) return;
    if (m_drag) endDrag(false);
    m_gap=gap;m_outerGap=outer;m_omega=omega;
    m_ignoreApplicationText=applications;m_ignoreTitleText=titles;m_ignoreRoleText=roles;
    m_ignoreApplications.setText(applications);m_ignoreTitles.setText(titles);m_ignoreRoles.setText(roles);
    if (m_clock.isValid()) scheduleReconcile();
}
void FluidTiles::shortcut(const QString &name, const QString &text, const QKeySequence &key) {
    auto a = new QAction("Fluid Tiles: "+text, this);
    a->setObjectName("FluidTiles-"+name);
    KGlobalAccel::self()->setDefaultShortcut(a, {key});
    KGlobalAccel::self()->setShortcut(a, {key});
    connect(a, &QAction::triggered, this, [this, name] { Action(name); });
}
bool FluidTiles::isActive() const { return m_enabled && !m_windows.empty(); }
void FluidTiles::scheduleReconcile() { if (!m_reconcile.isActive()) m_reconcile.start(0); }
void FluidTiles::watchOutput(LogicalOutput *o) {
    connect(o, &LogicalOutput::geometryChanged, this, &FluidTiles::scheduleReconcile);
    connect(o, &LogicalOutput::scaleChanged, this, &FluidTiles::scheduleReconcile);
    if (auto backend = o->backendOutput()) {
        const QString name = o->name();
        connect(backend->renderLoop(), &RenderLoop::framePresented, this,
            [this, name](RenderLoop *, std::chrono::nanoseconds time, PresentationMode) {
                auto &m = m_metrics[name];
                const auto ns = time.count();
                // Record only continuous motion, never count idle time as stutter.
                if (m_animating || m_drag) {
                    if (m.lastPresentation && ns > m.lastPresentation) sample(m.presentationIntervals, (ns-m.lastPresentation)/1e6);
                    m.lastPresentation = ns;
                } else m.lastPresentation = 0;
            });
    }
    scheduleReconcile();
}
bool FluidTiles::eligible(EffectWindow *w) const {
    if (!w || w->isDeleted() || !w->window()) return false;
    auto c = w->window();
    // Internal decoration tooltips can be normal + resizable + non-transient.
    // Test these independent flags before considering ordinary toplevels.
    if (c->isPopupWindow() || c->isInternal() || c->isModal() || c->isHidden() || c->pid()<0) return false;
    if (c->resourceClass()=="plasmashell") return false;
    if (c->resourceClass()=="org.fluidtiles.Settings" || c->resourceName()=="fluidtiles-settings") return false;
    if (c->output() && effects->screens().contains(c->output())) {
        const auto maximum=c->maxSize();
        const auto frame=c->frameGeometry(), client=c->clientGeometry();
        const auto area=effects->clientArea(MaximizeArea,w);
        // A bounded-size client must not be stretched indefinitely to fill a
        // tile it cannot acknowledge. Keep it native/floating instead.
        if (maximum.width()+frame.width()-client.width()<area.width()-2*m_outerGap
            || maximum.height()+frame.height()-client.height()<area.height()-2*m_outerGap) return false;
    }
    return c->isClient() && c->isNormalWindow() && !c->isSpecialWindow() && !c->isTransient()
        && c->isResizable() && c->readyForPainting() && !c->isMinimized()
        && !c->isFullScreen() && c->maximizeMode() == MaximizeRestore && !m_floating.contains(idOf(w)) && !ignored(w);
}
bool FluidTiles::ignored(EffectWindow *w) const {
    if (!w || !w->window()) return false;
    auto c=w->window();
    return m_ignoreApplications.matches(c->resourceClass()) || m_ignoreApplications.matches(c->resourceName())
        || m_ignoreTitles.matches(c->caption()) || m_ignoreRoles.matches(c->windowRole());
}
QString FluidTiles::groupFor(EffectWindow *w) const {
    auto c = w->window();
    if (!c->output() || !effects->screens().contains(c->output())) return {};
    const auto desks = c->desktops();
    const auto current = effects->currentDesktop(c->output());
    const auto desktop = desks.isEmpty() || desks.contains(current) ? current : desks.front();
    const auto activities=c->activities();
    const auto activity=activities.isEmpty() || activities.contains(effects->currentActivity())
        ? effects->currentActivity() : activities.front();
    return c->output()->uuid()+"/"+(desktop ? desktop->id() : "none")+"/"+activity;
}
QRectF FluidTiles::areaFor(EffectWindow *w) const {
    if (!w || !w->window() || !w->window()->output() || !effects->screens().contains(w->window()->output())) return {};
    QRectF area = effects->clientArea(MaximizeArea, w);
    const double scale = w->window()->output()->scale();
    const double gap = Fluid::Tree::snap(m_outerGap, scale);
    return area.adjusted(gap, gap, -gap, -gap);
}
QHash<QString, QSizeF> FluidTiles::minimums(const QString &group) const {
    QHash<QString, QSizeF> result;
    for (const auto &[id, state] : m_windows) {
        if (state.group != group || !state.effect || !state.effect->window()) continue;
        result[id] = m_appearance->minimumFrame(state.effect);
        const double inset=m_appearance->border(state.effect);
        result[id]+=QSizeF(2*inset,2*inset);
    }
    return result;
}
void FluidTiles::watch(EffectWindow *w, bool newlyAdded) {
    if (!w || !w->window() || w->isDeleted()) return;
    auto c = w->window();
    const auto id = idOf(w);
    if (m_windows.contains(id)) return;
    if (m_enabled && eligible(w)) w->setData(WindowAddedGrabRole,QVariant::fromValue(static_cast<void *>(this)));
    State state; state.id = id; state.effect = w; state.restore = c->frameGeometry(); state.motion.reset(state.restore);
    // Only a fresh visible map gets an entrance. Enabling with existing
    // clients, capacity recovery and native-state restoration are not opens.
    state.openingPending=newlyAdded && m_enabled && eligible(w) && !m_drag
        && w->isOnCurrentDesktop() && w->isOnCurrentActivity()
        && !effects->activeFullScreenEffect() && !effects->isScreenLocked()
        && !c->isInteractiveMove() && !c->isInteractiveResize();
    const auto pendingRestore=c->property("_fluidtilesRestore").toRectF();
    if (c->isMinimized() && pendingRestore.isValid()) {
        state.restore=pendingRestore;state.restoreValid=true;state.restorePending=true;
    }
    state.order=++m_nextOrder;
    if (auto active=effects->activeWindow(); active && active!=w) state.insertNear=idOf(active);
    m_windows.emplace(id, std::move(state));
    m_appearance->watch(w);
    connect(w,&EffectWindow::windowDamaged,this,[this,id] { auto it=m_windows.find(id);if(it!=m_windows.end())it->second.captureDirty=true; });
    if (c->isInternal() || c->isPopupWindow()) return;
    for (auto signal : {&Window::minimizedChanged, &Window::fullScreenChanged, &Window::desktopsChanged,
                        &Window::activitiesChanged, &Window::readyForPaintingChanged, &Window::hiddenChanged,
                        &Window::modalChanged})
        connect(c, signal, this, &FluidTiles::scheduleReconcile);
    connect(c, &Window::outputChanged, this, &FluidTiles::scheduleReconcile);
    connect(c, &Window::maximizedChanged, this, &FluidTiles::scheduleReconcile);
    connect(c, &Window::maximizeableChanged, this, &FluidTiles::scheduleReconcile);
    connect(c, &Window::transientChanged, this, &FluidTiles::scheduleReconcile);
    connect(c,&Window::captionChanged,this,[this] { if (!m_ignoreTitles.empty()) scheduleReconcile(); });
    connect(c,&Window::windowClassChanged,this,&FluidTiles::scheduleReconcile);
    connect(c,&Window::windowRoleChanged,this,[this] { if (!m_ignoreRoles.empty()) scheduleReconcile(); });
    auto layerChanged=[this,id] {
        auto it=m_windows.find(id);
        if (it==m_windows.end() || it->second.layerChanging) return;
        auto &s=it->second;
        if (s.layerOwned && s.effect && s.effect->window()) {
            // Remember explicit user/application changes made while our
            // temporary tiling layer is in force, not our own writes.
            s.originalAbove=s.effect->window()->keepAbove();
            s.originalBelow=s.effect->window()->keepBelow();
        }
        scheduleReconcile();
    };
    connect(c,&Window::keepAboveChanged,this,layerChanged);
    connect(c,&Window::keepBelowChanged,this,layerChanged);
    connect(c, &Window::frameGeometryChanged, this, [this, id] {
        auto it = m_windows.find(id); if (it == m_windows.end()) return;
        ++it->second.commits;it->second.captureDirty=true;
        if (it->second.effect && it->second.effect->window()->isDock()) scheduleReconcile();
        auto &s=it->second;
        auto c=s.effect->window();
        const double tolerance=c->output()?1.1/c->output()->scale():1.1;
        if (s.restorePending && (closeRect(c->frameGeometry(),s.restore,tolerance) || c->isInteractiveMove())) s.restorePending=false;
        if (!s.tiled && s.restorePending) scheduleReconcile();
        if ((!m_enabled || ignored(s.effect)) && !s.restorePending && !c->isMinimized() && !c->isFullScreen()
            && c->maximizeMode()==MaximizeRestore) c->setProperty("_fluidtilesRestore",QVariant());
        if (m_drag && m_drag->id==id && m_drag->nativeResize && m_drag->freeResize)
            s.motion.reset(c->frameGeometry());
        if (s.tiled) {
            effects->addRepaint(RectF(s.motion.rect().adjusted(-32,-32,32,32)));
        }
    });
    // Let KWin own titlebar/CSD dragging, while retaining the layout slot. This
    // preserves native controls, pointer grabs and Escape cancellation.
    connect(c, &Window::interactiveMoveResizeStarted, this, [this, id, c] {
        auto it = m_windows.find(id); if (it == m_windows.end()) return;
        if (!it->second.tiled) {
            cancelFloatingTransition(it->second);
            it->second.restorePending=false;it->second.restoreValid=false;
            c->setProperty("_fluidtilesRestore",QVariant());return;
        }
        if ((!c->isInteractiveMove() && !c->isInteractiveResize()) || !m_enabled) return;
        if (m_drag) endDrag(false);
        Drag d; d.id=id; d.group=it->second.group; d.initial=it->second.motion.rect();
        d.generation=++m_nextDrag;
        d.origin=d.last=Cursors::self()->mouse()->pos();
        d.moving=c->isInteractiveMove(); d.nativeMove=d.moving; d.nativeResize=!d.moving; d.button=Qt::LeftButton;
        if (d.nativeResize) {
            const auto gravity=c->interactiveMoveResizeGravity();
            const bool left=gravity==Gravity::Left || gravity==Gravity::TopLeft || gravity==Gravity::BottomLeft;
            const bool right=gravity==Gravity::Right || gravity==Gravity::TopRight || gravity==Gravity::BottomRight;
            const bool top=gravity==Gravity::Top || gravity==Gravity::TopLeft || gravity==Gravity::TopRight;
            const bool bottom=gravity==Gravity::Bottom || gravity==Gravity::BottomLeft || gravity==Gravity::BottomRight;
            auto &tree=m_trees[d.group];
            if (left || right) d.horizontal=tree.edgeSplit(id,Fluid::Axis::Horizontal,right);
            if (top || bottom) d.vertical=tree.edgeSplit(id,Fluid::Axis::Vertical,bottom);
            d.freeResize=tree.root && tree.root->leaf();
            rememberSplitRatios(d);
        }
        m_drag=d;
        syncLayers();
        ResetMetrics(); effects->addRepaintFull();
    });
    connect(c, &Window::interactiveMoveResizeFinished, this, [this,id] {
        if (!m_drag || (!m_drag->nativeMove && !m_drag->nativeResize) || m_drag->id!=id) return;
        const auto generation=m_drag->generation;
        QTimer::singleShot(0,this,[this,id,generation] {
            if (m_drag && m_drag->id==id && m_drag->generation==generation) endDrag();
        });
    });
    scheduleReconcile();
}
void FluidTiles::detach(State &s, bool restore) {
    s.openingPending=false;
    if (!s.tiled) { cancelFloatingTransition(s); return; }
    if (m_drag) m_drag.reset();
    m_trees[s.group].remove(s.id);
    m_pendingPairChecks.insert(s.group);
    s.tiled = false; s.group.clear();
    restoreLayer(s);
    m_appearance->sync(s.effect,false);
    m_appearance->cancelPendingFrame(s.effect);
    if (s.effect && s.effect->data(WindowAddedGrabRole).value<void *>()==this) s.effect->setData(WindowAddedGrabRole,QVariant());
    if (s.redirected && s.effect && !s.effect->isDeleted()) { effects->makeOpenGLContextCurrent(); unredirect(s.effect); }
    s.redirected = false;
    if (restore && s.effect && !s.effect->isDeleted() && s.effect->window()) {
        s.restorePending=true;
        s.effect->window()->moveResize(RectF(s.restore));
    }
}
void FluidTiles::cancelFloatingTransition(State &s) {
    if (!s.floatingTransition) return;
    s.floatingTransition=false;
    if (s.redirected && s.effect && !s.effect->isDeleted()) {
        effects->makeOpenGLContextCurrent();unredirect(s.effect);
    }
    s.redirected=false;
    if (s.effect && !s.effect->isDeleted()) {
        s.motion.reset(s.effect->frameGeometry());s.target=s.effect->frameGeometry();
    }
    effects->addRepaintFull();
}
void FluidTiles::floatToCenter(State &s) {
    auto c=s.effect->window();
    const QRectF visual=presentation(s);
    // Preserve velocity if the shortcut interrupts another layout animation.
    auto motion=s.motion;
    if (!motion.moving()) motion.reset(visual);
    const QSizeF preferred=s.restoreValid?s.restore.size():c->frameGeometry().size();
    const QRectF area=effects->clientArea(MaximizeArea,s.effect);
    if (!c->output() || !effects->screens().contains(c->output()) || area.isEmpty()) { detach(s,true);return; }
    detach(s,false); // restore supported titlebars before calculating frame hints
    QSizeF size=c->constrainFrameSize(QSizeF(std::min(preferred.width(),area.width()),std::min(preferred.height(),area.height())));
    const double scale=c->output()->scale();
    const QPointF position=area.center()-QPointF(size.width()/2,size.height()/2);
    s.target=QRectF(QPointF(Fluid::Tree::snap(position.x(),scale),Fluid::Tree::snap(position.y(),scale)),size);
    s.restore=s.target;s.restorePending=true;s.motion=motion;
    if (effects->animationTimeFactor()==0) s.motion.reset(s.target);
    else s.motion.target(s.target);
    s.floatingTransition=true;s.captureDirty=true;
    c->moveResize(RectF(s.target));
    syncLayers();
    // Floating clients are in the normal layer; keep the selected one above
    // other normal floats as well as the tiles it just left.
    workspace()->raiseWindow(c);
    if (!m_animating) m_lastStep=m_clock.nsecsElapsed();
    m_animating=true;effects->addRepaintFull();
}
void FluidTiles::restoreAll() {
    m_enabled=false;
    m_appearance->setEnabled(false);
    if (m_drag) endDrag(false);
    for (auto &[id, s] : m_windows) {
        m_appearance->cancelPendingFrame(s.effect);
        detach(s, true);
        restoreLayer(s);
        // Native states retain their appearance. Restore the geometry KWin
        // will use when leaving fullscreen/maximized, including after unload.
        if (s.restoreValid && s.effect && !s.effect->isDeleted()) {
            auto c=s.effect->window();
            if (c->isFullScreen()) c->setFullscreenGeometryRestore(RectF(s.restore));
            c->setGeometryRestore(RectF(s.restore));
            if (c->isMinimized() && !c->isFullScreen() && c->maximizeMode()==MaximizeRestore) c->moveResize(RectF(s.restore));
            const double tolerance=c->output()?1.1/c->output()->scale():1.1;
            s.restorePending=!closeRect(c->frameGeometry(),s.restore,tolerance);
        }
        s.restoreValid=false; s.capacityBlocked=false;
    }
    m_trees.clear();m_pendingPairChecks.clear(); m_lastFocus.clear(); m_animating = false;
    effects->addRepaintFull();
}
void FluidTiles::restoreLayer(State &s) {
    if (!s.layerOwned) return;
    s.layerChanging=true;
    if (s.effect && !s.effect->isDeleted() && s.effect->window()) {
        auto c=s.effect->window();
        c->setKeepBelow(s.originalBelow);
        c->setKeepAbove(s.originalAbove);
    }
    s.layerOwned=false;s.layerChanging=false;
}
void FluidTiles::syncLayers() {
    for (auto &[id,s] : m_windows) {
        if (!s.effect || s.effect->isDeleted() || !s.effect->window()) continue;
        auto c=s.effect->window();
        // Shell, popup and internal surfaces retain KWin's native layering.
        // Fullscreen/maximized clients also keep their native state policy.
        const bool ordinary=c->isClient() && !c->isSpecialWindow() && !c->isInternal()
            && !c->isPopupWindow() && (c->isNormalWindow() || c->isDialog() || c->isUtility())
            && !c->isFullScreen() && c->maximizeMode()==MaximizeRestore && !c->isMinimized();
        if (!m_enabled || !ordinary) { restoreLayer(s); continue; }
        const bool below=s.tiled && !(m_drag && m_drag->moving && m_drag->id==id);
        if (!s.layerOwned) {
            if (!below && !c->keepBelow()) continue;
            s.originalAbove=c->keepAbove();s.originalBelow=c->keepBelow();s.layerOwned=true;
        }
        const bool above=!below && s.originalAbove;
        s.layerChanging=true;
        c->setKeepBelow(below);
        c->setKeepAbove(above);
        s.layerChanging=false;
        // A normal float with no original keep-below preference needs no
        // ongoing ownership; tiles and temporarily normalized floats do.
        if (!below && !s.originalBelow) s.layerOwned=false;
    }
}
void FluidTiles::reconcile() {
    m_appearance->setEnabled(m_enabled);
    for (auto &[id,s] : m_windows) {
        const bool willTile=s.tiled || (m_enabled && eligible(s.effect));
        // Capture the original native floating frame before removing its SSD.
        if (!s.tiled && willTile && !s.restoreValid && s.effect && !s.restorePending) {
            s.restore=s.effect->frameGeometry();s.restoreValid=true;
        }
        m_appearance->sync(s.effect,s.tiled);
        if (willTile) m_appearance->cancelPendingFrame(s.effect);
    }
    // Restore temporarily unmanaged clients too, including newly ignored and
    // explicitly floated windows whose previous buffer can arrive late.
    for (auto &[id,s] : m_windows) if (!s.tiled && s.restorePending && s.effect && !s.effect->isDeleted()) {
            auto c=s.effect->window();
            if (c->isFullScreen() || c->isMinimized() || c->maximizeMode()!=MaximizeRestore
                || c->isInteractiveMove() || c->isInteractiveResize()) continue;
            const double tolerance=c->output()?1.1/c->output()->scale():1.1;
            if (closeRect(c->frameGeometry(),s.restore,tolerance)) s.restorePending=false;
            else if (!closeRect(c->moveResizeGeometry(),s.restore,tolerance)) c->moveResize(RectF(s.restore));
    }
    if (!m_enabled) return;
    // Change tree topology only outside a drag, whose split pointers must stay valid.
    if (m_drag) { syncLayers(); return; }
    QSet<QString> changed;
    // Detach first, then insert in stable observation order. UUID ordering is
    // random and must not decide a layout when enabling with existing windows.
    for (auto &[id, s] : m_windows) {
        const bool ok = eligible(s.effect);
        const QString group = ok ? groupFor(s.effect) : QString{};
        const bool rule=ignored(s.effect);
        if (s.tiled && (!ok || group != s.group)) {
            changed.insert(s.group);
            detach(s,rule);
        }
        if (rule && s.restoreValid && s.effect && !s.effect->isDeleted()) {
            auto c=s.effect->window();
            c->setGeometryRestore(RectF(s.restore));
            if (c->isFullScreen()) c->setFullscreenGeometryRestore(RectF(s.restore));
            if (!s.restorePending && !c->isFullScreen() && c->maximizeMode()==MaximizeRestore)
                c->moveResize(RectF(s.restore));
            s.restorePending=true;s.restoreValid=false;
        }
    }
    std::vector<State *> ordered;
    QHash<QString,QString> lastInserted;
    for (auto &[id,s] : m_windows) ordered.push_back(&s);
    std::sort(ordered.begin(),ordered.end(),[](auto a,auto b) { return a->order<b->order; });
    // A work-area/scale reduction can make an already valid tree infeasible.
    // Release newest tiles until it fits, keeping these exclusions temporary.
    for (auto &[group,tree] : m_trees) {
        for (;;) {
            State *newest=nullptr;
            for (auto state : ordered) if (state->tiled && state->group==group) newest=state;
            if (!newest) break;
            const auto area=areaFor(newest->effect);
            if (area.isEmpty()) break;
            const double gap=Fluid::Tree::snap(m_gap,newest->effect->window()->output()->scale());
            applyPairPreference(group,newest->effect);
            const auto min=tree.minimum(minimums(group),gap,newest->effect->window()->output()->scale());
            if (min.width()<=area.width() && min.height()<=area.height()) break;
            detach(*newest,true);newest->capacityBlocked=true;changed.insert(group);
        }
    }
    for (auto state : ordered) {
        auto &s=*state;
        const auto &id=s.id;
        const bool ok=eligible(s.effect);
        const QString group=ok?groupFor(s.effect):QString{};
        if (!ok || group.isEmpty()) { s.openingPending=false;continue; }
        if (!s.tiled) {
            auto c = s.effect->window();
            if (!s.restoreValid) {
                if (!s.restorePending) s.restore=c->frameGeometry();
                s.restoreValid=true;
            }
            c->setProperty("_fluidtilesRestore",s.restore);
            if (!s.floatingTransition) s.motion.reset(c->frameGeometry());
            s.lastRequest = {};
            s.group = group; s.tiled = true;
            s.effect->setData(WindowAddedGrabRole,QVariant::fromValue(static_cast<void *>(this)));
            c->setQuickTileModeAtCurrentPosition(QuickTileMode());
            auto &tree = m_trees[group];
            const double scale = c->output()->scale();
            const double gap=Fluid::Tree::snap(m_gap,scale);
            QString near=s.insertNear;
            if (!tree.find(near)) near=lastInserted.value(group);
            if (!tree.find(near)) near=m_lastFocus.value(group);
            if (!tree.find(near)) {
                if (auto hovered=at(Cursors::self()->mouse()->pos()); hovered && hovered->group==group) near=hovered->id;
            }
            s.insertNear.clear();
            const auto area = areaFor(s.effect);
            if (!tree.insertFitting(id, near, area, gap, scale, minimums(group))) {
                s.openingPending=false;
                s.tiled = false; s.group.clear(); s.capacityBlocked=true;
                if (s.effect->data(WindowAddedGrabRole).value<void *>()==this) s.effect->setData(WindowAddedGrabRole,QVariant());
                qCInfo(fluidLog) << "Floated a window because minimum sizes cannot fit"; continue;
            }
            s.floatingTransition=false;
            m_pendingPairChecks.insert(group);
            m_appearance->sync(s.effect,true);
            m_appearance->cancelPendingFrame(s.effect);
            s.capacityBlocked=false;
            lastInserted[group]=id;
            if (s.effect==effects->activeWindow()) m_lastFocus[group]=id;
        }
        changed.insert(group);
    }
    for (const auto &group : changed) layoutGroup(group);
    syncLayers();
    effects->addRepaintFull();
}
void FluidTiles::applyPairPreference(const QString &group, EffectWindow *w) {
    if (!m_pendingPairChecks.contains(group) || m_drag || !w || !w->window()) return;
    auto output=w->window()->output();
    if (!output || !effects->screens().contains(output)) return;
    const auto tree=m_trees.find(group);if (tree==m_trees.end()) { m_pendingPairChecks.remove(group);return; }
    const auto area=areaFor(w);if(area.isEmpty())return;
    tree->second.preferSideBySidePair(area,Fluid::Tree::snap(m_gap,output->scale()),output->scale(),minimums(group));
    m_pendingPairChecks.remove(group);
}
void FluidTiles::layoutGroup(const QString &group, bool immediate) {
    State *representative = nullptr;
    for (auto &[id,s] : m_windows) if (s.tiled && s.group == group && s.effect) { representative=&s; break; }
    if (!representative) return;
    auto output = representative->effect->window()->output();
    if (!output || !effects->screens().contains(output)) return;
    const double scale = output->scale();
    const double gap = Fluid::Tree::snap(m_gap, scale);
    applyPairPreference(group,representative->effect);
    const auto layout = m_trees[group].layout(areaFor(representative->effect), gap, scale, minimums(group));
    for (auto it = layout.cbegin(); it != layout.cend(); ++it) {
        auto &s = m_windows.at(it.key());
        s.target = Fluid::PresentationGeometry::clientFrame(it.value(),m_appearance->border(s.effect));
        if (s.openingPending) {
            s.openingPending=false;
            if (!immediate && effects->animationTimeFactor()!=0 && !m_drag
                && s.effect->isOnCurrentDesktop() && s.effect->isOnCurrentActivity()
                && !effects->activeFullScreenEffect() && !effects->isScreenLocked()
                && !s.effect->window()->isInteractiveMove() && !s.effect->window()->isInteractiveResize()) {
                // Commit the final native tile as usual; only presentation
                // starts smaller. This also works when the first client
                // remembers a frame already as large as the work area.
                const QRectF area=effects->clientArea(MaximizeArea,s.effect);
                const QSizeF size=s.target.size()*.65;
                s.openingFrom=QRectF(area.center()-QPointF(size.width()/2,size.height()/2),size);
                s.motion.reset(s.openingFrom);
            }
        }
        if (immediate || effects->animationTimeFactor() == 0) s.motion.reset(s.target);
        else s.motion.target(s.target);
    }
    if (!m_animating) m_lastStep = m_clock.nsecsElapsed();
    m_animating = true;
    effects->addRepaintFull();
}
void FluidTiles::flushGeometry() {
    for (auto &[id,s] : m_windows) {
        if (!s.tiled || !s.effect || s.effect->isDeleted() || !s.effect->isOnCurrentDesktop() || !s.effect->isOnCurrentActivity()) continue;
        if (m_drag && (m_drag->nativeMove || m_drag->nativeResize) && m_drag->id==id) continue;
        const auto c=s.effect->window();
        const double tolerance=c->output()?1.1/c->output()->scale():1.1;
        // KWin can change its requested rectangle after emitting the geometry
        // signal for an old client acknowledgement. Check here, after that
        // event finishes; a cache of what we last sent is not authoritative.
        const QRectF request=m_drag && m_drag->freeResize && m_drag->id==id?s.motion.rect():s.target;
        if (closeRect(s.lastRequest, request) && closeRect(c->moveResizeGeometry(),request,tolerance)) continue;
        s.lastRequest = request; ++s.requests;
        s.effect->window()->moveResize(RectF(request));
    }
}
QRectF FluidTiles::presentation(const State &s) const {
    if (!s.effect) return {};
    // Desktop effects supply the transform in their window paint pass. Own
    // tiling transforms are suspended there, so outlines follow native bounds.
    if (effects->activeFullScreenEffect()) return s.effect->frameGeometry();
    // At rest the client can round a configure by a physical pixel. Match its
    // actual edge instead of drawing a one-pixel gap inside the outline.
    return (s.tiled || s.floatingTransition) && (s.motion.moving() || s.redirected || (m_drag && m_drag->id==s.id))
        ?s.motion.rect():QRectF(s.effect->frameGeometry());
}
void FluidTiles::prePaintScreen(ScreenPrePaintData &data) {
    const qint64 now = m_clock.nsecsElapsed();
    const double dt = m_lastStep ? (now-m_lastStep)/1e9 : 0;
    m_lastStep = now;
    if (effects->isScreenLocked() && m_drag) endDrag(false);
    bool moving = false;
    for (auto &[id,s] : m_windows) if (s.tiled || s.floatingTransition) {
        if (s.floatingTransition && (!m_enabled || !s.effect || s.effect->isDeleted()
            || s.effect->window()->isMinimized() || s.effect->window()->isFullScreen()
            || s.effect->window()->maximizeMode()!=MaximizeRestore || s.effect->window()->isHidden()
            || !s.effect->isOnCurrentDesktop() || !s.effect->isOnCurrentActivity()
            || effects->activeFullScreenEffect() || effects->isScreenLocked())) {
            cancelFloatingTransition(s);continue;
        }
        if (m_drag && (m_drag->nativeMove || (m_drag->nativeResize && m_drag->freeResize)) && m_drag->id==id && s.effect)
            s.motion.reset(s.effect->frameGeometry());
        else s.motion.step(dt, m_omega / std::max(.1, effects->animationTimeFactor()));
        if (s.effect && s.effect->isOnCurrentDesktop() && s.effect->isOnCurrentActivity()) moving |= s.motion.moving();
    }
    m_animating = moving || m_drag.has_value();
    flushGeometry();
    for (auto &[id,s] : m_windows) if (s.effect && !s.effect->isDeleted()) {
        m_appearance->damage(s.effect,presentation(s));
    }
    for (auto &[id,s] : m_windows) if ((s.tiled || s.floatingTransition) && s.effect && !s.effect->isDeleted()) {
        const bool visible = s.effect->isOnCurrentDesktop() && s.effect->isOnCurrentActivity();
        // Wayland clients can round their content size by a physical pixel.
        // Stop redirecting at rest so stationary text stays sharp and ordinary
        // applications/video don't pay for an unnecessary offscreen copy.
        const double tolerance = s.motion.moving() || m_drag ? .01 : 1.1/s.effect->window()->output()->scale();
        const bool needsTransform = visible && !effects->activeFullScreenEffect()
            && !closeRect(s.motion.rect(), s.effect->frameGeometry(), tolerance);
        if (needsTransform && s.redirected && s.snapshotRevision!=m_appearance->sourceRevision()) {
            unredirect(s.effect);s.redirected=false;
        }
        if (needsTransform && !s.redirected) {
            redirect(s.effect);s.redirected=true;s.captureDirty=true;s.snapshotRevision=m_appearance->sourceRevision();
        }
        else if (!needsTransform && s.redirected) { unredirect(s.effect); s.redirected = false; }
        if (s.floatingTransition && !s.motion.moving() && !needsTransform) s.floatingTransition=false;
    }
    if (m_animating) {
        data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
        if (data.screen) {
            auto &m = m_metrics[data.screen->name()]; ++m.paints;
            if (m.lastPaint) sample(m.paintIntervals, (now-m.lastPaint)/1e6);
            m.lastPaint = now;
        }
    } else for (auto &[name,m] : m_metrics) m.lastPaint = 0;
    effects->prePaintScreen(data);
}
void FluidTiles::prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) {
    auto it = m_windows.find(idOf(w));
    if (it != m_windows.end() && it->second.redirected) data.setTransformed();
    effects->prePaintWindow(view, w, data);
}
void FluidTiles::apply(EffectWindow *w, int, WindowPaintData &data, WindowQuadList &) {
    if (effects->activeFullScreenEffect()) return;
    auto it = m_windows.find(idOf(w));
    if (it == m_windows.end() || (!it->second.tiled && !it->second.floatingTransition)) return;
    const QRectF actual = w->frameGeometry(), visual = it->second.motion.rect();
    if (actual.width() <= 0 || actual.height() <= 0) return;
    data *= QVector2D(visual.width()/actual.width(), visual.height()/actual.height());
    data += visual.topLeft()-actual.topLeft();
}
bool FluidTiles::drawWindow(const RenderTarget &target,const RenderViewport &viewport,EffectWindow *w,int mask,
                            const Region &region,WindowPaintData &data) {
    const int depth=m_drawDepth.value(w);
    m_drawDepth[w]=depth+1;
    const auto incoming=data;
    const auto it=m_windows.find(idOf(w));
    const QRectF visual=it!=m_windows.end()?presentation(it->second):QRectF(w->frameGeometry());
    const bool redirected=it!=m_windows.end() && it->second.redirected;
    KWin::BorderRadius saved;
    if (!depth && redirected) setShader(w,m_appearance->clipShader(w,visual,viewport.scale()));
    // effects->drawWindow advances its iterator before calling this effect.
    // Its offscreen capture normally continues with the next effect rather
    // than re-entering this wrapper. Scope the source bypass around the base
    // call itself; depth also protects captures initiated by other effects.
    const bool sourceChanged=redirected && (it->second.captureDirty || it->second.snapshotBounds!=QRectF(w->expandedGeometry()));
    const bool captureScope=sourceChanged || depth>0;
    if (captureScope) saved=m_appearance->beginCapture(w);
    const bool result=OffscreenEffect::drawWindow(target,viewport,w,mask,region,data);
    if (captureScope) m_appearance->endCapture(w,saved);
    if (!depth && redirected && result) {
        it->second.captureDirty=false;it->second.snapshotBounds=w->expandedGeometry();
    }
    m_drawDepth[w]=depth;
    if (!depth) m_drawDepth.remove(w);
    if (!result || depth>0) return result;
    return m_appearance->drawOutline(target,viewport,w,mask,region,incoming,visual);
}
bool FluidTiles::blocksDirectScanout() const {
    return OffscreenEffect::blocksDirectScanout() || m_appearance->needsCompositing();
}
void FluidTiles::postPaintScreen() {
    effects->postPaintScreen();
    if (m_animating) {
        // Fullscreen damage is intentional in the first prototype: prioritize
        // correct fractional-scale damage over prematurely optimizing it.
        effects->addRepaintFull();
    }
}
bool FluidTiles::floatingAbove(const QPointF &point) const {
    const auto order=effects->stackingOrder();
    for (auto it=order.crbegin();it!=order.crend();++it) {
        auto w=*it;
        if (!w || w->isDeleted() || !w->window() || !w->window()->isClient() || w->window()->isMinimized()
            || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity() || !w->frameGeometry().contains(point)) continue;
        const auto state=m_windows.find(idOf(w));
        return state==m_windows.end() || !state->second.tiled;
    }
    return false;
}
FluidTiles::State *FluidTiles::at(const QPointF &point) {
    if (floatingAbove(point)) return nullptr;
    const auto order = effects->stackingOrder();
    for (auto it = order.crbegin(); it != order.crend(); ++it) {
        auto found = m_windows.find(idOf(*it));
        if (found != m_windows.end() && found->second.tiled && (*it)->isOnCurrentDesktop()
            && (*it)->isOnCurrentActivity() && found->second.motion.rect().contains(point)) return &found->second;
    }
    return nullptr;
}
FluidTiles::State *FluidTiles::active() {
    auto w = effects->activeWindow(); if (!w) return nullptr;
    auto it = m_windows.find(idOf(w)); return it == m_windows.end() ? nullptr : &it->second;
}
bool FluidTiles::beginDrag(const QPointF &point, bool moving) {
    if (!m_enabled || effects->isScreenLocked() || effects->activeFullScreenEffect()) return false;
    auto s = at(point); if (!s) return false;
    Drag d; d.id = s->id; d.group = s->group; d.origin = d.last = point; d.initial = s->motion.rect(); d.moving = moving;
    if (!moving) {
        auto &tree = m_trees[d.group];
        d.horizontal = tree.edgeSplit(s->id, Fluid::Axis::Horizontal, point.x() >= d.initial.center().x());
        d.vertical = tree.edgeSplit(s->id, Fluid::Axis::Vertical, point.y() >= d.initial.center().y());
        // If an outer edge is selected, resize the available internal split.
        if (!d.horizontal) d.horizontal = tree.edgeSplit(s->id, Fluid::Axis::Horizontal, point.x() < d.initial.center().x());
        if (!d.vertical) d.vertical = tree.edgeSplit(s->id, Fluid::Axis::Vertical, point.y() < d.initial.center().y());
        if (!d.horizontal && !d.vertical) {
            if (!tree.root || !tree.root->leaf()) return false;
            d.freeResize=true;d.resizeLeft=point.x()<d.initial.center().x();d.resizeTop=point.y()<d.initial.center().y();
        }
        rememberSplitRatios(d);
    }
    m_drag = d;
    syncLayers();
    effects->activateWindow(s->effect);
    if (!d.freeResize) layoutGroup(d.group, true);
    else { s->motion.reset(d.initial);s->lastRequest={};effects->addRepaintFull(); }
    if (moving) s->effect->elevate(true);
    ResetMetrics();
    return true;
}
bool FluidTiles::beginDividerDrag(const QPointF &point) {
    if (!m_enabled || effects->isScreenLocked() || effects->activeFullScreenEffect()) return false;
    if (floatingAbove(point)) return false;
    for (auto &[id,s] : m_windows) {
        if (!s.tiled || !s.effect || !s.effect->isOnCurrentDesktop() || !s.effect->isOnCurrentActivity()) continue;
        auto &tree = m_trees[s.group];
        Fluid::Node *horizontal = nullptr, *vertical = nullptr;
        double hx = 7, vy = 7;
        const double gap = Fluid::Tree::snap(m_gap, s.effect->window()->output()->scale());
        auto visit = [&](auto &&self, Fluid::Node *n) -> void {
            if (!n || n->leaf() || !n->box.adjusted(-6,-6,6,6).contains(point)) return;
            const bool h = n->axis == Fluid::Axis::Horizontal;
            const double divider = h ? n->first->box.right()+gap/2 : n->first->box.bottom()+gap/2;
            const double distance = std::abs((h ? point.x() : point.y())-divider);
            if (h && distance<hx) { hx=distance; horizontal=n; }
            if (!h && distance<vy) { vy=distance; vertical=n; }
            self(self,n->first.get()); self(self,n->second.get());
        };
        visit(visit, tree.root.get());
        if (!horizontal && !vertical) continue;
        Drag d; d.id=id; d.group=s.group; d.origin=d.last=point;
        d.horizontal=horizontal; d.vertical=vertical; d.button=Qt::LeftButton;
        rememberSplitRatios(d);
        m_drag=d; layoutGroup(d.group,true); ResetMetrics(); return true;
    }
    return false;
}
void FluidTiles::rememberSplitRatios(Drag &d) {
    auto it=m_windows.find(d.id);
    if (it==m_windows.end() || !it->second.effect) return;
    const double gap=Fluid::Tree::snap(m_gap,it->second.effect->window()->output()->scale());
    if (d.horizontal) { d.horizontalStart=d.horizontal->first->box.right()+gap/2; d.horizontalRatio=d.horizontal->ratio; }
    if (d.vertical) { d.verticalStart=d.vertical->first->box.bottom()+gap/2; d.verticalRatio=d.vertical->ratio; }
}
void FluidTiles::updateDrag(const QPointF &point) {
    if (!m_drag) return;
    auto &d = *m_drag;
    if (d.moving) {
        // The pointer is grabbed by this filter, so the visual window can move
        // freely above the reserved tile without moving the client's hit region.
        auto it=m_windows.find(d.id);
        if (it!=m_windows.end()) it->second.motion.reset(d.initial.translated(point-d.origin));
        effects->addRepaintFull();
    } else if (d.freeResize) {
        if (!d.nativeResize) {
            auto it=m_windows.find(d.id);
            if (it!=m_windows.end() && it->second.effect && !it->second.effect->isDeleted()) {
                auto &s=it->second;auto c=s.effect->window();
                const QPointF delta=point-d.origin;
                QSizeF size=c->constrainFrameSize(QSizeF(std::max(1.0,d.initial.width()+(d.resizeLeft?-delta.x():delta.x())),
                    std::max(1.0,d.initial.height()+(d.resizeTop?-delta.y():delta.y()))));
                const double scale=c->output()->scale();
                size={Fluid::Tree::snap(size.width(),scale),Fluid::Tree::snap(size.height(),scale)};
                const QPointF origin(d.resizeLeft?d.initial.right()-size.width():d.initial.x(),
                    d.resizeTop?d.initial.bottom()-size.height():d.initial.y());
                s.motion.reset(QRectF(origin,size));effects->addRepaintFull();
            }
        }
        // Native singleton resizes belong entirely to KWin while held. The
        // normal tile target is retained and becomes the spring goal on release.
    } else {
        const auto mins = minimums(d.group);
        const double gap = Fluid::Tree::snap(m_gap, m_windows.at(d.id).effect->window()->output()->scale());
        auto &tree = m_trees[d.group];
        // Absolute delta preserves subpixel motion and avoids rounding drift.
        if (d.horizontal) tree.resize(d.horizontal, d.horizontalStart+point.x()-d.origin.x(), gap, mins);
        if (d.vertical) tree.resize(d.vertical, d.verticalStart+point.y()-d.origin.y(), gap, mins);
        layoutGroup(d.group, true);
    }
    d.last = point;
}
void FluidTiles::endDrag(bool commit) {
    if (!m_drag) return;
    const Drag drag=*m_drag;
    m_drag.reset(); // Native cancellation emits a nested finish signal.
    if ((!commit || drag.cancelled) && !drag.moving) {
        if (drag.horizontal) drag.horizontal->ratio=drag.horizontalRatio;
        if (drag.vertical) drag.vertical->ratio=drag.verticalRatio;
    }
    if (drag.moving && !drag.nativeMove) {
        auto it=m_windows.find(drag.id);
        if (it!=m_windows.end() && it->second.effect && !it->second.effect->isDeleted()) it->second.effect->elevate(false);
    }
    if (drag.nativeMove || drag.nativeResize) {
        auto it=m_windows.find(drag.id);
        if (it!=m_windows.end() && it->second.effect && !it->second.effect->isDeleted()) {
            auto c=it->second.effect->window();
            if (c->isInteractiveMove() || c->isInteractiveResize()) c->cancelInteractiveMoveResize();
            if (drag.nativeMove) {
                c->setQuickTileModeAtCurrentPosition(QuickTileMode());
                if (c->maximizeMode()!=MaximizeRestore) c->setMaximize(false,false);
            }
            it->second.motion.reset(c->frameGeometry());
            it->second.lastRequest={}; // Also snap back after dropping in empty space.
        }
    }
    if (commit && !drag.cancelled && drag.moving) {
        // The dragged window may cover the destination. Hit-test reserved
        // layout slots, rather than the topmost (dragged) window's rectangle.
        const State *destination=nullptr;
        double nearest=std::max(24.0,m_gap)*std::max(24.0,m_gap);
        for (const auto &[id,other] : m_windows) {
            if (!other.tiled || other.group!=drag.group || id==drag.id || !other.effect || other.effect->isDeleted()) continue;
            const auto &r=other.target;
            const QPointF closest(std::clamp(drag.last.x(),r.left(),r.right()),std::clamp(drag.last.y(),r.top(),r.bottom()));
            const auto delta=drag.last-closest;
            const double distance=delta.x()*delta.x()+delta.y()*delta.y();
            if (distance<nearest) { nearest=distance; destination=&other; }
        }
        if (destination) {
            auto &tree=m_trees[drag.group];
            const auto &r=destination->target;
            const double nx=std::clamp((drag.last.x()-r.x())/r.width(),0.0,1.0);
            const double ny=std::clamp((drag.last.y()-r.y())/r.height(),0.0,1.0);
            const double edge=std::min({nx,1-nx,ny,1-ny});
            if (edge>=.22) {
                tree.swap(drag.id,destination->id);
                const auto minimum=tree.minimum(minimums(drag.group),Fluid::Tree::snap(m_gap,destination->effect->window()->output()->scale()),destination->effect->window()->output()->scale());
                const auto area=areaFor(destination->effect);
                if (minimum.width()>area.width() || minimum.height()>area.height()) tree.swap(drag.id,destination->id);
            }
            else {
                auto backup=tree.snapshot();
                const bool horizontal=std::min(nx,1-nx)<=std::min(ny,1-ny);
                tree.placeBeside(drag.id,destination->id,horizontal?Fluid::Axis::Horizontal:Fluid::Axis::Vertical,
                    horizontal?nx>.5:ny>.5);
                const auto minimum=tree.minimum(minimums(drag.group),Fluid::Tree::snap(m_gap,destination->effect->window()->output()->scale()),destination->effect->window()->output()->scale());
                const auto area=areaFor(destination->effect);
                if (minimum.width()>area.width() || minimum.height()>area.height()) tree.root=std::move(backup);
            }
        }
    }
    syncLayers();
    layoutGroup(drag.group);
    flushGeometry(); scheduleReconcile(); effects->addRepaintFull();
}
bool FluidTiles::filterMotion(PointerMotionEvent *event) {
    if (!m_drag) return false;
    if (effects->isScreenLocked() || effects->activeFullScreenEffect()) { endDrag(false); return false; }
    if (m_drag->nativeMove) { m_drag->last=event->position; return false; }
    if (m_drag->nativeResize) { updateDrag(event->position); return false; }
    updateDrag(event->position); return true;
}
bool FluidTiles::filterButton(PointerButtonEvent *event) {
    if (m_drag) {
        if (m_drag->nativeMove || m_drag->nativeResize) { m_drag->last=event->position; return false; }
        const auto expected = m_drag->button;
        if (event->button == expected && event->state == PointerButtonState::Released) endDrag();
        return true;
    }
    if (event->state != PointerButtonState::Pressed) return false;
    if (!(event->modifiers & Qt::MetaModifier)) return event->button == Qt::LeftButton && beginDividerDrag(event->position);
    if (event->button != Qt::LeftButton && event->button != Qt::RightButton) return false;
    const bool began = beginDrag(event->position, event->button == Qt::LeftButton);
    if (began) m_drag->button = event->button;
    return began;
}
bool FluidTiles::filterKey(KeyboardKeyEvent *event) {
    if (m_drag && event->key == Qt::Key_Escape) {
        if (m_drag->nativeMove || m_drag->nativeResize) { m_drag->cancelled=true; return false; }
        endDrag(false); return true;
    }
    return false;
}
QString FluidTiles::neighbour(const State &s, int direction) const {
    const QPointF center = s.target.center();
    double best = 1e100; QString result;
    for (const auto &[id, other] : m_windows) {
        if (!other.tiled || other.group != s.group || id == s.id) continue;
        const auto delta = other.target.center()-center;
        double primary = direction == 0 ? -delta.x() : direction == 1 ? delta.y() : direction == 2 ? -delta.y() : delta.x();
        if (primary <= 1) continue;
        double secondary = (direction == 0 || direction == 3) ? std::abs(delta.y()) : std::abs(delta.x());
        double score = primary + 2*secondary;
        if (score < best) { best = score; result = id; }
    }
    return result;
}
bool FluidTiles::SetEnabled(bool enabled) {
    if (effects->isScreenLocked()) return false;
    if (m_enabled == enabled) return true;
    m_enabled = enabled;
    m_appearance->setEnabled(enabled);
    KConfigGroup config(effects->config(), "Effect-fluidtiles");
    config.writeEntry("TilingEnabled",enabled); config.sync();
    if (!enabled) restoreAll(); else { m_lastStep = m_clock.nsecsElapsed(); scheduleReconcile(); }
    return true;
}
bool FluidTiles::Action(const QString &name) {
    if (name == "disable") return SetEnabled(false);
    if (name == "toggle") return SetEnabled(!m_enabled);
    if (!m_enabled || effects->isScreenLocked() || effects->activeFullScreenEffect()) return false;
    if (m_drag) endDrag();
    auto s = active(); if (!s) return false;
    if (name == "float") {
        if (s->tiled) { m_floating.insert(s->id); floatToCenter(*s); }
        else if (m_floating.contains(s->id)) m_floating.remove(s->id);
        else m_floating.insert(s->id);
        // The next explicit transition from floating captures the position the
        // user last chose, rather than an earlier tiled geometry.
        s->restoreValid=false;
        s->capacityBlocked=false;
        if (s->effect) s->effect->window()->setProperty("_fluidtilesRestore",QVariant());
        scheduleReconcile(); return true;
    }
    if (!s->tiled) return false;
    if (name == "split") {
        auto n = m_trees[s->group].find(s->id);
        if (!n || !n->parent) return false;
        auto p = n->parent;
        p->axis = p->axis == Fluid::Axis::Horizontal ? Fluid::Axis::Vertical : Fluid::Axis::Horizontal;
        const auto min=m_trees[s->group].minimum(minimums(s->group),Fluid::Tree::snap(m_gap,s->effect->window()->output()->scale()),s->effect->window()->output()->scale());
        const auto area=areaFor(s->effect);
        if (min.width()>area.width() || min.height()>area.height()) {
            p->axis = p->axis == Fluid::Axis::Horizontal ? Fluid::Axis::Vertical : Fluid::Axis::Horizontal;
            return false;
        }
        m_pendingPairChecks.remove(s->group); // explicit rotation overrides the automatic preference
        layoutGroup(s->group); return true;
    }
    const QStringList directions{"left", "down", "up", "right"};
    const auto dir = directions.indexOf(name.section('-', -1));
    if (dir < 0) return false;
    auto otherId = neighbour(*s, dir); if (otherId.isEmpty()) return false;
    if (name.startsWith("focus-")) effects->activateWindow(m_windows.at(otherId).effect);
    else if (name.startsWith("swap-")) {
        auto &tree=m_trees[s->group]; tree.swap(s->id,otherId);
        const auto min=tree.minimum(minimums(s->group),Fluid::Tree::snap(m_gap,s->effect->window()->output()->scale()),s->effect->window()->output()->scale());
        const auto area=areaFor(s->effect);
        if (min.width()>area.width() || min.height()>area.height()) { tree.swap(s->id,otherId); return false; }
        layoutGroup(s->group);
    }
    else return false;
    return true;
}
bool FluidTiles::TestPointer(const QString &phase, double x, double y) {
    if (!m_test) return false;
    if (phase == "resize") return beginDrag({x,y}, false);
    if (phase == "divider") return beginDividerDrag({x,y});
    if (phase == "move") return beginDrag({x,y}, true);
    if (phase == "motion" && m_drag) { updateDrag({x,y}); return true; }
    if (phase == "release" && m_drag) { endDrag(); return true; }
    if (phase == "cancel" && m_drag) { endDrag(false); return true; }
    return false;
}
void FluidTiles::ResetMetrics() { m_metrics.clear(); }
bool FluidTiles::TestWindow(const QString &id, const QString &operation) {
    if (!m_test) return false;
    auto it=m_windows.find(id);
    if (it==m_windows.end() || !it->second.effect || !it->second.effect->window()) return false;
    auto c=it->second.effect->window();
    if (operation=="minimize") c->setMinimized(true);
    else if (operation=="restore") c->setMinimized(false);
    else if (operation=="fullscreen") c->setFullScreen(true);
    else if (operation=="unfullscreen") c->setFullScreen(false);
    else if (operation=="maximize") c->setMaximize(true,true);
    else if (operation=="unmaximize") c->setMaximize(false,false);
    else if (operation=="desktop-1" || operation=="desktop-2")
        c->setDesktops({VirtualDesktopManager::self()->desktops().at(operation.endsWith('1')?0:1)});
    else if (operation=="sticky") c->setOnAllDesktops(true);
    else if (operation=="switch-desktop-1" || operation=="switch-desktop-2")
        VirtualDesktopManager::self()->setCurrent(operation.endsWith('1')?1:2,c->output());
    else if (operation=="desktop-gesture-half") {
        Q_EMIT effects->desktopChanging(effects->currentDesktop(c->output()),QPointF(.5,0),nullptr,c->output());
    }
    else if (operation=="desktop-gesture-cancel") { Q_EMIT effects->desktopChangingCancelled(); }
    else if (operation=="native-move") c->performMousePressCommand(Options::MouseUnrestrictedMove,Cursors::self()->mouse()->pos());
    else if (operation=="native-resize") c->performMousePressCommand(Options::MouseUnrestrictedResize,Cursors::self()->mouse()->pos());
    else if (operation=="native-cancel") {
        if (m_drag && m_drag->id==id) m_drag->cancelled=true;
        c->cancelInteractiveMoveResize();
    }
    else if (operation=="move-floating" && !it->second.tiled) {
        cancelFloatingTransition(it->second);it->second.restorePending=false;
        c->moveResize(RectF(120,180,500,330));
    }
    else if (operation=="cover-divider" && !it->second.tiled) {
        const auto area=areaFor(it->second.effect);
        c->moveResize(RectF(area.center().x()-c->width()/2,area.center().y()-c->height()/2,c->width(),c->height()));
    }
    else if (operation=="activate") effects->activateWindow(it->second.effect);
    else if ((operation=="preview-wide" || operation=="preview-tall") && it->second.tiled && m_drag && m_drag->id==id) {
        QRectF frame=c->frameGeometry();
        if (operation=="preview-wide") frame.setWidth(frame.width()*2);
        else frame.setHeight(frame.height()*2);
        it->second.motion.reset(frame);effects->addRepaintFull();
    }
    else if (operation=="half-opacity") c->setOpacity(.5);
    else if (operation=="full-opacity") c->setOpacity(1.0);
    else if (operation=="keep-above") c->setKeepAbove(true);
    else if (operation=="keep-below") c->setKeepBelow(true);
    else if (operation=="normal-layer") { c->setKeepAbove(false);c->setKeepBelow(false); }
    else return false;
    return true;
}
QString FluidTiles::Status() const {
    Dl_info library{};
    dladdr(reinterpret_cast<void *>(&jsonRect),&library);
    QJsonArray windows;
    for (const auto &[id,s] : m_windows) if (s.effect && s.effect->window()) {
        windows.append(QJsonObject{{"id", id}, {"class", s.effect->window()->resourceClass()}, {"title", s.effect->window()->caption()},
            {"instance",s.effect->window()->resourceName()}, {"role",s.effect->window()->windowRole()},
            {"ignored",ignored(s.effect)}, {"opacity",s.effect->opacity()},
            {"appearance",m_appearance->status(s.effect,presentation(s))},
            {"keepAbove",s.effect->window()->keepAbove()}, {"keepBelow",s.effect->window()->keepBelow()},
            {"layerOwned",s.layerOwned}, {"stackingOrder",s.effect->window()->stackingOrder()},
            {"eligible",eligible(s.effect)}, {"normal",s.effect->window()->isNormalWindow()},
            {"popup",s.effect->window()->isPopupWindow()}, {"internal",s.effect->window()->isInternal()},
            {"modal",s.effect->window()->isModal()}, {"transient",s.effect->window()->isTransient()},
            {"minimum",QJsonArray{s.effect->window()->minSize().width(),s.effect->window()->minSize().height()}},
            {"fullscreen",s.effect->window()->isFullScreen()}, {"minimized",s.effect->window()->isMinimized()},
            {"maximized",s.effect->window()->maximizeMode()!=MaximizeRestore}, {"restorePending",s.restorePending},
            {"order",qint64(s.order)}, {"manualFloating",m_floating.contains(id)}, {"capacityBlocked",s.capacityBlocked},
            {"tiled", s.tiled}, {"floatingTransition",s.floatingTransition}, {"openingFrom",jsonRect(s.openingFrom)}, {"group", s.group},
            {"client",jsonRect(s.effect->window()->clientGeometry())},{"buffer",jsonRect(s.effect->window()->bufferGeometry())},
            {"workArea",jsonRect(s.effect->window()->output() && effects->screens().contains(s.effect->window()->output())?QRectF(effects->clientArea(MaximizeArea,s.effect)):QRectF())},
            {"actual", jsonRect(s.effect->frameGeometry())}, {"visual", jsonRect(s.motion.rect())}, {"target", jsonRect(s.target)},
            {"requested", jsonRect(s.effect->window()->moveResizeGeometry())}, {"redirected", s.redirected},
            {"restore", jsonRect(s.restore)}, {"requests", qint64(s.requests)}, {"commits", qint64(s.commits)},
            {"visible", s.effect->isOnCurrentDesktop() && s.effect->isOnCurrentActivity()}});
    }
    QJsonArray outputs;
    for (auto o : effects->screens()) outputs.append(QJsonObject{{"name", o->name()}, {"scale", o->scale()}, {"refresh_millihz", int(o->refreshRate())}, {"geometry", jsonRect(o->geometryF())}});
    QJsonObject metrics;
    for (const auto &[name,m] : m_metrics) metrics[name] = QJsonObject{{"paint", distribution(m.paintIntervals)}, {"presentation", distribution(m.presentationIntervals)}, {"paint_frames", qint64(m.paints)}};
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"version", "0.5.3"}, {"enabled", m_enabled}, {"dragging", m_drag.has_value()}, {"nativeDrag", m_drag && m_drag->nativeMove},
        {"activeWindow",effects->activeWindow()?idOf(effects->activeWindow()):QString{}},
        {"fullScreenEffect",effects->activeFullScreenEffect()?QString::fromLatin1(effects->activeFullScreenEffect()->metaObject()->className()):QString{}},
        {"libraryPath",QString::fromLocal8Bit(library.dli_fname?library.dli_fname:"")},
        {"nativeResize",m_drag && m_drag->nativeResize}, {"freeResize",m_drag && m_drag->freeResize}, {"animating", m_animating}, {"innerGap", m_gap}, {"outerGap",m_outerGap},
        {"ignoreRules",QJsonObject{{"applications",m_ignoreApplicationText},{"titles",m_ignoreTitleText},{"roles",m_ignoreRoleText}}},
        {"springFrequency",m_omega}, {"animationTimeFactor",effects->animationTimeFactor()}, {"appearance",m_appearance->status()}, {"testMode", m_test}, {"outputs", outputs}, {"windows", windows}, {"metrics", metrics}}).toJson(QJsonDocument::Compact));
}

KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(FluidTiles, "metadata.json", return KWin::OffscreenEffect::supported();, return false;)
#include "effect.moc"
