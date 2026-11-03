// SPDX-License-Identifier: GPL-3.0-or-later
// A local QML module supplies KWin's normal native effect loader with our
// package-owned plugin directory. All tiling/rendering lives in the effect.
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <core/output.h>
#include <cmath>
#include <config-kwin.h>
#include <QQmlExtensionPlugin>
#include <QQmlParserStatus>
#include <qqml.h>
#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

// A minimized Wayland client can acknowledge a size change without drawing a
// buffer of that size. Its first visible commit can therefore replace KWin's
// requested restore geometry after the native renderer has fully unloaded.
// This one-shot helper belongs to the QML bootstrap module (retained by Qt's
// registered type engine), never to the unloaded effect. It has no tiling,
// animation or input handling and deletes itself once restoration completes.
class MinimizedRestore : public QObject {
public:
    explicit MinimizedRestore(KWin::Window *window) : QObject(window), m_window(window),
        m_rect(window->property("_fluidtilesRestore").toRectF()) {
        setObjectName("_fluidtiles_pending_restore");
        m_timer.setSingleShot(true);
        connect(&m_timer,&QTimer::timeout,this,[this] { apply(); });
        connect(window,&KWin::Window::minimizedChanged,this,[this] { m_timer.start(0); });
        connect(window,&KWin::Window::fullScreenChanged,this,[this] { m_timer.start(0); });
        connect(window,&KWin::Window::maximizedChanged,this,[this] { m_timer.start(0); });
        connect(window,&KWin::Window::frameGeometryChanged,this,[this] { m_timer.start(0); });
        connect(window,&KWin::Window::interactiveMoveResizeStarted,this,[this] { finish(); });
    }
private:
    void finish() { if (m_window) m_window->setProperty("_fluidtilesRestore",QVariant()); deleteLater(); }
    void apply() {
        if (!m_window || m_window->isDeleted()) { deleteLater(); return; }
        if (m_window->isMinimized() || m_window->isFullScreen() || m_window->maximizeMode()!=KWin::MaximizeRestore) return;
        auto close=[&](const QRectF &r) {
            const double tolerance=m_window->output()?1.1/m_window->output()->scale():1.1;
            return std::abs(r.x()-m_rect.x())<tolerance && std::abs(r.y()-m_rect.y())<tolerance
                && std::abs(r.width()-m_rect.width())<tolerance && std::abs(r.height()-m_rect.height())<tolerance;
        };
        if (close(m_window->frameGeometry())) finish();
        else if (!close(m_window->moveResizeGeometry())) m_window->moveResize(KWin::RectF(m_rect));
    }
    QPointer<KWin::Window> m_window;
    QRectF m_rect;
    QTimer m_timer;
};

static void cancelPendingRestore(KWin::Window *window) {
    const auto pending=window->findChildren<QObject *>("_fluidtiles_pending_restore",Qt::FindDirectChildrenOnly);
    for (auto object : pending) delete object;
}

class FluidController : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QUrl pluginRoot MEMBER m_root)
public:
    explicit FluidController(QObject *parent=nullptr) : QObject(parent) {}
    ~FluidController() override {
        if (m_ownsEffect && m_handler) for (auto w : m_handler->stackingOrder()) {
            auto c=w->window();
            if (!c || c->isDeleted() || !c->isMinimized() || !c->property("_fluidtilesRestore").toRectF().isValid()) continue;
            cancelPendingRestore(c);
            new MinimizedRestore(c);
        }
        if (m_ownsEffect && m_handler) m_handler->unloadEffect("fluidtiles");
        if (!m_path.isEmpty()) QCoreApplication::removeLibraryPath(m_path);
    }
    void classBegin() override {}
    void componentComplete() override {
        // QML components are destroyed automatically on script disable.
        QTimer::singleShot(0,this,[this] {
            if (QCoreApplication::applicationVersion()!=QString::fromLatin1(KWIN_PLUGIN_VERSION_STRING)) {
                qWarning("Fluid Tiles was built for a different KWin version; rebuild before loading."); return;
            }
            const QDir package(QDir(m_root.toLocalFile()).filePath("../.."));
            QFile metadata(package.filePath("metadata.json"));
            QString path=m_root.toLocalFile();
            if (metadata.open(QIODevice::ReadOnly)) {
                const auto revision=QJsonDocument::fromJson(metadata.readAll()).object().value("X-FluidTiles-Revision").toString();
                if (!revision.isEmpty()) {
                    if (!QRegularExpression("^[a-f0-9]{64}$").match(revision).hasMatch()) {
                        qWarning("Fluid Tiles package revision invalid"); return;
                    }
                    path=package.filePath("versions/"+revision+"/contents/native");
                }
            }
            // Resolve metadata at each activation, even if Qt cached this
            // controller/QML component. Settings and the CLI load the same
            // immutable native payload after an in-session package update.
            m_path=QDir(path).canonicalPath();
            if (m_path.isEmpty()) { qWarning("Fluid Tiles native plugin directory missing"); return; }
            QCoreApplication::addLibraryPath(m_path);
            m_handler=KWin::effects;
            if (!m_handler) return;
            for (auto w : m_handler->stackingOrder()) if (w->window()) cancelPendingRestore(w->window());
            // The installed script owns this internal helper even if an older
            // loader already loaded it. Disabling the script must always stop it.
            m_ownsEffect=m_handler->isEffectLoaded("fluidtiles") || m_handler->loadEffect("fluidtiles");
            if (!m_ownsEffect) qWarning("Fluid Tiles native effect failed to load");
        });
    }
private:
    QUrl m_root;
    QString m_path;
    bool m_ownsEffect=false;
    QPointer<KWin::EffectsHandler> m_handler;
};
class FluidBootstrapPlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
public:
    void registerTypes(const char *uri) override { qmlRegisterType<FluidController>(uri,1,0,"FluidController"); }
};
#include "bootstrap.moc"
