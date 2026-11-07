# Integrated appearance

With **Expand a single tiled window** enabled (the default), a lone tile fills
the usable desktop with no outer gap, custom outline or owned corner rounding.
Panels remain visible. The configured appearance returns when another tile
joins, or when the option is disabled. Floating windows retain their own
appearance. Application-drawn headers and transparency remain application-owned.

Fluid Tiles 0.5 adds outlines, corner clipping and native titlebar control to the same controller that owns tiling and animation. Open **Fluid Tiles Settings → Appearance**; reopen an already-running settings window after upgrading to load the new tab. Saving settings never enables the controller.

| Setting | Default | Meaning |
|---|---|---|
| Outlines | On | Independent section toggle. Off removes outlines and reserved outline space. |
| Rounded corners | On | Independent section toggle. Off restores native corner behavior. |
| Titlebar control | On | Independent section toggle. Off restores supported native titlebars. |
| Outline thickness | 2 logical px | 0–8 px. Positive widths snap to whole physical pixels, with at least one physical pixel. Zero removes both the outline and its reserved layout space. |
| Corner radius | 8 logical px | 0–32 px, bounded by the current window size. Zero restores native corner behavior. |
| Use KDE colors | On | Active: KDE selection background. Inactive: KDE inactive foreground. Changes to the color scheme update the outlines. |
| Custom colors | KDE-style blue/gray defaults | Native KDE color controls, enabled when system colors are unchecked. Window/effect opacity also modulates the outline. |
| Hide titlebars on | Tiled windows | Choose tiled windows or tiled and floating windows. The section toggle restores native titlebars. |
| Appearance exclusions | KDE helper presets, excluding Plasma settings | Whole-field, case-insensitive application/class/instance globs, one per line. Separate from the tiling ignore fields. |

Ordinary application windows and dialogs can receive outlines, including Fluid Tiles Settings and windows excluded from tiling. Internal surfaces, menus, tooltips, panels, desktop/shell surfaces, minimized, maximized and fullscreen windows retain native presentation. The built-in appearance application presets exclude launchers and helper applications. Plasma application IDs are excluded from tiling, but ordinary Plasma settings windows and dialogs can receive outlines and rounded corners. Panels, desktop surfaces and layer-shell surfaces remain structurally excluded regardless of application ID. A transient dialog can be decorated while remaining outside tiling.

Hiding a native titlebar removes the KDE-drawn frame. It does not remove a browser toolbar or another header painted by the application itself. Forced KWin decoration rules take precedence; fixed-size clients and bounded floating clients that cannot preserve their frame size keep their native frame. Use the existing Meta+left drag gesture for tiles with hidden titlebars. Floating windows restore supported native controls in the default mode. Since 0.5.1, the float shortcut animates a tile to the center of its usable screen area while restoring its remembered floating size. Outlines and corners follow that transition, and floating appearance returns to the native rendering path once settled.

The outline lives outside the client frame, with its thickness reserved inside each tiled allocation. Inner and outer gaps continue to measure visible empty space. Floating geometry is retained. Native client rounding can produce a physical-pixel difference between requested and committed frame edges; settled outlines follow the committed frame rather than leaving a gap beside it.

KWin's native scene clipping handles settled windows without an additional full-window offscreen copy. Existing temporary animation textures use presentation-space corner masking, with source corner clipping temporarily bypassed during fresh captures so a stretched snapshot does not scale the baked corners. Cached snapshots avoid repeated source changes; recursive captures do not draw duplicate outlines. The per-window outline pass follows stacking, opacity, viewport transforms and native color conversion. Since 0.5.2, outlines remain attached to both incoming and outgoing windows throughout KWin Desktop Slide, using its translation and clipping. Other fullscreen desktop effects still suppress the custom outline. Previous and new extents are damaged when necessary; idle decoration does not drive frames. Native KDE shadows remain in use; no new blur or shadow effect is supplied.

Application-provided transparency remains transparent: clipping cannot reconstruct pixels already omitted by an application. Native fullscreen/maximized corner behavior is restored, including the current decoration's native radius. Appearance state and native titlebar policy restore on exclusion, disable and renderer unload. An appearance shader error restores native appearance and leaves tiling operating; the runtime reason is exposed in diagnostics.

Preferences use `[Script-fluidtiles-controller]` in `kwinrc`: `OutlinesEnabled`, `RoundedCornersEnabled`, `TitlebarControlEnabled`, `BorderWidth`, `CornerRadius`, `UseSystemBorderColors`, `ActiveBorderColor`, `InactiveBorderColor`, `TitlebarScope` (0 hide tiled, 1 hide supported tiled/floating), and `AppearanceIgnoredApplications`. The installer migrates the old shared toggle and titlebar mode once, preserving effective choices and any newer independent settings. Appearance migration version 2 removes the two Plasma IDs only from an unchanged legacy factory exclusion list; explicit custom lists remain user-owned. New installs and KDE Defaults use the separate appearance defaults. Disabled sections retain their tweak values. Existing gap, spring, ignore and activation preferences are preserved on installation. Runtime diagnostics include effective colors, availability/error, outline/capture counters, frame/outer bounds, native corner radius, decoration policy and ownership.
