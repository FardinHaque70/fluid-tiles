// SPDX-License-Identifier: GPL-3.0-or-later
#include "animationeditor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
class PreciseSpinBox final : public QDoubleSpinBox {
  protected:
    QString textFromValue(double value) const override {
        auto text = QDoubleSpinBox::textFromValue(value);
        const auto decimal = locale().decimalPoint();
        if (text.contains(decimal)) {
            while (text.endsWith('0'))
                text.chop(1);
            if (text.endsWith(decimal))
                text.chop(decimal.size());
        }
        return text;
    }
};
} // namespace

CurveCanvas::CurveCanvas(QWidget *p) : QWidget(p) {
    setMinimumHeight(150);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Animation curve; arrow keys move the selected handle, "
                         "Space switches handles"));
    setToolTip(tr("Drag either Bézier handle. Arrow keys adjust it; Space "
                  "selects the other handle."));
}
void CurveCanvas::setCurve(const Fluid::AnimationCurve &c) {
    curve = c;
    if (!m_dragging) {
        m_yLow = std::min({-.2, c.y1 - .15, c.y2 - .15});
        m_yHigh = std::max({1.2, c.y1 + .15, c.y2 + .15});
    }
    update();
}
QPointF CurveCanvas::point(double x, double y) const {
    return {24 + x * (width() - 48), height() - 22 - (y - m_yLow) / (m_yHigh - m_yLow) * (height() - 38)};
}
void CurveCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().brush(QPalette::Base));
    p.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DotLine));
    for (double x : {0., .25, .5, .75, 1.})
        p.drawLine(point(x, m_yLow), point(x, m_yHigh));
    for (double y : {0., .5, 1.})
        p.drawLine(point(0, y), point(1, y));
    QPainterPath path;
    path.moveTo(point(0, 0));
    if (curve.kind == Fluid::AnimationCurve::Bezier)
        path.cubicTo(point(curve.x1, curve.y1), point(curve.x2, curve.y2), point(1, 1));
    else {
        Fluid::ScalarAnimation a;
        a.reset(0);
        a.target(1, curve);
        for (int i = 1; i <= 160; ++i) {
            a.step(1. / 160);
            path.lineTo(point(i / 160., a.value));
        }
    }
    p.setPen(QPen(palette().color(QPalette::Highlight), 2.5));
    p.drawPath(path);
    if (curve.kind == Fluid::AnimationCurve::Bezier) {
        p.setPen(QPen(palette().color(QPalette::Mid), 1.5));
        p.drawLine(point(0, 0), point(curve.x1, curve.y1));
        p.drawLine(point(1, 1), point(curve.x2, curve.y2));
        for (int i = 0; i < 2; ++i) {
            p.setBrush(palette().color(i == m_handle ? QPalette::Highlight : QPalette::Text));
            p.drawEllipse(point(i ? curve.x2 : curve.x1, i ? curve.y2 : curve.y1), 6, 6);
        }
    }
    p.setPen(palette().color(QPalette::Text));
    p.drawText(8, 16,
               curve.kind == Fluid::AnimationCurve::Spring ? tr("Spring response · 1 second")
                                                           : tr("Progress"));
    if (hasFocus()) {
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        focus.rect = rect().adjusted(2, 2, -2, -2);
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &p, this);
    }
}
void CurveCanvas::mousePressEvent(QMouseEvent *e) {
    if (curve.kind != Fluid::AnimationCurve::Bezier)
        return;
    setFocus();
    const auto a = e->position() - point(curve.x1, curve.y1), b = e->position() - point(curve.x2, curve.y2);
    m_handle = QPointF::dotProduct(a, a) < QPointF::dotProduct(b, b) ? 0 : 1;
    m_dragging = true;
    mouseMoveEvent(e);
}
void CurveCanvas::mouseMoveEvent(QMouseEvent *e) {
    if (!m_dragging)
        return;
    double x = std::clamp((e->position().x() - 24) / (width() - 48), 0., 1.),
           y = std::clamp((height() - 22 - e->position().y()) * (m_yHigh - m_yLow) / (height() - 38) + m_yLow,
                          -2., 3.);
    if (m_handle) {
        curve.x2 = x;
        curve.y2 = y;
    } else {
        curve.x1 = x;
        curve.y1 = y;
    }
    update();
    Q_EMIT edited();
}
void CurveCanvas::mouseReleaseEvent(QMouseEvent *) {
    m_dragging = false;
    setCurve(curve);
}
void CurveCanvas::keyPressEvent(QKeyEvent *e) {
    if (curve.kind != Fluid::AnimationCurve::Bezier) {
        QWidget::keyPressEvent(e);
        return;
    }
    if (e->key() == Qt::Key_Space) {
        m_handle = 1 - m_handle;
        update();
        return;
    }
    double d = (e->modifiers() & Qt::ShiftModifier) ? .1 : .01;
    auto &x = m_handle ? curve.x2 : curve.x1;
    auto &y = m_handle ? curve.y2 : curve.y1;
    if (e->key() == Qt::Key_Left)
        x -= d;
    else if (e->key() == Qt::Key_Right)
        x += d;
    else if (e->key() == Qt::Key_Up)
        y += d;
    else if (e->key() == Qt::Key_Down)
        y -= d;
    else {
        QWidget::keyPressEvent(e);
        return;
    }
    x = std::clamp(x, 0., 1.);
    y = std::clamp(y, -2., 3.);
    update();
    Q_EMIT edited();
}

AnimationPreview::AnimationPreview(QWidget *p) : QWidget(p) {
    setMinimumHeight(140);
    setMaximumHeight(180);
    setAccessibleName(tr("Tiling animation preview"));
    m_timer.setInterval(8);
    connect(&m_timer, &QTimer::timeout, this, &AnimationPreview::tick);
    resetScene();
}
QRectF AnimationPreview::area() const {
    return QRectF(16, 26, std::max(100, width() - 32), std::max(80, height() - 42));
}
void AnimationPreview::resetScene() {
    const auto r = area();
    const double half = (r.width() - 8) / 2;
    std::array<QRectF, 3> boxes{QRectF(r.x(), r.y(), half, r.height()),
                                QRectF(r.x() + half + 8, r.y(), half, (r.height() - 8) / 2),
                                QRectF(r.x() + half + 8, r.center().y() + 4, half, (r.height() - 8) / 2)};
    for (int i = 0; i < 3; ++i) {
        m_tiles[i].motion.reset(boxes[i]);
        m_tiles[i].opacity.reset(1);
        m_tiles[i].visible = true;
        m_tiles[i].closing = false;
    }
}
void AnimationPreview::move(Tile &t, const QRectF &r, Fluid::AnimationEvent e) {
    const auto p = m_settings.profile(e);
    t.motion.configure(p.curve);
    if (p.enabled)
        t.motion.target(r);
    else
        t.motion.reset(r);
}
void AnimationPreview::play(const QString &action) {
    using E = Fluid::AnimationEvent;
    if (action != "Interrupt")
        resetScene();
    const auto r = area();
    auto &t = m_tiles[2];
    if (action == "Open") {
        const auto p = m_settings.profile(E::Open);
        const auto goal = t.motion.goal();
        if (p.enabled) {
            t.motion.reset(Fluid::transitionRect(p, goal, r));
            if (p.fade) {
                t.opacity.reset(0);
                t.opacity.target(1, p.opacity);
            }
        }
        move(t, goal, E::Open);
    } else if (action == "Close") {
        const auto p = m_settings.profile(E::Close);
        t.closing = true;
        move(t, Fluid::transitionRect(p, t.motion.rect(), r, true), E::Close);
        if (p.enabled && p.fade)
            t.opacity.target(0, p.opacity);
        else
            t.opacity.reset(0);
        move(m_tiles[1], QRectF(r.center().x() + 4, r.top(), (r.width() - 8) / 2, r.height()), E::Rearrange);
    } else if (action == "Float")
        move(t,
             QRectF(r.center() - QPointF(r.width() * .22, r.height() * .3),
                    QSizeF(r.width() * .44, r.height() * .6)),
             E::Float);
    else if (action == "Drop") {
        const auto goal = t.motion.goal();
        t.motion.reset(QRectF(r.topLeft() + QPointF(10, 5), goal.size()));
        move(t, goal, E::Drop);
    } else if (action == "Resize") {
        auto a = m_tiles[0].motion.rect();
        a.setWidth(r.width() * .7);
        m_tiles[0].motion.reset(a);
        move(m_tiles[0], QRectF(r.topLeft(), QSizeF((r.width() - 8) / 2, r.height())), E::Drop);
    } else {
        const auto a = m_tiles[0].motion.goal(), b = t.motion.goal();
        move(m_tiles[0], b, E::Rearrange);
        move(t, a, E::Rearrange);
    }
    if (!m_clock.isValid())
        m_clock.start();
    m_last = m_clock.nsecsElapsed();
    if (isVisible())
        m_timer.start();
    update();
}
void AnimationPreview::tick() {
    const auto now = m_clock.nsecsElapsed();
    const double dt = (now - m_last) / 1e9 * m_settings.speed();
    m_last = now;
    bool moving = false;
    for (auto &t : m_tiles) {
        t.motion.step(dt, devicePixelRatioF());
        t.opacity.step(dt);
        moving |= t.motion.moving() || t.opacity.moving();
        if (t.closing && !t.motion.moving() && !t.opacity.moving())
            t.visible = false;
    }
    if (!moving)
        m_timer.stop();
    update();
}
void AnimationPreview::hideEvent(QHideEvent *e) {
    m_timer.stop();
    QWidget::hideEvent(e);
}
void AnimationPreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().brush(QPalette::AlternateBase));
    p.setPen(palette().color(QPalette::Text));
    p.drawText(16, 18, tr("Preview · changes stay here until Apply"));
    if (!m_clock.isValid())
        resetScene();
    const auto colors =
        std::array<QColor, 3>{palette().color(QPalette::Highlight), QColor("#298d80"), QColor("#9866bb")};
    p.setClipRect(area());
    for (int i = 0; i < 3; ++i) {
        const auto &t = m_tiles[i];
        if (!t.visible)
            continue;
        p.setOpacity(std::clamp(t.opacity.value, 0., 1.));
        p.setBrush(colors[i]);
        p.setPen(QPen(colors[i].lighter(145), 1.5));
        p.drawRoundedRect(t.motion.rect(), 6, 6);
        p.setPen(Qt::white);
        p.drawText(t.motion.rect(), Qt::AlignCenter, QString::number(i + 1));
    }
}

AnimationEditor::AnimationEditor(QWidget *parent) : QWidget(parent) {
    setObjectName("kcfg_AnimationSettings");
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto top = new QHBoxLayout;
    m_enabled = new QCheckBox(tr("Animate tiled windows"));
    m_enabled->setObjectName("animationsEnabled");
    top->addWidget(m_enabled);
    top->addStretch();
    top->addWidget(new QLabel(tr("Speed:")));
    m_speed = new QSlider(Qt::Horizontal);
    m_speed->setObjectName("animationSpeed");
    m_speed->setRange(50, 200);
    m_speed->setMaximumWidth(160);
    m_speed->setAccessibleName(tr("Animation speed"));
    top->addWidget(m_speed);
    m_speedLabel = new QLabel;
    top->addWidget(m_speedLabel);
    root->addLayout(top);
    m_presets = new QListWidget;
    m_presets->setObjectName("animationPresets");
    m_presets->setViewMode(QListView::IconMode);
    m_presets->setResizeMode(QListView::Adjust);
    m_presets->setMovement(QListView::Static);
    m_presets->setIconSize({34, 22});
    m_presets->setGridSize({140, 60});
    m_presets->setMinimumHeight(130);
    m_presets->setMaximumHeight(140);
    m_presets->setWordWrap(true);
    root->addWidget(m_presets);
    m_description = new QLabel;
    m_description->setWordWrap(true);
    m_description->setMinimumHeight(34);
    root->addWidget(m_description);
    auto saved = new QHBoxLayout;
    auto button = [&](const QString &text, const char *name, auto slot) {
        auto b = new QPushButton(text);
        b->setObjectName(name);
        saved->addWidget(b);
        connect(b, &QPushButton::clicked, this, slot);
    };
    button(tr("Save As…"), "animationSaveAs", &AnimationEditor::saveAs);
    button(tr("Rename…"), "animationRename", &AnimationEditor::renamePreset);
    button(tr("Delete"), "animationDelete", &AnimationEditor::deletePreset);
    saved->addStretch();
    auto details = new QPushButton(tr("Preset details"));
    saved->addWidget(details);
    connect(details, &QPushButton::clicked, this, [this] {
        auto dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Animation preset details"));
        auto layout = new QVBoxLayout(dialog);
        QString text;
        for (const auto &p : Fluid::animationPresets())
            if (p.id == m_settings.presetId()) {
                text = "<b>" + p.name.toHtmlEscaped() + "</b><p>" + p.description.toHtmlEscaped() + "</p>";
                for (const auto &v : Fluid::animationProvenance()) {
                    const auto s = v.toObject();
                    if (s["file"].toString() != p.source)
                        continue;
                    const auto url = "https://github.com/" + s["repository"].toString() + "/blob/" +
                                     s["revision"].toString() + "/" + s["path"].toString();
                    text += "<p><a href=\"" + url.toHtmlEscaped() + "\">" +
                            s["repository"].toString().toHtmlEscaped() + " · original recipe</a></p><p>" +
                            tr("Revision: ") + s["revision"].toString().left(12) + "</p>";
                    text += "<p>" +
                            tr("Window geometry and opacity are adapted to KWin. "
                               "Workspace, shell, popup and rotating-border effects are "
                               "omitted. Slides use the usable work area; movement style "
                               "hints do not add a second entrance.") +
                            "</p>";
                }
            }
        if (text.isEmpty())
            text = tr("Your saved animation settings. Built-in source recipes, "
                      "credits and license notices are included with Fluid Tiles.");
        auto label = new QLabel(text);
        label->setWordWrap(true);
        label->setOpenExternalLinks(true);
        layout->addWidget(label);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
        dialog->resize(480, 240);
        dialog->open();
    });
    button(tr("Reset animations"), "animationReset", &AnimationEditor::resetAll);
    root->addLayout(saved);
    m_error = new QLabel;
    m_error->setWordWrap(true);
    root->addWidget(m_error);
    m_preview = new AnimationPreview;
    m_preview->setObjectName("animationPreview");
    root->addWidget(m_preview);
    auto actions = new QHBoxLayout;
    for (const QString &name : {"Open", "Close", "Rearrange", "Drop", "Resize", "Float", "Interrupt"}) {
        auto b = new QPushButton(tr(qPrintable(name)));
        b->setObjectName("preview" + name);
        actions->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, name] { m_preview->play(name); });
    }
    root->addLayout(actions);
    auto advanced = new QToolButton;
    advanced->setText(tr("Event settings & curve editor"));
    advanced->setCheckable(true);
    advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advanced->setArrowType(Qt::RightArrow);
    advanced->setObjectName("animationAdvanced");
    root->addWidget(advanced);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVisible(false);
    root->addWidget(scroll, 1);
    auto options = new QWidget;
    auto layout = new QVBoxLayout(options);
    layout->setContentsMargins(0, 0, 6, 0);
    scroll->setWidget(options);
    connect(advanced, &QToolButton::toggled, this, [scroll, advanced](bool show) {
        scroll->setVisible(show);
        advanced->setArrowType(show ? Qt::DownArrow : Qt::RightArrow);
    });
    auto events = new QHBoxLayout;
    m_event = new QComboBox;
    m_event->setObjectName("animationEvent");
    m_event->addItems(
        {tr("Opening"), tr("Closing"), tr("Rearrangement"), tr("Drop settling"), tr("Floating transition")});
    events->addWidget(m_event);
    m_inherit = new QCheckBox(tr("Use rearrangement settings"));
    m_inherit->setObjectName("animationInherit");
    events->addWidget(m_inherit);
    events->addStretch();
    auto reset = new QPushButton(tr("Reset event"));
    reset->setObjectName("animationResetEvent");
    events->addWidget(reset);
    layout->addLayout(events);
    m_details = new QGroupBox(tr("Event profile"));
    layout->addWidget(m_details);
    auto form = new QFormLayout(m_details);
    m_eventEnabled = new QCheckBox(tr("Enabled"));
    m_eventEnabled->setObjectName("animationEventEnabled");
    form->addRow(m_eventEnabled);
    m_style = new QComboBox;
    m_style->setObjectName("animationStyle");
    form->addRow(tr("Style:"), m_style);
    m_origin = new QComboBox;
    m_origin->setObjectName("animationOrigin");
    m_origin->addItem(tr("Destination center"), "tile");
    m_origin->addItem(tr("Desktop center"), "desktop");
    form->addRow(tr("Entrance origin:"), m_origin);
    auto spin = [&](const char *name, double lo, double hi, int decimals = 2) {
        auto w = new PreciseSpinBox;
        w->setObjectName(name);
        w->setRange(lo, hi);
        w->setDecimals(decimals);
        w->setSingleStep(decimals ? .05 : 1);
        return w;
    };
    m_size = spin("animationSize", 0, 100, 0);
    m_size->setSuffix(tr(" %"));
    form->addRow(tr("Start / end size:"), m_size);
    m_direction = new QComboBox;
    m_direction->setObjectName("animationDirection");
    for (const QString &d : {"nearest", "top", "bottom", "left", "right"})
        m_direction->addItem(d, d);
    form->addRow(tr("Slide direction:"), m_direction);
    m_fade = new QCheckBox(tr("Animate opacity separately"));
    m_fade->setObjectName("animationFade");
    form->addRow(m_fade);
    m_track = new QComboBox;
    m_track->setObjectName("animationTrack");
    m_track->addItems({tr("Geometry curve"), tr("Opacity curve")});
    form->addRow(tr("Editing:"), m_track);
    m_type = new QComboBox;
    m_type->setObjectName("animationCurveType");
    m_type->addItems({tr("Spring"), tr("Cubic Bézier")});
    form->addRow(tr("Motion curve:"), m_type);
    m_library = new QComboBox;
    m_library->setObjectName("animationCurveLibrary");
    m_library->addItem(tr("Choose an easing…"), "");
    const auto library = Fluid::animationCurveLibrary();
    for (auto it = library.begin(); it != library.end(); ++it)
        m_library->addItem(it.key(), it.key());
    form->addRow(tr("Easing library:"), m_library);
    m_mass = spin("animationMass", .01, 100, 8);
    m_stiffness = spin("animationStiffness", 1, 100000, 8);
    m_damping = spin("animationDamping", .1, 10000, 8);
    form->addRow(tr("Mass:"), m_mass);
    form->addRow(tr("Stiffness:"), m_stiffness);
    form->addRow(tr("Damping:"), m_damping);
    m_bounce = new QSlider(Qt::Horizontal);
    m_bounce->setRange(0, 100);
    m_bounce->setObjectName("animationBounce");
    form->addRow(tr("Bounce:"), m_bounce);
    m_duration = spin("animationDuration", 1, 10000, 0);
    m_duration->setSuffix(tr(" ms"));
    form->addRow(tr("Duration:"), m_duration);
    auto pointsWidget = new QWidget;
    pointsWidget->setObjectName("animationBezierPoints");
    auto points = new QHBoxLayout(pointsWidget);
    points->setContentsMargins(0, 0, 0, 0);
    const QStringList labels{"X1", "Y1", "X2", "Y2"};
    for (int i = 0; i < 4; ++i) {
        auto label = new QLabel(labels[i]);
        points->addWidget(label);
        m_points[i] = spin(qPrintable("animation" + labels[i]), i % 2 ? -2 : 0, i % 2 ? 3 : 1, 3);
        label->setBuddy(m_points[i]);
        m_points[i]->setAccessibleName(labels[i]);
        points->addWidget(m_points[i]);
    }
    form->addRow(pointsWidget);
    m_canvas = new CurveCanvas;
    m_canvas->setObjectName("animationCurveCanvas");
    form->addRow(m_canvas);
    auto note = new QLabel(tr("Springs settle by physics. Duration applies to Bézier curves. Speed "
                              "affects both. Held drags and resizes follow the pointer directly."));
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();
    connect(m_event, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_track->setCurrentIndex(0);
        refresh();
    });
    connect(m_track, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        auto o = m_settings.document["overrides"].toObject();
        o.remove(Fluid::eventKey(event()));
        m_settings.document["overrides"] = o;
        commit();
        refresh();
    });
    connect(m_presets, &QListWidget::currentItemChanged, this, [this](auto item) {
        if (m_loading || !item)
            return;
        m_settings.select(item->data(Qt::UserRole).toString());
        commit();
        refresh();
        m_preview->play("Open");
    });
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_loading)
            return;
        m_settings.document["enabled"] = enabled;
        commit();
    });
    connect(m_speed, &QSlider::valueChanged, this, [this](int v) {
        if (m_loading)
            return;
        m_settings.document["speed"] = v / 100.;
        m_speedLabel->setText(QString::number(v / 100., 'f', 2) + QString::fromUtf8("×"));
        commit();
    });
    for (auto box : {m_eventEnabled, m_inherit, m_fade})
        connect(box, &QCheckBox::toggled, this, [this] { edit(); });
    for (auto combo : {m_type, m_style, m_origin, m_direction})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { edit(); });
    for (auto w : {m_mass, m_stiffness, m_damping, m_duration, m_size, m_points[0], m_points[1], m_points[2],
                   m_points[3]})
        connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { edit(); });
    connect(m_bounce, &QSlider::valueChanged, this, [this](int v) {
        if (m_loading)
            return;
        const double z = 1. - v * .0045;
        {
            QSignalBlocker block(m_damping);
            m_damping->setValue(2 * z * std::sqrt(m_mass->value() * m_stiffness->value()));
        }
        edit();
    });
    connect(m_library, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (!index)
            return;
        auto p = rawProfile();
        auto c = Fluid::AnimationCurve::fromJson(
            Fluid::animationCurveLibrary()[m_library->itemData(index).toString()].toObject());
        c.duration = m_duration->value();
        if (m_track->currentIndex())
            p.opacity = c;
        else
            p.curve = c;
        m_settings.setProfile(event(), p);
        commit();
        refresh();
        m_preview->play("Open");
    });
    connect(m_canvas, &CurveCanvas::edited, this, [this] {
        if (m_loading)
            return;
        auto p = rawProfile();
        if (m_track->currentIndex())
            p.opacity = m_canvas->curve;
        else
            p.curve = m_canvas->curve;
        m_settings.setProfile(event(), p);
        commit();
        refresh();
    });
    root->addStretch();
    rebuildPresets();
    refresh();
}
Fluid::AnimationEvent AnimationEditor::event() const {
    return Fluid::animationEvents.at(m_event->currentIndex());
}
Fluid::AnimationProfile AnimationEditor::rawProfile() const {
    const auto key = Fluid::eventKey(event());
    const auto overrides = m_settings.document["overrides"].toObject();
    if (overrides.contains(key))
        return Fluid::AnimationProfile::fromJson(overrides[key].toObject());
    for (const auto &p : Fluid::animationPresets())
        if (p.id == m_settings.presetId())
            return Fluid::AnimationProfile::fromJson(p.profiles[key].toObject());
    for (const auto &v : m_settings.document["customPresets"].toArray())
        if (v.toObject()["id"].toString() == m_settings.presetId())
            return Fluid::AnimationProfile::fromJson(v.toObject()["profiles"].toObject()[key].toObject());
    return {};
}
void AnimationEditor::setSettings(const QString &value) {
    auto custom = m_settings.document["customPresets"].toArray();
    m_settings = Fluid::AnimationSettings::read(value);
    m_value = value;
    if (value.isEmpty() && !custom.isEmpty()) {
        m_settings.document["customPresets"] = custom;
        m_value = m_settings.serialized();
    }
    rebuildPresets();
    refresh();
    Q_EMIT settingsChanged();
}
void AnimationEditor::rebuildPresets() {
    m_loading = true;
    m_presets->clear();
    auto add = [&](const QString &name, const QString &id) {
        QPixmap icon(34, 22);
        icon.fill(Qt::transparent);
        QPainter paint(&icon);
        paint.setRenderHint(QPainter::Antialiasing);
        paint.setPen(Qt::NoPen);
        paint.setBrush(palette().color(QPalette::Highlight));
        paint.drawRoundedRect(1, 1, 14, 20, 2, 2);
        paint.setBrush(palette().color(QPalette::Highlight).lighter(130));
        paint.drawRoundedRect(18, 1, 15, 9, 2, 2);
        paint.drawRoundedRect(18, 12, 15, 9, 2, 2);
        auto item = new QListWidgetItem(QIcon(icon), name, m_presets);
        item->setData(Qt::UserRole, id);
        item->setTextAlignment(Qt::AlignCenter);
        if (id == m_settings.presetId())
            m_presets->setCurrentItem(item);
    };
    for (const auto &p : Fluid::animationPresets())
        add(p.name, p.id);
    for (const auto &v : m_settings.document["customPresets"].toArray()) {
        const auto o = v.toObject();
        add(o["name"].toString(), o["id"].toString());
    }
    m_loading = false;
}
void AnimationEditor::refresh() {
    m_loading = true;
    const auto raw = rawProfile();
    const auto p = raw.inherit ? m_settings.profile(event()) : raw;
    const auto c = m_track->currentIndex() ? p.opacity : p.curve;
    m_enabled->setChecked(m_settings.enabled());
    m_speed->setValue(qRound(m_settings.speed() * 100));
    m_speedLabel->setText(QString::number(m_settings.speed(), 'f', 2) + QString::fromUtf8("×"));
    QString desc;
    for (const auto &preset : Fluid::animationPresets())
        if (preset.id == m_settings.presetId()) {
            desc = preset.description;
            m_description->setToolTip(preset.source);
        }
    if (desc.isEmpty())
        desc = tr("Your saved animation profile.");
    if (!m_settings.document["overrides"].toObject().isEmpty())
        desc = tr("Custom · ") + desc;
    m_description->setText(desc);
    m_error->setText(m_settings.error);
    m_error->setVisible(!m_settings.error.isEmpty());
    const bool child = event() == Fluid::AnimationEvent::Drop || event() == Fluid::AnimationEvent::Float,
               lifecycle = event() == Fluid::AnimationEvent::Open || event() == Fluid::AnimationEvent::Close;
    m_inherit->setVisible(child);
    m_inherit->setChecked(raw.inherit);
    m_details->setEnabled(!raw.inherit);
    m_eventEnabled->setChecked(p.enabled);
    m_style->clear();
    m_style->addItem(tr("Pop"), "pop");
    m_style->addItem(tr("Slide"), "slide");
    m_style->addItem(tr("Fade only"), "fade");
    if (event() == Fluid::AnimationEvent::Close)
        m_style->addItem(tr("Use KDE closing effect"), "system");
    m_style->setCurrentIndex(m_style->findData(p.style));
    m_style->setEnabled(lifecycle);
    m_origin->setCurrentIndex(m_origin->findData(p.origin));
    m_origin->setEnabled(event() == Fluid::AnimationEvent::Open && p.style == "pop");
    m_size->setValue(p.size * 100);
    m_size->setEnabled(lifecycle && p.style == "pop");
    m_direction->setCurrentIndex(m_direction->findData(p.direction));
    m_direction->setEnabled(lifecycle && p.style == "slide");
    m_fade->setVisible(lifecycle);
    m_fade->setChecked(p.fade);
    m_track->setEnabled(lifecycle && p.fade);
    m_type->setCurrentIndex(c.kind == Fluid::AnimationCurve::Spring ? 0 : 1);
    m_mass->setValue(c.mass);
    m_stiffness->setValue(c.stiffness);
    m_damping->setValue(c.damping);
    m_duration->setValue(c.duration);
    const bool spring = c.kind == Fluid::AnimationCurve::Spring;
    auto form = static_cast<QFormLayout *>(m_details->layout());
    form->setRowVisible(m_style, lifecycle);
    form->setRowVisible(m_origin, event() == Fluid::AnimationEvent::Open && p.style == "pop");
    form->setRowVisible(m_size, lifecycle && p.style == "pop");
    form->setRowVisible(m_direction, lifecycle && p.style == "slide");
    form->setRowVisible(m_track, lifecycle && p.fade);
    for (auto w : {m_mass, m_stiffness, m_damping})
        form->setRowVisible(w, spring);
    form->setRowVisible(m_bounce, spring);
    m_bounce->setValue(
        qRound(std::clamp((1 - c.damping / (2 * std::sqrt(c.mass * c.stiffness))) / .0045, 0., 100.)));
    form->setRowVisible(m_duration, !spring);
    form->setRowVisible(m_library, !spring);
    form->setRowVisible(findChild<QWidget *>("animationBezierPoints"), !spring);
    m_library->setCurrentIndex(0);
    const std::array<double, 4> values{c.x1, c.y1, c.x2, c.y2};
    for (int i = 0; i < 4; ++i) {
        m_points[i]->setValue(values[i]);
        m_points[i]->setVisible(!spring);
    }
    m_canvas->setCurve(c);
    bool custom = false;
    for (const auto &v : m_settings.document["customPresets"].toArray())
        custom |= v.toObject()["id"].toString() == m_settings.presetId();
    findChild<QPushButton *>("animationRename")->setEnabled(custom);
    findChild<QPushButton *>("animationDelete")->setEnabled(custom);
    m_preview->setSettings(m_settings);
    m_loading = false;
}
void AnimationEditor::edit() {
    if (m_loading)
        return;
    auto p = rawProfile();
    p.enabled = m_eventEnabled->isChecked();
    p.inherit = (event() == Fluid::AnimationEvent::Drop || event() == Fluid::AnimationEvent::Float) &&
                m_inherit->isChecked();
    p.fade = m_fade->isChecked();
    p.style = m_style->currentData().toString();
    p.origin = m_origin->currentData().toString();
    p.direction = m_direction->currentData().toString();
    p.size = m_size->value() / 100.;
    auto &c = m_track->currentIndex() ? p.opacity : p.curve;
    c.kind = m_type->currentIndex() ? Fluid::AnimationCurve::Bezier : Fluid::AnimationCurve::Spring;
    c.mass = m_mass->value();
    c.stiffness = m_stiffness->value();
    c.damping = m_damping->value();
    c.duration = m_duration->value();
    c.x1 = m_points[0]->value();
    c.y1 = m_points[1]->value();
    c.x2 = m_points[2]->value();
    c.y2 = m_points[3]->value();
    c.classic = false;
    bool valid = false;
    Fluid::AnimationCurve::fromJson(c.json(), &valid);
    if (!valid) {
        m_error->setText(tr("Choose positive spring values with a stable "
                            "stiffness-to-mass ratio."));
        m_error->show();
        return;
    }
    m_settings.setProfile(event(), p);
    commit();
    refresh();
}
void AnimationEditor::commit() {
    m_value = m_settings.serialized();
    m_preview->setSettings(m_settings);
    Q_EMIT settingsChanged();
}
void AnimationEditor::resetAll() {
    auto custom = m_settings.document["customPresets"];
    m_settings = Fluid::AnimationSettings{};
    m_settings.document["customPresets"] = custom;
    commit();
    rebuildPresets();
    refresh();
}
void AnimationEditor::saveAs() {
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Save animation preset"), tr("Name:"), QLineEdit::Normal,
                                         tr("My animation"), &ok)
                       .trimmed();
    if (!ok || name.isEmpty())
        return;
    auto custom = m_settings.document["customPresets"].toArray();
    auto stored = m_settings;
    stored.document["enabled"] = true; // Master off must not erase per-event choices.
    const QString id = "user-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    custom.append(QJsonObject{{"id", id},
                              {"name", name},
                              {"profiles", stored.profiles()},
                              {"speed", m_settings.speed()},
                              {"enabled", m_settings.enabled()}});
    m_settings.document["customPresets"] = custom;
    m_settings.select(id);
    commit();
    rebuildPresets();
    refresh();
}
void AnimationEditor::renamePreset() {
    auto custom = m_settings.document["customPresets"].toArray();
    for (int i = 0; i < custom.size(); ++i) {
        auto p = custom[i].toObject();
        if (p["id"].toString() != m_settings.presetId())
            continue;
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("Rename preset"), tr("Name:"), QLineEdit::Normal,
                                                p["name"].toString(), &ok)
                              .trimmed();
        if (!ok || name.isEmpty())
            return;
        p["name"] = name;
        custom[i] = p;
    }
    m_settings.document["customPresets"] = custom;
    commit();
    rebuildPresets();
    refresh();
}
void AnimationEditor::deletePreset() {
    auto custom = m_settings.document["customPresets"].toArray();
    for (int i = custom.size() - 1; i >= 0; --i)
        if (custom[i].toObject()["id"].toString() == m_settings.presetId())
            custom.removeAt(i);
    m_settings.document["customPresets"] = custom;
    m_settings.select("fluid");
    commit();
    rebuildPresets();
    refresh();
}
