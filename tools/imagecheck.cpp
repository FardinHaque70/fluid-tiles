// SPDX-License-Identifier: GPL-3.0-or-later
// Pixel inspection of compositor captures without third-party Python packages.
#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>
#include <cmath>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=3)return 2;
    QImage image(QString::fromLocal8Bit(argv[1]));if(image.isNull())return 3;
    const auto spec=QJsonDocument::fromJson(argv[2]).object();
    const auto size=spec["logicalSize"].toArray();
    if(size.size()!=2 || size[0].toDouble()<=0 || size[1].toDouble()<=0)return 4;
    QJsonArray pixels;
    for(auto value:spec["points"].toArray()) {
        const auto p=value.toArray();
        const int x=std::clamp(int(std::round(p[0].toDouble()*image.width()/size[0].toDouble())),0,image.width()-1);
        const int y=std::clamp(int(std::round(p[1].toDouble()*image.height()/size[1].toDouble())),0,image.height()-1);
        const auto c=image.pixelColor(x,y);pixels.append(QJsonArray{c.red(),c.green(),c.blue(),c.alpha()});
    }
    const auto result=QJsonDocument(QJsonObject{{"size",QJsonArray{image.width(),image.height()}},{"pixels",pixels}}).toJson(QJsonDocument::Compact);
    puts(result.constData());return 0;
}
