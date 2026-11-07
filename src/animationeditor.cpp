// SPDX-License-Identifier: GPL-3.0-or-later
#include "animationeditor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
class PreciseSpinBox final : public QDoubleSpinBox {
  public:
    QSize sizeHint() const override { return {110, QDoubleSpinBox::sizeHint().height()}; }
    QSize minimumSizeHint() const override { return {80, QDoubleSpinBox::minimumSizeHint().height()}; }

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
    setMinimumHeight(130);
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

AnimationEditor::AnimationEditor(QWidget *parent) : QWidget(parent) {
    setObjectName("kcfg_AnimationSettings");
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto top = new QHBoxLayout;
    m_enabled = new QCheckBox(tr("Animate tiled windows"));
    m_enabled->setObjectName("animationsEnabled");
    top->addWidget(m_enabled);
    top->addStretch();
    auto speedLabel = new QLabel(tr("Speed:"));
    top->addWidget(speedLabel);
    m_speed = new PreciseSpinBox;
    m_speed->setObjectName("animationSpeed");
    m_speed->setRange(.1, 2);
    m_speed->setDecimals(2);
    m_speed->setSingleStep(.1);
    m_speed->setSuffix(QString::fromUtf8("×"));
    m_speed->setAccessibleName(tr("Animation speed"));
    speedLabel->setBuddy(m_speed);
    top->addWidget(m_speed);
    root->addLayout(top);
    auto presets = new QHBoxLayout;
    auto presetLabel = new QLabel(tr("Preset:"));
    presets->addWidget(presetLabel);
    m_presets = new QComboBox;
    m_presets->setObjectName("animationPresets");
    m_presets->setPlaceholderText(tr("Custom settings"));
    presetLabel->setBuddy(m_presets);
    presets->addWidget(m_presets, 1);
    auto details = new QPushButton(tr("Preset details"));
    presets->addWidget(details);
    connect(details, &QPushButton::clicked, this, [this] {
        auto dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Animation preset details"));
        auto layout = new QVBoxLayout(dialog);
        QString text;
        for (const auto &p : Fluid::animationPresets())
            if (p.id == m_settings.presetId()) {
                const auto name = p.id == "fluid" ? tr("Default") : p.id == "hyprland" ? tr("Elastic") : p.name;
                text = "<b>" + name.toHtmlEscaped() + "</b><p>" + p.description.toHtmlEscaped() + "</p>";
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
    auto resetAll = new QPushButton(tr("Reset animations"));
    resetAll->setObjectName("animationReset");
    presets->addWidget(resetAll);
    connect(resetAll, &QPushButton::clicked, this, &AnimationEditor::resetAll);
    root->addLayout(presets);
    m_description = new QLabel;
    m_description->setWordWrap(true);
    root->addWidget(m_description);
    m_error = new QLabel;
    m_error->setWordWrap(true);
    root->addWidget(m_error);
    auto scroll = new QScrollArea;
    scroll->setObjectName("animationOptionsScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll, 1);
    auto options = new QWidget;
    auto layout = new QVBoxLayout(options);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(options);
    auto events = new QHBoxLayout;
    m_event = new QComboBox;
    m_event->setObjectName("animationEvent");
    m_event->addItems(
        {tr("Opening"), tr("Closing"), tr("Rearrangement"), tr("Drop settling"), tr("Floating transition")});
    events->addWidget(new QLabel(tr("Event:")));
    events->addWidget(m_event);
    m_inherit = new QCheckBox(tr("Use rearrangement settings"));
    m_inherit->setObjectName("animationInherit");
    events->addWidget(m_inherit);
    events->addStretch();
    auto reset = new QPushButton(tr("Reset event"));
    reset->setObjectName("animationResetEvent");
    events->addWidget(reset);
    layout->addLayout(events);
    auto columns = new QHBoxLayout;
    layout->addLayout(columns);
    m_details = new QGroupBox(tr("Event"));
    m_details->setObjectName("animationEventPanel");
    columns->addWidget(m_details);
    auto form = new QFormLayout(m_details);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setFormAlignment(Qt::AlignTop);
    m_curveDetails = new QGroupBox(tr("Curve"));
    m_curveDetails->setObjectName("animationCurvePanel");
    columns->addWidget(m_curveDetails, 1);
    auto curveForm = new QFormLayout(m_curveDetails);
    curveForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    curveForm->setFormAlignment(Qt::AlignTop);
    m_eventEnabled = new QCheckBox(tr("Animate this event"));
    m_eventEnabled->setObjectName("animationEventEnabled");
    form->addRow(m_eventEnabled);
    m_style = new QComboBox;
    m_style->setObjectName("animationStyle");
    form->addRow(tr("Style:"), m_style);
    m_origin = new QComboBox;
    m_origin->setObjectName("animationOrigin");
    m_origin->addItem(tr("Destination center"), "tile");
    m_origin->addItem(tr("Desktop center"), "desktop");
    form->addRow(tr("Origin:"), m_origin);
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
    form->addRow(tr("Size:"), m_size);
    m_direction = new QComboBox;
    m_direction->setObjectName("animationDirection");
    for (const QString &d : {"nearest", "top", "bottom", "left", "right"})
        m_direction->addItem(d, d);
    form->addRow(tr("Slide direction:"), m_direction);
    m_fade = new QCheckBox(tr("Fade opacity"));
    m_fade->setObjectName("animationFade");
    form->addRow(m_fade);
    m_track = new QComboBox;
    m_track->setObjectName("animationTrack");
    m_track->addItems({tr("Geometry curve"), tr("Opacity curve")});
    auto selectors = new QHBoxLayout;
    selectors->addWidget(m_track);
    m_type = new QComboBox;
    m_type->setObjectName("animationCurveType");
    m_type->addItems({tr("Spring"), tr("Cubic Bézier")});
    selectors->addWidget(m_type);
    curveForm->addRow(selectors);
    m_type->setAccessibleName(tr("Motion curve type"));
    m_track->setAccessibleName(tr("Animation track"));
    m_library = new QComboBox;
    m_library->setObjectName("animationCurveLibrary");
    m_library->addItem(tr("Choose an easing…"), "");
    const auto library = Fluid::animationCurveLibrary();
    for (auto it = library.begin(); it != library.end(); ++it)
        m_library->addItem(it.key(), it.key());
    m_library->setAccessibleName(tr("Easing library"));
    m_library->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_library->setMinimumContentsLength(12);
    m_mass = spin("animationMass", .01, 100, 8);
    m_stiffness = spin("animationStiffness", 1, 100000, 8);
    m_damping = spin("animationDamping", .1, 10000, 8);
    auto physics = new QWidget;
    physics->setObjectName("animationSpringParameters");
    auto physicsLayout = new QGridLayout(physics);
    physicsLayout->setContentsMargins(0, 0, 0, 0);
    const std::array<QDoubleSpinBox *, 3> parameters{m_mass, m_stiffness, m_damping};
    const QStringList names{tr("Mass"), tr("Stiffness"), tr("Damping")};
    for (int i = 0; i < 3; ++i) {
        auto label = new QLabel(names[i]);
        label->setBuddy(parameters[i]);
        physicsLayout->addWidget(label, 0, i);
        physicsLayout->addWidget(parameters[i], 1, i);
    }
    curveForm->addRow(physics);
    m_bounce = new QSlider(Qt::Horizontal);
    m_bounce->setRange(0, 100);
    m_bounce->setObjectName("animationBounce");
    curveForm->addRow(tr("Bounce:"), m_bounce);
    m_duration = spin("animationDuration", 1, 10000, 0);
    m_duration->setSuffix(tr(" ms"));
    m_duration->setAccessibleName(tr("Duration"));
    m_duration->setToolTip(tr("Bézier duration in milliseconds"));
    auto timing = new QWidget;
    timing->setObjectName("animationBezierTiming");
    auto timingLayout = new QHBoxLayout(timing);
    timingLayout->setContentsMargins(0, 0, 0, 0);
    timingLayout->addWidget(m_library, 1);
    timingLayout->addWidget(m_duration);
    curveForm->addRow(timing);
    auto pointsWidget = new QWidget;
    pointsWidget->setObjectName("animationBezierPoints");
    auto points = new QGridLayout(pointsWidget);
    points->setContentsMargins(0, 0, 0, 0);
    const QStringList labels{"X1", "Y1", "X2", "Y2"};
    for (int i = 0; i < 4; ++i) {
        auto label = new QLabel(labels[i]);
        points->addWidget(label, i / 2, (i % 2) * 2);
        m_points[i] = spin(qPrintable("animation" + labels[i]), i % 2 ? -2 : 0, i % 2 ? 3 : 1, 3);
        label->setBuddy(m_points[i]);
        m_points[i]->setAccessibleName(labels[i]);
        points->addWidget(m_points[i], i / 2, (i % 2) * 2 + 1);
    }
    curveForm->addRow(pointsWidget);
    m_canvas = new CurveCanvas;
    m_canvas->setObjectName("animationCurveCanvas");
    curveForm->addRow(m_canvas);
    m_type->setToolTip(tr("Springs settle by physics; Bézier curves use a duration. "
                          "Speed affects both. Held drags and resizes follow the pointer directly."));
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
    connect(m_presets, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_loading || index < 0)
            return;
        m_settings.select(m_presets->itemData(index).toString());
        commit();
        refresh();
    });
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_loading)
            return;
        m_settings.document["enabled"] = enabled;
        commit();
    });
    connect(m_speed, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading)
            return;
        m_settings.document["speed"] = v;
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
    QSignalBlocker block(m_presets);
    m_presets->clear();
    m_presets->addItem(tr("Default"), "fluid");
    m_presets->addItem(tr("Elastic"), "hyprland");
    m_presets->setCurrentIndex(m_presets->findData(m_settings.presetId()));
}
void AnimationEditor::refresh() {
    m_loading = true;
    const auto raw = rawProfile();
    const auto p = raw.inherit ? m_settings.profile(event()) : raw;
    const auto c = m_track->currentIndex() ? p.opacity : p.curve;
    m_enabled->setChecked(m_settings.enabled());
    m_speed->setValue(m_settings.speed());
    const bool modified = !m_settings.document["overrides"].toObject().isEmpty();
    m_presets->setCurrentIndex(modified ? -1 : m_presets->findData(m_settings.presetId()));
    QString desc = m_settings.presetId() == "fluid"
                       ? tr("Balanced, responsive motion.")
                       : m_settings.presetId() == "hyprland"
                             ? tr("Bouncy springs from the Hyprland example. Adapted for Fluid Tiles.")
                             : tr("Previously saved animation settings.");
    if (modified)
        desc = tr("Custom · ") + desc;
    m_description->setText(desc);
    m_error->setText(m_settings.error);
    m_error->setVisible(!m_settings.error.isEmpty());
    const bool child = event() == Fluid::AnimationEvent::Drop || event() == Fluid::AnimationEvent::Float,
               lifecycle = event() == Fluid::AnimationEvent::Open || event() == Fluid::AnimationEvent::Close;
    m_inherit->setVisible(child);
    m_inherit->setChecked(raw.inherit);
    m_details->setEnabled(!raw.inherit);
    m_curveDetails->setEnabled(!raw.inherit);
    m_eventEnabled->setChecked(p.enabled);
    m_style->clear();
    m_style->addItem(tr("Pop"), "pop");
    m_style->addItem(tr("Slide"), "slide");
    m_style->addItem(tr("Fade only"), "fade");
    if (event() == Fluid::AnimationEvent::Close)
        m_style->addItem(tr("No closing animation (legacy)"), "system");
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
    auto curveForm = static_cast<QFormLayout *>(m_curveDetails->layout());
    m_track->setVisible(lifecycle && p.fade);
    curveForm->setRowVisible(findChild<QWidget *>("animationSpringParameters"), spring);
    curveForm->setRowVisible(m_bounce, spring);
    m_bounce->setValue(
        qRound(std::clamp((1 - c.damping / (2 * std::sqrt(c.mass * c.stiffness))) / .0045, 0., 100.)));
    curveForm->setRowVisible(findChild<QWidget *>("animationBezierTiming"), !spring);
    curveForm->setRowVisible(findChild<QWidget *>("animationBezierPoints"), !spring);
    m_library->setCurrentIndex(0);
    const std::array<double, 4> values{c.x1, c.y1, c.x2, c.y2};
    for (int i = 0; i < 4; ++i) {
        m_points[i]->setValue(values[i]);
        m_points[i]->setVisible(!spring);
    }
    m_canvas->setCurve(c);
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
