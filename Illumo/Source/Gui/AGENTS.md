# Illumo GUI Subsystem Guidance

This directory contains the consolidated primitive-composed GUI toolkit for
Illumo (`GuiKit`, `GuiDialog`, `GuiMenuShell`, `GridAtlas`, `GuiTypes`).

## Invariants

- Product UI remains primitive-composed through `GameVisual` and `UiTheme`.
  Do NOT introduce a retained widget tree, scene graph UI node hierarchy,
  or complex event bubbling system.
- `GuiKit` routines are stateless helpers drawing shapes, text, sprites, and
  chrome directly onto caller-owned `GameVisual` instances.
- Reusable UI elements (like `GuiDialog`) manage their own draft parameters,
  animations, and hit testing while delegating all drawing to `GameVisual`
  primitives and `GuiKit`.
- Theming and colors route through `UiTheme` as the canonical source of truth.
  Fades target `UiTheme::transparentOf(color)` (same color, zero alpha), never
  transparent black: blending is straight-alpha, so a black fade darkens the
  interpolated edge.
- Soft chrome (glass panels, gradients, glows, soft shadows, sheens, vignettes,
  keycaps) is composed in `GuiKit` from `GameVisual` gradient quads. Keep the
  flat helpers (`drawPanel`, `drawCard`, `drawBackdrop`, `drawButton`, ...)
  byte-stable; tools and labels depend on their exact shapes.
- Overlay behavior that more than one screen would repeat belongs in
  `GuiMenuShell`: easing curves (`GuiEasing`), damped springs for physical
  motion (`GuiSpring`, `GuiSpringArray`), reveal/selection/stretching-span/
  sheen/press/value-pulse/ambient/caret clocks with reduced motion
  (`GuiMenuAnimator`), fitted virtual
  space and row-window arithmetic (`GuiPanelLayout`), and virtual-space pointer
  sampling with press and release edges (`GuiPointerTracker`). A new screen
  composes these rather than re-deriving timings, scale fitting, or hit-test
  bookkeeping; its own layout constants, rows, and drawing stay with the
  screen. Centered overlays fit into the shared design space with
  `GuiPanelLayout::fit`; docked bars and panels anchor to the real window with
  `GuiPanelLayout::viewport`.
- `GuiMenuShell` owns no drawing, no input polling beyond pointer sampling, and
  no knowledge of product rows. Keep it that way.
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
