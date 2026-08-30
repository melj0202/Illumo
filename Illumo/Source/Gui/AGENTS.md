# Illumo GUI Subsystem Guidance

This directory contains the consolidated primitive-composed GUI toolkit for
Illumo (`GuiKit`, `GuiDialog`, `GridAtlas`, `GuiTypes`).

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
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
