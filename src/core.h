// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace Fluid {
// An analytic critically damped spring. Retargeting preserves visible position
// and velocity; elapsed wall time, rather than tick count, determines motion.
struct Spring {
    double value = 0, velocity = 0, goal = 0;
    void reset(double x) { value = goal = x; velocity = 0; }
    void step(double seconds, double omega = 24) {
        if (seconds <= 0) return;
        const double error = value - goal;
        const double c = velocity + omega * error;
        const double decay = std::exp(-omega * seconds);
        value = goal + (error + c * seconds) * decay;
        velocity = (velocity - omega * c * seconds) * decay;
        if (std::abs(value - goal) < 0.025 && std::abs(velocity) < 0.1) reset(goal);
    }
    bool moving() const { return value != goal || velocity != 0; }
};

struct Motion {
    Spring left, top, right, bottom;
    void reset(const QRectF &r) { left.reset(r.left()); top.reset(r.top()); right.reset(r.right()); bottom.reset(r.bottom()); }
    void target(const QRectF &r) { left.goal = r.left(); top.goal = r.top(); right.goal = r.right(); bottom.goal = r.bottom(); }
    void step(double dt, double omega) { for (auto s : {&left, &top, &right, &bottom}) s->step(dt, omega); }
    QRectF rect() const { return {QPointF(left.value, top.value), QPointF(right.value, bottom.value)}; }
    QRectF goal() const { return {QPointF(left.goal, top.goal), QPointF(right.goal, bottom.goal)}; }
    bool moving() const { return left.moving() || top.moving() || right.moving() || bottom.moving(); }
};

// Frame/allocation conversions are shared by layout, hit testing diagnostics
// and rendering. A positive stroke always occupies whole physical pixels.
struct BorderGeometry {
    QRectF frame;
    double border=0;
    QRectF outer() const { return frame.adjusted(-border,-border,border,border); }
    static double thickness(double logical, double scale) {
        return logical<=0?0:std::max(1.0,std::round(logical*scale))/scale;
    }
    static QRectF clientFrame(const QRectF &allocation,double inset) {
        return allocation.adjusted(inset,inset,-inset,-inset);
    }
};
enum class Axis { Horizontal, Vertical };
struct Node {
    QString id;
    Axis axis = Axis::Horizontal;
    double ratio = 0.5;
    QRectF box;
    Node *parent = nullptr;
    std::unique_ptr<Node> first, second;
    bool leaf() const { return !first; }
};

class Tree {
public:
    std::unique_ptr<Node> root;
    Node *find(const QString &id) const { return findIn(root.get(), id); }
    bool insert(const QString &id, const QString &near, const QRectF &area,
                double gap = 0, double scale = 1, const QHash<QString, QSizeF> &minimums = {}) {
        if (find(id)) return false;
        if (!root) { root = std::make_unique<Node>(); root->id = id; root->box = area; return true; }
        // A reconcile can insert several existing windows before the first
        // layout is presented. Refresh boxes after every topology change so
        // the split axis uses the target tile, never a new child's empty box.
        layout(area, gap, scale, minimums);
        auto leaf = find(near);
        if (!leaf) { leaf = root.get(); while (!leaf->leaf()) leaf = leaf->second.get(); }
        const QString previous = leaf->id;
        leaf->axis = leaf->box.width() >= leaf->box.height() ? Axis::Horizontal : Axis::Vertical;
        leaf->id.clear();
        leaf->first = std::make_unique<Node>(); leaf->first->id = previous; leaf->first->parent = leaf;
        leaf->second = std::make_unique<Node>(); leaf->second->id = id; leaf->second->parent = leaf;
        return true;
    }
    bool remove(const QString &id) {
        auto n = find(id);
        if (!n) return false;
        if (!n->parent) { root.reset(); return true; }
        auto parent = n->parent;
        auto grand = parent->parent;
        auto sibling = n == parent->first.get() ? std::move(parent->second) : std::move(parent->first);
        sibling->parent = grand;
        if (!grand) root = std::move(sibling);
        else if (grand->first.get() == parent) grand->first = std::move(sibling);
        else grand->second = std::move(sibling);
        return true;
    }
    // Hyprland's Dwindle selection/aspect split is the first choice. KWin
    // clients can enforce minima, so retry the other axis and then other
    // leaves before declaring this insertion infeasible. Failed attempts must
    // leave both topology and existing split ratios unchanged.
    bool insertFitting(const QString &id, const QString &near, const QRectF &area,
                       double gap, double scale, const QHash<QString, QSizeF> &mins) {
        if (find(id)) return false;
        const auto before = snapshot();
        auto fits = [&] { const auto size=minimum(mins,gap,scale);
            return size.width()<=area.width() && size.height()<=area.height(); };
        if (!root) {
            insert(id,near,area,gap,scale,mins);
            if (fits()) return true;
            root.reset(); return false;
        }
        layout(area,gap,scale,mins);
        std::vector<Node *> leaves;
        collectLeaves(root.get(),leaves);
        auto preferred=find(near);
        if (!preferred) preferred=leaves.back();
        std::stable_sort(leaves.begin(),leaves.end(),[](auto a,auto b) {
            return a->box.width()*a->box.height()>b->box.width()*b->box.height();
        });
        std::vector<QString> candidates{preferred->id};
        for (auto leaf : leaves) if (leaf!=preferred) candidates.push_back(leaf->id);
        for (const auto &target : candidates) {
            insert(id,target,area,gap,scale,mins);
            if (fits()) return true;
            auto parent=find(id)->parent;
            parent->axis=parent->axis==Axis::Horizontal?Axis::Vertical:Axis::Horizontal;
            if (fits()) return true;
            remove(id);
        }
        root=clone(before.get(),nullptr);
        return false;
    }
    QHash<QString, QRectF> layout(const QRectF &area, double gap, double scale, const QHash<QString, QSizeF> &minimums = {}) {
        QHash<QString, QRectF> result;
        if (root) place(root.get(), area, std::max(0.0, gap), std::max(1.0, scale), minimums, result);
        return result;
    }
    QSizeF minimum(const QHash<QString, QSizeF> &mins, double gap, double scale=0) const { return minSize(root.get(), mins, gap, scale); }
    // Call after membership changes, not during every layout: explicit split
    // rotations and edge drops must remain under the user's control.
    bool preferSideBySidePair(const QRectF &area, double gap, double scale, const QHash<QString, QSizeF> &mins = {}) {
        if (!root || root->leaf() || !root->first->leaf() || !root->second->leaf() || root->axis==Axis::Horizontal) return false;
        root->axis=Axis::Horizontal;
        const auto required=minimum(mins,gap,scale);
        if (required.width()>area.width() || required.height()>area.height()) {
            root->axis=Axis::Vertical;return false;
        }
        return true;
    }
    // Find the nearest ancestor split controlled by an edge of this tile.
    Node *edgeSplit(const QString &id, Axis axis, bool farEdge) const {
        auto n = find(id);
        while (n && n->parent) {
            auto p = n->parent;
            if (p->axis == axis && ((p->first.get() == n) == farEdge)) return p;
            n = p;
        }
        return nullptr;
    }
    void resize(Node *n, double coordinate, double gap, const QHash<QString, QSizeF> &mins) {
        if (!n || n->leaf()) return;
        const bool horizontal = n->axis == Axis::Horizontal;
        const double extent = (horizontal ? n->box.width() : n->box.height()) - gap;
        if (extent <= 0) return;
        const auto a = minSize(n->first.get(), mins, gap), b = minSize(n->second.get(), mins, gap);
        const double lo = (horizontal ? a.width() : a.height()) / extent;
        const double hi = 1 - (horizontal ? b.width() : b.height()) / extent;
        if (lo > hi) return;
        const double position = coordinate - (horizontal ? n->box.x() : n->box.y()) - gap / 2;
        n->ratio = std::clamp(position / extent, lo, hi);
    }
    bool swap(const QString &a, const QString &b) {
        auto x = find(a), y = find(b);
        if (!x || !y || x == y) return false;
        std::swap(x->id, y->id); return true;
    }
    bool placeBeside(const QString &id, const QString &target, Axis axis, bool after) {
        if (id==target || !find(id) || !find(target)) return false;
        remove(id);
        auto n=find(target);
        n->id.clear(); n->axis=axis; n->ratio=.5;
        n->first=std::make_unique<Node>(); n->first->parent=n;
        n->second=std::make_unique<Node>(); n->second->parent=n;
        n->first->id=after?target:id;
        n->second->id=after?id:target;
        return true;
    }
    std::unique_ptr<Node> snapshot() const { return clone(root.get(),nullptr); }
    static double snap(double x, double scale) { return std::round(x * scale) / scale; }
private:
    static void collectLeaves(Node *n, std::vector<Node *> &leaves) {
        if (n->leaf()) { leaves.push_back(n); return; }
        collectLeaves(n->first.get(),leaves); collectLeaves(n->second.get(),leaves);
    }
    static std::unique_ptr<Node> clone(const Node *n, Node *parent) {
        if (!n) return {};
        auto copy=std::make_unique<Node>();
        copy->id=n->id; copy->axis=n->axis; copy->ratio=n->ratio; copy->box=n->box; copy->parent=parent;
        copy->first=clone(n->first.get(),copy.get()); copy->second=clone(n->second.get(),copy.get());
        return copy;
    }
    static Node *findIn(Node *n, const QString &id) {
        if (!n) return nullptr;
        if (n->leaf()) return n->id == id ? n : nullptr;
        if (auto x = findIn(n->first.get(), id)) return x;
        return findIn(n->second.get(), id);
    }
    static QSizeF minSize(const Node *n, const QHash<QString, QSizeF> &mins, double gap, double scale=0) {
        if (!n) return {};
        if (n->leaf()) {
            const auto size=mins.value(n->id, {120, 80});
            // Budget each client's minimum in physical pixels before adding
            // subtree requirements, so nested fractional rounding cannot
            // cumulatively take space away from a descendant.
            if (scale>0) return {std::ceil(size.width()*scale-1e-6)/scale,std::ceil(size.height()*scale-1e-6)/scale};
            return size;
        }
        auto a = minSize(n->first.get(), mins, gap, scale), b = minSize(n->second.get(), mins, gap, scale);
        if (n->axis == Axis::Horizontal) return {a.width() + gap + b.width(), std::max(a.height(), b.height())};
        return {std::max(a.width(), b.width()), a.height() + gap + b.height()};
    }
    static void place(Node *n, const QRectF &r, double gap, double scale, const QHash<QString, QSizeF> &mins, QHash<QString, QRectF> &out) {
        n->box = r;
        if (n->leaf()) { out[n->id] = r; return; }
        const bool h = n->axis == Axis::Horizontal;
        const double size = (h ? r.width() : r.height()) - gap;
        const auto a = minSize(n->first.get(), mins, gap, scale), b = minSize(n->second.get(), mins, gap, scale);
        const double low = h ? a.width() : a.height(), high = size - (h ? b.width() : b.height());
        // Caller floats a window if constraints cannot fit; keep geometry valid
        // even during hotplug or an unusually small temporary work area.
        // Snapping an ancestor edge can leave a subpixel shortfall. Do not
        // replace a minimum-constrained split with 50/50 in that case: that
        // could undersize a large client by hundreds of pixels.
        const double split = low <= high ? std::clamp(size * n->ratio, low, high)
            : low-high<=1.0/scale+1e-6 ? std::clamp(low,1.0,std::max(1.0,size-1.0)) : std::max(1.0, size / 2);
        if (h) {
            const double edge = snap(r.x() + split, scale);
            place(n->first.get(), {r.x(), r.y(), std::max(1.0, edge-r.x()), r.height()}, gap, scale, mins, out);
            place(n->second.get(), {edge+gap, r.y(), std::max(1.0, r.right()-edge-gap), r.height()}, gap, scale, mins, out);
        } else {
            const double edge = snap(r.y() + split, scale);
            place(n->first.get(), {r.x(), r.y(), r.width(), std::max(1.0, edge-r.y())}, gap, scale, mins, out);
            place(n->second.get(), {r.x(), edge+gap, r.width(), std::max(1.0, r.bottom()-edge-gap)}, gap, scale, mins, out);
        }
    }
};
}
