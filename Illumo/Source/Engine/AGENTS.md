# Engine subsystem guidance

This file specializes the repository `AGENTS.md` for `Illumo/Source/Engine/`.

## Scope and boundaries

Engine hosts the generic process runner, long-lived services, the render
backend, the fixed phases of a frame and the debug overlay. The runtime
(`RuntimeShell`, in the Wasm library) supplies the run loop and runs one
program between the phases (D-E31); Engine must remain independent of Game,
Rulesets and the Wasm library.

- Engine may depend on Foundation, Services, Rendering interfaces, and the
  platform-neutral window abstraction.
- Concrete OpenGL backend creation is allowed only through the existing
  Rendering factory boundary.
- Do not include game-domain types or register game commands here.
- Own logger lifetime, system CLI dispatch, the startup report, process result
  handling and the relaunch in the generic runner (`Application.cpp`). The
  application definition's `run` callback owns the frame loop.
- Treat `CreateIllumoApplication` as the narrow reverse seam: the platform
  entry obtains declarative policy from the consumer without including its
  types.

## Required invariants

- `Illumo` exclusively owns long-lived services. Prefer `unique_ptr`; borrowed
  pointers in `IllumoContext` never transfer ownership.
- Populate and freeze `IllumoContext` during initialization. Do not grow the
  context casually; prefer explicit constructor dependencies when a genuinely
  different consumer appears. `fileTree` is the one member the program
  publishes (the WASM host, when it starts) and withdraws (when it stops);
  readers such as DebugOverlay's `files` browser (`FileTreeOverlay.h`) look it
  up at use time and never cache it.
- A frame runs in fixed phases (`Illumo.h`): `beginUpdate` (input, global
  hotkeys, camera), the debug overlay's update, the program's update,
  `endUpdate` (unread key/char events are discarded), `beginRender` (drawables
  cleared), the program's dispatch, the overlay's dispatch, `endRender`. The
  overlay updates first so global console input is not stolen, and dispatches
  last so it draws on top. Every phase is a no-op before `initialize` and after
  `shutdown`.
- `DebugOverlay::start` is a fallible `bool` contract (D-E5: it checks the
  context). A rejected overlay is stopped and dropped; the program runs
  without it.
- Keep window/input/program/render work on the main thread. Worker threads
  owned by another subsystem must join or quiesce before their owner is
  destroyed.
- Negotiate ordinary close through `shouldClose` and the program's answer; a
  declined request calls `deferClose`, which clears the native close flag and
  keeps frames running. A restart (`IRenderWindow::requestRestart`) is a close
  that also relaunches: deferral drops it, and `RunIllumoApplication`
  relaunches only after the engine and logger have shut down (D-UI13).
- Keep the `Renderer` backend-neutral. Engine composes
  `CreateOpenGLBackend`; programs see only the context/interface boundary.
- Host pipeline configuration writes DrawList default passes only. Preserve
  application overrides installed during start, update, or dispatch; clearing
  an override restores the current host fallback. A scene switch
  (`SceneDirector`) resets all application overrides to detach borrowed
  callbacks.
- The program, and any `SceneDirector` with its scenes, must be stopped and
  destroyed before `Illumo::shutdown`: their content still holds engine
  assets (the old registry destroyed modules inside shutdown; nothing does
  now). Detach logger sinks and
  callbacks before destroying the objects they target. Shutdown must be safe
  after partial initialization and must release the graphics context after
  dependent resources.

## API and compatibility

`Illumo`'s frame phases, `IllumoContext`, and service lifecycle ordering are
semi-public engine contracts. Changes require the runtime shell, the tools that
drive the phases (`IllEd/tools/CloseWindowTests.cpp`) and the lifecycle tests to
move together. Do not convert context pointers into hidden ownership or make
program callbacks concurrent without an authorized architecture change.

## Documentation and verification

- `docs/packages/engine.md`
- `docs/latex/sections/04-runtime-loop.tex`
- `Illumo/Tests/TestIllumoHost.cpp` and `Illumo/Tests/TestRuntimeUtilities.cpp`

Use MockBackend tests for lifecycle and queue behavior; use a Debug and Release
startup/shutdown smoke when host, window, backend, or DebugOverlay composition
changes. Update this file only for durable Engine contracts.
