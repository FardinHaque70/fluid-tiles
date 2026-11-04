// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QStringList>
#include <array>

namespace Fluid {
enum class AnimationEvent { Open, Close, Rearrange, Drop, Float };
QString eventKey(AnimationEvent event);
const std::array<AnimationEvent, 5> animationEvents{AnimationEvent::Open, AnimationEvent::Close,
                                                    AnimationEvent::Rearrange, AnimationEvent::Drop,
                                                    AnimationEvent::Float};

struct AnimationCurve {
    enum Kind { Spring, Bezier } kind = Spring;
    double mass = 1, stiffness = 1024, damping = 64, duration = 250;
    double x1 = .33, y1 = 1, x2 = .68, y2 = 1;
    bool classic = false;
    double ease(double progress) const;
    QJsonObject json() const;
    static AnimationCurve fromJson(const QJsonObject &, bool *valid = nullptr);
    bool operator==(const AnimationCurve &) const = default;
};
struct AnimationProfile {
    bool enabled = true, inherit = false, fade = false;
    AnimationCurve curve, opacity;
    QString style = "pop", origin = "tile", direction = "nearest";
    double size = .65;
    QJsonObject json() const;
    static AnimationProfile fromJson(const QJsonObject &, bool *valid = nullptr);
};
struct AnimationPreset {
    QString id, name, description, source;
    QJsonObject profiles;
};
const QList<AnimationPreset> &animationPresets();
QJsonObject animationCurveLibrary();
QJsonArray animationProvenance();
QJsonObject animationDefaults();

// One private, versioned KConfig value. The same resolver is used by the
// compositor, preview and settings tests; upstream Lua is never executed.
class AnimationSettings {
  public:
    QJsonObject document = animationDefaults();
    QString error;
    static AnimationSettings read(const QString &);
    AnimationProfile profile(AnimationEvent) const;
    QJsonObject profiles() const;
    QString serialized() const;
    double speed() const;
    bool enabled() const;
    QString presetId() const;
    void select(const QString &id);
    void setProfile(AnimationEvent, const AnimationProfile &);
    static QJsonObject legacyPreset(double omega);
};

struct ScalarAnimation {
    double value = 0, velocity = 0, goal = 0, begun = 0, elapsed = 0;
    AnimationCurve curve;
    void reset(double x);
    void target(double x, const AnimationCurve &);
    void step(double seconds, double positionEpsilon = .001, double velocityEpsilon = .01);
    bool moving() const { return value != goal || velocity != 0; }
};
struct AnimationMotion {
    ScalarAnimation left, top, right, bottom;
    AnimationCurve curve;
    void reset(const QRectF &);
    void target(const QRectF &);
    void configure(const AnimationCurve &);
    void step(double seconds, double scale = 1);
    QRectF rect() const;
    QRectF goal() const;
    bool moving() const;
};
QRectF transitionRect(const AnimationProfile &, const QRectF &frame, const QRectF &workArea,
                      bool closing = false);
} // namespace Fluid
