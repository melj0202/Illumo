# Plan: disable C++ exceptions everywhere

Status: PROPOSED. No source has been changed. Tier 2 (Tier 3 for the recorder
and allocation-failure milestones, which change error-handling architecture).
Implementation waits for the decisions in section 10.

## 1. Objective and end state

Owner directive: exceptions MUST be off in the WASM runtime and in the C++
compiler, host and guest. Chosen scope: everything (native host and WASM
guests). Chosen error model: return errors, do not abort.

End state, all measurable:

- Native first-party targets compile without `/EHsc` (MSVC) or with
  `-fno-exceptions` (Clang), and `_HAS_EXCEPTIONS=0` where the MSVC STL is used.
- Guests compile with `-fno-exceptions`; `-fwasm-exceptions`,
  `-mllvm -wasm-use-legacy-eh=false`, `-lunwind` and the `unwind` link entry are
  gone, so the SDK selects its `noeh` libc++.
- `wasmtime_config_wasm_exceptions_set` is `false`; a guest carrying EH opcodes
  is rejected by the engine.
- Zero `try`, `catch`, `throw` in first-party code (a CI grep enforces it).
- No first-party call to a throwing std facility without a non-throwing form.
- All tests that exist today still pass, or are replaced by an equivalent that
  does not need exceptions (section 7).

## 2. Current-state evidence

Source: read-only inventory taken 2026-09-29 (excludes `build*/`, `.claude/`,
`Illumo/thirdparty/`, `archive/`, `docs/`). Counts are sites, not files.

- Engine: `Illumo/Source/Wasm/WasmEngineConfig.h:47` enables wasm exceptions.
  `WasmInstance.cpp` and the C API never catch.
- Guest flags, the only four places: `IllumoGuest/CMakeLists.txt:16,18` and
  `cmake/IllumoWasm.cmake:299,318`.
- Host flags: none explicit. `cmake/IllumoBuild.cmake:123-167` adds no EH flag,
  so MSVC gets CMake's default `/EHsc`.
- Toolchain: WASI SDK 34 ships `lib/wasm32-wasip1/noeh/` and `eh/`.
  `-fno-exceptions` selects `noeh` automatically; `libunwind.a` exists only
  in `eh/`, so the unwind link entries must go.
- `.clang-tidy` disables `bugprone-exception-escape` (line 19); no change needed.

Site groups (production code only):

| Group | Where | Nature |
|---|---|---|
| A. ABI validation throws | `IllumoGuest` headers `Wire.h`, `Frame.h`, `Input.h`, `Protocol.h`, `Display.h`, `Clipboard.h`, `SnapshotWindow.h`; `RecordingBackend.cpp` (~36 throws), `Files.cpp`, `Console.cpp`, `Dialog.cpp`, `FontProvider.cpp`, `Environment.cpp`, `Diagnostics.cpp`, `PanelSurfaces.cpp` | Throw on malformed input; caught by frame-drop and startup handlers in `Program.cpp`, `ModuleApplication.cpp`, `RecordingBackend.cpp:255-270`. The host also compiles these headers (`length_error`, `out_of_range`). |
| B. Boundary catches | `Illumo/Source/Wasm/*` (`WasmProgram`, `WasmGameModule`, `WasmGuest`, `WasmFrameRenderer`, `WasmRenderServices`, `WasmGameServices`, `WasmWorker`, `WasmFileServices`), `RuntimeShell`, `FrameCapture`, `Application`, `Illumo.cpp`, `SceneDirector`, `SimulationLanes`, `SimulationProtocol` | `catch(std::exception)` converts to `fail(what())` / error string. These become plain error returns once group A stops throwing. |
| C. Allocation failure | `SparseCellGrid.cpp` (9 throws, ~22 try blocks), `SceneGraphCompile.cpp`, `SceneQueryIndex.cpp`, `IllumoCodecStreams.cpp:299`, `ChainedStackAlloc.h`, `ArenaAlloc.h`, `PoolAlloc.h`, `DebugAlloc.cpp` (global `operator new`), `SystemInfo.h:18` | `catch(bad_alloc)` degrades: return false, invalidate journal, discard change tracking. AGENTS.md records "allocation failure discards the journal" as an invariant. |
| D. Numeric parsing | `EnvVarsValues`, `CommandLineCore`, `CommandLine`, `EditorScene`, `MeshViewerScene`, `CanvasScene`, `CellGameModule`, `ConfigurationMenu`, `WinWasmCompilerMain` | `stol/stod/stof/stoll/stoul` inside `catch(...)`. |
| E. Filesystem | `WasmFileServices.cpp` (9 non-`error_code` calls), `EnvVars.cpp:41`, `AtomicFile.cpp:67`, `MeshViewerScene.cpp:104` | Throwing overloads. |
| F. JSON | `EnvVarsValues.cpp:109`, `RuleSetRegistry.cpp:2309,2386,2436` (throwing `parse`, unguarded `get<>`/`value`) | Everything else already uses `allow_exceptions=false`. |
| G. Internal control flow | `AtomicFile.cpp` (throw to error string), `WinWasmCompilerMain.cpp` (function-try-block), rethrow-cleanup in `CommandRegistry.h`, `SceneGraph.cpp:142`, `WinWasmCompiler.cpp:180`, `PanelSurfaces.cpp:381`, `ChainedStackAlloc.h:137` | Rollback then rethrow. Becomes RAII guard or explicit error return. |
| H. Swallow-all in `noexcept` / dtors | `Illumo.cpp:496`, `RenderWindow.cpp:38,389`, `GLBackend.cpp:402`, `TitleScene.cpp`, diagnostics shutdown | `catch(...){}` around logging. Delete once logging cannot throw. |
| I. Standalone throws in API | `SceneDirector.h:70`, `InputContext.h:35`, `GuestPlatform.cpp:23`, `EditorApplication.cpp:250`, `ViewerApplication.cpp:93`, `GameApplication.cpp:153`, `WasmFileServices.cpp` ctor (78,82,934,940,948) and `poll()` 1027 | Programmer-error or startup-failure throws. |
| J. `.at()` / `std::get` | `RenderWorld.cpp`, `RecordingBackend.cpp` (15 `.at`), `VisualProxies.cpp`, `Files.cpp`, `SceneDocument.cpp`, `SceneInstance.cpp`, `IlscCodec.cpp`, `PackageManifest.cpp:452`, `GLDevice.cpp:119`, `EditorDocument.cpp`, `EditorSceneCommands.cpp` | Throw on misuse; most are guarded by an earlier check, verify each. |
| K. Thread creation | `WorkerPool.cpp:82` (already handled), `WasmWorker.cpp:201`, `WasmFileServices.cpp:85`, `SimulationRunner.cpp:9`, `AssetManager.cpp:148`, `WasmInstance.cpp:335` | `std::thread` throws `system_error`. |

Tests, harnesses and tools: about 30 more sites, including three throwing
`operator new` replacements used for fault injection
(`TestSceneAllocations.cpp`, `TestGamePackage.cpp`, `TestSimulationWorker.cpp`)
and `CompatibilityGuest.cpp` (a guest that deliberately throws).

Working-tree note: `WasmGameModule.*`, `CellGameModule.*`, `ModuleApplication.*`
are untracked user work in progress. `CanvasScene.cpp` and `CellGameModule.cpp`
contain near-identical try/catch sets; confirm which is built before touching
either. Preserve the user's changes; coordinate edits to those files.

## 3. Scope and non-goals

In scope: first-party C++ under `Illumo`, `IllumoGuest`, `IllumoGame`, `IllEd`,
`IllMeshViewer`, `Templates`; their tests and tools; CMake; engine options;
docs and the decision log.

Non-goals: vendored code under `Illumo/thirdparty/` is not edited (compiled
with its existing flags where it is C, or per-target flags where C++; see
section 6); no new dependency (AGENTS.md); no behavior change beyond what
removing exceptions forces; no Linux port work.

## 4. Constraints and invariants that must hold

- Wire and file formats are unchanged (frame schema v7, `.ilsc`, sparse v4).
- Guest failure stays contained: a malformed guest frame is dropped and logged,
  the host does not die.
- A guest that fails to start fails the launch; a debug overlay that fails is
  dropped (AGENTS.md).
- `SparseCellGrid` allocation-failure behavior is an invariant (see D1).
- Hot-loop allocation gates and frame-allocation tests stay green.
- Compile-time engine options must match between the host and the out-of-process
  compiler; compiled artifacts must be rebuilt.
- Coding rules: no `auto`, namespaces or recursion; Mozilla `clang-format`.

## 5. Proposed design

### 5.1 Result type
Add one small header in `Illumo/Include/Illumo/Foundation/` (verify the right
home against `Illumo/Source/Foundation/AGENTS.md`) with a non-throwing result
carrying an error string, plus parse helpers over `std::from_chars`
(`parseInt64`, `parseUInt64`, `parseDouble`, `parseFloat`). Groups D and the
parse wrappers collapse onto it. Prefer `std::expected` if the pinned toolchains
support it in both the MSVC and WASI libc++ configurations; otherwise a local
minimal type.

### 5.2 Guest recorder and ABI validators (group A)
Keep the existing semantics (validate, drop the frame, log once). Replace
`throw` with a sticky error: validators and writers return `bool` or set
`m_error` and return early, and `Program` / `ModuleApplication` check the flag
at the same points they catch today. `RecordingBackend` already has `m_error`.
`.at()` becomes a checked lookup returning an error. Wire readers return a
status. Startup contract "throw to fail startup" becomes an init that returns
`bool` plus an error string (`Program.h:95`, `ModuleApplication.h:93`). This is
the largest milestone.

### 5.3 Allocation failure (group C)
Exceptions cannot carry `bad_alloc` any more. Two options are presented as
decision D1: (a) allocation failure aborts everywhere, deleting the degrade
paths; (b) keep the recoverable paths by using non-throwing allocation
(`std::nothrow`, `try_*` growth via pre-reserve with a checked
`reserve` estimate) only where a documented recovery exists
(`SparseCellGrid`, `SceneGraphCompile`, `SceneQueryIndex`, codec streams).
With `-fno-exceptions`, standard containers still abort on OOM, so (b) needs
explicit non-throwing storage in those files.

### 5.4 Standard library and JSON
- Numeric parsing to `from_chars` (5.1).
- Filesystem calls get `std::error_code` overloads.
- JSON: define `JSON_NOEXCEPTION` for the Content layer and guests (the header
  already aborts instead of throwing when EH is off). Convert
  `EnvVarsValues.cpp` and `RuleSetRegistry.cpp` to
  `parse(text, nullptr, false)` plus `is_discarded()`, and replace unguarded
  `get<>` / `value()` in `RuleSetRegistry.cpp` (lines ~699, 986, 1481-1732)
  with type-checked reads. Verify the guarded reads in `IlscCodec.cpp`
  (lines 244, 277, 354, 546, 669-682) before relying on them.
- `std::thread` creation: check `joinable`/failure is not observable without
  exceptions; document that thread creation failure aborts, or use a platform
  create call that returns an error (decision D5).
- `std::get(variant)`: switch to `std::get_if` or a checked visit.

### 5.5 Cleanup-then-rethrow (group G)
Replace with RAII scope guards or explicit error returns.

### 5.6 Build and engine
- `cmake/IllumoBuild.cmake`: in `illumo_configure_cpp_target`, on MSVC remove
  `/EHsc` for first-party targets and add `/EHs-c- /D_HAS_EXCEPTIONS=0`; add
  `/we4530` so any leftover handler is an error (MSVC only warns by default).
  On Clang add `-fno-exceptions`. Scope to first-party targets so vendored C++
  keeps its flags; libraries linked into first-party binaries must not require
  exceptions (verify Tracy, nlohmann, tinyobjloader, freetype, GLFW,
  miniaudio at link time).
- Guest flags: replace the four `-fwasm-exceptions` sites with
  `-fno-exceptions`; delete `-mllvm -wasm-use-legacy-eh=false`, `-lunwind`,
  and the `unwind` link entry.
- Engine: `wasmtime_config_wasm_exceptions_set(config, false)`. Flip only after
  every guest module is rebuilt without EH; the engine option mask does not
  carry this bit, so the compiled artifacts differ and stale artifacts must be
  regenerated. Consider adding it to `WasmEngineOptions` if a mixed period is
  needed.
- Enforcement: a CTest or script greps first-party sources for
  `\b(try|catch|throw)\b` outside comments and fails on any hit.

## 6. Alternatives considered

- Guests only: rejected by the owner (scope "everything").
- Abort on failure everywhere: smaller diff, rejected by the owner
  ("return errors"); revisited only for allocation failure (D1).
- Leave wasm exceptions enabled in the engine: harmless to the sandbox, but the
  owner wants it off, and it keeps a surface no guest uses.

## 7. Tests

Tests that depend on throwing must be redesigned, not deleted:

- Throwing `operator new` fault injection (three files): replace with a
  failure-injecting allocator hook that returns null to the nothrow paths
  chosen in D1, or drop the assertion if D1 is (a).
- `CompatibilityGuest.cpp` (throw/catch in a guest): replace with a guest that
  exercises the traps and error returns that remain.
- Tests that expect `length_error` / `out_of_range` / `bad_array_new_length`
  (`TestWasmWindows`, `TestWasmRuntime`, `TestWasmFrame`, `TestAllocators`,
  `TestRuntimeUtilities`): assert the returned error instead.
- Harness `try/catch` in each `TestMain.cpp`: remove; a failing case reports
  through the existing `require` path.
- `TestServices.cpp:559`: the `CommandRegistry` callback-throws contract goes
  away; document it.
- Tests and tools that use `std::filesystem` without `error_code`
  (`IllMeshViewer`, `CloseWindowTests.cpp`): switch to `error_code`.

## 8. Milestones

M0. Baseline. Record Release and Debug pass state, `PackageBench` and
    `PackageFrameAllocations` numbers, exe size, and the frame-time zones
    (Tracy). Confirm host compiler (MSVC or clang-cl), which files
    `CanvasScene.cpp` vs `CellGameModule.cpp` build, and standard-library
    `std::expected` support in both toolchains.
M1. Foundation: result type and `from_chars` helpers, with unit tests.
M2. Mechanical: numeric parsing (D), filesystem `error_code` (E), `.at` and
    `std::get` audit (J), `noexcept` cleanup (H).
M3. JSON (F): `EnvVarsValues`, `RuleSetRegistry`, `JSON_NOEXCEPTION`, tests
    for malformed rule catalogs.
M4. Engine, services, scene, rendering, platform (B, G, I in Illumo core,
    `AtomicFile`, `WinWasmCompiler*`).
M5. Wasm host (`Illumo/Source/Wasm/*`) and file services: constructors return
    status, workers report errors.
M6. Guest SDK (`IllumoGuest`): wire, frame, recorder, startup contract (A).
    Largest and riskiest; land per file with the tests green.
M7. IllumoGame, IllEd, IllMeshViewer: parse wrappers, `SparseCellGrid` (C),
    platform seams, `Get*Platform` throws.
M8. Tests, harnesses, tools (section 7).
M9. Flip flags: host, guests, `wasmtime_config_wasm_exceptions_set(false)`;
    rebuild every guest and every compiled artifact; add the grep gate and
    `/we4530`.
M10. Docs: `docs/architecture-consensus.md`, matching LaTeX chapter, new entry
    in `docs/latex/sections/09-design-decision-log.tex`, `docs/contributing.md`
    (rule: no exceptions), nested `AGENTS.md` files that describe exception
    contracts, `README.md` if it lists flags.

Each milestone: affected targets build, exact tests pass, `clang-format`,
clang-tidy on touched files.

## 9. Verification and containment

- Full Release build and `ctest -L IllumoWorkspace` after M9; all package-level
  tests (`IllumoGame.Wasm.*`, `IllEd.Wasm.*`, `IllMeshViewer.Wasm.*`).
- Debug (ASan) build and `IllumoTidy`.
- Perf: rerun M0 benches. Expect equal or slightly better code size and speed;
  report any regression.
- Grep gate zero hits; `dumpbin`/`wasm-objdump` spot check that no EH tables or
  EH opcodes remain.
- Manual smoke: launch each app, malformed-input cases (bad rule catalog, bad
  package, corrupt save, oversized guest frame) show an error and continue.
- Containment: land on a branch off `release/v26.09`; milestones M1-M8 keep
  exceptions enabled (both styles coexist), so M9 is the only flag flip and can
  be reverted alone. Nothing lands on the working tree's untracked WIP files
  without the owner's say-so.

## 10a. Owner decisions (2026-09-29)

- D1: first (b); revised to (a) after implementation showed standard
  containers cannot report allocation failure without exceptions. Allocation
  failure is fatal everywhere. The degrade paths in `SparseCellGrid`,
  `SceneGraphCompile`, `SceneQueryIndex` and the codec streams, their
  fault-injection hooks and tests are removed. Size-overflow guards become
  `illumoFatal`. Explicit input-size limits stay ordinary errors.
- D2: global `operator new` aborts with a message (`illumoFatal`).
- D3: guest recovery is the sticky error: drop the frame, log once.
- D4: Wasmtime exceptions go off in the same milestone as the guests.
- D5: thread creation failure is a critical (fatal) failure.
- D6: the untracked WIP files (`WasmGameModule`, `CellGameModule`,
  `ModuleApplication`) were deleted (backed up outside the repository).

## 10. Open decisions (need the owner)

D1. Allocation failure policy: (a) abort everywhere, or (b) keep the
    documented degrade paths using non-throwing allocation in `SparseCellGrid`,
    `SceneGraphCompile`, `SceneQueryIndex`, codec streams, and abort elsewhere.
    Recommendation: (b), because AGENTS.md records the journal-discard on
    allocation failure as an invariant and the tests exercise it.
D2. Global `operator new` in `DebugAlloc.cpp`: on failure, abort with a log
    (the only behavior possible without exceptions). Confirm.
D3. Guest failure recovery: confirm the sticky-error design in 5.2 (drop the
    frame, log once) keeps the current behavior the owner wants.
D4. WASM engine: turn wasm exceptions off in the same milestone as the guests
    (M9), or add a bit to `WasmEngineOptions` for a transition period.
    Recommendation: same milestone, no transition bit.
D5. Thread creation failure: abort, or use platform APIs that return errors in
    `WasmWorker`, `WasmFileServices`, `SimulationRunner`, `AssetManager`.
    Recommendation: abort, matching D2.
D6. The untracked WIP files (`WasmGameModule`, `CellGameModule`,
    `ModuleApplication`): edit in place now, or wait until the owner commits.

## 11. Validation results

(none yet)

## 12. Completion record

(none yet)
