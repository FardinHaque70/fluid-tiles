# Fluid Tiles validation

## Version 0.6.0 animation overhaul

Validated 2026-11-05.

The implementation includes the shared analytic spring/Bézier engine, independent
geometry/opacity and event profiles, retained closing visuals, nine factory
presets, the copied 19-curve HyprMod library, native KDE editing and unsaved
preview, named custom presets, migration and activation-preserving updates.

- **134,769 core checks** and **11,781 animation checks** pass. CTest also verifies
  catalog regeneration/source hashes and real KConfig migration. The existing
  appearance migration test passes. Python syntax and diff-whitespace checks pass.
- The complete new animation suite passes at **100%, 145% and 200% scaling** on
  the same native binary. Every factory preset opens and closes; coverage also
  includes close during entrance, real midpoint geometry/opacity/outline pixels,
  desktop-change cancellation, true renderer unload while closing, native state
  interruptions, explicit float/retiling, repeated retargeting, settings deferred
  during a held drag, Off, and idle capture/resource release.
- The packaged KDE module passes preset selection, numeric/keyboard curve edits,
  Save As while disabled, rename/delete, reload/Cancel, Defaults retention,
  Apply persistence, hidden preview timer shutdown and unchanged activation.
  Actual form captures are in each animation report directory.
- Full baseline suites pass at **100% and 145%**, including native movement,
  split resize, constraints, settings, restoration, unload/reactivation and
  immutable payload updates. Updates retain animation preferences and activation.
- A separate update audit passes for running, emergency-disabled and fully
  disabled states, retaining animation preferences. Evidence:
  `test-results/060-activation-update/activation-update.json`.
- All **28 appearance cases**, singleton/native resize and Desktop Slide cases,
  nine centered-float cases, the historical opening suite, twelve single-window
  workflows with a real panel, and ten two-window orientation cases pass at 145%.
- Twelve counterbalanced virtual runs compare Classic and Fluid across the three
  scales. Both record **16.67 ms presentation p95** in each run. These are
  synthetic 60 Hz outputs, not a physical-display benchmark. The diagnostics
  expose refresh-relative missed-refresh estimates as well as paint intervals.

Native binary SHA-256:
`04c4a467034de8190c03f6ee423306a12aa5f77d82942ad5e11b1311d72d0e90`.

Evidence: `test-results/060-verified-animations-{100,145,200}/animations.json`,
`060-verified-appearance-145/appearance.json`,
`060-verified-interactions-145/interactions.json`,
`060-verified-float-145/float-center.json`,
`060-verified-opening-145/opening.json`, `060-verified-single-145/single.json`,
and `060-verified-pairs-145/pairs.json`, all beneath `test-results/`.
Full baseline reports are `060-verified-baseline-{100,145}/integration.json`;
the matching-binary index is `test-results/060-summary.json`.
The native animation suite can be reproduced with:

```sh
FLUIDTILES_TEST_CONTROL=1 FLUIDTILES_TEST_DESKTOP_EFFECTS=1 \
FLUIDTILES_TEST_ANIMATIONS=1 FLUIDTILES_TEST_SCALE=1.45 \
FLUIDTILES_TEST_SESSION=test-session-v6-a145 \
FLUIDTILES_TEST_RESULTS=test-results/060-verified-animations-145 \
python3 tests/integration.py
```

Use short session paths: Wayland's Unix socket path must fit its 108-byte limit.
All native testing uses the private `fluidtiles-test` socket and disposable
D-Bus/config/runtime roots. No host input injection, live package installation,
or host activation/preference changes were performed for this release.

Physical 144 Hz motion, mixed-output/hotplug and real Activity-service behavior
remain broader validation. In particular, the earlier application-specific
Brave/Codex resize stalls/black bars have not been established fixed. Native
application input regions continue to follow committed frames during transformed
presentation outside Fluid Tiles' grabbed interactions. This release supplies no
custom workspace, popup, blur, shadow or border-gradient animation.

## Version 0.5.4

Version 0.5.4 adds **Expand a single tiled window**, enabled by default in
Layout & animation. A lone tile fills KWin's usable work area, preserving panel
reservations, without outer gaps, custom outlines or owned corner rounding.
Native fullscreen/maximized flags remain unchanged. A second tile restores
configured spacing and appearance. Floating/ignored windows retain focus and
do not count toward the tiled tree. Restrictive native maxima can retain an
ordinary inset tile.

- Twelve targeted native workflows passed at **100%, 145% and 200% scaling** on
  the final binary. A real layer-shell panel reserves 48 logical pixels; pixel
  checks confirm the panel remains visible and tile content begins immediately
  below it. Layer-shell surfaces receive no custom application appearance.
- Coverage includes the real KDE settings default and live toggle, second-window
  opening/closure, infeasible insertion, slow clients, floating dialog focus,
  explicit floating/retiling, full/max/min round trips, desktop grouping,
  temporary single-tile resize, setting changes during resize, disable/unload
  restoration, maximum-size fallback, client headers and idle rendering.
- The existing full baseline, **28 appearance cases**, singleton-resize and
  Desktop Slide regressions, nine centered-float cases, opening-animation suite
  and ten pair-orientation workflows passed at 145% with the new option
  explicitly disabled. Core tests (134,769 assertions), configuration migration
  checks, Python syntax and build checks passed.
- Installed and verified live as **0.5.4**, preserving activation, all existing
  saved preferences (including authentication-dialog appearance) and shortcut
  bindings. The loaded immutable binary matches the tested hash.

Native binary SHA-256:
`ed7e07aa8629e3073e220a904b336a6e8ec809bf5d60bbd9f9c440d34081512c`.

Evidence: `test-results/single-{100,145,200}/single.json`,
`test-results/054-baseline-145/integration.json`,
`test-results/054-appearance-145/appearance.json`,
`test-results/054-interactions-145/interactions.json`,
`test-results/054-float-145/float-center.json`,
`test-results/054-opening-145/opening.json`,
`test-results/054-pairs-145/pairs.json`, and
`test-results/live-054-deployed.json`. These are local generated artifacts.
Reproduce the targeted suite with
`FLUIDTILES_TEST_CONTROL=1 FLUIDTILES_TEST_DESKTOP_EFFECTS=1 FLUIDTILES_TEST_SINGLE=1 FLUIDTILES_TEST_SCALE=1.45 FLUIDTILES_TEST_SESSION=test-session-single FLUIDTILES_TEST_RESULTS=test-results/single-145 python3 tests/integration.py`.
The panel fixture requires optional LayerShellQt development files at build time.
Virtual-output validation does not establish physical 144 Hz smoothness.

## Fluid Tiles 0.5.3 validation

Version 0.5.3 combines preferred side-by-side automatic two-window layouts with the separately requested centered entrance animation for newly opened windows.

- Reproduced the reported closure in the pure tree: removing the full-height member promoted its stacked sibling into two full-width rows. The controller now applies the preferred pair orientation after membership changes, retaining order, ratio and native minimum-size fitting.
- **Ten native pair workflows passed at 100%, 145% and 200%** on the combined binary: close each position, swap before closing, close during resize, minimize/float/desktop removal, explicit rotation across configuration reload, and a two-wide-client minimum-size fallback that keeps both windows tiled.
- The full baseline compositor suite, all **27 appearance cases**, all **nine centered-float cases**, and the singleton-resize/desktop-slide interaction suite passed at 145%. **134,769 core assertions**, Python syntax and configuration migration checks passed. No build warnings were reported.
- The opening-animation work independently passed its 100%/145%/200% opening profiles and its baseline, appearance, interaction and float regressions on this same binary. Evidence: `test-results/opening-summary.json` and [entrance behavior](docs/behavior-audit.md#centered-entrances-for-newly-opened-tiles).
- The combined update was installed once and then verified from this chat without another reload. Loaded version **0.5.3** and SHA/path matched the tested payload; activation, preferences and shortcuts were preserved. Evidence: `test-results/live-053-deployed.json`, `test-results/live-053-pairs-verified.json`, and `test-results/release-053-pairs-summary.json`.

Native binary SHA-256: `7e1504647235833d1331f93064e42501a1230244154c50917447652ec29e72ad`.

An additional authentication-dialog preference regression passed on this binary
at 145% scaling, together with all 28 appearance cases. A modal fixture with the
Polkit application ID receives a pixel-checked outline after removal from only
the appearance exclusions; it remains ignored by tiling and retains its native
geometry and titlebar. Reapplying the appearance exclusion releases owned state.
The live preference update changed only `AppearanceIgnoredApplications`, with
activation, other preferences, shortcuts and the loaded binary preserved.
Evidence: `test-results/auth-appearance-145/appearance.json` and
`test-results/live-auth-appearance-applied.json`. These generated reports and
session captures are local artifacts and are not included in the source repository.

Pair reports: `test-results/pairs-{100,145,200}/pairs.json`. Regression reports: `test-results/053-baseline-145/integration.json`, `test-results/053-appearance-145/appearance.json`, `test-results/053-float-center-145/float-center.json`, and `test-results/053-interactions-145/interactions.json`. Reproduce a pair profile using `FLUIDTILES_TEST_CONTROL=1 FLUIDTILES_TEST_DESKTOP_EFFECTS=1 FLUIDTILES_TEST_PAIRS=1 FLUIDTILES_TEST_SCALE=1.45 FLUIDTILES_TEST_SESSION=test-session-pairs-053 FLUIDTILES_TEST_RESULTS=test-results/pairs-145 python3 tests/integration.py`.

Native minimum widths may prevent columns; an already-valid tiled arrangement is retained in that case. Explicit rotate-split choices remain available. Physical interaction feel remains separate from virtual-output checks. Earlier release validation follows.

## Fluid Tiles 0.5.2 validation

Version 0.5.2 fixes temporary resizing of a single tile and delayed outlines on virtual desktop switches using KWin Desktop Slide.

- Reproduced both faults before the behavioral change using 0.5.1 behavior with test-only instrumentation: Meta resize rejected the singleton, native client geometry diverged from presentation by **220 logical px**, and a fixed half-slide showed **zero outline-colored samples**. Evidence: `test-results/052-reproduction/reproduction.json`.
- The targeted interaction suite passed at **100%, 145% and 200%** scaling on one native binary. Native singleton geometry/presentation mismatch was **zero**; the native half-slide contained **380 / 399** outline-colored samples across its top stroke, with both outgoing and incoming colors asserted separately. Captures were visually inspected.
- Covered Meta resize from all four corners, animated return on release and custom cancellation, unchanged tile/restore geometry, native KWin resize, slow Qt client-initiated system resize using real Wayland input, desktop/disable interruptions, repeated interrupted desktop slides and no idle outline redraws after switching.
- The full baseline regression suite, all **27 appearance cases**, and all **nine centered-float cases** passed at 145%. **134,719 core assertions**, Python syntax and configuration migration checks passed; build completed without warnings.
- Installed and verified live as **0.5.2**, preserving activation, saved preferences and shortcut bindings. Evidence: `test-results/live-052-deployed.json`; summary: `test-results/release-052-summary.json`.

Native binary SHA-256: `4e379b8ecc51d8a5040225e461a16eb9718cff2115fe971817a08b6f7dd8b31a`.

Reports: `test-results/interactions-{100,145,200}/interactions.json`, `test-results/052-baseline-145/integration.json`, `test-results/052-appearance-145/appearance.json`, and `test-results/052-float-center-145/float-center.json`. Reproduce the targeted profile with `FLUIDTILES_TEST_CONTROL=1 FLUIDTILES_TEST_DESKTOP_EFFECTS=1 FLUIDTILES_TEST_INTERACTIONS=1 FLUIDTILES_TEST_SCALE=1.45 FLUIDTILES_TEST_RESULTS=test-results/interactions-145 python3 tests/integration.py`.

Singleton free resizing is the user's requested policy; Hyprland's Dwindle itself keeps a singleton fixed. Native application size constraints still apply. Outline integration during fullscreen desktop effects is verified specifically with Desktop Slide; other fullscreen desktop effects retain suppression. Physical interaction feel is separate from the virtual-output evidence. Earlier release validation follows.

## Fluid Tiles 0.5.1 validation

Version 0.5.1 adds animated centering when a tiled window is explicitly toggled to floating. It uses the remembered floating size, constrained through KWin after supported floating titlebars return, and centers in the output's usable work area. The existing spring, animation-speed preference and appearance renderer follow the transition. Native movement/resizing takes over on interruption; settled floats retain no animation texture.

- Build passed without warnings; **134,719 core assertions** and configuration migration checks passed.
- Eight transition case groups passed at **100%, 145% and 200%** scaling. They cover centering, remembered size, titlebars, native stacking, slow clients, interrupted retiling, native move/resize, full/max/min states, disable, close and true renderer unload. The 145% profile adds a ninth case with a compositor screenshot/pixel check of the actual in-flight position.
- The full baseline compositor suite and all **27 appearance case groups** passed at 145% on the same binary. Existing restore-position expectations were updated only where the explicit float action now requests a new centered floating position.
- Installed and verified live as **0.5.1**, preserving activation, saved preferences and KDE shortcut bindings. Evidence: `test-results/live-051-deployed.json`; summary: `test-results/release-051-summary.json`.

Native binary SHA-256: `15de0bd25dc828dd73ed515f79f1bec3cd143135eeabe019ca6276173b15bc65`.

Reports: `test-results/float-center-{100,145,200}/float-center.json`, `test-results/float-center-baseline-145/integration.json`, and `test-results/float-center-appearance-145/appearance.json`. Reproduce the targeted profile with `FLUIDTILES_TEST_CONTROL=1 FLUIDTILES_TEST_DESKTOP_EFFECTS=1 FLUIDTILES_TEST_FLOAT_CENTER=1 FLUIDTILES_TEST_SCALE=1.45 FLUIDTILES_TEST_RESULTS=test-results/float-center-145 python3 tests/integration.py`.

Physical 144 Hz feel of this new float transition still needs user observation. During transformed presentation, application input hit regions continue to follow KWin's committed frame; broad multi-output workflows remain outside these checks. The following section records the previous 0.5.0 appearance release and its separate physical resize findings.

## Fluid Tiles 0.5.0 validation

Build target: KWin 6.8.2 / Plasma Wayland, Qt 6.12.0, NVIDIA RTX 4080. Audit date: 2026-11-03. Behavior/source references are in [the audit](docs/behavior-audit.md); appearance semantics and limits are in [appearance](docs/appearance.md).

## Automated checks

- Build passed without compiler warnings. Python control and test modules passed syntax checks.
- **134,719 core assertions passed**, including physical-pixel border widths and allocation/frame conversions, exact captured fifth-window minima, transactional insertion search, geometry invariants and spring continuity.
- **27 appearance case groups passed at 100%, 145% and 200%**, using the same native binary. These exercise three independently saved section controls, actual KDE color controls and defaults, outline/corner pixels including strongly stretched snapshots, opacity, visible-gap reservation, floating occlusion, actual Settings clicks and native layers, client-painted headers and real Wayland system movement, all edge drops, interrupted motion, native-state suppression, live KDE colors, separate appearance exclusions, forced KWin server-decoration rules, configuration during resize, geometry/titlebar/radius restoration, true renderer unload and no idle outline redraws/captures.
- Four real KConfig migration scenarios passed, covering a disabled master, Keep titlebars, preexisting independent choices, and defaults/reinstall behavior.
- The historical full suite passed with appearance explicitly off at 145% and 100%: 11 settings groups, 13 behavior groups, six native stacking groups, two popup groups, two native resize cases, four native-state unload/reactivation cases, payload-update validation and the existing drag/drop/rendering regressions.
- An injected appearance shader failure restored native appearance while tiling continued. Native operation never relies on this injection outside isolated test mode.
- The constrained five-window reproduction passed with appearance off. Appearance-enabled five-window evidence is recorded separately.
- Rendered the actual Appearance tab and inspected the three-window/resize captures. Qt-based pixel checks inspect compositor captures without external imaging dependencies. Sampled captures cannot prove absence of every transient artifact.

| Profile | Report |
|---|---|
| Appearance 145% | `test-results/appearance-145/appearance.json` |
| Appearance 100% | `test-results/appearance-100/appearance.json` |
| Appearance 200% | `test-results/appearance-200/appearance.json` |
| Shader fallback | `test-results/appearance-failure/appearance.json` |
| Baseline 145% | `test-results/appearance-baseline-145/integration.json` |
| Baseline 100% | `test-results/appearance-baseline-100/integration.json` |
| Five-window baseline | `test-results/appearance-fifth/fifth.json` |
| Five-window appearance | `test-results/appearance-fifth-enabled/fifth.json` |

Reproduce an appearance profile with:

```bash
FLUIDTILES_TEST_CONTROL=1 FLUIDTILES_TEST_DESKTOP_EFFECTS=1 \
FLUIDTILES_TEST_APPEARANCE=1 FLUIDTILES_TEST_SCALE=1.45 \
FLUIDTILES_TEST_RESULTS=test-results/appearance-145 python3 tests/integration.py
```

Use scale `1` or `2` for the other profiles. Omit `FLUIDTILES_TEST_APPEARANCE` for the historical baseline. `FLUIDTILES_TEST_BAD_APPEARANCE_SHADER=1` adds the isolated failure scenario. `FLUIDTILES_TEST_FIFTH=1` selects the larger constrained-client work area; add `FLUIDTILES_TEST_APPEARANCE_FIFTH=1` to enable appearance there.

All automated input helpers remain restricted to the disposable `fluidtiles-test` socket. The script configuration, caches, applications and compositor in these profiles are isolated from the user's session. Test settings defaults are explicitly returned to the baseline profile where required; production defaults remain appearance on, 2 px outlines, 8 px radius and hidden supported tiled titlebars.

## Performance and live validation

Virtual-output presentation p95 was 16.67 ms with appearance both off and on in all three scale profiles. These outputs run at synthetic 60 Hz; this is not a physical 144 Hz benchmark. Settled clients are unredirected and idle appearance generated no additional outline passes or texture captures during the idle check.

The 0.5.0 immutable payload was installed live at that validation stage. `test-results/live-050-independent-deployed.json` verifies the loaded binary path/hash, retained activation state, and preserved/migrated preferences, including the current 3 px outline. Native binary SHA-256: `b9117631b4d13e818d8bd460b44998ff4de4a12c5b6ac9c6ea3e3a72364ddc86`. A matched physical animation comparison uses the public split action with paired rotations, returns the selected split orientation after every pair, and restores the prior independent appearance preferences. It never enables test mode or injects host input. A guided manual resize check is required in addition to those animation measurements.

Two counterbalanced physical comparisons completed on DP-1 (3840×2160, 143.999 Hz, 145% scaling), with six measured runs per condition. Median presentation p95 was **13.83 ms off / 13.88 ms on**, with p50 near 6.94 ms. The first comparison included one **86.074 ms** enabled-run outlier; the repeat's worst enabled interval was 15.25 ms. This outlier did not reproduce, and p95 showed no systematic shift large enough to establish a regression. These measurements do not establish uniformly smooth 144 Hz rendering: the baseline also includes roughly two-refresh intervals. Raw missed-refresh counts are not exported by the current diagnostics. Reports: `test-results/live-050-pacing-first.json`, `test-results/live-050-pacing.json`; both verify layout restoration. The guided live resize monitor recorded four interactions (`test-results/live-050-manual-resize.json`). User feedback: most KDE applications, including Dolphin, feel good; Brave and the Codex app can show black bars and update less frequently. This is a partial result, not a complete smoothness pass. The monitor recorded a 125.006 ms maximum presentation interval across the mixed workload; its geometry-only samples cannot attribute that stall or the black pixels to a specific application or the plugin. A matched same-application comparison with Fluid Tiles disabled, plus Wayland/Xwayland classification and pixel captures, remains needed before assigning the cause. Upstream reports show relevant behavior in [Hyprland](https://github.com/hyprwm/Hyprland/discussions/14399) and an [open Electron resize synchronization proposal](https://github.com/electron/electron/pull/55415); neither proves the cause in this session.

## Limits

Application-painted headers and omitted transparent pixels remain application-controlled. Native client rounding may differ from requested geometry by a physical pixel; settled outlines follow committed edges. Fixed-size or bounded clients and forced KWin frame rules can limit titlebar hiding. The native plugin targets the installed KWin private ABI and must be rebuilt after a KWin update.

Physical-display smoothness, broad everyday application compatibility, mixed-monitor/hotplug workflows, real Activities, application size increments/aspect hints and every client-side decoration remain broader validation work. Existing one-shot minimized restoration in the Qt-retained bootstrap still handles a client's first visible buffer after renderer unload; it performs no tiling, animation or input interception.

## Centered new-window entrance validation

New tiles now start at 65% of their destination frame size at the usable work-area center and follow the existing spring to their tile. The pre-change remembered-large-frame reproduction began at approximately the final size (`test-results/053-reproduction/opening-reproduction.json`). The new first sample is visibly smaller, and a compositor pixel check confirms that the native full-size frame is not painted outside the presented entrance.

- Eight opening case groups passed at **100%, 145% and 200%** scaling: remembered large singleton; slow second client moving to a split; minimize/restore; enabling with existing windows; native full/max/min interruptions; disable/close; excluded clients; KDE animations disabled. Captures at 145% were visually inspected.
- The full baseline, all **27 appearance cases**, native singleton/desktop-slide interaction checks, and all **nine centered-float cases** passed at 145% on the same binary. Core tests passed **134,769 assertions**; Python syntax and the four appearance migrations passed. Build completed without warnings.
- Runs use a separate `test-session/opening` configuration, private D-Bus and only the `fluidtiles-test` Wayland socket. A hard-coded appearance-test configuration path was corrected to respect that isolated configuration; the appearance suite then passed.

Native binary SHA-256: `7e1504647235833d1331f93064e42501a1230244154c50917447652ec29e72ad`. Summary: `test-results/opening-summary.json`. Reports: `test-results/opening-{100,145,200}/opening.json`, `opening-baseline-145/integration.json`, `opening-appearance-145/appearance.json`, `opening-interactions-145/interactions.json`, and `opening-float-145/float-center.json` under `test-results/`.

Installed and independently verified live as **0.5.3**, enabled with the same tested binary and appearance renderer available. Activation, saved preferences and shortcuts were preserved. Evidence: `test-results/live-053-deployed.json` and `test-results/opening-live-verified.json`. No fake input was used on the host desktop.

Reproduce with `FLUIDTILES_TEST_CONTROL=1 FLUIDTILES_TEST_DESKTOP_EFFECTS=1 FLUIDTILES_TEST_OPENING=1 FLUIDTILES_TEST_SCALE=1.45 FLUIDTILES_TEST_SESSION=test-session/opening FLUIDTILES_TEST_RESULTS=test-results/opening-145 python3 tests/integration.py`. The native opening grab, final-geometry request path, and temporary transformed-presentation input limitation are documented in [the behavior audit](docs/behavior-audit.md). These virtual-output checks do not establish physical 144 Hz smoothness.

## Settings window pickers (2026-11-04)

Removed the Apply and display-scaling subtexts. All four exclusion fields now open a searchable KDE window picker, backed by the packaged settings KCModule. The settings-only disposable KWin audit passed all 12 groups, including real Wayland window/property lookup with tiling enabled and disabled, search, missing roles, unsaved cancellation, save/reload/defaults and preserved activation. Literal wildcard escaping, duplicate suppression and existing-rule preservation are covered by the settings executable. Core CTest and `git diff --check` passed. Reports and inspected screenshots: `test-results/picker-final/`.

The settings UI alone was updated in the installed 0.5.4 package. A module-load smoke test passed; `kwinrc` remained byte-for-byte unchanged, runtime tiling stayed enabled, and the running tiler payload was retained. Evidence: `test-results/picker-final/live-settings-update.json`. No host input was injected.
