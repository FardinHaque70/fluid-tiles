# Fluid Tiles

Dwindle tiling, smooth window transitions and integrated appearance for **KDE Plasma Wayland**.

Fluid Tiles combines a native KWin tiler with configurable outlines, rounded corners and titlebar control. It takes inspiration from Hyprland's tiling and motion, with Krohnkite as the KDE integration reference.

**Version 0.6.3 · Experimental · GPL-3.0-or-later**

## Features

- **Dynamic tiling:** a Dwindle tree with focus-based insertion, drag-to-swap and edge insertion. Automatic two-window layouts prefer side-by-side columns when application size limits allow.
- **Single-window mode:** enabled by default, a lone tile fills the desktop while keeping panels visible. Configured gaps, outlines and rounding return when a second tile opens.
- **Responsive resizing:** drag a divider or use Meta + right-drag to adjust splits. A single tile can resize freely while held, then animate back on release.
- **Tunable motion:** interruptible spring and Bézier animation profiles for opening, closing, rearrangement, drop settling and floating transitions. Default and Elastic presets and a compact visual curve editor keep tuning accessible.
- **Integrated appearance:** independently configurable outlines, rounded corners and supported titlebar hiding, with KDE theme colors or custom active/inactive colors.
- **Window rules:** configurable gaps and application, title and role exclusions, with a searchable window picker for adding rules. Appearance exclusions are separate from tiling exclusions; ordinary floating windows stay above tiles.
- **Restoration:** disabling Fluid Tiles restores the window geometry, decoration and layer state it owns.

## Compatibility

Developed and tested with **KWin 6.8.2 on Plasma Wayland**. Fluid Tiles uses KWin's private native APIs and must be built against the exact running KWin version. Rebuild and reinstall after a KWin update.

The current validation focuses on a single-monitor Wayland setup. Broader multi-monitor and Activity workflows need further testing. See [validation and known limits](VALIDATION.md).

## Installation

Install a C++20 compiler, CMake 3.25+, Python 3 and the development packages for matching KWin, Qt 6, Extra CMake Modules, KDE Frameworks 6 (GlobalAccel, Config, KCMUtils, ColorScheme and WidgetsAddons), KDecoration3, KWayland, Plasma Workspace's task manager library (`LibTaskManager`) and Wayland. The controller also uses KDE's `qdbus6`, `kreadconfig6` and `kwriteconfig6` tools. Optional LayerShellQt development files enable the panel fixture used by the single-window integration tests.

```bash
git clone https://github.com/FardinHaque70/fluid-tiles.git
cd fluid-tiles
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
python3 tools/control.py install
python3 tools/control.py start
```

Installation is user-local and requires no administrator privileges. A first `install` leaves Fluid Tiles disabled; updates preserve the current activation and login preference. `start` enables it now and at subsequent logins. The package is installed under `~/.local/share/kwin/scripts/fluidtiles-controller`.

You can also enable or disable it in **System Settings → Window Management → KWin Scripts**.

## Controls

| Action | Default control |
|---|---|
| Move and retile on drop | Meta + left-drag, or drag a visible titlebar |
| Resize a split | Meta + right-drag, or left-drag a divider |
| Focus left / down / up / right | Meta + H / J / K / L |
| Swap left / down / up / right | Meta + Shift + H / J / K / L |
| Toggle floating and center | Meta + Shift + Space |
| Rotate the parent split | Meta + Ctrl + Return |
| Toggle tiling | Meta + Ctrl + T |
| Emergency disable | Meta + Ctrl + Shift + F12 |

Dropping in a tile's center swaps windows; dropping near an edge inserts beside it. Escape cancels an interaction. Keyboard shortcuts can be rebound in KDE System Settings.

## Settings

Open **Fluid Tiles Settings** from the application launcher, or run:

```bash
python3 tools/control.py configure
```

Adjust gaps, animation presets and event profiles, **Expand a single tiled window**, ignored windows and the three independent appearance sections: **Outlines**, **Rounded corners** and **Titlebar control**. Changes apply live; the settings dialog also works while tiling is disabled. Application-drawn headers remain application-controlled.

The **Animations** tab offers **Default** and **Elastic** (the copied Hyprland Example recipe). Adjust events and curves side by side, including spring physics, opacity and entrance style. Edits stay unsaved until Apply.

See [settings and window rules](docs/settings.md) and [appearance behavior](docs/appearance.md).

## Management

```bash
python3 tools/control.py status   # Inspect the running plugin
python3 tools/control.py stop     # Disable and restore windows
python3 tools/control.py remove   # Remove the user-local package
```

Saved preferences are retained when removing the package.

## Development and testing

```bash
ctest --test-dir build --output-on-failure
python3 tests/appearance_migration_test.py
python3 tests/integration.py
```

The integration runner launches disposable KWin with separate D-Bus, configuration and runtime directories. Synthetic input belongs only in that test session, never on the host desktop. Generated results and session files are excluded from Git.

Tests cover layout, resizing, interrupted motion, appearance and state restoration, including fractional scaling. Virtual-output checks do not establish physical 144 Hz smoothness. Client buffers can arrive late during resizing, and application input regions follow KWin's committed geometry during visual transitions.

See the [validation record](VALIDATION.md), [behavior audit](docs/behavior-audit.md) and [Krohnkite integration reference](docs/krohnkite-reference.md) for details.

## Acknowledgments and license

[Hyprland](https://github.com/hyprwm/Hyprland) provides the behavioral reference; [Krohnkite](https://github.com/beefsack/Krohnkite) informs KDE integration and exclusion defaults. Fluid Tiles' tiler is a native KWin implementation.

Upstream animation reference configurations are retained with their original licenses and a [source manifest](package/contents/data/animations/upstream/manifest.json). The catalog copies their window animation recipes, with documented KWin adaptations. HyprMod supplies the 19 named easing curves. See the packaged [credits, notices and mapping](package/contents/data/animations/ADAPTATIONS.md).

Licensed under the [GNU General Public License, version 3 or later](LICENSE).
