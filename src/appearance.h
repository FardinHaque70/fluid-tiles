// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "settings.h"
#include "core.h"
#include <effect/effect.h>
#include <effect/globals.h>
#include <scene/borderradius.h>
#include <KConfigGroup>
#include <KConfigWatcher>
#include <QColor>
#include <QJsonObject>
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace KWin { class EffectWindow; class Window; class GLShader; class Item; class OutlinedBorderItem; }
namespace Fluid {
// Every geometry consumer uses the same physical-pixel border inset.
struct PresentationGeometry : BorderGeometry {
    KWin::BorderRadius radius;
};

class Appearance final : public QObject {
    Q_OBJECT
public:
    struct SnapshotStyle {
        double border=0, opacity=1;
        KWin::BorderRadius radius;
        QColor color;
        bool rounded=false;
    };
    explicit Appearance(QObject *parent=nullptr);
    ~Appearance() override;
    bool configure(const KConfigGroup &); // true for geometry-affecting changes
    void setEnabled(bool enabled);
    bool applies(KWin::EffectWindow *) const;
    double border(KWin::EffectWindow *, bool normal = false) const;
    void setExpanded(KWin::EffectWindow *, bool);
    bool isExpanded(KWin::EffectWindow *) const;
    QSizeF minimumFrame(KWin::EffectWindow *) const;
    void watch(KWin::EffectWindow *);
    void sync(KWin::EffectWindow *, bool tiled);
    void release(KWin::EffectWindow *);
    void forget(KWin::EffectWindow *);
    void cancelPendingFrame(KWin::EffectWindow *);
    void restoreAll();
    void focusChanged();
    SnapshotStyle snapshot(KWin::EffectWindow *,const QRectF &) const;
    void freeze(KWin::EffectWindow *,const SnapshotStyle &);
    void damage(KWin::EffectWindow *, const QRectF &visual);
    PresentationGeometry geometry(KWin::EffectWindow *, const QRectF &visual) const;
    bool prepareShader();
    KWin::GLShader *clipShader(KWin::EffectWindow *, const QRectF &visual, double scale);
    // Scoped by the effect's recursive capture guard; never modifies Window's
    // radius (which backdrop effects also consume).
    KWin::BorderRadius beginCapture(KWin::EffectWindow *);
    void endCapture(KWin::EffectWindow *, const KWin::BorderRadius &);
    bool drawOutline(const KWin::RenderTarget &, const KWin::RenderViewport &, KWin::EffectWindow *, int mask,
                     const KWin::Region &, const KWin::WindowPaintData &incoming, const QRectF &visual);
    QJsonObject status() const;
    QJsonObject status(KWin::EffectWindow *, const QRectF &visual) const;
    bool needsCompositing() const;
    bool available() const { return m_error.isEmpty(); }
    quint64 sourceRevision() const { return m_sourceRevision; }
Q_SIGNALS:
    void changed(bool geometry);
private:
    struct WindowState {
        QPointer<KWin::EffectWindow> effect;
        bool owned=false, policyOwned=false, changing=false, active=false;
        KWin::DecorationPolicy originalPolicy=KWin::DecorationPolicy::ClientPreference;
        KWin::DecorationPolicy writtenPolicy=KWin::DecorationPolicy::ClientPreference;
        KWin::BorderRadius originalRadius, writtenRadius;
        QRectF lastOuter, pendingFrame;
        bool framePending=false;
        bool expanded=false;
        bool closing=false;
        SnapshotStyle snapshot;
        std::unique_ptr<KWin::Item> boundsItem;
        std::unique_ptr<KWin::Item> outlineRoot;
        std::unique_ptr<KWin::OutlinedBorderItem> outline;
    };
    std::map<KWin::EffectWindow *, WindowState> m_windows;
    bool m_enabled=true, m_outlinesEnabled=true, m_roundingEnabled=true, m_titlebarsEnabled=true, m_systemColors=true;
    double m_width=2, m_radius=8;
    int m_titlebars=1; // Keep, HideTiled, HideAllSupported
    QColor m_customActive=QColor("#3daee9"), m_customInactive=QColor("#6e6e6e");
    QColor m_active, m_inactive;
    QString m_ignoreText=defaultIgnoredApplications, m_error;
    IgnorePatterns m_ignore;
    KSharedConfig::Ptr m_kdeConfig;
    KConfigWatcher::Ptr m_themeWatcher;
    std::unique_ptr<KWin::GLShader> m_clipShader;
    quint64 m_outlineDraws=0, m_slideOutlineDraws=0, m_captures=0, m_sourceRevision=0;
    void colorsChanged();
    void restore(WindowState &);
    void fail(const QString &);
    void setPolicy(WindowState &, KWin::DecorationPolicy, bool preserveFrame=true);
    void applyPendingFrame(WindowState &);
};
}
