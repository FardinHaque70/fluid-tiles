// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QPainter>
#include <QThread>
#include <QWidget>
#include <QMouseEvent>
#include <QWindow>
#include <QTimer>
#include <QScreen>

class Demo : public QWidget {
public:
    Demo(QString label, QColor color, int delay) : m_label(label), m_color(color), m_delay(delay) {
        setWindowTitle("Fluid Tiles test — "+label);
        setMinimumSize(160,120); resize(480,360);
    }
    void setSystemResizeTest() { m_systemResize=true; }
    void setSystemMoveTest() { m_systemMove=true; }
protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (m_systemMove && event->button()==Qt::RightButton && windowHandle()) {
            windowHandle()->startSystemMove();event->accept();return;
        }
        if (m_systemResize && event->button()==Qt::RightButton && windowHandle()) {
            windowHandle()->startSystemResize(event->position().x()<width()/2?Qt::LeftEdge:Qt::RightEdge);
            event->accept(); return;
        }
        QWidget::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent *) override {
        if (m_delay>0) QThread::msleep(m_delay);
        QPainter p(this); p.fillRect(rect(), m_color);
        p.setPen(QColor(255,255,255,40));
        for (int x=0; x<width(); x+=40) p.drawLine(x,0,x,height());
        for (int y=0; y<height(); y+=40) p.drawLine(0,y,width(),y);
        if (m_systemMove) {
            p.fillRect(QRect(0,0,width(),34),QColor("#253040"));p.setPen(Qt::white);
            p.drawText(QRect(12,0,width()-24,34),Qt::AlignLeft|Qt::AlignVCenter,"Application header (kept)");
        }
        p.setPen(Qt::white); auto f = font(); f.setPointSize(20); p.setFont(f);
        p.drawText(rect().adjusted(20,20,-20,-20), Qt::AlignCenter,
            m_label+"\n"+QString::number(width())+" × "+QString::number(height())
            +"\nMeta + right drag: resize\nMeta + left drag: swap");
    }
private:
    QString m_label; QColor m_color; int m_delay;
    bool m_systemResize=false, m_systemMove=false;
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto args = app.arguments();
    const QString label = args.value(1,"Test A");
    app.setDesktopFileName(args.value(4)=="authentication-prompt"?"org.kde.polkit-kde-authentication-agent-1":args.value(4)=="default-ignored"?"krunner":args.value(4)=="forced-server"?"fluidtiles-forced-frame":"fluidtiles-demo");
    Demo demo(label, QColor(args.value(2,"#225577")), args.value(3,"0").toInt());
    const auto kind=args.value(4);
    if (kind=="fixed") demo.setFixedSize(360,240);
    else if (kind=="authentication-prompt") { demo.setWindowFlag(Qt::Dialog);demo.setWindowModality(Qt::ApplicationModal); }
    else if (kind=="dialog") {
        Demo parent(label+" parent",QColor("#5b2c6f"),0);
        parent.show();
        demo.setParent(&parent,Qt::Dialog);
        demo.setWindowModality(Qt::WindowModal);
        demo.show();
        return app.exec();
    }
    else if (kind=="fifth-dolphin") demo.setMinimumSize(225,165);
    else if (kind=="fifth-settings") demo.setMinimumSize(930,650);
    else if (kind=="fifth-chat") { demo.setMinimumSize(476,596); demo.setWindowFlag(Qt::FramelessWindowHint); }
    else if (kind=="modal") demo.setWindowModality(Qt::ApplicationModal);
    else if (kind=="bounded") demo.setMaximumSize(600,400);
    else if (kind=="wide-minimum") demo.setMinimumSize(950,200);
    else if (kind=="single-bounded") demo.setMaximumWidth(1580);
    else if (kind=="large-minimum") demo.setMinimumSize(1450,900);
    else if (kind=="large-opening") demo.resize(app.primaryScreen()->availableGeometry().size()-QSize(20,20));
    else if (kind=="csd-system-move") { demo.setWindowFlag(Qt::FramelessWindowHint);demo.setSystemMoveTest(); }
    else if (kind=="system-resize") demo.setSystemResizeTest();
    else if (kind=="changing-title") {
        QTimer::singleShot(2000,&demo,[&demo] { demo.setWindowTitle("Fluid Tiles test — Audit Ignored title"); });
        QTimer::singleShot(4000,&demo,[&demo,label] { demo.setWindowTitle("Fluid Tiles test — "+label); });
    }
    demo.show(); return app.exec();
}
