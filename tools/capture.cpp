// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdio>
#include <thread>
#include <vector>
#include <unistd.h>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc < 2 || argc > 3) { fprintf(stderr,"Usage: fluidtiles-capture output.png [test-rectangles-json]\n"); return 2; }
    int fds[2]; if (pipe(fds)) return 3;
    std::vector<char> bytes;
    std::thread reader([&] {
        char chunk[65536]; ssize_t n;
        while ((n=read(fds[0],chunk,sizeof(chunk))) > 0) bytes.insert(bytes.end(),chunk,chunk+n);
        close(fds[0]);
    });
    QDBusInterface shot("org.kde.KWin.ScreenShot2", "/org/kde/KWin/ScreenShot2", "org.kde.KWin.ScreenShot2");
    QVariantMap options{{"include-cursor",false}, {"native-resolution",qEnvironmentVariableIntValue("FLUIDTILES_CAPTURE_NATIVE")==1}, {"hide-caller-windows",false}};
    const auto reply = shot.call("CaptureActiveScreen", options, QVariant::fromValue(QDBusUnixFileDescriptor(fds[1])));
    close(fds[1]); reader.join();
    if (reply.type() == QDBusMessage::ErrorMessage) { fprintf(stderr,"%s\n",qPrintable(reply.errorMessage())); return 4; }
    QDBusReply<QVariantMap> result(reply);
    const auto attrs = result.value();
    const int width=attrs.value("width").toInt(), height=attrs.value("height").toInt(), stride=attrs.value("stride").toInt();
    if (width<=0 || height<=0 || bytes.size() < size_t(stride)*height) return 5;
    QImage image(reinterpret_cast<const uchar*>(bytes.data()),width,height,stride,QImage::Format(attrs.value("format").toUInt()));
    if (!image.save(QString::fromLocal8Bit(argv[1]))) return 6;
    if (argc==3) {
        const auto check=QJsonDocument::fromJson(argv[2]).object();
        const auto output=check["output"].toArray();
        const double sx=width/output[2].toDouble(), sy=height/output[3].toDouble();
        for (const auto value : check["windows"].toArray()) {
            const auto r=value.toArray();
            for (double fy : {.3,.75}) for(double fx : {.08,.2,.4,.6,.8,.92}) {
                auto color=image.pixelColor(int((r[0].toDouble()+r[2].toDouble()*fx)*sx),int((r[1].toDouble()+r[3].toDouble()*fy)*sy));
                if (std::max({color.red(),color.green(),color.blue()})<=20) {
                    fprintf(stderr,"Black content detected at sample %.2f %.2f\n",fx,fy); return 7;
                }
            }
        }
    }
    return 0;
}
