// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core.h"
#include "settings.h"
#include "appearance.h"
#include <QFileSystemWatcher>
#include <effect/offscreeneffect.h>
#include <input.h>
#include <QElapsedTimer>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <map>

class FluidTiles final : public KWin::OffscreenEffect {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.FluidTiles")
public:
    FluidTiles();
    ~FluidTiles() override;
    bool isActive() const override;
    int requestedEffectChainPosition() const override { return 45; }
    void reconfigure(ReconfigureFlags) override;
    void prePaintScreen(KWin::ScreenPrePaintData &) override;
    void prePaintWindow(KWin::RenderView *, KWin::EffectWindow *, KWin::WindowPrePaintData &) override;
    void postPaintScreen() override;
    bool filterMotion(KWin::PointerMotionEvent *);
    bool filterButton(KWin::PointerButtonEvent *);
    bool filterKey(KWin::KeyboardKeyEvent *);
public Q_SLOTS:
    QString Status() const;
    bool SetEnabled(bool enabled);
    bool Action(const QString &name);
    void ResetMetrics();
    bool TestPointer(const QString &phase, double x, double y);
    bool TestWindow(const QString &id, const QString &operation);
protected:
    void apply(KWin::EffectWindow *, int, KWin::WindowPaintData &, KWin::WindowQuadList &) override;
    bool drawWindow(const KWin::RenderTarget &, const KWin::RenderViewport &, KWin::EffectWindow *, int,
                    const KWin::Region &, KWin::WindowPaintData &) override;
    bool blocksDirectScanout() const override;
private:
    struct State {
        QPointer<KWin::EffectWindow> effect;
        QString id, group, insertNear;
        QRectF restore, target, lastRequest;
        Fluid::Motion motion;
        bool captureDirty = true;
        quint64 snapshotRevision = 0;
        QRectF snapshotBounds;
        QRectF openingFrom;
        bool openingPending = false;
        bool floatingTransition = false;
        bool tiled = false, redirected = false, restoreValid = false, restorePending = false, capacityBlocked = false;
        bool layerOwned = false, layerChanging = false, originalAbove = false, originalBelow = false;
        quint64 order = 0;
        quint64 requests = 0, commits = 0;
    };
    struct Drag {
        QString id, group;
        Fluid::Node *horizontal = nullptr, *vertical = nullptr;
        QPointF origin, last;
        QRectF initial;
        bool moving = false;
        bool nativeMove = false, nativeResize = false, cancelled = false;
        bool freeResize = false, resizeLeft = false, resizeTop = false;
        Qt::MouseButton button = Qt::RightButton;
        double horizontalStart = 0, verticalStart = 0;
        double horizontalRatio = .5, verticalRatio = .5;
        quint64 generation = 0;
    };
    struct Metrics {
        std::vector<double> paintIntervals, presentationIntervals;
        qint64 lastPaint = 0, lastPresentation = 0;
        quint64 paints = 0;
    };
    std::map<QString, State> m_windows;
    std::map<QString, Fluid::Tree> m_trees;
    std::map<QString, Metrics> m_metrics;
    QSet<QString> m_floating, m_pendingPairChecks;
    QHash<QString, QString> m_lastFocus;
    quint64 m_nextOrder = 0;
    quint64 m_nextDrag = 0;
    std::optional<Drag> m_drag;
    std::unique_ptr<KWin::InputEventFilter> m_filter;
    std::unique_ptr<Fluid::Appearance> m_appearance;
    QHash<KWin::EffectWindow *, int> m_drawDepth;
    QTimer m_reconcile;
    QTimer m_configReload;
    QFileSystemWatcher m_configFiles;
    Fluid::IgnorePatterns m_ignoreApplications, m_ignoreTitles, m_ignoreRoles;
    QString m_ignoreApplicationText, m_ignoreTitleText, m_ignoreRoleText;
    QElapsedTimer m_clock;
    qint64 m_lastStep = 0;
    bool m_enabled = true, m_animating = false, m_test = false;
    double m_gap = 10, m_outerGap = 10, m_omega = 24;
    void watch(KWin::EffectWindow *, bool newlyAdded = false);
    void watchOutput(KWin::LogicalOutput *);
    void scheduleReconcile();
    void reconcile();
    bool eligible(KWin::EffectWindow *) const;
    bool ignored(KWin::EffectWindow *) const;
    QString groupFor(KWin::EffectWindow *) const;
    QRectF areaFor(KWin::EffectWindow *) const;
    QHash<QString, QSizeF> minimums(const QString &group) const;
    void layoutGroup(const QString &, bool immediate = false);
    void applyPairPreference(const QString &, KWin::EffectWindow *);
    void detach(State &, bool restore);
    void restoreAll();
    void floatToCenter(State &);
    void cancelFloatingTransition(State &);
    void syncLayers();
    void restoreLayer(State &);
    void flushGeometry();
    QRectF presentation(const State &) const;
    State *at(const QPointF &);
    bool floatingAbove(const QPointF &) const;
    State *active();
    bool beginDrag(const QPointF &, bool moving);
    bool beginDividerDrag(const QPointF &);
    void updateDrag(const QPointF &);
    void endDrag(bool commit = true);
    void rememberSplitRatios(Drag &);
    QString neighbour(const State &, int direction) const;
    void shortcut(const QString &, const QString &, const QKeySequence &);
};
