# Illumo GUI Subsystem Guidance

This directory contains the consolidated primitive-composed GUI toolkit for
Illumo (`GuiKit`, `GuiDialog`, `GuiMenuShell`, `GridAtlas`, `GuiTypes`,
`GuiTextEdit`, `GuiFileTree`, `GuiToolStyle`, `GuiPanelDock`,
`GuiPanelPointer`, `PanelSurfaces`). These sources also build into the guest
engine.

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
  keycaps, the liquid selection drop and press splash) is composed in `GuiKit`
  from `GameVisual` gradient quads. The glass menus' motion character is
  liquid and a little bouncy (D-UI8): configure springs from `GuiMotion`
  presets instead of literal frequencies, and draw a travelling selection with
  `drawLiquidSelection` rather than restating pill layers. Keep the
  flat helpers (`drawPanel`, `drawCard`, `drawBackdrop`, `drawButton`, ...)
  byte-stable; tools and labels depend on their exact shapes.
- Hover and focus weight goes through `GuiKit::drawEmphasizedText` (and its
  centered and measuring companions), which read the product's
  `FontWeightRamp::ui()` (D-UI9). Measure emphasized labels with
  `measureEmphasizedText` rather than character-count estimates; weight
  changes their width.
- Overlay behavior that more than one screen would repeat belongs in
  `GuiMenuShell`: easing curves and clock-driven spring shapes (`GuiEasing`),
  damped springs for physical motion (`GuiSpring`, `GuiSpringArray`) tuned
  from the shared `GuiMotion` presets, the pointer tilt every glass panel
  swivels by (`GuiPanelTilt`: shift the layout origin by the body layer so
  hit testing follows, offset glass and decoration by depth relative to it),
  reveal/row-drop/liquid-selection/
  sheen/press/wobble/value-pulse/ambient/caret clocks with reduced motion
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
- Text entry goes through `GuiTextEdit` (UTF-8 caret and selection, clipboard
  through the caller, characters from `InputManager::getCharQueue`) drawn by
  `GuiKit::drawTextField`. Do not write another line editor.
- Tree views go through `GuiFileTree`: it keeps expansion state and the
  listings received so far and flattens them without recursion; the owner
  lists directories itself (`takePendingListings`) from whatever source it
  has. It never reads files.
- Tool apps (IllEd, IllMeshViewer) draw with `GuiToolStyle`, the plain
  tool look (D-UI7): its own neutral palette like the console's, flat
  rectangles, lines and text only, fixed tool metrics, no animation. Do not
  mix it with the glass `GuiKit` chrome in one window.
- Tool panels live in a `GuiPanelDock`: it lays out the columns, splitters and
  title bars, handles hide, pop-out, tear-off and dock, and serializes the
  layout. It never draws content and keeps no widget tree. Panel content
  draws into a `GuiPanelPlacement` and reads input through `GuiPanelPointer`,
  which samples the main window or the placement's `IPanelSurfaces` window
  and honours `inputBlocked`. Content code must not know whether it is
  detached.
- `IPanelSurfaces` (`PanelSurfaces.h`) is only an interface; implementations
  live in the guest SDK (`GuestPanelSurfaces`) and `Illumo::TestSupport`
  (`FakePanelSurfaces`). Products must work fully docked when it is absent.
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
