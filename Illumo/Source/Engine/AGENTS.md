# Engine subsystem guidance

This file specializes the repository `AGENTS.md` for `Illumo/Source/Engine/`.

## Scope and boundaries

Engine hosts the generic application runner, long-lived services, the render
backend, module lifecycle, and the per-frame service/module sequence. A
consumer-supplied application definition chooses the required product module;
Engine must remain independent of Game and Rulesets.

- Engine may depend on Foundation, Services, Rendering interfaces, and the
  platform-neutral window abstraction.
- Concrete OpenGL backend creation is allowed only through the existing
  Rendering factory boundary.
- Do not include game-domain types or register game commands here.
- Own logger lifetime, system CLI dispatch, DebugModule composition, process
  result handling, and the chrono update/render loop in the generic runner.
- Treat `CreateIllumoApplication` as the narrow reverse seam: the platform
  entry obtains declarative policy from the consumer without including its
  types.

## Required invariants

- `Illumo` exclusively owns long-lived services and accepted modules. Prefer
  `unique_ptr`; borrowed pointers in `IllumoContext` never transfer ownership.
- Populate and freeze `IllumoContext` during initialization. Do not grow the
  context casually; prefer explicit constructor dependencies when a genuinely
  different consumer appears.
- `IModule::Start` is a fallible `bool` contract. Remove a failed module before
  any `Update`, `DispatchDrawables`, or `Exit` call.
- For accepted modules, call `Start`, repeated `Update` and
  `DispatchDrawables`, and one `Exit` in a deterministic order.
- Optional overlay modules (`DebugModule`) `Update` before the required
  product module so global console input is not stolen. `DispatchDrawables`
  stays required then optional so overlays draw on top. Discard unconsumed
  key/char events at the end of each host update.
- Keep window/input/module/render work on the main thread. Worker threads owned
  by another subsystem must join or quiesce before their owner is destroyed.
- Keep the `Renderer` backend-neutral. Engine composes
  `CreateOpenGLBackend`; modules see only the context/interface boundary.
- Detach logger sinks and callbacks before destroying the objects they target.
  Shutdown must be safe after partial initialization and must release the
  graphics context after dependent resources.

## API and compatibility

`IModule`, `IllumoContext`, and service lifecycle ordering are semi-public
engine contracts. Changes require all module implementations and lifecycle
tests to move together. Do not convert context pointers into hidden ownership
or make module callbacks concurrent without an authorized architecture change.

## Documentation and verification

- `docs/packages/engine.md`
- `docs/latex/sections/04-runtime-loop.tex`
- `Illumo/Tests/TestRuntimeUtilities.cpp` and module integration tests

Use MockBackend tests for lifecycle and queue behavior; use a Debug and Release
startup/shutdown smoke when host, window, backend, or DebugModule composition
changes. Update this file only for durable Engine contracts.
