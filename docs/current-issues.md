# Illumo current issues

This is a review snapshot, not proof that every item remains open. Reproduce or
inspect an item against the live tree before fixing it.

Last reviewed: 2026-08-18

## Open correctness issues

No high-signal open product bugs at last review. Prefer re-checking against the
live tree before treating historical notes as still open.

## Coverage gaps

- Manually smoke-test native save/load dialogs, fullscreen transitions, and live
  OpenGL console presentation after relevant changes; MockBackend cannot prove
  those platform paths.
- Smoke-test Wireworld edit mode: keys `1`/`H` head, `3`/`T` tail, `4`
  conductor, left-paint, right-erase; startup electron-on-wire seed.

## Structural risks

- `CommandQueue` reserves 2,048 entries, grows to a configurable 65,536 default
  ceiling, and tracks rejected/high-water counts; callers must continue to
  surface rejection rather than assuming unbounded growth.
- Per-command token payload pointers must remain valid until submission returns.
- Dense simulation still scans the full grid (O(W×H)); D-P5 single-pass dirty
  AABB + buffer swap and D-P7 optional row-parallel reduce cost but do not
  remove the dense scan. Visual dirty rectangles still gate upload/fade work.
- `Canvas` still combines view + GPU enroll over `CellGrid` (D-C2); further
  CanvasRenderer extraction is optional.
- `IllumoContext` remains a frozen non-owning service bag; adding public service
  fields is a source-contract change for Illumo consumers (D-E5/D-E6).

## Resolved during the 2026-08-06 boundary-consolidation pass

- Injected `IBackend` during host initialization (`GLBackend` constructed
  outside `Renderer`); removed OpenGL includes from `Renderer.h` (D-R11).
- Command-queue overflow logs once per frame and exposes drop counters (D-R12).
- Failed `Start` modules are erased by the host; module `Update` /
  `DispatchDrawables` early-return when core state is missing.
- Wireworld ruleset-aware seed (conductor + head/tail electron) and sticky
  mouse brush for head/tail/conductor/empty.
- Scene header documents its role as a per-frame FrameRenderList (name kept).
- Extracted `CellGrid` domain; rulesets depend only on domain storage (D-C2);
  domain-without-renderer headless tests (`IllumoGame.Domain.*`).
- Removed `UseTokenProof` product frame bypass; `RenderProofQuad` is test-only
  (D-R13).
- Mode splash owned by `CellGameModule` (`modeSplash` unique_ptr), not a
  file-scope global (D-OWN1).

## Resolved during the 2026-08-04 session

- Split the headless suite into 80 separately named, process-isolated CTest
  entries and added an 85% LLVM production-line coverage gate.
- Added explicit Wireworld neighbor truth tables and direct file-backed
  save/load round-trip, corruption, size-overlap, and command tests.
- Covered the game module, InputManager, AssetManager, backend-token conversion,
  SysCmdLine, SplashText, environment, logger, and command registry paths.
- Renamed the live product, targets, tests, runtime title, and saves to Illumo;
  D-N2 later superseded that executable/product identity with `IllumoGame`
  while retaining Illumo for the repository and reusable library.
- Kept `DebugModule` out of Release compilation and composition.
- Added advanced console editing, selection, history, completion, measured caret
  placement, horizontal input scrolling, and improved panel visuals.
- Fixed detailed help truncation by increasing the easy-font UI mesh from 2,000
  to 6,000 quads and adding a regression test.
- Replaced duplicated/hard-coded help with registry metadata and one known-ruleset
  source.
- Implemented validated generic, simulation, canvas, ruleset, file, camera, and
  display commands.
- Connected and hardened save/load; loads now refresh visual targets immediately.
- Fixed fullscreen enter/exit state restoration.
- Added required `commandLine`/registry context validation.
- Prevented shutdown logging from retaining a dangling console pointer.
- Consolidated first-party documentation under `docs/` with one current LaTeX
  entrypoint.

## Behaviors that are intentional

- Wireworld encodes head `0`, empty `1`, tail `2`, conductor `3`.
- Binary rules encode alive as `0` and dead as `1`.
- `CellGrid` owns dense domain state; `Canvas` adds RGB targets/fade and GPU
  enrollment (D-C2).
- Rendering `Scene` is a rebuilt ordered frame list; persistent world nodes
  belong to the separate handle-based `SceneGraph`, which is not an ECS.
- Production drawables use render tokens; immediate `Draw()` remains only for
  tests or incomplete stubs.
