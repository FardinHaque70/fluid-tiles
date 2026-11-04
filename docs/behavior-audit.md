# Fluid Tiles behavior audit

## Version 0.5.4 single-window expansion

The user requested an enabled-by-default option for a lone window to fill the
desktop, then explicitly chose to keep KDE panels visible. This is a tiled
presentation policy per output/desktop/Activity group, not native fullscreen or
native maximization. It uses KWin's `MaximizeArea`, suppressing only the lone
tile's outer gap, custom outline and owned corner radius. Ordinary floating and
ignored windows keep their own presentation and focus; they do not count toward
the tiled tree. A second eligible tile restores normal spacing and appearance.
Layer-shell surfaces are explicitly excluded from appearance even when KWin
reports them as normal windows with an application ID outside the presets.

References re-inspected at the pinned revisions below: Hyprland's
`CWindowTarget::updatePos` obtains workspace-specific gaps and its monitor work
area separately from fullscreen handling, and supports a separate single-window
aspect-ratio policy. Dwindle's `recalculate` short-circuits for native fullscreen.
Krohnkite's complete `kwindriver.ts::addWindow` → `shouldIgnore` / `shouldFloat`
path and `res/config.xml` were checked; it has separate `soleWindowWidth/Height`
(100%), `soleWindowNoGaps` (false), and `soleWindowNoBorders` (false) defaults.
The new default and bundled visual suppression are this user's requested policy,
not a claim of identical upstream defaults.

KWin adaptation: tiled membership and native restore geometry remain owned by
the existing controller. Fitting an incoming second window predicts restored
border extents and normal gaps before accepting it. A restrictive maximum that
can fill the ordinary tile but not the whole work area keeps its valid ordinary
tile. Source revisions invalidate temporary captures when presentation mode
changes; previous/new outline bounds are damaged. The setting cancels an active
tiling interaction before relayout, and native full/max/min, floating, disable
and unload release expanded presentation through the existing ownership path.

The targeted disposable suite uses a real layer-shell panel with an exclusive
zone; its optional fixture requires LayerShellQt development files. The panel
and input helpers require `WAYLAND_DISPLAY=fluidtiles-test`. The historical
regressions explicitly disable the option to preserve their prior baseline.

Audit date: 2026-11-03. Automated interactive tests use a disposable KWin compositor and test applications. The user has separately enabled the installed package for live testing. Hyprland is the gold standard for tiling decisions; Krohnkite supplies the KDE integration and exclusion reference.

## References actually inspected

- [Krohnkite, 177040c6f72229fbb4a6ff78baf59d72d212b0c4](https://github.com/beefsack/Krohnkite/tree/177040c6f72229fbb4a6ff78baf59d72d212b0c4): `src/engine/control.ts`, `window.ts`, `layoutstore.ts`, and `src/driver/kwinwindow.ts`; the 0.4 admission follow-up also inspects `src/driver/kwindriver.ts::addWindow` and `res/config.xml`. In particular: move/drop handlers, resize/arrange handlers, native-state overrides, floating geometry, commit suppression during native interactions, and desktop/Activity surface selection.
- [Hyprland, e93501dd1bfeabf6206cdaebd853008d47a7dcc1](https://github.com/hyprwm/Hyprland/tree/e93501dd1bfeabf6206cdaebd853008d47a7dcc1): `src/layout/algorithm/tiled/dwindle/DwindleAlgorithm.cpp` (`addTarget`, `removeTarget`, `resizeTarget`, recursive placement) and `src/layout/supplementary/DragController.cpp` (begin/end/motion). Insertion checks an existing target, selects orientation from that target's dimensions, reparents the tree, and recomputes layout. Removal promotes the sibling. Resize searches the relevant ancestor splits.
- [Qt 6 plugin lifetime documentation](https://doc.qt.io/qt-6/qpluginloader.html#loadHints-prop): plugin caching and the default `PreventUnloadHint`, relevant to update URLs and the one-shot restore helper.
- Installed KWin 6.8.2 headers: `window.h`, `virtualdesktops.h`, `effect/effecthandler.h`, `workspace.h`, and `utils/gravity.h`, to check the API and native interactive resize lifecycle actually used by this build.

These are behavioral references, not a source port. No upstream implementation is copied into Fluid Tiles. Hyprland has configurable behavior; Fluid Tiles does not implement every setting or layout. Its explicit choices are stable split directions after insertion, a new tile after its selected neighbor, center-drop swapping, edge-drop insertion, and a reserved slot during dragging. Those choices must not be described as complete Hyprland equivalence.

## Findings and fixes

| Workflow / invariant | Finding | Change / verification |
|---|---|---|
| Enable with existing windows | Children had empty geometry until final layout; third insertion chose the wrong axis. UUID map order also changed window placement. | Refresh geometry before insertion; stable observation order and batch insertion target. Verify three-window shape and repeated enable cycles. |
| Open beside the focused window | By reconcile time, the new window can already be active; the previous target was lost. | Capture the existing active window when observing a new window; retain the last tiled focus per context. Verify opening a fourth window from an older selected tile leaves other tiles unchanged. |
| Temporary drag vs persistent float | These must remain separate. Original dragging failures were fixed before this audit. | Retain existing native titlebar and Meta-drag regression cases; verify explicit floating survives other layout changes and retiling captures the latest floating position. |
| Native border/system resize | Native resize was not connected to the split controller. | Use KWin's resize gravity to choose ancestor splits; follow pointer deltas, let KWin own the source request, and reconcile the final tile on completion. Test native KWin resize with actual Wayland motion/release/Escape. |
| Escape during resize | Cancellation released capture without restoring ratios. | Remember original split ratios and restore them on cancellation. Test custom and native resize cancellation. |
| Delayed completion | A queued finish signal could terminate a newer interaction on the same window. | Tag native interactions with a generation and match it before processing deferred completion. |
| Floating geometry across minimize/fullscreen/maximize | Reinsertion replaced saved floating geometry with tiled/native geometry. | Preserve the saved rectangle across temporary exclusion. Verify it remains unchanged through native state round trips and rapid floating toggles with a slow client. |
| Disable during fullscreen/maximize/minimize | Already-detached clients were skipped when restoring. | Update KWin's native restore rectangles and verify all three states after disabling and after full renderer unload. Minimized clients require a one-shot helper in the Qt-retained bootstrap module to correct their first visible buffer; it does no tiling, input interception or animation, cancels on user interaction or reactivation, and removes itself when restoration completes. |
| Fast disable/re-enable with a slow client | A late restore acknowledgement displaced the client after tiling resumed; the request cache suppressed correction. | Keep the pending restore baseline and compare KWin's requested geometry at paint time, after late acknowledgement handlers finish. A cache of what Fluid Tiles sent is not authoritative. Exercise rapid lifecycle changes with an 80 ms paint delay. |
| Size constraints | Rejected clients were put into the user's persistent floating set and could never recover automatically. Maximum sizes could leave clients permanently stretched. | Separate capacity exclusion from manual floating; retry when layout changes. Keep fixed-size, dialog and restrictive-maximum clients native. Verify capacity recovery when other windows minimize and when a scale/work-area reduction is reversed. |
| Swap/rotate/drop constraints | Swapping unequal size requirements could create an infeasible tree. Gap rounding was inconsistent between fit checks and layout. | Validate changed topology against minimums and the snapped gap; reject infeasible operations transactionally. |
| Desktop / Activity selection | Activity grouping used the current Activity even for windows outside it; multi-desktop windows always chose the first desktop. | Follow Krohnkite's current-if-member, otherwise-first selection. Test separate desktops and sticky windows. Activity service integration remains unverified. |
| Floating-window input priority | Divider/Meta handlers could intercept a floating window covering a tiled divider. | Respect the topmost floating client before starting tiling input capture. Verify move, resize and divider attempts over a fixed-size floating window. |
| Interrupted interaction | Closing a window or switching desktops could commit an unfinished operation. | Cancel the operation on context loss and closing. Verify closing a native-dragged client and repeatedly interrupting slow-client resizes with desktop changes. |
| Settings lifecycle | A separate trial bypassed the visible checkbox. Failed activation could also leave autoload enabled; an already-loaded helper was not owned by the regular loader. | Remove trial activation, clean up activation failures, and make the installed loader own its helper. Verify normal config disable/re-enable and failed activation rollback. Migrate `fluidtiles-loader` to `fluidtiles-controller` once to avoid the earlier cached bootstrap; resolve immutable native revisions in both Settings and CLI paths. A package-update test checks the actual loaded shared-library path after Settings re-enables it. |

## Validation contract

The expanded suite is in `tests/audit_behaviors.py`, invoked by `tests/integration.py`. Existing tests cover native titlebar/CSD-style movement through KWin, Meta movement, all four edge drops, swaps, split resizing, rapid retargeting, minimize/fullscreen/close, restoration, and script unload/reload. Core tests cover tree ownership, geometry constraints and spring continuity.

Native resize is tested through two initiation paths: KWin's mouse-resize command, and a Qt client calling `QWindow::startSystemResize` with the real input serial. Motion, button release and Escape use the actual Wayland fake-input protocol. This validates the native lifecycle and a real client-initiated Wayland resize; it does not establish compatibility with every application's client-side decoration.

A pass means only the enumerated scenarios passed. Physical-display frame pacing, broad application compatibility, Activities with the real Activity service, mixed-monitor/hotplug workflows, size increments/aspect hints, all client-side decorations, and popup/input behavior during transformed animation require further validation. The conservative maximum-size policy may float a bounded application that could fit a smaller tile. No claim of Hyprland-equivalent smoothness follows from these tests.

Final version 0.2.0 passed the complete suite at 145% and 100% scaling using the same native binary. Both runs passed all 13 expanded behavior groups, two native resize cases, four native-state unload/reactivation cases, the payload-update check and the existing seven native/Meta drag/drop cases. Core tests passed 51,392 assertions. Results: `test-results/audit-145/integration.json` and `test-results/audit-100/integration.json`; reproduction and limits are in [VALIDATION.md](../VALIDATION.md). The native effect was left disabled after that 0.2 audit; the user subsequently enabled the 0.3 package for live testing.

Version 0.3.0 adds the [KDE settings dialog](settings.md), live gap/animation preferences and ignore rules. The full behavior suite was rerun at 145% and 100%, including nine new settings groups. Current verification and artifact paths are in [VALIDATION.md](../VALIDATION.md).


## Version 0.4 admission corrections

The earlier audit missed Krohnkite's **outer** admission guard in `src/driver/kwindriver.ts::addWindow`, which explicitly rejects `popupWindow`. Inspecting only `shouldIgnore` / `shouldFloat` was insufficient. A live Close-button tooltip was normal, resizable and non-transient, but also `popup=true` with empty class/role. Fluid Tiles now rejects popup and internal windows explicitly, along with modal/hidden clients. These exclusions precede ordinary-window checks. Unmanaged tooltip closure no longer cancels an unrelated resize. A real Close-button hover in isolated KWin is the regression scenario, rather than a simulated ordinary tiny window.

Krohnkite's `res/config.xml` supplies the 13 default ignored application IDs and the `quake` role. These defaults now appear in both the native reader and KDE configuration form. Parentless modal windows are excluded; clearing configurable patterns cannot opt a popup into tiling. Skip-pager exclusions remain off, matching Krohnkite's default.

Hyprland source checked for these decisions: `src/desktop/view/Popup.cpp` creates `VIEW_TYPE_POPUP` views separately from window targets; `src/desktop/view/window/WaylandBackend.cpp::traitsFrom` identifies transient/fixed-size floating suggestions and modal traits; `X11Backend.cpp` includes tooltip/menu/dialog/utility atoms in floating types. Revision is the pinned Hyprland revision above.

The fifth Dolphin was rejected because only one split axis of the preferred tile was tried. The exact captured geometry/minima reproduce the rejection in the core regression. Hyprland's `DwindleAlgorithm.cpp::addTarget` selects an opening tile and aspect-based axis; it does not contain Fluid Tiles' minimum-tree-fit rejection. `WindowTarget.cpp` and the default `misc:size_limits_tiled=false` do not establish that KWin clients can ignore their own minimum hints. Fluid Tiles therefore keeps the preferred Dwindle insertion first, then tries the opposite axis and other existing leaves (largest area, stable traversal tie-break) before capacity exclusion. This is an explicit KWin compatibility adaptation, not a claim of identical Hyprland size-hint policy. A failed search restores the original topology and ratios. Existing trees are not globally repacked by this search.

The captured fractional-scale case also exposed a placement bug: an ancestor's pixel rounding could cause a subpixel minimum shortfall, which triggered a 50/50 fallback that undersized a client substantially. Small rounding shortfalls now retain the minimum-constrained boundary within one physical pixel. Unequal-minimum stress sequences additionally exposed cumulative nested rounding; leaf minima are now budgeted in physical pixels before adding subtree requirements, consistently for fit checks and placement.


## Version 0.4.1 floating stacking

Window eligibility and stacking are separate. Version 0.4 correctly left Settings floating but did not separate its KWin layer from tiles, so activating a tile could obscure it.

Checked Hyprland's pinned `src/render/Renderer.cpp::renderWorkspaceWindows`: tiled main/popup passes precede its floating pass. Checked Krohnkite's `res/config.xml` defaults and the complete `engine/window.ts::commit` → `driver/kwinwindow.ts::commit` path: tiles default to Below, floats to Normal. Fluid Tiles now uses KWin's native flags to implement that relative ordering for both rendering and input. It does not use paint-only elevation for ordinary floating windows.

Tiles temporarily use Keep Below. Ordinary floats retain native Normal/Above layering; a pre-existing Keep Below preference is temporarily normalized and restored when the policy releases the window. Saved Above/Below preferences also restore when a tile floats, minimizes, enters a native fullscreen/maximized state, or Fluid Tiles disables/unloads. User/application changes to the flags are remembered separately from the policy's own writes. A window being moved temporarily joins the floating layer and returns to the tiled layer on release/cancellation. Shell/internal/popup surfaces retain KWin's native layering. Active fullscreen windows keep their native precedence above ordinary floats.

Regression coverage uses the real Settings application: repeatedly activate tiles and then click the still-visible Settings window using actual Wayland input; verify native stacking indices, focus and tiling input priority. It also checks manual floating/retiling, a fixed-size excluded client, active fullscreen precedence, preference changes while managed, disable restoration and independent native readback after true renderer unload. No test changes host window flags.


## Insertion after swapping two windows

The four-case isolated reproduction in `tests/insertion_behaviors.py` keeps one window focused while swapping it repeatedly between left and right, then opens a third window. All cases split the currently focused column and preserve the other column's geometry. Bottom-left placement is expected when the focused window has moved left. The pinned Hyprland `ConfigValues.cpp` defaults `dwindle:use_active_for_splits=true`; its Dwindle `addTarget` prefers the active target in that mode. This does not establish what the user's earlier Hyprland configuration selected, since cursor-based insertion is also supported upstream. No product behavior or live configuration changed for this investigation. Report: `test-results/insertion-after-swap/insertion.json`.


## Version 0.5 appearance

References checked before implementation: the pinned Hyprland `CHyprBorderDecoration.cpp` reserves border extents on all edges, derives drawing from presentation geometry and scales to monitor pixels; its configuration defines separate active/inactive colors and corner radius. Krohnkite's `kwinwindow.ts::commit` backs up native no-border state and restores it on release. Fluid Tiles preserves the full KWin decoration-policy enum instead of reducing it to a Boolean.

The matching KWin 6.8.2 implementations of `WindowItem`, `OutlinedBorderItem`, `ItemRendererOpenGL`, `OffscreenEffect`, base shaders and decoration-policy APIs were inspected. The renderer reuses native primitives and shader resources at runtime. Settled windows use native clipping and a separate per-window outline pass. Temporary snapshots bypass source clipping only within the recursive capture and use presentation-space clipping for the final texture. Empty scene bounds include the stroke in native damage/occlusion calculations without painting it twice. The window's own opacity is applied to the outline. Native fullscreen/maximized decoration radius is restored from the current native decoration, rather than imposing the normal-window radius on those states.

Tiled allocations include per-window stroke padding, and minimum-size fitting predicts supported titlebar removal before committing it. A failed insertion therefore leaves its floating frame intact. Corners, outlines and titlebar removal have a separate eligibility/exclusion path from tiling, so ordinary floating dialogs and Settings can be decorated while tooltips/internal/shell surfaces remain untouched. Original policy and radius are owned transactionally; asynchronous frame acknowledgements are corrected without taking over native interactions.

Automated appearance coverage includes 27 case groups at 100%, 145% and 200%; native forced-server-decoration rules; real Settings focus/click behavior; Wayland client-initiated movement with an application-painted header; all edge drops; offscreen release at rest; opacity and corner pixel checks; floating occlusion; color versus geometry settings during resize; native-state/disable/unload restoration; live KDE colors; and zero idle outline redraws/captures. The previous full behavior suite also runs with appearance explicitly off. Physical frame-pacing evidence is kept separate from virtual-output metrics.

## Shared split width limit reported after 0.5

The live four-window capture (`test-results/live-width-limit-status.json`) shows a full-height Dolphin on the left; on the right, System Settings and ChatGPT share the top row, with Configure/System Settings spanning the bottom. The captured client minimum widths are 930, 475.862 and 474 logical pixels respectively. The two upper clients are already at their minima. Their horizontal subtree's combined minimum propagates through the right-hand vertical subtree to the root divider, so the lower client cannot shrink below about 1416.55 logical pixels despite its own 474-pixel minimum. Outline reservations and physical-pixel gap/minimum rounding are included. A standalone reproduction using `Fluid::Tree` and the captured hints confirms 1416.55 pixels with the original minima, versus 474.483 pixels with only the two upper width minima relaxed; it does not move host windows.

Hyprland comparison uses the same pinned revision above: `CDwindleAlgorithm::resizeTarget` walks ancestors to choose the shared splits and clamps their ratios to 0.1–1.9 (5–95%); it does not aggregate the descendant application minima into that resize clamp. `SDwindleNodeData::recalcSizePosRecursive` can also change split orientation with aspect ratio when preserve/smart/precise split options permit it. `CWindowTarget::updatePos` has optional tiled min/max *rule* clamping (`misc:size_limits_tiled`, default false); that setting alone is not proof that all native applications accept undersized requests. Shared-boundary resizing is therefore comparable, but Fluid Tiles' hard stop is a stricter sizing policy, not exact Hyprland behavior. Runtime behavior of these exact applications under Hyprland has not been tested. No resize policy or live geometry was changed during this investigation.


## Version 0.5.1 centered float shortcut

Explicitly toggling a tile to floating now restores supported floating decorations, constrains its remembered floating frame size through KWin, and centers that frame in its output's usable work area. The presented rectangle retargets the existing spring rather than snapping to the native committed bounds. The temporary offscreen path and per-window appearance pass now cover this short floating transition too; settled floats release redirection. Other reasons for floating (ignore rules, capacity and native states) keep their existing behavior.

References rechecked: Hyprland's pinned `CLayoutManager::changeFloatingMode` → `CSpace::toggleTargetFloating` → `CAlgorithm::setFloating` and `CDefaultFloatingAlgorithm::movedTarget/newTarget`; Krohnkite's `kwindriver.ts::addWindow`, `kwinwindow.ts::shouldIgnore/shouldFloat/commit` and `engine.ts::toggleFloat`. Hyprland's normal toggle can restore floating size around the previous tile center, while its new-float path centers absent an explicit position. Always centering an explicit toggle in the output work area is this user's requested behavior, not a claim of exact upstream parity. The KDE adaptation restores titlebars before sizing, raises the resulting ordinary float, preserves late-ack corrections, and yields animation ownership to native move/resize.

The floating transition is separate from persistent floating membership. Re-tiling mid-flight preserves the currently presented spring state; native move/resize, native full/max/min states, desktop visibility loss, fullscreen desktop effects, screen locking, disable, close and unload cancel/release it. Geometry-only diagnostics expose `floatingTransition` and `workArea`. The existing limitation remains: input hit regions during transformed presentation follow KWin's committed geometry.


## Version 0.5.2 singleton resize and desktop-slide outlines

Both user reports were reproduced in disposable KWin with the pre-fix 0.5.1 behavior plus test-only diagnostics: the singleton Meta resize path rejected the operation; during a native resize its committed client rectangle shrank by 220 pixels while Fluid Tiles kept presenting the full tile; and a frozen native desktop-slide gesture yielded zero outline-colored samples across the top stroke. Evidence: `test-results/052-reproduction/reproduction.json`, `singleton-native-samples.json`, and desktop captures in that directory.

Hyprland's pinned `CDwindleAlgorithm::resizeTarget` zeros the allowed delta on axes touching both sides of the monitor work area and has no parent split to modify for a singleton. Allowing a temporary free resize and then animating back is this user's explicit preference. Krohnkite's outer `kwindriver.ts::addWindow` eligibility and `kwinwindow.ts::shouldIgnore/shouldFloat` remain unchanged; its `commit` guard against writing geometry during native movement/resizing informs the native ownership path here.

A singleton now has a temporary resize mode within tiled membership. Meta/right-drag uses the selected corner and native frame-size constraints; the normal tile target and persistent floating restore rectangle stay unchanged. Native KWin and Qt client-initiated system resizes own their geometry while held, with presentation tracking the committed rectangle instead of scaling it back to the tile. On release or Escape the existing spring retargets to the original tile; desktop changes and disable release the interaction. Multi-window split resizing retains its existing policy.

The outline delay came from suppressing outlines whenever `activeFullScreenEffect` was non-null. The installed KWin 6.8.2 [Slide implementation](https://github.com/KDE/kwin/blob/v6.8.2/src/plugins/slide/slide.cpp) uses that slot and already supplies per-window translation and device-region clipping. Fluid Tiles now allows the ordinary outline pass specifically for the loaded `slide` effect and uses native presentation bounds while fullscreen effects own the transform. Other fullscreen effects retain suppression. Both outgoing and incoming colored strokes are checked in a fixed half-slide rendered by the real Slide effect, followed by interrupted real desktop switches and an idle repaint check.

## Centered entrances for newly opened tiles

The pre-change reproduction opens an ordinary client with a remembered work-area-sized frame in disposable KWin. Its first sampled presentation was already 100.2% / 102.3% of the final tile dimensions, confirming why an empty-screen opening appeared to snap (`test-results/053-reproduction/opening-reproduction.json`). A naturally smaller default client alone would not reproduce this case.

Inspected the pinned Hyprland `Window.cpp` map path (final layout recalculation followed by `applyAnimateIn`) and `WindowAnimationController.cpp::animateIn/applyPopin/apply`: opening presentation starts smaller and animates toward the already-computed layout goal. Hyprland's pop-in is centered on the destination window, with configurable styles and percentages. Fluid Tiles instead starts at 65% of the destination frame size at the output's usable work-area center, as requested by the user; subsequent windows therefore translate as well as grow. This is an explicit presentation policy, not exact upstream equivalence. Dwindle `addTarget` still supplies the full work-area singleton target and the normal split targets.

Krohnkite's complete `kwindriver.ts::addWindow` admission guard, `kwinwindow.ts::shouldIgnore/shouldFloat`, and `res/config.xml` defaults were rechecked. Eligibility and configurable exclusions are unchanged. A fresh, visible, eligible KWin `windowAdded` event arms the entrance; constructor enumeration and later reinsertion do not. Failed admission, disable and native-state detachment consume it. The final native frame is committed through the existing coalesced request path, while the temporary offscreen presentation and outlines follow the existing spring and release redirection at rest. KWin's opening grab prevents competing Scale animation. No native frame below the client's minimum is requested for the smaller presentation.

The entrance yields to native interactions and fullscreen desktop effects, respects the existing spring setting and KDE's disabled-animation preference, and does not overwrite the saved floating rectangle. Tests exercise a remembered large first window, a slow second client, actual rendered pixels, native full/max/min interruption, disable/close, excluded clients, and enable with existing windows. They run only in disposable KWin; physical 144 Hz feel remains separate from virtual-output evidence.


## Version 0.5.3 preferred two-window orientation

Closing the unsplit full-height member of a three-window Dwindle tree promoted its stacked sibling without changing that sibling's axis. The pre-fix pure-tree reproduction produced two full-width rows on a 1600×1000 work area. Hyprland's pinned `removeTarget` likewise promotes the sibling, but then calls `recalculate` → `calculateWorkspace` → `recalcSizePosRecursive`. With its default dynamic split settings, the promoted node's full workspace proportions select left/right placement on a landscape screen. `preserve_split`, smart splitting and precise movement can preserve orientation instead; the [official Dwindle documentation](https://wiki.hypr.land/configuring/layouts/dwindle-layout/) describes the aspect-based default. Krohnkite's full removal path (`kwindriver.ts` windowRemoved callback → `control.ts::onWindowRemoved` → engine unmanage and arrange) was also checked.

Fluid Tiles now marks groups whose tiled membership changes and prefers left/right columns when that group has exactly two leaf windows. Reorientation keeps the surviving child order and split ratio, uses the existing spring animation, and runs before capacity rejection so a valid pair is retained. Native minimum sizes and physical-pixel extents remain part of the fit check; if columns do not fit, an already-valid row arrangement stays tiled. No admission-search trial or larger tree is repacked by this policy.

The automatic preference applies on insertion/removal, including close, minimize, float and desktop migration. It also applies in portrait work areas when both clients fit, honoring the user's explicit preference rather than copying Hyprland's aspect rule wholesale. It runs only for pending membership changes, so explicit rotate-split or edge-drop choices survive ordinary configuration/geometry reconciles. A successful rotate command clears a pending automatic check.

Core regressions cover removing each of three positions at 100%, 145% and 200%, child/ratio ownership, portrait areas, transactional minimum-size fallback, rescue of tall clients and unchanged larger trees. The native suite closes all positions, swaps before closure, closes during an active resize, exercises minimize/float/desktop removal, verifies explicit rotation after a color save, and checks that two wide clients remain tiled when native minima require rows.
