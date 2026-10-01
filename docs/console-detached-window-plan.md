# Detached developer console window — execution plan

Status: implemented 2026-09-23 (see Validation). Tier 2 per `.agent/PLANS.md`.

## Objective

The developer console can leave the game window and live in its own OS window:

- **Pop out:** a header button, `console_mode detached`, or dragging the
  floating console's title bar out of the game window.
- **Come back:** the detached window's `dock` button (console returns open and
  floating inside the game), closing the OS window, or `` ` `` from either
  window (console returns closed).
- While detached the game keeps full keyboard/mouse input; the console window
  owns its own keyboard focus, editing, scrolling, and mouse interaction.

## Current-state evidence

- `CommandLine` (Services) composes all chrome and text as `GameVisual`
  primitives and submits them through the token renderer (D-R15, D-P2, D-UI4).
- `RenderWindow` owns the only GLFW window and the only OpenGL context;
  `GLBackend` caches state for that single context. The repository contract
  states that moving the OpenGL context or adding render targets for another
  window needs its own accepted design.
- `DebugModule` (Debug/RelWithDebInfo only) routes Grave, console editing, and
  mouse input; product modules read input only while the console is closed.

## Design

1. **No second GL context.** The detached console is a GLFW window created
   with `GLFW_NO_API`. GLFW supplies creation, native title bar, move/resize,
   close, focus, and keyboard/mouse callbacks on every platform.
2. **CPU presentation.** A new `SoftwareCanvas` (Rendering/Primitives)
   rasterizes a `GameVisual`'s primitives in painter order into an RGBA image:
   filled/outline rectangles, lines, and font-atlas text (same glyph metrics
   as `GameVisual::pushTextRun`). The platform presents that image; Windows
   uses GDI `StretchDIBits`. The Linux presenter reports unavailable (Linux is
   not a supported platform; detaching fails with a console error there).
3. **One composition path.** `CommandLine` gains a detached mode: layout uses
   the detached window's pixel size at UI scale 1, the panel fills it, there
   is no slide, and composition is split from token submission so the same
   primitives feed either the Renderer (attached) or the canvas (detached).
4. **Requests, not windows, in Services.** `CommandLine` raises
   `Detach`/`Attach` requests (header button, drag-out, `console_mode`);
   `DebugModule` owns the `PixelWindow`, its lifetime, input routing, and
   presentation. Services still depends on no platform window code.

Alternatives rejected: a shared-context second GL window (VAOs are not shared,
backend state caches assume one context, violates the single-context
contract); a GDI/Win32-drawn console (would fork the console's drawing and
diverge from the in-game look).

## Constraints and invariants

- Main-thread only; the window is created, pumped (by the existing
  `glfwPollEvents`), presented, and destroyed on the main thread.
- The in-game console path (tokens, D-P2 settled replay, wrap cache) is
  unchanged when attached. Detached composition reuses the same dirty rules.
- `CommandLine` stays compiled into WASM guests; detach code there is inert.
- While detached `isOpen` is false, so product input and global hotkeys behave
  as if the console were closed; the closed-console alert badge is suppressed.
- `DebugModule::Exit` destroys the window before engine teardown.

## Verification

- Headless: `SoftwareCanvas` pixel tests (fill/blend, outline, line, text
  coverage, painter order); `GameVisual` paint order; `CommandLine` detached
  layout/composition, header buttons, drag-out request, `console_mode
  detached`, input interactivity while detached, badge suppression.
- Full Release build (runtime + guests) and the labeled CTest suite.
- Manual RelWithDebInfo smoke: pop out via button and drag, type/scroll/click
  in the detached window, resize it, dock, close with X, `` ` `` from both
  windows, game input while detached, app exit while detached.

## Validation

2026-09-23, Windows 11, Visual Studio 18 generator:

- Release workspace build (runtime and all WASM guests) succeeded; the
  labeled suite passed 509/509, including `Illumo.SoftwareCanvas.Primitives`
  and `Illumo.CommandLine.DetachedWindow`.
- RelWithDebInfo manual smoke with `IllumoRuntime`: pop out via the header
  button, via `console_mode detached`, and via dragging the floating title bar
  onto the game window's title bar; typing, clipboard paste, Tab completion,
  help search, and watch strip in the detached window; moving it with the
  native title bar; game menu input while detached; `dock` button; closing
  with the window's X; console key from both windows; `quit` while detached
  exits cleanly with an empty error log.
- Not verified live: native resize (the automation could not start a drag
  outside the window; size changes are re-read every frame and covered by
  `setDetachedSize` tests) and Linux (presenter reports unsupported).
- Test-automation note: this session's desktop automation delivered plain
  clicks without button messages to GLFW windows; press-move-release drags
  did arrive, so button checks used zero-length drags.
