# Platform subsystem guidance

This file specializes the repository `AGENTS.md` for
`Illumo/Source/Platform/` and applies to all platform children.

## Scope and boundaries

Platform is an Illumo system subsystem. It owns OS entry points and native
save/load dialogs. Entry code obtains a consumer's declarative
`IllumoApplicationDefinition` and passes it to the engine runner. Native dialog
code implements the public Illumo platform contract without owning game state
or persistence parsing.

- Keep OS headers, frameworks, and handles inside the applicable platform
  child.
- Do not include IllumoGame, Game, or Rulesets types.
- Do not duplicate the main loop, parser, renderer, or save format by platform.
- Treat dialog cancellation as a normal empty result; selection must not mutate
  game state.
- `PixelWindow` secondary windows are `GLFW_NO_API` and presented from CPU
  images by the per-OS presenter; never give them an OpenGL context or share
  the main context (D-UI5). Create, pump, present, and destroy them on the
  main thread, before GLFW terminates. Detached tool panels reach them only
  through `ISurfaceWindow` / `ISurfaceWindowFactory` (`SurfaceWindow.h`).

## Support status

Windows is the only supported and currently verified platform. Linux sources
are repaired for Ubuntu 24.04 x86_64 X11/XWayland with gtkmm-3 dialogs; do not
describe Linux as supported until native configure, compile, launch, dialog,
render, and shutdown evidence exists on that host. macOS is not targeted; no
platform scaffold is retained and CMake rejects Apple targets. Source presence
does not establish support.

## Documentation and verification

Use `docs/packages/platform.md`, the applicable child map, and the portability
chapter. Native changes require a target-OS build and manual dialog/window
smoke; static inspection is a limitation, not validation.
