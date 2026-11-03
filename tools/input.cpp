// SPDX-License-Identifier: GPL-3.0-or-later
// Test helper restricted to our isolated Wayland socket, never the host socket.
#include <KWayland/Client/fakeinput.h>
#include <KWayland/Client/registry.h>
#include <QCoreApplication>
#include <QPointF>
#include <QTimer>
#include <wayland-client.h>
#include <linux/input-event-codes.h>
#include <cmath>
#include <cstdio>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    const auto socket=qgetenv("WAYLAND_DISPLAY");
    if (socket!="fluidtiles-test" || args.size()<3) {
        fprintf(stderr,"Test helper requires WAYLAND_DISPLAY=fluidtiles-test and x y [divider]\n"); return 2;
    }
    wl_display *display=wl_display_connect(socket.constData());
    if(!display)return 3;
    int result=0;
    {
        KWayland::Client::Registry registry;
        KWayland::Client::FakeInput *input=nullptr;
        QObject::connect(&registry,&KWayland::Client::Registry::fakeInputAnnounced,&app,[&](quint32 name,quint32 version) {
            input=registry.createFakeInput(name,version,&registry);
        });
        registry.create(display);registry.setup();
        wl_display_roundtrip(display);wl_display_roundtrip(display);
        if(!input) { fprintf(stderr,"Fake-input protocol unavailable in this KWin build\n");result=4; }
        else {
            input->authenticate("Fluid Tiles isolated integration test","Test resize event routing inside the disposable compositor");
            const double x=args[1].toDouble(),y=args[2].toDouble();
            const bool divider=args.value(3)=="divider";
            const bool clientMove=args.value(3).startsWith("client-move");
            const bool native=args.value(3).startsWith("titlebar") || args.value(3).startsWith("native-resize") || clientMove;
            const bool nativeResize=args.value(3).startsWith("native-resize");
            const bool metaMove=args.value(3)=="meta-move";
            const bool cancel=args.value(3).endsWith("-cancel");
            const bool withMeta=!divider && !native;
            const quint32 button=(nativeResize || clientMove)?BTN_RIGHT:(divider || native || metaMove?BTN_LEFT:BTN_RIGHT);
            input->requestPointerMoveAbsolute({x,y});
            if (args.value(3)=="click") {
                wl_display_flush(display);
                QTimer::singleShot(100,&app,[&] { input->requestPointerButtonPress(BTN_LEFT);wl_display_flush(display); });
                QTimer::singleShot(180,&app,[&] { input->requestPointerButtonRelease(BTN_LEFT);wl_display_flush(display); });
                QTimer::singleShot(350,&app,&QCoreApplication::quit);
                result=app.exec();
            } else if (args.value(3)=="hover-close") {
                wl_display_flush(display);
                QTimer::singleShot(3200,&app,[&] { input->requestPointerMoveAbsolute({0,0});wl_display_flush(display); });
                QTimer::singleShot(3600,&app,&QCoreApplication::quit);
                result=app.exec();
            } else {
            if(withMeta)input->requestKeyboardKeyPress(KEY_LEFTMETA);
            wl_display_flush(display);
            QTimer::singleShot(100,&app,[&,button] { input->requestPointerButtonPress(button);wl_display_flush(display); });
            int frame=0;QTimer timer;timer.setInterval(8);
            QObject::connect(&timer,&QTimer::timeout,&app,[&] {
                if(frame<20) { ++frame;return; }
                if(native || metaMove) {
                    const double p=std::min(1.,(frame-20)/180.0);
                    input->requestPointerMoveAbsolute({x+(args.value(4).toDouble()-x)*p,y+(args.value(5).toDouble()-y)*p});
                    if(cancel && frame==225) {
                        input->requestKeyboardKeyPress(KEY_ESC); input->requestKeyboardKeyRelease(KEY_ESC);
                    }
                } else {
                    const double dx=100*std::sin((frame-20)/240.0*2*M_PI);
                    input->requestPointerMoveAbsolute({x+dx,y});
                }
                wl_display_flush(display);
                if(++frame>=261) {
                    input->requestPointerButtonRelease(button);
                    if(withMeta)input->requestKeyboardKeyRelease(KEY_LEFTMETA);
                    wl_display_flush(display);timer.stop();QTimer::singleShot(150,&app,&QCoreApplication::quit);
                }
            });
            timer.start();result=app.exec();
            }
        }
    }
    wl_display_disconnect(display);return result;
}
