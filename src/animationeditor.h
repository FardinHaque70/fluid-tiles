// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "animation.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QGroupBox;

class CurveCanvas final : public QWidget {
    Q_OBJECT
  public:
    explicit CurveCanvas(QWidget *parent = nullptr);
    Fluid::AnimationCurve curve;
    void setCurve(const Fluid::AnimationCurve &);
  Q_SIGNALS:
    void edited();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    int m_handle = 0;
    bool m_dragging = false;
    double m_yLow = -.2, m_yHigh = 1.2;
    QPointF point(double, double) const;
};

class AnimationEditor final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QString animationSettings READ settings WRITE setSettings NOTIFY settingsChanged USER true)
  public:
    explicit AnimationEditor(QWidget *parent = nullptr);
    QString settings() const { return m_value; }
    void setSettings(const QString &);
  Q_SIGNALS:
    void settingsChanged();

  private:
    Fluid::AnimationSettings m_settings;
    QString m_value;
    bool m_loading = false;
    QComboBox *m_presets;
    QLabel *m_description, *m_error;
    QCheckBox *m_enabled, *m_eventEnabled, *m_inherit, *m_fade;
    QSlider *m_bounce;
    QComboBox *m_event, *m_track, *m_type, *m_style, *m_origin, *m_direction, *m_library;
    QDoubleSpinBox *m_speed, *m_mass, *m_stiffness, *m_damping, *m_duration, *m_size;
    std::array<QDoubleSpinBox *, 4> m_points;
    QGroupBox *m_details, *m_curveDetails;
    CurveCanvas *m_canvas;
    Fluid::AnimationEvent event() const;
    Fluid::AnimationProfile rawProfile() const;
    void rebuildPresets();
    void refresh();
    void edit();
    void commit();
    void resetAll();
};
