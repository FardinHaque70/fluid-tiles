# Fluid Tiles settings

The native KDE configuration dialog is shared by the app-menu entry and the KWin Scripts configuration button. Its packaged configuration module binds the existing schema to KDE's standard settings controls.

## Opening the dialog

Search the application launcher for **Fluid Tiles Settings**. This works with the tiler disabled. The command `python3 tools/control.py configure` opens the same window.

When Fluid Tiles is checked, its **Configure** button in **System Settings → Window Management → KWin Scripts** also opens the form. KDE's plugin delegate disables this button for unchecked scripts, which is why the standalone entry is provided. Reopen System Settings after installing the update if it still shows the earlier metadata.

The settings window itself is excluded from tiling. Opening, saving, cancelling or resetting preferences does not check the script's activation checkbox. Apply updates an already running controller without reloading it. Defaults changes the form first; Save/Apply commits those changes. Cancel discards unsaved changes.

## Fields

| Setting | Default | Behavior |
|---|---|---|
| Gap between windows | 10 px | Inner gap, 0–80 desktop pixels, with display scaling applied. |
| Gap around the layout | 10 px | Outer gap, 0–80 desktop pixels, with display scaling applied. |
| Animation responsiveness | 24 | Range 8–60. Higher values settle faster; KDE's global animation speed remains effective. |
| Expand a single tiled window | On | A lone tile fills the usable desktop without outer gaps, outlines or rounded corners. Panels remain visible. A second tile restores configured spacing and appearance. Floating and ignored windows do not count as tiles. Native maximum-size limits can retain the ordinary inset tile. |
| Application ID / class / instance | Krohnkite KDE presets | Ignore a window if either its application/class identifier or instance matches a pattern. |
| Window title | Empty | Ignore matching titles; membership updates if a client changes its title. |
| Window role | `quake` | Ignore matching roles when the application provides one. Many Wayland clients do not provide roles. |

The application defaults are `krunner`, `yakuake`, `spectacle`, `kded5`, `xwaylandvideobridge`, `plasmashell`, `ksplashqml`, `org.kde.plasmashell`, `org.kde.polkit-kde-authentication-agent-1`, `org.kde.kruler`, `kruler`, `kwin_wayland`, and `ksmserver-logout-greeter`, taken from Krohnkite’s configuration. Defaults restores these presets. Existing explicitly saved rules are preserved on upgrade; missing keys acquire the defaults.

Popup/internal windows (including decoration tooltips), hidden windows, special windows, modal/transient windows and non-resizable clients are excluded independently of patterns. Clearing the fields cannot make a tooltip tile.

## Appearance

Version 0.5 adds three independently switchable sections in the Appearance tab: **Outlines** (width and automatic KDE or custom colors), **Rounded corners** (radius), and **Titlebar control** (hide on tiles or on tiled and floating windows). Turning a section off disables its fields and restores only that native feature; its saved values are retained. Shared appearance exclusions remain separate from tiling exclusions. Defaults hide supported tiled titlebars; floating windows keep their controls. Gap values retain visible empty space between outlines. See [appearance details and limits](appearance.md).

## Ignore patterns

Enter **one pattern per line**. Matching is case-insensitive and covers the whole property. `*` matches any text, including `/`; `?` matches one character. Blank lines are skipped, leading/trailing whitespace is ignored, and absent properties do not match. These are wildcard patterns, not regular expressions. A match in any of the three fields is enough to exclude the window.

Each ignore field, including appearance exclusions, has a **Pick a window…** button. Search the open-window list, select a window, then choose **Add rule**. Application fields offer the actual KWin application/class identifier and a separate instance when available; title and role fields use their corresponding property. Missing properties cannot be added. Picking appends a literal rule, preserves existing entries, and skips duplicates. Wildcard characters in picked values are escaped with character classes. You can edit the resulting rule before Apply; cancelling or reloading discards unsaved additions. The picker works while Fluid Tiles is disabled.

The list comes from KDE's Wayland window task model, and the selected window's properties come from KWin's `getWindowInfo` D-Bus method. It does not depend on the native tiler. This is a settings convenience; admission checks, pattern matching and Krohnkite-derived defaults remain unchanged.

Application examples:

```text
firefox
org.kde.*
```

Title examples:

```text
*Picture-in-Picture*
*Preferences*
```

Role example:

```text
pop-up*
```

These are examples, not enabled defaults. Adding a rule restores a currently tiled window's floating geometry. A window already in fullscreen/maximized/minimized state keeps that native state and respects the rule when restored. Newly opened matching windows stay unmanaged. Removing a rule lets an eligible window rejoin automatically; a window explicitly set to persistent floating remains floating until that choice is toggled off.

## Storage and upgrade

For a border on KDE's authentication dialog while keeping it floating, remove
`org.kde.polkit-kde-authentication-agent-1` only from **Appearance → Appearance
exclusions**, and retain it under **Ignored windows**. The two exclusion lists
are independent. This is a saved preference; restoring appearance defaults
restores the preset exclusion.

The schema is `package/contents/config/main.xml`, with values in `kwinrc` under `[Script-fluidtiles-controller]`: `InnerGap`, `OuterGap`, `SpringFrequency`, `ExpandSingleWindow`, `IgnoreApplications`, `IgnoreTitles`, and `IgnoreRoles`. Use the dialog for multiline rules so KConfig handles their serialization.

The installer migrates the earlier shared appearance toggle and titlebar choice once into `OutlinesEnabled`, `RoundedCornersEnabled`, `TitlebarControlEnabled` and `TitlebarScope` (0 tiled, 1 tiled and floating). Existing independent choices win. A previously disabled master leaves all three off; Keep titlebars leaves titlebar control off.

The installer migrates earlier manual gap/spring settings from `[Effect-fluidtiles]` once, preserving existing values in the script group. Subsequent installs respect the new preferences, including a reset to Defaults. The renderer's emergency enable/disable state remains separate from these preferences.

The settings app's desktop entry is `org.fluidtiles.Settings.desktop`. It starts the settings frontend included in this package; it does not start the tiler. Removing Fluid Tiles with the control tool removes only its recognized managed launcher and package. Saved preferences are retained.
