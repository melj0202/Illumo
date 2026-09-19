# Illumo Engine

The reusable application runner, host, and source-level module contract live in
Illumo:

- `IllumoConfig` carries application name and configuration path. Private window/backend
  factories are injected by `IllumoTestAccess` in tests.
- `Illumo` owns long-lived generic services and drives update/render/shutdown.
- F11 asks the window to toggle fullscreen and reports its resulting environment
  value. The window publishes the state; the host does not invert it again.
- `IllumoContext` is a frozen, non-owning service bag with no game assumptions,
  exposing `IModuleHost*` for deferred module transitions.
- `IModule` retains `Start` / `Update` / `DispatchDrawables` / `Exit`.
- Optional `IModule::OnCloseRequested()` accepts by default. The runner calls
  `Illumo::processCloseRequest()` before termination; started modules may defer
  close to show product confirmation. Deferral clears the native close flag and
  normal updates/rendering continue. Failed required-module transition remains
  terminal. Custom windows serving deferring modules must implement
  `cancelCloseRequest()`. `shouldClose()` remains a raw observation.
- `IModuleHost` allows modules to request runtime transitions (`RequestTransition`)
  deferred to frame boundaries with input queue and scene clearing.
  Transitions reset all application pass overrides before owner retirement and
  after rejected startup, preserving host defaults. Optional modules must
  reinstall their overrides after transitions.
- `DebugModule` is an optional generic renderer/tooling overlay in Debug and
  RelWithDebInfo builds. It updates before the required product module so console toggle and
  editing work on every screen, and it dispatches afterward so console, FPS,
  and renderer-demo drawables sit on top.
- `IllumoApplicationDefinition` accepts declarative consumer policy while
  `RunIllumoApplication` owns logging, CLI, module registration, timing, and
  process results.

Construction loads generic defaults; `initialize()` creates the window/context,
constructs and initializes the backend exactly once, and transfers
`std::unique_ptr<IBackend>` to `Renderer`. Failures are logged and returned; the
library does not terminate the process.

Module registrations are required or optional. Optional rejection destroys the
module immediately. Required failure rolls back all accepted modules in reverse
order, with exceptions contained so every cleanup is attempted. Illumo invokes
the consumer-supplied required-module factory through the application
definition; it never includes or constructs a concrete Game type directly.
At runtime, an active required module may transition to a new required module
via `IModuleHost::RequestTransition`, which safely runs `Exit()` on the old
module, purges pending input queues, clears `Scene` drawables, and invokes
`Start()` on the incoming module.

Registration closes at startup and accepts at most one required product module.
The first non-null deferred transition wins; competing or empty requests log a
rejection and cannot replace it. Unstarted rejected requests are destroyed without
lifecycle callbacks. DebugModule destruction does not invoke Exit; the host owns
accepted-module shutdown and exceptional-start cleanup.
