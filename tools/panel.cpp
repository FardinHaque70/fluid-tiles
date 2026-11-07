// SPDX-License-Identifier: GPL-3.0-or-later
// Real layer-shell reservation, restricted to the disposable test compositor.
#include <QApplication>
#include <QWidget>
#include <QWindow>
#include <LayerShellQt/Window>
#include <cstdio>

int main(int argc,char **argv) {
    if (qgetenv("WAYLAND_DISPLAY")!="fluidtiles-test") {
        fprintf(stderr,"Panel fixture requires WAYLAND_DISPLAY=fluidtiles-test\n");return 2;
    }
    QApplication app(argc,argv);
    app.setDesktopFileName(app.arguments().value(1,"fluidtiles-panel"));
    const bool desktop=app.arguments().value(2)=="desktop";
    QWidget panel;
    panel.setWindowTitle(desktop?"Fluid Tiles isolated desktop":"Fluid Tiles isolated panel");
    panel.setWindowFlag(Qt::FramelessWindowHint);
    panel.setStyleSheet("background: #ffcc00;");
    panel.resize(1600,desktop?1000:48);
    if (!desktop) panel.setFixedHeight(48);
    panel.winId();
    auto layer=LayerShellQt::Window::get(panel.windowHandle());
    if (!layer) return 3;
    layer->setScope(app.arguments().value(1,"fluidtiles-panel"));
    if (desktop) layer->setAnchors(LayerShellQt::Window::Anchors::fromInt(LayerShellQt::Window::AnchorTop|LayerShellQt::Window::AnchorBottom|LayerShellQt::Window::AnchorLeft|LayerShellQt::Window::AnchorRight));
    else layer->setAnchors(LayerShellQt::Window::Anchors::fromInt(LayerShellQt::Window::AnchorTop|LayerShellQt::Window::AnchorLeft|LayerShellQt::Window::AnchorRight));
    layer->setExclusiveZone(desktop?0:48);
    layer->setDesiredSize(QSize(0,desktop?0:48));
    layer->setLayer(desktop?LayerShellQt::Window::LayerBackground:LayerShellQt::Window::LayerTop);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    panel.show();
    return app.exec();
}
