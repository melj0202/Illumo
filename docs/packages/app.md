# Illumo application runtime

Illumo owns process-level system behavior:

- `Illumo/Source/Engine/Application.cpp` initializes logging, applies the
  consumer's defaults callback, invokes engine `SysCmdLine`, fallibly
  initializes the host, registers the consumer's required module and optional
  Debug `DebugModule` overlay, drives the `std::chrono::steady_clock` frame loop,
  performs shutdown, and returns an explicit process code. The overlay remains
  active across main-menu and canvas transitions.
- `Illumo/Source/Platform/<port>/` supplies the selected entry point and native
  dialogs.
- `Illumo/Include/Illumo/Engine/Application.h` is the narrow reverse seam. A
  consumer defines `CreateIllumoApplication()` and returns only declarative
  identity, CLI metadata, defaults, and its required module factory.

IllumoGame implements that definition in
`IllumoGame/Source/Game/IllumoGameApplication.cpp`. It contains game policy but
no process loop, logger lifetime, platform SDK code, or system parser.
