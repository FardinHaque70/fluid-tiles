// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <scene/windowitem.h>
#include <scene/outlinedborderitem.h>
#include <scene/scene.h>
#include <scene/workspacescene.h>
#include <scene/itemrenderer.h>
#include <opengl/eglcontext.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <compositor.h>
#include <KColorScheme>
#include <KDecoration3/Decoration>
#include <QFile>
#include <QJsonArray>
#include <QTimer>
#include <cmath>

using namespace KWin;
namespace Fluid {
namespace {
QJsonArray rect(const QRectF &r) { return {r.x(),r.y(),r.width(),r.height()}; }
QJsonArray radii(const BorderRadius &r) { return {r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft()}; }
bool near(const QRectF &a, const QRectF &b, double tolerance) {
    return std::abs(a.x()-b.x())<tolerance && std::abs(a.y()-b.y())<tolerance
        && std::abs(a.width()-b.width())<tolerance && std::abs(a.height()-b.height())<tolerance;
}
}
Appearance::Appearance(QObject *parent) : QObject(parent) {
    m_ignore.setText(m_ignoreText);
    m_kdeConfig=KSharedConfig::openConfig("kdeglobals");
    m_themeWatcher=KConfigWatcher::create(m_kdeConfig);
    connect(m_themeWatcher.data(),&KConfigWatcher::configChanged,this,[this] { colorsChanged(); });
    colorsChanged();
}
Appearance::~Appearance() { restoreAll(); }
bool Appearance::configure(const KConfigGroup &config) {
    // Legacy fallback only matters before the installer's one-shot migration.
    const bool legacy=config.readEntry("AppearanceEnabled",true);
    const int legacyTitlebars=std::clamp(config.readEntry("TitlebarMode",1),0,2);
    const bool outlines=config.readEntry("OutlinesEnabled",legacy);
    const bool rounding=config.readEntry("RoundedCornersEnabled",legacy);
    const bool titlebarsEnabled=config.readEntry("TitlebarControlEnabled",legacy && legacyTitlebars!=0);
    const double width=std::clamp(config.readEntry("BorderWidth",2.0),0.0,8.0);
    const double radius=std::clamp(config.readEntry("CornerRadius",8.0),0.0,32.0);
    const int titlebars=std::clamp(config.readEntry("TitlebarScope",legacyTitlebars==2?1:0),0,1)+1;
    const auto ignore=config.readEntry("AppearanceIgnoredApplications",defaultIgnoredApplications);
    const bool geometry=outlines!=m_outlinesEnabled || titlebarsEnabled!=m_titlebarsEnabled || width!=m_width || titlebars!=m_titlebars || ignore!=m_ignoreText;
    if (rounding!=m_roundingEnabled || radius!=m_radius || titlebarsEnabled!=m_titlebarsEnabled || titlebars!=m_titlebars || ignore!=m_ignoreText) ++m_sourceRevision;
    m_outlinesEnabled=outlines;m_roundingEnabled=rounding;m_titlebarsEnabled=titlebarsEnabled;
    m_width=width;m_radius=radius;m_titlebars=titlebars;
    m_ignoreText=ignore;m_ignore.setText(ignore);
    m_systemColors=config.readEntry("UseSystemBorderColors",true);
    m_customActive=config.readEntry("ActiveBorderColor",QColor("#3daee9"));
    m_customInactive=config.readEntry("InactiveBorderColor",QColor("#6e6e6e"));
    if (!m_customActive.isValid()) m_customActive=QColor("#3daee9");
    if (!m_customInactive.isValid()) m_customInactive=QColor("#6e6e6e");
    colorsChanged();
    return geometry;
}
void Appearance::colorsChanged() {
    if (m_systemColors) {
        m_kdeConfig->reparseConfiguration();
        m_active=KColorScheme(QPalette::Active,KColorScheme::Selection,m_kdeConfig).background().color();
        m_inactive=KColorScheme(QPalette::Inactive,KColorScheme::Window,m_kdeConfig).foreground(KColorScheme::InactiveText).color();
    } else { m_active=m_customActive;m_inactive=m_customInactive; }
    for (auto &[w,s] : m_windows) if (!s.lastOuter.isEmpty()) effects->addRepaint(RectF(s.lastOuter.adjusted(-2,-2,2,2)));
    Q_EMIT changed(false);
}
void Appearance::setEnabled(bool enabled) {
    m_enabled=enabled;
    if (!enabled) restoreAll();
}
bool Appearance::applies(EffectWindow *w) const {
    if (!m_enabled || !(m_outlinesEnabled || m_roundingEnabled || m_titlebarsEnabled) || !m_error.isEmpty() || !w || w->isDeleted() || !w->window()) return false;
    auto c=w->window();
    if (!c->output() || !effects->screens().contains(c->output())) return false;
    // Layer-shell panels can report NET::Normal and have arbitrary app IDs.
    // They remain compositor shell surfaces, never application decorations.
    return c->isClient() && c->pid()>=0 && !c->inherits("KWin::LayerShellV1Window")
        && !c->isInternal() && !c->isPopupWindow() && !c->isSpecialWindow()
        && !c->isHidden() && !c->isMinimized() && !c->isFullScreen() && c->maximizeMode()==MaximizeRestore
        && (c->isNormalWindow() || c->isDialog() || c->isUtility()) && c->readyForPainting()
        && !m_ignore.matches(c->resourceClass()) && !m_ignore.matches(c->resourceName());
}
bool Appearance::isExpanded(EffectWindow *w) const {
    const auto it=m_windows.find(w);
    return it!=m_windows.end() && it->second.expanded;
}
void Appearance::setExpanded(EffectWindow *w, bool expanded) {
    watch(w);
    auto it=m_windows.find(w);if(it==m_windows.end() || it->second.expanded==expanded)return;
    it->second.expanded=expanded;++m_sourceRevision;
    damage(w,w->frameGeometry());
}
double Appearance::border(EffectWindow *w, bool normal) const {
    if(auto it=m_windows.find(w);it!=m_windows.end() && it->second.closing)return it->second.snapshot.border;
    return applies(w) && (normal || !isExpanded(w)) && m_outlinesEnabled && w->window()->output()?PresentationGeometry::thickness(m_width,w->window()->output()->scale()):0;
}
QSizeF Appearance::minimumFrame(EffectWindow *w) const {
    auto c=w->window();
    const auto minimum=c->minSize();
    const bool hiddenForTile=applies(w) && m_titlebarsEnabled && c->userCanSetNoBorder() && c->isResizable()
        && (c->isDecorated() || c->decorationPolicy()==DecorationPolicy::None);
    const auto extra=hiddenForTile?QSizeF(0,0):QSizeF(c->frameGeometry().width()-c->clientGeometry().width(),c->frameGeometry().height()-c->clientGeometry().height());
    return {std::max(120.0,minimum.width()+extra.width()),std::max(80.0,minimum.height()+extra.height())};
}
void Appearance::watch(EffectWindow *w) {
    if (!w || !w->window() || m_windows.contains(w)) return;
    auto c=w->window();
    auto &s=m_windows[w];s.effect=w;
    connect(c,&Window::decorationPolicyChanged,this,[this,w] {
        auto it=m_windows.find(w);if(it==m_windows.end())return;
        auto &s=it->second;
        if (!s.changing && s.owned && w->window()->decorationPolicy()!=s.writtenPolicy) {
            s.originalPolicy=w->window()->decorationPolicy();s.policyOwned=false;
            Q_EMIT changed(true);
        }
    });
    connect(c,&Window::borderRadiusChanged,this,[this,w] {
        auto it=m_windows.find(w);if(it==m_windows.end())return;
        auto &s=it->second;
        if (!s.changing && s.owned && w->window()->borderRadius()!=s.writtenRadius) {
            // No-border transitions can asynchronously reset KWin's radius.
            // That reset belongs to our policy rather than a new preference.
            if (!s.policyOwned || !w->window()->borderRadius().isNull()) s.originalRadius=w->window()->borderRadius();
            Q_EMIT changed(false);
        }
    });
    connect(c,&Window::frameGeometryChanged,this,[this,w] {
        auto it=m_windows.find(w);if(it==m_windows.end())return;
        if (!it->second.changing) applyPendingFrame(it->second);
        if (applies(w)) damage(w,w->frameGeometry());
    });
    connect(c,&Window::interactiveMoveResizeStarted,this,[this,w] {
        auto it=m_windows.find(w);if(it!=m_windows.end())it->second.framePending=false;
    });
}
void Appearance::setPolicy(WindowState &s, DecorationPolicy policy, bool preserveFrame) {
    auto c=s.effect->window();
    if (c->decorationPolicy()==policy) { s.writtenPolicy=policy;return; }
    const QRectF frame=c->frameGeometry();
    s.changing=true;s.writtenPolicy=policy;
    c->setDecorationPolicy(policy);
    if (preserveFrame) c->moveResize(RectF(frame));
    s.pendingFrame=c->moveResizeGeometry();s.framePending=preserveFrame;
    s.changing=false;
}
void Appearance::applyPendingFrame(WindowState &s) {
    if (!s.framePending || !s.effect || s.effect->isDeleted())return;
    auto c=s.effect->window();
    if (c->isInteractiveMove() || c->isInteractiveResize()) { s.framePending=false;return; }
    if (c->isFullScreen() || c->isMinimized() || c->maximizeMode()!=MaximizeRestore)return;
    const double tolerance=c->output() && effects->screens().contains(c->output())?1.1/c->output()->scale():1.1;
    if (near(c->frameGeometry(),s.pendingFrame,tolerance))s.framePending=false;
    else if (!near(c->moveResizeGeometry(),s.pendingFrame,tolerance)) {
        s.changing=true;c->moveResize(RectF(s.pendingFrame));s.changing=false;
    }
}
void Appearance::sync(EffectWindow *w, bool tiled) {
    watch(w);
    auto it=m_windows.find(w);if(it==m_windows.end())return;
    auto &s=it->second;
    if (!applies(w)) { restore(s);return; }
    auto c=w->window();
    if (!s.owned) {
        s.owned=true;s.originalPolicy=c->decorationPolicy();s.originalRadius=c->borderRadius();
        s.writtenPolicy=s.originalPolicy;s.writtenRadius=s.originalRadius;
    }
    const bool hide=m_titlebarsEnabled && (m_titlebars==2 || (m_titlebars==1 && tiled)) && c->userCanSetNoBorder() && c->isResizable() && (c->isDecorated() || s.policyOwned);
    if (hide && c->decorationPolicy()!=DecorationPolicy::None) {
        // Bounded clients may not be able to fill the unchanged floating frame
        // after the server frame is removed.
        if (tiled || (c->maxSize().width()>=c->width() && c->maxSize().height()>=c->height())) {
            s.policyOwned=true;setPolicy(s,DecorationPolicy::None,!tiled);
        }
    } else if (!hide && s.policyOwned) {
        setPolicy(s,s.originalPolicy);s.policyOwned=false;
    }
    const double limit=std::min(c->width(),c->height())/2;
    s.writtenRadius=s.expanded?BorderRadius():(m_roundingEnabled && m_radius>0?BorderRadius(std::min(m_radius,limit)):(s.policyOwned?BorderRadius():s.originalRadius));
    s.changing=true;c->setBorderRadius(s.writtenRadius);s.changing=false;
    if (!s.boundsItem && w->windowItem()) s.boundsItem=std::make_unique<Item>(w->windowItem());
    if (s.boundsItem) {
        const double b=border(w);
        s.boundsItem->setGeometry(RectF(-b,-b,c->width()+2*b,c->height()+2*b));
    }
    applyPendingFrame(s);
}
void Appearance::restore(WindowState &s) {
    if (!s.lastOuter.isEmpty())effects->addRepaint(RectF(s.lastOuter.adjusted(-2,-2,2,2)));
    s.lastOuter={};s.boundsItem.reset();
    if (!s.owned || !s.effect || s.effect->isDeleted()) { s.owned=false;return; }
    auto c=s.effect->window();
    if (s.policyOwned) { setPolicy(s,s.originalPolicy);s.policyOwned=false; }
    const bool nativeState=c->isFullScreen() || c->maximizeMode()!=MaximizeRestore;
    const auto restoredRadius=nativeState?(c->decoration()?BorderRadius::from(c->decoration()->borderRadius()):BorderRadius()):s.originalRadius;
    s.changing=true;c->setBorderRadius(restoredRadius);s.changing=false;
    s.owned=false;
}
void Appearance::release(EffectWindow *w) { auto it=m_windows.find(w);if(it!=m_windows.end())restore(it->second); }
void Appearance::cancelPendingFrame(EffectWindow *w) { auto it=m_windows.find(w);if(it!=m_windows.end())it->second.framePending=false; }
void Appearance::forget(EffectWindow *w) { release(w);m_windows.erase(w); }
void Appearance::restoreAll() { for(auto &[w,s]:m_windows)restore(s); }
void Appearance::focusChanged() {
    for (auto &[w,s]:m_windows) {
        const bool active=w==effects->activeWindow();
        if (s.active!=active && !s.lastOuter.isEmpty())effects->addRepaint(RectF(s.lastOuter.adjusted(-2,-2,2,2)));
        s.active=active;
    }
}
Appearance::SnapshotStyle Appearance::snapshot(EffectWindow *w,const QRectF &visual) const {
    const auto p=geometry(w,visual);
    return {p.border,w->opacity(),p.radius,w==effects->activeWindow()?m_active:m_inactive,
        applies(w) && !isExpanded(w) && m_roundingEnabled && m_radius>0};
}
void Appearance::freeze(EffectWindow *w,const SnapshotStyle &style) {
    auto it=m_windows.find(w);if(it==m_windows.end())return;
    it->second.closing=true;it->second.snapshot=style;
}
PresentationGeometry Appearance::geometry(EffectWindow *w,const QRectF &visual) const {
    PresentationGeometry p;p.frame=visual;p.border=border(w);
    if(auto it=m_windows.find(w);it!=m_windows.end() && it->second.closing){p.radius=it->second.snapshot.radius;return p;}
    if (applies(w)) {
        const auto native=w->window()->borderRadius();
        const double radius=std::min(m_radius,std::min(visual.width(),visual.height())/2);
        p.radius=isExpanded(w)?BorderRadius():(m_roundingEnabled && m_radius>0?BorderRadius(radius):native);
    }
    return p;
}
void Appearance::damage(EffectWindow *w,const QRectF &visual) {
    auto it=m_windows.find(w);if(it==m_windows.end())return;
    auto &s=it->second;
    const auto next=applies(w)?geometry(w,visual).outer():QRectF();
    if(s.lastOuter==next)return;
    if(!s.lastOuter.isEmpty())effects->addRepaint(RectF(s.lastOuter.adjusted(-2,-2,2,2)));
    if(!next.isEmpty())effects->addRepaint(RectF(next.adjusted(-2,-2,2,2)));
    s.lastOuter=next;
}
void Appearance::fail(const QString &error) {
    if(!m_error.isEmpty())return;
    m_error=error;
    QTimer::singleShot(0,this,[this] { restoreAll();Q_EMIT changed(true); });
}
bool Appearance::prepareShader() {
    if(!m_error.isEmpty())return false;
    if(m_clipShader)return true;
    if(qEnvironmentVariableIntValue("FLUIDTILES_TEST_MODE")==1 && qEnvironmentVariableIntValue("FLUIDTILES_TEST_BAD_APPEARANCE_SHADER")==1) { fail("Injected appearance shader failure");return false; }
    QFile vertex(":/opengl/base.vert"),fragment(":/opengl/base.frag");
    if(!vertex.open(QIODevice::ReadOnly) || !fragment.open(QIODevice::ReadOnly)) { fail("KWin base shaders unavailable");return false; }
    auto vs=vertex.readAll(),fs=fragment.readAll();
    // Extend KWin's own shader resources: its color conversion, saturation,
    // modulation and premultiplied-alpha handling remain unchanged.
    if(!vs.contains("position0 = position.xy;") || !fs.contains("void main(void)")) { fail("Unsupported KWin base shader interface");return false; }
    vs.replace("uniform mat4 modelViewProjectionMatrix;","uniform mat4 modelViewProjectionMatrix;\nuniform vec2 fluidPresentationScale;");
    vs.replace("position0 = position.xy;","position0 = position.xy * fluidPresentationScale;");
    // Preserve native shadows outside the frame, while clipping the surface
    // and frame corners inside it. Do not clip the entire shadow texture.
    fs.replace("result *= 1.0 - clamp(0.5 + f / df, 0.0, 1.0);",
        "if (all(lessThanEqual(abs(position0 - box.xy), box.zw))) result *= 1.0 - clamp(0.5 + f / max(df, 0.0001), 0.0, 1.0);");
    m_clipShader=ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture | ShaderTrait::Modulate
        | ShaderTrait::AdjustSaturation | ShaderTrait::TransformColorspace | ShaderTrait::RoundedCorners,vs,fs);
    if(!m_clipShader) { fail("Rounded-corner shader compilation failed");return false; }
    return true;
}
GLShader *Appearance::clipShader(EffectWindow *w,const QRectF &visual,double scale) {
    const auto it=m_windows.find(w);
    const bool closing=it!=m_windows.end() && it->second.closing;
    if((closing?!it->second.snapshot.rounded:(!applies(w) || isExpanded(w) || !m_roundingEnabled || m_radius<=0)) || !prepareShader())return nullptr;
    const auto actual=w->frameGeometry();
    if(actual.isEmpty())return nullptr;
    const auto p=geometry(w,visual);
    ShaderBinder binder(m_clipShader.get());
    m_clipShader->setUniform("fluidPresentationScale",QVector2D(visual.width()/actual.width(),visual.height()/actual.height()));
    m_clipShader->setUniform(GLShader::Vec4Uniform::Box,QVector4D(visual.width()*scale/2,visual.height()*scale/2,visual.width()*scale/2,visual.height()*scale/2));
    m_clipShader->setUniform(GLShader::Vec4Uniform::CornerRadius,p.radius.scaled(scale).toVector());
    return m_clipShader.get();
}
BorderRadius Appearance::beginCapture(EffectWindow *w) {
    const auto it=m_windows.find(w);const bool closing=it!=m_windows.end() && it->second.closing;
    if((closing?!it->second.snapshot.rounded:(!applies(w) || !m_roundingEnabled || m_radius<=0)) || !w->windowItem())return {};
    ++m_captures;
    auto item=w->windowItem()->windowContainer();
    const auto saved=item->borderRadius();item->setBorderRadius(BorderRadius());return saved;
}
void Appearance::endCapture(EffectWindow *w,const BorderRadius &saved) {
    const auto it=m_windows.find(w);const bool closing=it!=m_windows.end() && it->second.closing;
    if(w && w->windowItem() && (closing?it->second.snapshot.rounded:(!w->isDeleted() && applies(w) && m_roundingEnabled && m_radius>0)))w->windowItem()->windowContainer()->setBorderRadius(saved);
}
bool Appearance::drawOutline(const RenderTarget &target,const RenderViewport &viewport,EffectWindow *w,int mask,
                             const Region &region,const WindowPaintData &incoming,const QRectF &visual) {
    const auto it=m_windows.find(w);const bool closing=it!=m_windows.end() && it->second.closing;
    if(!closing && !applies(w))return true;
    const auto fullScreenEffect=effects->activeFullScreenEffect();
    // Slide already supplies per-window translation and clipping. Draw in that
    // same pass so both incoming and outgoing desktop borders remain attached.
    if(fullScreenEffect && fullScreenEffect!=effects->findEffect(QStringLiteral("slide")))return true;
    const auto p=geometry(w,visual);if(p.border<=0)return true;
    if(!prepareShader())return true;
    ++m_outlineDraws;
    if(fullScreenEffect) ++m_slideOutlineDraws;
    auto &s=m_windows.at(w);
    if(!s.outlineRoot) {
        s.outlineRoot=std::make_unique<Item>();
        s.outline=std::make_unique<OutlinedBorderItem>(RectF(),BorderOutline(),s.outlineRoot.get());
    }
    const QRectF actual=w->frameGeometry();
    s.outlineRoot->setPosition(actual.topLeft());
    s.outlineRoot->setOpacity(closing?s.snapshot.opacity:w->opacity());
    s.outline->setInnerRect(RectF(visual.translated(-actual.topLeft())));
    s.outline->setOutline(BorderOutline(p.border,closing?s.snapshot.color:(w==effects->activeWindow()?m_active:m_inactive),p.radius));
    auto renderer=effects->scene()->renderer(Compositor::self()->primaryDevice());
    return renderer->renderItem(target,viewport,s.outlineRoot.get(),mask,region,incoming,{},{});
}
QJsonObject Appearance::status() const {
    return {{"enabled",m_enabled && (m_outlinesEnabled || m_roundingEnabled || m_titlebarsEnabled)},
        {"outlinesEnabled",m_outlinesEnabled},{"roundedCornersEnabled",m_roundingEnabled},{"titlebarControlEnabled",m_titlebarsEnabled},{"titlebarScope",m_titlebars-1},{"available",m_error.isEmpty()},{"error",m_error},
        {"borderWidth",m_width},{"cornerRadius",m_radius},{"titlebarMode",m_titlebarsEnabled?m_titlebars:0},{"useSystemColors",m_systemColors},
        {"activeColor",m_active.name(QColor::HexArgb)},{"inactiveColor",m_inactive.name(QColor::HexArgb)},
        {"ignoredApplications",m_ignoreText},{"shaderReady",bool(m_clipShader)},
        {"outlineDraws",qint64(m_outlineDraws)},{"slideOutlineDraws",qint64(m_slideOutlineDraws)},{"captures",qint64(m_captures)}};
}
QJsonObject Appearance::status(EffectWindow *w,const QRectF &visual) const {
    const auto it=m_windows.find(w);auto c=w->window();const auto p=geometry(w,visual);
    return {{"applied",applies(w)},{"expanded",isExpanded(w)},{"owned",it!=m_windows.end() && it->second.owned},
        {"titlebarOwned",it!=m_windows.end() && it->second.policyOwned},{"noBorder",c->noBorder()},
        {"policy",int(c->decorationPolicy())},{"titlebarSupported",c->userCanSetNoBorder() && c->isResizable() && (c->isDecorated() || (it!=m_windows.end() && it->second.policyOwned))},
        {"border",p.border},{"frame",rect(p.frame)},{"outer",rect(p.outer())},{"radius",radii(c->borderRadius())}};
}
bool Appearance::needsCompositing() const {
    // An active fullscreen client suppresses the ordinary decorations behind
    // it; preserve KWin's fullscreen scanout opportunity in that case.
    auto active=effects->activeWindow();
    if(active && active->window()->isFullScreen())return false;
    for(const auto &[w,s]:m_windows)if(applies(w) && border(w)>0 && w->isOnCurrentDesktop() && w->isOnCurrentActivity())return true;
    return false;
}
}
