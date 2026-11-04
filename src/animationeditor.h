// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "animation.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
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

class AnimationPreview final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool running READ running)
  public:
    explicit AnimationPreview(QWidget *parent = nullptr);
    void setSettings(const Fluid::AnimationSettings &s) { m_settings = s; }
    void play(const QString &action);
    bool running() const { return m_timer.isActive(); }

  protected:
    void paintEvent(QPaintEvent *) override;
    void hideEvent(QHideEvent *) override;

  private:
    struct Tile {
        Fluid::AnimationMotion motion;
        Fluid::ScalarAnimation opacity;
        bool visible = true, closing = false;
    };
    std::array<Tile, 3> m_tiles;
    Fluid::AnimationSettings m_settings;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last = 0;
    QRectF area() const;
    void resetScene();
    void move(Tile &, const QRectF &, Fluid::AnimationEvent);
    void tick();
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
    QListWidget *m_presets;
    QLabel *m_description, *m_error, *m_speedLabel;
    QCheckBox *m_enabled, *m_eventEnabled, *m_inherit, *m_fade;
    QSlider *m_speed, *m_bounce;
    QComboBox *m_event, *m_track, *m_type, *m_style, *m_origin, *m_direction, *m_library;
    QDoubleSpinBox *m_mass, *m_stiffness, *m_damping, *m_duration, *m_size;
    std::array<QDoubleSpinBox *, 4> m_points;
    QGroupBox *m_details;
    CurveCanvas *m_canvas;
    AnimationPreview *m_preview;
    Fluid::AnimationEvent event() const;
    Fluid::AnimationProfile rawProfile() const;
    void rebuildPresets();
    void refresh();
    void edit();
    void commit();
    void resetAll();
    void saveAs();
    void renamePreset();
    void deletePreset();
};
