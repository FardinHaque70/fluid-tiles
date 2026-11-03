# Fluid Tiles implementation rules

Hyprland is the user's gold standard for tiling behavior and motion. Before changing window admission, insertion, dragging, resizing or native-state conditions, inspect the corresponding Hyprland source and document any KWin-specific adaptation. Use Krohnkite's full KDE path as the integration reference: `kwindriver.ts::addWindow`, `kwinwindow.ts::shouldIgnore` / `shouldFloat`, and `res/config.xml` defaults. Do not infer its behavior from only one inner function.

Pinned inspected revisions and source paths are in `docs/behavior-audit.md` and `docs/krohnkite-reference.md`. Do not claim exact upstream equivalence when behavior deliberately differs.

Reproduce reported bugs and add meaningful regression coverage. Run native changes in disposable KWin first; the test socket is `fluidtiles-test`. Never use the fake-input helper on the host desktop. Preserve the user's current activation state and preferences during an authorized live update. Virtual-output checks do not establish physical 144 Hz smoothness.
