# Animation recipe provenance and adaptations

The `upstream/` directory contains unchanged source snapshots and their licenses.
Its manifest records exact repository revisions, original paths, URLs and SHA-256
hashes. `catalog.json` is built by `tools/animation_catalog.py`; run that tool with
`--check` to verify the catalog against those literals without executing Lua or
importing the copied Python module.

## Credits and notices

- Hyprland example configuration: Hyprland contributors, copyright 2022–2026
  vaxerski, BSD 3-Clause. See `upstream/hyprland-LICENSE`.
- HyDE recipes: the HyDE project and its contributors, GNU GPL version 3.
  See `upstream/hyde-LICENSE`. The `end4.lua` recipe describes its Material easing
  as by End4; that credit is retained in the Popin Soft preset.
- ML4W configuration: Stephan Raabe and ML4W contributors, GNU GPL version 3.
  See `upstream/ml4w-LICENSE`. The included ML4W default documents the shared
  Popin Soft recipe; Dynamic is a separate bundled profile.
- HyprMod easing library: copyright 2026 Ivo Šmerek, GNU GPL version 3.
  See `upstream/hyprmod-LICENSE`. All 19 `BUILTIN_PRESETS` tuples are copied.

The snapshots and derived preset data are distributed with these notices and
license texts. Fluid Tiles remains GPL-3.0-or-later. The native Qt editor and
motion engine are independently implemented; upstream UI code and interpreters
are not embedded.

## Exact mappings

| Upstream value | Fluid Tiles |
| --- | --- |
| `windowsIn` | Opening geometry |
| `windowsOut` | Closing geometry |
| `windowsMove` | Rearrangement; inherited by drop settling and float transitions |
| `fadeIn`, `fadeOut` | Independent opening/closing opacity tracks |
| Bézier `speed` in deciseconds | Duration in milliseconds, multiplied by 100 |
| Spring `mass`, `stiffness`, `damping` / `dampening` | Same physical parameters; source speed is not a spring duration |
| Unspecified child settings | Resolve the parent chain in that source recipe |
| Implicit `default` Bézier | Pinned hyprutils default `(0, .75, .15, 1)` |
| `popin N%` | Destination-centered scale from/to N%; minimum dimension 5 logical pixels |
| Empty window style | Destination-centered pop to/from the upstream default minimum |
| `slide [side]` | Slide across the corresponding usable-work-area boundary; nearest edge if unspecified |

Upstream window movement styles select opening/closing geometry; they do not
add pop/slide motion to arbitrary rearrangements. That distinction is preserved.
The source HyDE duration multiplier is 1 in the factory data. Fluid Tiles' global
0.1×–2× speed and KDE's animation factor apply to both curve types by scaling time.

Opacity is clamped to [0, 1] when painted, including overshooting opacity curves.
Geometry curves retain overshoot, with positive presentation dimensions. Spring
retargeting retains each edge's physical velocity; Béziers restart from the
current visible geometry. This is not a claim that Hyprland's vector retargeting
or every compositor-specific stopping threshold is identical.

Workspace, layer, popup, active-opacity/dimming, shadow-opacity, monitor/zoom,
border-color and rotating-gradient settings are omitted. KDE retains those
effects and native input regions. No imported profile changes window admission,
tiling layout policy, or the user's appearance preferences. Presets are labeled
“Adapted for Fluid Tiles” in the editor, whose details link to the original recipe.

## Fluid Tiles presets

Fluid uses critical springs with angular frequencies 28 (opening/floating),
32 (rearrangement) and 36 (drop). It preserves the requested desktop-centered
65% entrance, fades in over 140 ms, and closes to 90% in 140 ms with a separate
120 ms linear fade. Spring settling thresholds are 0.1 physical pixel and
1 physical pixel/second. The speed multiplier defaults to 1.

Classic retains the old spring cutoff and delegates closing to KDE. Migration
saves the previous frequency as a custom “Classic — previous settings” preset
before selecting Fluid once. Off suppresses managed-window lifecycle effects
and snaps to geometry immediately. KDE's own zero animation factor also settles
all tracks and releases closing visuals.

ML4W Smooth is intentionally not bundled: its current source requests a
100-decisecond closing geometry animation. The original timing has not been
silently shortened and presented as the upstream preset.
