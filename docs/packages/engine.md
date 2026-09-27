# Illumo Engine

The reusable application runner, the host services and the fixed phases of a
frame live in Illumo:

- `IllumoConfig` carries application name and configuration path. Private window/backend
  factories are injected by `IllumoTestAccess` in tests.
- `Illumo` owns long-lived generic services and the fixed parts of a frame.
  The runtime's `RuntimeShell` owns the loop and runs one program between
  those parts (D-E31; see "Frame phases" below). There is no module registry.
- F11 asks the window to toggle fullscreen and reports its resulting environment
  value. The window publishes the state; the host does not invert it again.
- `IllumoContext` is a frozen, non-owning service bag with no game assumptions.
  Its `scenes` slot (`SceneDirector*`, null by default) is set by a guest
  program in its own context (`GuestProgram`); `camera` and `renderWorld`
  then follow the active scene. Its `fileTree`
  slot (`const IFileTreeSource*`, null by default) is set by the
  program that owns a mounted file tree when it starts and cleared when it
  stops (`WasmProgram` publishes `IllumoRuntime`'s tree through `VfsTreeSource`);
  tools read it at use time and never cache it. Its `panelSurfaces` slot
  (`IPanelSurfaces*`, null by default) offers extra windows for detached tool
  panels (D-E27); the guest application composes it only when the host
  granted `Windows`, and products must work fully docked without it. Its
  `audio` slot (`IAudio*`, null by default) plays sound effects (D-E28); the
  guest application composes it only when the host granted `Audio`, and
  products must also work silently. The native engine never fills it; the
  WASM runtime's `AudioDevice` reaches guests through `WasmProgram`.
- `DebugOverlay` (`Illumo/Include/Illumo/Engine/DebugOverlay.h`) is the
  generic renderer/tooling overlay of debug-tool builds
  (`ILLUMO_ENABLE_DEBUG_TOOLS`: Debug and RelWithDebInfo); Release compiles
  none (D-B1). Its calls are `start(IllumoContext&)`, `update`,
  `dispatch(DrawList&)` and `stop`. `start` returns false when the context
  lacks a service it needs (D-E5). It updates before the program so console
  toggle and editing work on every screen, and it dispatches afterward so
  console, FPS, and renderer-demo drawables sit on top. Its `files [path|off]` command opens
  `FileTreeOverlay` (`Illumo/Source/Engine/FileTreeOverlay.h`), a browser over
  `IllumoContext::fileTree` drawn with `GuiFileTree` (default root `/`; any
  other root must be a directory). Up/Down/PageUp/PageDown/Home/End select,
  Right or Enter expands, Left collapses or selects the parent, Escape closes,
  and the wheel scrolls; DebugOverlay consumes those inputs while the console
  is closed (plain keys only). Listings are read synchronously when a
  directory expands; a withdrawn tree closes the browser, and hosts without a
  tree report that none is mounted. `Illumo.Debug.FileTreeOverlay` covers it.
- `IllumoApplicationDefinition` accepts declarative consumer policy: name,
  command line, defaults and the `run` callback,
  `int (*)(Illumo&, std::chrono::steady_clock::time_point launched)`, which
  runs the frame loop on the initialized engine and returns the process exit
  code. `RunIllumoApplication` (`Application.cpp`) owns logging, the command
  line, the startup report, engine initialization and shutdown, process
  results and the relaunch.

Construction loads generic defaults; `initialize()` creates the window/context,
constructs and initializes the backend exactly once, and transfers
`std::unique_ptr<IBackend>` to `Renderer`. Failures are logged and returned; the
library does not terminate the process.

## Frame phases

`Illumo` supplies the fixed parts of a frame (`beginUpdate(dt)`,
`endUpdate()`, `beginRender()`, `endRender()`). `RuntimeShell`
(`Illumo/Include/Illumo/Wasm/RuntimeShell.h`) calls them around its one
`WasmProgram` and the overlay, in this order:

1. `Illumo::beginUpdate`: input, global hotkeys (F11, F3, F5), camera.
2. `DebugOverlay::update` (debug-tool builds only; it reads console keys
   before the program can drain them).
3. `WasmProgram::update`.
4. `Illumo::endUpdate`: unread key and character events are dropped.
5. `Illumo::beginRender`: the pipeline is configured, the frame's drawables
   are cleared, and the frame's `DrawList` is returned.
6. `WasmProgram::dispatch`, then `DebugOverlay::dispatch` (on top).
7. `Illumo::endRender`: assets are pumped and the frame is rendered and
   presented.

Every phase does nothing before `initialize` or after `shutdown`. The shell's
`run` starts the program, runs paced frames until it closes, then stops it; the
shell also owns the `--capture` and `--bench-frames` runs.

Start runs the program first, then the overlay. A program that fails to start
fails the launch; an overlay that fails to start is dropped. Stop runs the
overlay, then the program. DebugOverlay destruction does not call `stop`; the
shell does. Whoever owns a program or a `SceneDirector` destroys it before
`Illumo::shutdown`, because scene content holds engine assets.

`shouldClose()` reports that the window asked to close (or restart) or that
there is no window. The shell then asks the program (`closeRequested`); a
declined request calls `Illumo::deferClose()`, which clears the native close
flag and drops a restart riding on it, and frames continue. A finished capture
or benchmark closes without asking. Custom windows must implement
`cancelCloseRequest()` for a declined close to keep them open.

Illumo never includes or constructs a product type. Inside a program, scenes
are managed by its `SceneDirector` (`content.md`). A switch applies at a frame
boundary: it drains the key and character queues and resets the frame's pass
overrides and drawables, preserving host defaults. A failed first scene closes
the product.
