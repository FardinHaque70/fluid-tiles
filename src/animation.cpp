// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation.h"
#include <QFile>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

static void initializeAnimationResources() { Q_INIT_RESOURCE(animations); }
namespace Fluid {
namespace {
double component(double t, double p, double q) {
    const double u = 1 - t;
    return 3 * u * u * t * p + 3 * u * t * t * q + t * t * t;
}
bool number(const QJsonObject &o, const char *key, double lo, double hi) {
    return o.value(key).isDouble() && std::isfinite(o.value(key).toDouble()) &&
           o.value(key).toDouble() >= lo && o.value(key).toDouble() <= hi;
}
QJsonObject catalog() {
    static const bool initialized = [] {
        initializeAnimationResources();
        return true;
    }();
    Q_UNUSED(initialized);
    QFile f(":/fluidtiles/animations/catalog.json");
    if (!f.open(QIODevice::ReadOnly))
        qFatal("Missing animation preset catalog");
    auto d = QJsonDocument::fromJson(f.readAll());
    if (!d.isObject())
        qFatal("Invalid animation preset catalog");
    return d.object();
}
QJsonObject presetProfiles(const AnimationSettings &s) {
    for (const auto &p : animationPresets())
        if (p.id == s.presetId())
            return p.profiles;
    for (const auto &v : s.document.value("customPresets").toArray()) {
        const auto o = v.toObject();
        if (o.value("id").toString() == s.presetId())
            return o.value("profiles").toObject();
    }
    return {};
}
} // namespace
QString eventKey(AnimationEvent e) {
    return QStringList{"open", "close", "rearrange", "drop", "float"}.at(int(e));
}
double AnimationCurve::ease(double progress) const {
    if (progress <= 0)
        return 0;
    if (progress >= 1)
        return 1;
    // Invert x(t), rather than incorrectly using time as the Bezier parameter.
    // Bisection also handles flat endpoint tangents and coincident handles.
    double lo = 0, hi = 1;
    for (int i = 0; i < 42; ++i) {
        double t = (lo + hi) / 2;
        if (component(t, x1, x2) < progress)
            lo = t;
        else
            hi = t;
    }
    return component((lo + hi) / 2, y1, y2);
}
QJsonObject AnimationCurve::json() const {
    return {{"type", kind == Spring ? "spring" : "bezier"},
            {"mass", mass},
            {"stiffness", stiffness},
            {"damping", damping},
            {"duration", duration},
            {"x1", x1},
            {"y1", y1},
            {"x2", x2},
            {"y2", y2},
            {"classic", classic}};
}
AnimationCurve AnimationCurve::fromJson(const QJsonObject &o, bool *valid) {
    AnimationCurve c;
    const QString type = o.value("type").toString();
    bool ok = type == "spring" || type == "bezier";
    c.kind = type == "bezier" ? Bezier : Spring;
    auto read = [&](const char *key, double &v, double lo, double hi) {
        if (o.contains(key)) {
            ok &= number(o, key, lo, hi);
            v = o.value(key).toDouble(v);
        }
    };
    read("mass", c.mass, .01, 100);
    read("stiffness", c.stiffness, 1, 100000);
    read("damping", c.damping, .1, 10000);
    read("duration", c.duration, 1, 10000);
    read("x1", c.x1, 0, 1);
    read("x2", c.x2, 0, 1);
    read("y1", c.y1, -2, 3);
    read("y2", c.y2, -2, 3);
    c.classic = o.value("classic").toBool();
    if (c.kind == Spring) {
        const double omega = std::sqrt(c.stiffness / c.mass),
                     z = c.damping / (2 * std::sqrt(c.stiffness * c.mass));
        ok &= omega >= 2 && omega <= 200 && z >= .1 && z <= 10;
    }
    if (valid)
        *valid = ok;
    return ok ? c : AnimationCurve{};
}
QJsonObject AnimationProfile::json() const {
    return {{"enabled", enabled},        {"inherit", inherit},     {"curve", curve.json()},
            {"opacity", opacity.json()}, {"fade", fade},           {"style", style},
            {"origin", origin},          {"direction", direction}, {"size", size}};
}
AnimationProfile AnimationProfile::fromJson(const QJsonObject &o, bool *valid) {
    AnimationProfile p;
    bool a = true, b = true;
    p.enabled = o.value("enabled").toBool(true);
    p.inherit = o.value("inherit").toBool();
    p.fade = o.value("fade").toBool();
    p.curve = AnimationCurve::fromJson(o.value("curve").toObject(), &a);
    if (o.contains("opacity"))
        p.opacity = AnimationCurve::fromJson(o.value("opacity").toObject(), &b);
    p.style = o.value("style").toString("pop");
    p.origin = o.value("origin").toString("tile");
    p.direction = o.value("direction").toString("nearest");
    p.size = o.value("size").toDouble(.65);
    bool ok = (p.inherit || (a && b)) && QStringList{"pop", "slide", "fade", "system"}.contains(p.style) &&
              QStringList{"tile", "desktop"}.contains(p.origin) &&
              QStringList{"nearest", "top", "bottom", "left", "right"}.contains(p.direction) &&
              std::isfinite(p.size) && p.size >= 0 && p.size <= 1;
    if (valid)
        *valid = ok;
    return p;
}
const QList<AnimationPreset> &animationPresets() {
    static const QList<AnimationPreset> presets = [] {
        QList<AnimationPreset> out;
        for (const auto &v : catalog().value("presets").toArray()) {
            const auto p = v.toObject();
            out.append({p["id"].toString(), p["name"].toString(), p["description"].toString(),
                        p["source"].toString(), p["profiles"].toObject()});
        }
        return out;
    }();
    return presets;
}
QJsonObject animationCurveLibrary() {
    static const auto curves = catalog().value("curves").toObject();
    return curves;
}
QJsonArray animationProvenance() {
    static const auto sources = catalog().value("provenance").toArray();
    return sources;
}
QJsonObject animationDefaults() {
    return {{"version", 1}, {"preset", "fluid"},          {"enabled", true},
            {"speed", 1.0}, {"overrides", QJsonObject{}}, {"customPresets", QJsonArray{}}};
}
QString AnimationSettings::serialized() const {
    return QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
}
double AnimationSettings::speed() const { return document.value("speed").toDouble(1); }
bool AnimationSettings::enabled() const { return document.value("enabled").toBool(true); }
QString AnimationSettings::presetId() const { return document.value("preset").toString("fluid"); }
AnimationSettings AnimationSettings::read(const QString &text) {
    AnimationSettings s;
    if (text.trimmed().isEmpty())
        return s;
    auto d = QJsonDocument::fromJson(text.toUtf8());
    const auto o = d.object();
    bool ok = d.isObject() && o.value("version").toInt() == 1 && o.value("enabled").isBool() &&
              number(o, "speed", .1, 2) && o.value("overrides").isObject() &&
              o.value("customPresets").isArray();
    s.document = o;
    QStringList ids;
    for (const auto &p : animationPresets())
        ids.append(p.id);
    for (const auto &v : o.value("customPresets").toArray()) {
        const auto p = v.toObject();
        const auto id = p.value("id").toString();
        ok &= !id.isEmpty() && !ids.contains(id) && !p.value("name").toString().trimmed().isEmpty();
        if (p.contains("speed"))
            ok &= number(p, "speed", .1, 2);
        if (p.contains("enabled"))
            ok &= p.value("enabled").isBool();
        ids.append(id);
        for (auto e : animationEvents) {
            bool valid = false;
            auto profile = AnimationProfile::fromJson(
                p.value("profiles").toObject().value(eventKey(e)).toObject(), &valid);
            ok &= valid && (!profile.inherit || e == AnimationEvent::Drop || e == AnimationEvent::Float);
        }
    }
    ok &= ids.contains(s.presetId());
    auto base = presetProfiles(s);
    auto overrides = o.value("overrides").toObject();
    for (auto e : animationEvents) {
        bool valid = false;
        auto p = overrides.contains(eventKey(e)) ? overrides[eventKey(e)].toObject()
                                                 : base[eventKey(e)].toObject();
        auto profile = AnimationProfile::fromJson(p, &valid);
        ok &= valid && (!profile.inherit || e == AnimationEvent::Drop || e == AnimationEvent::Float);
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        ok &= QStringList{"open", "close", "rearrange", "drop", "float"}.contains(it.key());
    if (!ok) {
        s.document = animationDefaults();
        s.error = "Invalid animation settings; using Fluid defaults";
    }
    return s;
}
AnimationProfile AnimationSettings::profile(AnimationEvent e) const {
    auto base = presetProfiles(*this);
    auto overrides = document.value("overrides").toObject();
    const auto key = eventKey(e);
    auto p = AnimationProfile::fromJson(overrides.contains(key) ? overrides[key].toObject()
                                                                : base[key].toObject());
    if (p.inherit && (e == AnimationEvent::Drop || e == AnimationEvent::Float))
        p = profile(AnimationEvent::Rearrange);
    if (!enabled())
        p.enabled = false;
    return p;
}
QJsonObject AnimationSettings::profiles() const {
    QJsonObject out;
    for (auto e : animationEvents)
        out[eventKey(e)] = profile(e).json();
    return out;
}
void AnimationSettings::select(const QString &id) {
    document["preset"] = id;
    document["overrides"] = QJsonObject{};
    document["enabled"] = true;
    document["speed"] = 1.0;
    for (const auto &v : document["customPresets"].toArray()) {
        auto o = v.toObject();
        if (o["id"].toString() == id) {
            document["speed"] = o.value("speed").toDouble(1);
            document["enabled"] = o.value("enabled").toBool(true);
        }
    }
}
void AnimationSettings::setProfile(AnimationEvent e, const AnimationProfile &p) {
    auto o = document["overrides"].toObject();
    o[eventKey(e)] = p.json();
    document["overrides"] = o;
}
QJsonObject AnimationSettings::legacyPreset(double omega) {
    AnimationSettings s;
    s.select("classic");
    auto profiles = s.profiles();
    for (auto e : animationEvents)
        if (e != AnimationEvent::Close) {
            auto p = AnimationProfile::fromJson(profiles[eventKey(e)].toObject());
            p.curve.stiffness = omega * omega;
            p.curve.damping = 2 * omega;
            profiles[eventKey(e)] = p.json();
        }
    return {{"id", "classic-previous"}, {"name", "Classic — previous settings"}, {"profiles", profiles}};
}
void ScalarAnimation::reset(double x) {
    value = goal = begun = x;
    velocity = elapsed = 0;
}
void ScalarAnimation::target(double x, const AnimationCurve &c) {
    if (x == goal && c == curve)
        return;
    begun = value;
    goal = x;
    elapsed = 0;
    if (c.kind != AnimationCurve::Spring)
        velocity = 0;
    curve = c;
}
void ScalarAnimation::step(double dt, double pe, double ve) {
    if (!moving() || dt <= 0 || !std::isfinite(dt))
        return;
    elapsed += dt;
    if (curve.kind == AnimationCurve::Bezier) {
        const double progress = std::min(1., elapsed * 1000 / curve.duration);
        if (progress >= 1) {
            reset(goal);
            return;
        }
        value = begun + (goal - begun) * curve.ease(progress);
        return;
    }
    const double w = std::sqrt(curve.stiffness / curve.mass), g = curve.damping / (2 * curve.mass),
                 x = value - goal, v = velocity;
    if (std::abs(g - w) <= w * 1e-7) {
        const double decay = std::exp(-g * dt), b = v + g * x;
        value = goal + decay * (x + b * dt);
        velocity = decay * (v - g * b * dt);
    } else if (g < w) {
        const double wd = std::sqrt(w * w - g * g), decay = std::exp(-g * dt), sn = std::sin(wd * dt),
                     cs = std::cos(wd * dt);
        value = goal + decay * (x * cs + (v + g * x) / wd * sn);
        velocity = decay * (v * cs - (g * v + w * w * x) / wd * sn);
    } else {
        const double root = std::sqrt(g * g - w * w), r1 = -w * w / (g + root), r2 = -g - root,
                     a = (v - r2 * x) / (r1 - r2), b = x - a;
        value = goal + a * std::exp(r1 * dt) + b * std::exp(r2 * dt);
        velocity = a * r1 * std::exp(r1 * dt) + b * r2 * std::exp(r2 * dt);
    }
    if ((std::abs(value - goal) < pe && std::abs(velocity) < ve) || elapsed > 15)
        reset(goal);
}
void AnimationMotion::reset(const QRectF &r) {
    left.reset(r.left());
    top.reset(r.top());
    right.reset(r.right());
    bottom.reset(r.bottom());
}
void AnimationMotion::target(const QRectF &r) {
    left.target(r.left(), curve);
    top.target(r.top(), curve);
    right.target(r.right(), curve);
    bottom.target(r.bottom(), curve);
}
void AnimationMotion::configure(const AnimationCurve &c) {
    curve = c;
    for (auto *s : {&left, &top, &right, &bottom})
        s->target(s->goal, c);
}
void AnimationMotion::step(double dt, double scale) {
    for (auto *s : {&left, &top, &right, &bottom})
        s->step(dt, curve.classic ? .025 : .1 / scale, curve.classic ? .1 : 1 / scale);
}
QRectF AnimationMotion::rect() const {
    return {left.value, top.value, std::max(.01, right.value - left.value),
            std::max(.01, bottom.value - top.value)};
}
QRectF AnimationMotion::goal() const {
    return {QPointF(left.goal, top.goal), QPointF(right.goal, bottom.goal)};
}
bool AnimationMotion::moving() const {
    return left.moving() || top.moving() || right.moving() || bottom.moving();
}
QRectF transitionRect(const AnimationProfile &p, const QRectF &frame, const QRectF &area, bool closing) {
    if (p.style == "fade" || p.style == "system")
        return frame;
    if (p.style == "pop") {
        const QSizeF size(std::max(5., frame.width() * p.size), std::max(5., frame.height() * p.size));
        const QPointF center = !closing && p.origin == "desktop" ? area.center() : frame.center();
        return {center - QPointF(size.width() / 2, size.height() / 2), size};
    }
    auto r = frame;
    QString dir = p.direction;
    if (dir == "nearest") {
        const auto c = frame.center();
        const std::array<double, 4> ds{c.y() - area.top(), area.bottom() - c.y(), c.x() - area.left(),
                                       area.right() - c.x()};
        dir = QStringList{"top", "bottom", "left", "right"}.at(std::min_element(ds.begin(), ds.end()) -
                                                               ds.begin());
    }
    if (dir == "left")
        r.moveRight(area.left());
    else if (dir == "right")
        r.moveLeft(area.right());
    else if (dir == "bottom")
        r.moveTop(area.bottom());
    else
        r.moveBottom(area.top());
    return r;
}
} // namespace Fluid
