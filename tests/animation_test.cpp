// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Fluid;
static int checks = 0;
static void require(bool b, const char *message) {
    ++checks;
    if (!b)
        throw std::runtime_error(message);
}
static bool near(double a, double b, double e = 1e-8) { return std::abs(a - b) < e; }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        require(animationPresets().size() == 9, "Factory catalog size");
        for (double speed : {.1, .37, 1., 2.}) {
            AnimationSettings settings;
            settings.document["speed"] = speed;
            auto loaded = AnimationSettings::read(settings.serialized());
            require(loaded.error.isEmpty() && near(loaded.speed(), speed),
                    "Numeric animation speed did not round-trip");
            auto custom = AnimationSettings::legacyPreset(8);
            custom["speed"] = speed;
            settings.document["customPresets"] = QJsonArray{custom};
            loaded = AnimationSettings::read(settings.serialized());
            loaded.select("classic-previous");
            require(loaded.error.isEmpty() && near(loaded.speed(), speed),
                    "Custom preset rejected numeric animation speed");
        }
        for (double speed : {0., .09, 2.01}) {
            AnimationSettings settings;
            settings.document["speed"] = speed;
            require(!AnimationSettings::read(settings.serialized()).error.isEmpty(),
                    "Out-of-range animation speed accepted");
            settings.document["speed"] = 1.;
            auto custom = AnimationSettings::legacyPreset(8);
            custom["speed"] = speed;
            settings.document["customPresets"] = QJsonArray{custom};
            require(!AnimationSettings::read(settings.serialized()).error.isEmpty(),
                    "Custom preset accepted out-of-range animation speed");
        }
        for (const auto &preset : animationPresets()) {
            AnimationSettings settings;
            settings.select(preset.id);
            auto loaded = AnimationSettings::read(settings.serialized());
            require(loaded.error.isEmpty(), "Factory preset rejected");
            for (auto event : animationEvents) {
                auto p = settings.profile(event);
                bool valid = false;
                AnimationProfile::fromJson(p.json(), &valid);
                require(valid, "Factory profile invalid");
                AnimationMotion motion;
                motion.configure(p.curve);
                motion.reset({20, 30, 700, 500});
                motion.target({700, 50, 150, 300});
                for (int i = 0; i < 144 * 16 && motion.moving(); ++i) {
                    motion.step(1. / 144, 1.45);
                    const auto r = motion.rect();
                    require(r.width() > 0 && r.height() > 0 && std::isfinite(r.x()),
                            "Invalid presentation during copied preset");
                }
                require(!motion.moving() && motion.rect() == QRectF(700, 50, 150, 300),
                        "Factory motion failed to settle exactly");
            }
        }
        for (const auto &value : animationCurveLibrary()) {
            bool valid = false;
            auto curve = AnimationCurve::fromJson(value.toObject(), &valid);
            require(valid, "Easing library rejected");
            require(curve.ease(0) == 0 && curve.ease(1) == 1, "Bezier endpoints");
            for (int i = 0; i <= 100; ++i)
                require(std::isfinite(curve.ease(i / 100.)), "Nonfinite Bezier");
        }
        AnimationCurve linear;
        linear.kind = AnimationCurve::Bezier;
        linear.x1 = 0;
        linear.y1 = 0;
        linear.x2 = 1;
        linear.y2 = 1;
        for (int i = 0; i < 100; ++i)
            require(near(linear.ease(i / 100.), i / 100.), "Bezier x inversion incorrect");
        auto overshoot = AnimationCurve::fromJson(animationCurveLibrary()["easeOutBack"].toObject());
        require(overshoot.ease(.7) > 1, "Bezier overshoot was clamped");
        for (double damping : {10., 40., 80.}) {
            AnimationCurve c;
            c.mass = 1;
            c.stiffness = 400;
            c.damping = damping;
            ScalarAnimation reference;
            reference.reset(0);
            reference.target(100, c);
            reference.step(.5, 1e-12, 1e-12);
            for (int hz : {60, 144, 240}) {
                ScalarAnimation a;
                a.reset(0);
                a.target(100, c);
                for (int i = 0; i < hz / 2; ++i)
                    a.step(1. / hz, 1e-12, 1e-12);
                require(near(a.value, reference.value, 1e-9) && near(a.velocity, reference.velocity, 1e-8),
                        "Spring depends on refresh rate");
                auto x = a.value, v = a.velocity;
                a.target(-100, c);
                require(a.value == x && a.velocity == v,
                        "Spring retarget lost position or physical velocity");
                a.step(.7);
                require(std::isfinite(a.value) && std::isfinite(a.velocity), "Spring long-frame instability");
                for (int i = 0; i < 20; ++i) {
                    a.target(i % 2 ? 150 : -80, c);
                    a.step(.013);
                }
                a.target(25, c);
                for (int i = 0; i < 3000 && a.moving(); ++i)
                    a.step(1. / 144);
                require(!a.moving() && a.value == 25, "Repeated reversals did not converge");
            }
        }
        ScalarAnimation tween;
        linear.duration = 200;
        tween.reset(10);
        tween.target(30, linear);
        tween.step(.1);
        require(near(tween.value, 20), "Bezier duration not milliseconds");
        auto previous = tween.value;
        tween.target(-10, linear);
        require(tween.value == previous, "Bezier retarget jumped");
        tween.step(.2);
        require(tween.value == -10 && !tween.moving(), "Bezier endpoint not exact");
        AnimationSettings s;
        auto open = s.profile(AnimationEvent::Open);
        const QRectF target(700, 100, 500, 400), area(0, 0, 1600, 1000);
        auto start = transitionRect(open, target, area);
        require(start.center() == area.center() && near(start.width() / target.width(), .65),
                "Default entrance changed");
        s.select("hyprland");
        open = s.profile(AnimationEvent::Open);
        start = transitionRect(open, target, area);
        require(start.center() == target.center() && near(start.width() / target.width(), .87),
                "Upstream pop geometry mapping");
        require(near(open.curve.stiffness, 238.1191) && near(open.curve.damping, 24.21279333),
                "Copied spring changed");
        auto close = s.profile(AnimationEvent::Close);
        require(near(close.curve.duration, 149) && near(close.opacity.duration, 146),
                "Upstream duration conversion");
        s.select("hyde-fast");
        require(s.profile(AnimationEvent::Open).curve.duration == 300, "HyDE duration scaling");
        s.select("popin-soft");
        require(s.profile(AnimationEvent::Rearrange).curve.y1 == .7, "Inherited windowsMove recipe lost");
        for (const QString &dir : {"top", "bottom", "left", "right", "nearest"}) {
            open.style = "slide";
            open.direction = dir;
            require(!transitionRect(open, target, area).intersects(area), "Slide starts inside work area");
        }
        auto custom = AnimationSettings::legacyPreset(8);
        s.document["customPresets"] = QJsonArray{custom};
        s.select("classic-previous");
        require(AnimationSettings::read(s.serialized()).error.isEmpty() &&
                    s.profile(AnimationEvent::Float).curve.stiffness == 64,
                "Legacy preference not retained");
        auto drop = s.profile(AnimationEvent::Drop);
        drop.inherit = true;
        s.setProfile(AnimationEvent::Drop, drop);
        require(s.profile(AnimationEvent::Drop).curve == s.profile(AnimationEvent::Rearrange).curve,
                "Profile inheritance incorrect");
        s.document["enabled"] = false;
        for (auto e : animationEvents)
            require(!s.profile(e).enabled, "Master disabled profile still active");
        for (const QString &bad : {"{", "[]", "{\"version\":2}", "null"})
            require(!AnimationSettings::read(bad).error.isEmpty(), "Invalid document accepted");
        s = AnimationSettings{};
        auto bad = s.profile(AnimationEvent::Rearrange);
        bad.curve.mass = 0;
        s.setProfile(AnimationEvent::Rearrange, bad);
        require(AnimationSettings::read(s.serialized()).presetId() == "fluid" &&
                    !AnimationSettings::read(s.serialized()).error.isEmpty(),
                "Unsafe spring did not fall back");
        std::cout << "Animation checks passed: " << checks << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
