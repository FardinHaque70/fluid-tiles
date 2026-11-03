# Krohnkite behavior reference

Hyprland is the gold standard for tiling decisions and motion quality. Krohnkite supplies the KDE integration reference, including window admission and default exclusions. The user's explicit interaction requirements take priority over either project's defaults.

The initial Fluid Tiles prototype was new native code, informed by a limited inspection of Krohnkite's resize path. It was not a port of Krohnkite. The wider review below establishes a concrete reference for subsequent implementation work; it does not mean every referenced feature is already implemented.

## Reviewed source revisions

- [beefsack/Krohnkite, 177040c6f72229fbb4a6ff78baf59d72d212b0c4](https://github.com/beefsack/Krohnkite/tree/177040c6f72229fbb4a6ff78baf59d72d212b0c4): newer fork, used as the main reference for this review.
- [anametologin/krohnkite, f5865562955c45fddb9f4c6ae7e657140d670a95](https://github.com/anametologin/krohnkite/tree/f5865562955c45fddb9f4c6ae7e657140d670a95): archived fork previously examined, used for comparison.

These repositories were inspected as source references. Neither was installed or enabled, and no Krohnkite source is currently vendored into the product.

## Behavior mapping

| Area | Krohnkite reference | Fluid Tiles decision/status |
|---|---|---|
| Drag state | `Dragging` is distinct from persistent `Floating`; dragging windows can remain tileable. | Retain a layout slot while allowing free movement. The current implementation uses drag state plus tiled membership; future refactoring should preserve this distinction explicitly. |
| Floating versus tiled stacking | `res/config.xml` defaults `tiledWindowsLayer=0` (Below), `floatedWindowsLayer=1` (Normal). `engine/window.ts::commit` passes these to `driver/kwinwindow.ts::commit`, which writes KWin’s `keepBelow` / `keepAbove` flags. | Version 0.4.1 uses the same native Below/Normal separation. Ordinary floating windows, including the excluded Settings app, remain above tiles after tiled focus/activation. Original flags are restored on release/disable/unload; explicit flag changes are tracked. |
| Geometry ownership | Window commits skip geometry enforcement in `Dragging` state. | Native titlebar dragging belongs to KWin. Skip layout geometry requests for that window until the drag finishes. Meta+left-drag uses an input grab and movable visual presentation. Implemented and tested. |
| Drop handling | The controller returns layout-managed drags to tiled state. Other tiled drops can swap with the window under the cursor, snap back, or float depending on configuration. | User requirement: always rejoin tiling on release; center swaps, edge inserts, empty-space drop snaps back, Escape cancels. Persistent floating is explicit. Implemented and tested. |
| Layout-specific dragging | Columns defines its own insertion/reordering policy, including directional screen edges and before/after target placement. | Keep drop policy separate from motion. Current Dwindle edge insertion is not a claim of Columns equivalence. |
| Live resize | Controller asks the layout to adjust from a tile's geometry delta, then arranges the layout; completion also reconciles layout. | Use this separation of layout policy and window presentation. Direct divider/Meta+right resizing exists. Native KWin border/system resize now adjusts ancestor splits, with KWin retaining source geometry ownership and Escape restoring original ratios. See the behavior audit for test coverage. |
| Layout state | Layout instances and state are stored per surface/context. | Preserve layouts per monitor/desktop and complete Activity/hotplug semantics. Desktop and sticky-window isolation are tested; real Activity-service and hotplug workflows remain unverified. |
| Window ordering | A dedicated store handles ordering, swapping, insertion and master promotion. | Separate ordering/layout operations from effect callbacks as more layouts are added. Current tree supports swap and insertion; master/stack promotion is not implemented. |
| Native states | Native fullscreen/maximized state overrides the internal tiling state. | Preserve KWin state ownership. Current prototype excludes fullscreen/maximized windows and reinserts eligible restored windows. |
| Window admission | `kwindriver.ts::addWindow` checks non-deleted, nonnegative PID, **not popup**, normal, not hidden, and nontrivial dimensions before adding a window. Docks are handled separately. `kwinwindow.ts::shouldIgnore` excludes special windows, plasmashell and configured patterns; `shouldFloat` excludes modal, transient and non-resizable windows, plus dialog/splash/utility by default. | Explicit independent popup/internal/modal/hidden checks added in 0.4, alongside existing normal/special/transient/resizable checks. Internal decoration tooltips can be normal and resizable, so normal alone is insufficient. Tiny ordinary clients are not permanently excluded before their first valid buffer. |
| Rules and constraints | Driver exposes ignore/float rules, visibility, size hints and geometry adjustment. | Use as a checklist for application compatibility. Current prototype has basic eligibility/minimum-size checks; restrictive maximum sizes use a conservative floating policy. Application/class/instance, title and role ignore patterns are configurable through KDE's settings module. Increment/aspect hints still need further work. |

## Source entry points

- [Controller: drag, drop, resize and shortcuts](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/engine/control.ts)
- [Window states and geometry commits](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/engine/window.ts)
- [Layout storage and context](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/engine/layoutstore.ts)
- [Window ordering](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/engine/windowstore.ts)
- [Master/stack layout](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/layouts/tilelayout.ts)
- [Columns drag and resize behavior](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/layouts/columns.ts)
- [Binary-tree layout](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/layouts/btreelayout.ts)
- [KWin events and geometry integration](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/driver/kwindriver.ts)
- [Size constraints and visibility](https://github.com/beefsack/Krohnkite/blob/177040c6f72229fbb4a6ff78baf59d72d212b0c4/src/driver/kwinwindow.ts)

## Performance boundaries

Krohnkite's controller has a 100 ms guard for layout-specific drag processing. This does **not** establish that all window motion or rendering is limited to 10 FPS. Likewise, receiving resize notifications from buffer geometry does not, by itself, prove the cause of a user's resize stutter.

Fluid Tiles should learn the interaction/state model while keeping its own frame-coordinated native presentation, coalesced geometry requests, and interruptible motion. Smoothness must be assessed using live drag/resize behavior and presentation measurements.

Krohnkite's count-balanced binary-tree layout also differs from Hyprland-style Dwindle insertion. Using Krohnkite as a reference does not imply silently replacing the user's requested Dwindle layout.

## Next implementation checkpoints

1. Keep the tested drag/drop behavior and explicit persistent-floating operation as regression requirements.
2. Native border/system resizing is implemented. Continue physical-display frame-pacing measurements and application compatibility checks before claiming Hyprland-like smoothness.
3. Address application size hints/rules and context isolation using Krohnkite's driver, surface and window models as a reference.
4. Add master/stack or other layouts only after validating the current resize and animation paths. Compare each layout's ordering/resize behavior with its Krohnkite implementation.

Version 0.4 supplies the 13 application IDs from `res/config.xml::ignoreClass` and the `quake` role by default; title rules remain empty. The defaults are configurable. Skip-pager exclusion is **not** enabled by default in Krohnkite and is not introduced here. App matching covers both resource class and instance.

Any future direct reuse of source must retain upstream attribution and the applicable MIT license notices. The current review and mapping do not copy implementation code.

The expanded audit and corrections are recorded in [behavior-audit.md](behavior-audit.md).
