# Illumo repository guidance

This file is the durable project-wide operating contract for work in Illumo.
Read the closest nested `AGENTS.md` before changing a subsystem. Use the live
tree and canonical documentation for implementation detail; do not treat this
file as an architecture catalog.

## Project identity and boundaries

This C++23 workspace contains the product-agnostic `Illumo` static library and
the in-tree applications that consume it. `IllumoGame` is the
cellular-automata learning sandbox; it ships only as the isolated
`IllumoGame.wasm` package played by the generic `IllumoRuntime` host (Windows
x64). `IllEd` is the SceneGraph world editor
used to bootstrap later Illumo applications (editor versus runtime, in the
same sense as Unreal Editor versus Unreal). Every interactive client program
is a WASM package in `apps/<name>/` beside that one host: `game`, `illed`
(`IllEd.wasm`) and `meshviewer` (`IllMeshViewer.wasm`); `IllEdCore` and
`IllMeshViewerCore` remain only as native test-oracle libraries. The supported product path is
Windows, GLFW, OpenGL, and — for the simulator — the sparse infinite or
finite-toroidal canvas. Illumo remains a reusable runtime and rendering
foundation; application policy stays in the consuming product. The optional
`Illumo::Content` layer owns the one scene format every program reads
(`.ilsc` format 2, instantiated by `SceneInstance`), `.ilpk` packages, and
the host virtual file tree on which apps, content packages and mods mount
(`docs/content-packages-and-scenes-design.md`).

The approved `SceneGraph` v2 uses graph-local handles, intrusive hierarchy,
TRS state, a compiled preorder, revision-cached bounds, ordered borrowed
attachments, identity/payload metadata, and handle-based queries. Its derived
BVH accelerates queries; `SceneGraphDrawable` consumes graph-owned snapshots.
Keep this boundary separate from ECS components, retained UI, serialization,
update callbacks, render graphs, and backend execution. Scene mutation,
extraction, and attachment emission remain main-thread affine. Parallel scene
stages and culling tiers remain measurement-gated; moving the OpenGL context
or recording attachment payloads requires a separate accepted design.

## Sources of truth

Inspect the applicable implementation, tests, CMake, and documentation before
editing. Route detail to these canonical sources:

- repository use and exact common commands: `README.md`;
- current architecture and decision catalog: `docs/architecture-consensus.md`;
- intended charter direction and current boundary: `docs/charter-direction.md`;
- capture API and runtime `--capture` mode: `docs/frame-capture.md`;
- WASM runtime design, ABI and cutover: `docs/wasm-game-runtime-design.md`,
  `docs/wasm-game-runtime-plan.md`, `docs/wasm-game-cutover-plan.md`, and
  `docs/wasm-apps-cutover-plan.md`;
- persistent scene hierarchy contract: `docs/scene-graph-v2-design.md`;
- scene implementation and verification record: `docs/scene-graph-v2-plan.md`;
- long-form design book and chart-only map: `docs/latex/illumo.tex` and
  `docs/latex/architecture-map.tex`;
- formal decisions: `docs/latex/sections/09-design-decision-log.tex`;
- package ownership maps: `docs/packages/`;
- coding and dependency policy: `docs/contributing.md`;
- large-work planning: `.agent/PLANS.md`;
- repository-scoped Codex skills: `.agents/skills/`;
- migration scaffolds, retained verbatim for reuse:
  `.agent/reference/PROJECT_AGENTS_TEMPLATE.md` and
  `.agent/reference/MIGRATION_GUIDE.md`.

Before substantive work, read `docs/output/illumo.pdf` when its content is not
already available. When a task authorizes documentation writes and the PDF is
absent or older than its LaTeX sources, rebuild it with `docs/build.ps1` first.
A read-only task does not authorize regenerating it.

When code, tests, CMake, instructions, and documentation disagree, identify
the conflict. The live implementation is current-state evidence, but do not
silently bless unintended behavior; correct stale documentation in the same
authorized change or report the mismatch.

## First steps and scope discipline

1. Run `git status --short` and preserve every existing user change.
2. Read `README.md`, `docs/architecture-consensus.md`, this file, and the
   closest nested guidance.
3. Search narrowly and inspect call sites, tests, configuration, ownership,
   and error paths relevant to the request.
4. Exclude generated, vendored, and historical trees unless the task concerns
   them: `build*/`, `Illumo/thirdparty/`, `archive/`, LaTeX auxiliaries,
   source dumps, ZIPs, and prior-agent records.
5. Keep the requested boundary. Review and diagnosis do not authorize fixes;
   preserve production code when the task is documentation-only.

Use `.agent/PLANS.md` for medium, subsystem-scale, high-risk, migration, or
architectural work. Keep design authority, writes, and integration with the
lead agent; read-only subagents may supply bounded evidence.

```text
Drawable::AppendCommands(Renderer*)
  -> RenderCommand tagged-union tokens
  -> vector CommandQueue (2,048 reserve; configurable 65,536 default ceiling)
  -> IBackend::SubmitCommandQueue
  -> GLBackend/GLDevice or MockBackend
```

`SceneGraph` owns persistent nodes, compiled transforms/bounds, and two reusable
snapshot buffers. `SceneGraphDrawable` enters the existing per-frame `Scene`
as one drawable and uses one immutable view for collection, depth, and color.
Attachments remain borrowed; detach or invalidate snapshots before retiring
or changing their content. Already emitted token payloads must outlive
synchronous submission. Unknown bounds fail open. Renderer fits shadows after
caster collection, so shadow relevance is tested from snapshot values in the
depth pass. CA storage and UI remain separate from the scene subsystem.

Production drawables use the token path. Immediate `Draw()` is only the fallback
for tests or incomplete stubs. Game and rules code must not issue raw OpenGL
draw calls. Resource enrollment is rare; per-frame work emits bind, update,
uniform, state, and draw tokens. Token payload pointers must remain valid until
submission returns.

Backend resources use non-convertible slot+generation `MeshHandle`,
`ShaderHandle`, and `TextureHandle` values; renderer styles use the same model.
Backends allocate handles and validate replacement, destruction, queries, and
command submission. `AssetManager` caches file textures, shaders, and immutable
static meshes by canonical path+options. Texture/shader file work uses one CPU
decode worker and performs GPU replacement only from render-thread `pump`;
mesh decode and enrollment are synchronous and main-thread affine. Debug builds
poll texture/shader timestamps every 500 ms; explicit reload remains available
for those resource types in all builds. `MeshVisual` borrows managed mesh
handles while the acquiring owner retains their `AssetManager` references;
procedural dynamic geometry remains visual-owned. The Debug renderer demo
acquires both its atlas and contract-compatible sprite shader through this
managed path.

`GameVisual` is the reusable painter-correct 2D host. One stable ordered stream
spans shapes, sprites, and text; only adjacent compatible items batch. Parent and
local `Transform2D`, normalized pivots, atlas regions/flips, and bounded dynamic
quad buffers are supported. `SpriteAnimator` is passive and caller-updated.

Product UI is primitive-composed rather than a separate widget system.
`CommandLine` and the Release-visible `ConfigurationMenu` build their panels
from `GameVisual` fills, outlines, lines, and text; `GLString` may add cached
panel chrome; FPS and `SplashText` use that decorated-label path. `UiTheme` is
shared value-only styling, and `Illumo/Gui` is the single home for reusable UI
behavior: `GuiKit` drawing helpers, `GuiDialog` modals, and `GuiMenuShell`
overlay easing, animation clocks, virtual-space fitting, row windows, and
pointer edges. Tool apps (IllEd, IllMeshViewer) use the plain `GuiToolStyle`
look and a `GuiPanelDock` of detachable panels whose content draws into a
`GuiPanelPlacement` and reads `GuiPanelPointer` (D-UI7); extra windows come
from `IllumoContext::panelSurfaces` and products must work fully docked
without it. A new screen composes those rather than restating them.
Preserve the existing drawable owners and Scene layers; do not introduce a
retained UI tree for this surface.

Canvas truth (verify here before trusting older notes):

- Production domain: `SparseCellGrid` owns signed 64-bit world cells in a
  hash map of non-background 16x16 chunks with separate stored-cell occupancy
  and neighbor-counting masks. Sparse chunks accumulate non-background cells
  and neighbors of counted cells in retained
  contiguous per-chunk scratch records with fixed candidate masks and neighbor
  counts. Candidate target discovery tests edge and corner bits directly. The
  serial builder remains source-centric, while large or explicitly parallel
  preparation assigns independent target records to the reusable worker pool;
  neighbor counters are initialized only when their candidate bit is first set.
  A retained generation-stamped flat index maps chunk addresses directly to
  scratch records without sorting or binary searches. Next-generation output
  uses a retained inactive chunk map and recycled `unordered_map` node handles,
  avoiding steady-state chunk-node allocation. At 16,384 or more candidate
  cells, candidate evaluation uses the reusable worker pool with coarse ranges
  of roughly 2,048 candidate cells and up to four automatic workers; small
  candidate sets remain direct and serial. Current and inactive chunk maps
  maintain transactional aggregate stored-cell, counted-cell, and
  candidate-preferred-chunk totals, so path selection and settled statistics do
  not rescan all allocated chunks. A retained changed-chunk frontier
  retains up to 16,384 changed addresses and expands them by one chunk; the
  work comparison still selects frontier versus complete evaluation, and only
  tracking overflow or allocation failure discards the journal. Exact
  local candidate preparation/evaluation work is compared with a cached-total
  complete-path estimate; cheaper frontiers patch the prior map, while broad
  changes fall back. Sparse frontier targets use candidate masks or halos by
  counted-neighbor work, and settled worlds evaluate no chunks. Complete mixed
  worlds select candidate
  or deterministic 18x18 halo evaluation independently per target from actual
  counted-neighbor contribution work; Wireworld conductors are stored but only
  heads contribute. All-dense counted chunks bypass scratch construction. At
  32 or more halo targets, a grid-owned reusable pool uses up to eight workers.
  Complete-halo targets use a retained generation-stamped flat index and
  retained result storage instead of a local hash set, sorting, and disposable
  vectors. All evaluators index a per-ruleset 256x9 transition table; dense
  halo targets build rolling rows directly from chunk counting masks without
  materializing an 18x18 byte halo. A persistent `SimulationRunner` advances a
  spare sparse grid while the main thread reads the published one. Completed
  generations publish only at frame boundaries with `SparseGenerationDelta`;
  journals of at least 2,048 presentation chunks carry a lightweight
  replacement marker instead of per-chunk payloads. The former display grid
  consumes the delta before reuse. Only one generation
  may be outstanding, overdue whole steps are dropped, and state mutations,
  persistence, ruleset changes, manual stepping, and shutdown drain first.
  In the package, generations costing more than 4 ms run on up to eight
  isolated simulation lanes (`CSimWorkerGuest.wasm` stores owning interleaved
  eight-row chunk bands with one-row halos; D-E17) and the control store
  merges one exact delta per generation; there a drain retires the outstanding
  generation instead of waiting for it.
  Status derives achieved TPS from published completions and reports rolling
  generation latency. The default `0 x 0` topology is infinite and
  non-toroidal; positive chunk width and height select a finite torus with
  canonical wrapped cells. Mixed zero/positive axes are invalid. The grid's
  revision changes only when cell contents actually change.
- Production presentation: `CanvasView` separates visible diagnostics from a
  globally aligned cache padded by two 16-cell chunks on every side. Camera
  motion inside it changes only the MVP. Aligned origin shifts copy retained
  CPU texels and resample newly exposed strips. Near zoom is one exact nearest-filtered
  texel per cell; far zoom uses integer density LOD with immediate coarsening
  and 80% refinement hysteresis. One-revision changes map changed chunks to
  deduplicated exact or overview bins; revision gaps, non-aligned jumps, LOD/resize,
  palette changes, torus wrap, and replacement refill the bounded cache. Dirty 16x16-texel
  tiles merge into at most eight rectangles or their AABB. Uploads through
  64 KiB are direct; larger requests use a non-waiting three-PBO/fence ring and
  direct fallback. Re-enrollment and destruction delete GL textures, buffers,
  and fences. Exact-cell LOD fades changing colors; density overviews snap.
  Dense visible revisions that dirty a quarter of the cache resample the
  complete cache. Changed-bin marking stops once that threshold is reached.
  Overview resamples walk occupied cells and snap-convert sampled RGB in one
  pass. A retained active-texel set makes remaining fade/snap work
  proportional to changing colors. It owns one reusable RGB texture plus one
  world-space quad.
  Finite worlds draw only their centered canonical rectangle; presentation and
  editor-facing bounds stay blank outside it even though simulation neighbors
  wrap across opposite edges.
  `RenderWindow` defaults to monitor-synchronized swapping; persisted `vsync=0`
  remains the explicit uncapped profiling mode. Debug FPS output separates
  paced swap completions from CPU submissions.
- Persistence always writes sparse version 4 with family/ruleset identity and
  topology, and reads versions 4, 3, and 2 plus the prior dense format.
  Version 2 and legacy dense files select infinite topology.
  Legacy `CellGrid`/`Canvas` remain compatibility fixtures,
  not a second production runtime path.

Ruleset truth:

- Active catalogs include Life-like, Generations, Wireworld, elementary 1D,
  cyclic/CCA (including long-range Griffeath rules), colorized Life,
  Larger-than-Life (with decay trails), Hodgepodge chemistry, directional
  Turmites, HPP lattice gas, five-species dominance, Golly-table
  self-replicating loops, and Abelian sandpile rules.
- Binary rules encode `0` as alive and `1` as dead.
- Wireworld encodes head `0`, empty `1`, tail `2`, conductor `3`.
- Rule 90 and Rule 184 are elementary 1D space-time diagrams: source row is the
  maximum counted Y, destination is Y+1, older rows stay history.
- Count-based `RuleSet` transitions (`nextState`) build a cached 256x9 table;
  full-state histograms, extended counts, and north/east/south/west directional
  neighborhoods use isolated contracts. Palette
  evaluation (`evalCell`) supplies colors. Production hot loops index the table
  instead of making virtual transition calls and use separate stored/counting masks,
  retained chunk-local candidate scratch, and a generation-stamped flat address
  index. Mixed targets independently select candidate or halo evaluation, with
  bounded worker-pool evaluation for large work sets. Counting-dense centers
  skip neighbor-count scratch preparation and evaluate as halo.
  Dense-majority and large frontiers skip candidate scratch. Both
  result paths share retained transactional chunk-map node storage; a local
  changed-region path patches the retained prior generation. Dense
  `calcGeneration` support remains only for compatibility tests.
  Headless benches: `IllumoGame.Sim.MicroBench`,
  `IllumoGame.Sim.SparseMicroBench`.

## Source map

| Concern | Primary files |
|---|---|
| Application runner and main loop | `Illumo/Source/Engine/Application.cpp` |
| Public library API | `Illumo/Include/Illumo/*` |
| Host, services, modules | `Illumo/Source/Engine/Illumo.cpp`, public Engine headers |
| Persistent scene hierarchy | `Illumo/Include/Illumo/Scene/*`, `Illumo/Source/Scene/*` |
| CA modes and editor | `IllumoGame/Source/Game/CellGameModule.*`, `CellContext.h`, `CellPattern.*`, `PatternCodec.*`, `BuiltinPatterns.*` |
| World editor | `IllEd/Source/EditorModule*.cpp`, `EditorDocument.*`, `EditorHistory.*`, `EditorSelection.*`, `EditorShortcuts.*`, `EditorGizmo.*`, `EditorInspector.*`, `EditorClipboard.*`, `EditorAssetBrowser.*`, `EditorToolbar.*` |
| Scene format, packages, virtual file tree | `Illumo/Include/Illumo/Content/*`, `Illumo/Source/Content/*`, `Illumo/tools/IllumoPack.cpp` |
| OS clipboard text | `Illumo/Include/Illumo/Platform/Clipboard.h`, platform `*Clipboard.cpp` |
| Domain cell storage | `IllumoGame/Source/Game/SparseCellGrid.*` |
| Bounded view, fade, dirty upload | `IllumoGame/Source/Game/CanvasView.*`, `Illumo/Shader/canvas_*` |
| Compatibility dense storage | `IllumoGame/Source/Game/CellGrid.*`, `Canvas.*` |
| CA behavior | `IllumoGame/Source/Rulesets/*` (`nextState`/palette) |
| Renderer and tokens | `Illumo/Include/Illumo/Rendering/*`, `Illumo/Source/Rendering/*` |
| Resource handles and file assets | `Illumo/Include/Illumo/Rendering/ResourceHandle*`, `Illumo/Source/Rendering/AssetManager.cpp` |
| World meshes, overlay primitives, animation | `Illumo/Source/Rendering/Primitives/*` (`MeshVisual`, `GameVisual`) |
| GUI subsystem, dialogs, and atlas helpers | `Illumo/Include/Illumo/Gui/*`, `Illumo/Source/Gui/*` (`GuiKit`, `GuiDialog`, `GridAtlas`, `GuiToolStyle`, `GuiPanelDock`, `GuiPanelPointer`, `PanelSurfaces.h`) |
| Detached panel windows (host) | `Illumo/Source/Wasm/WasmPanelWindows.*`, `Illumo/Include/Illumo/Platform/SurfaceWindow.h`, `IllumoGuest/Include/IllumoGuest/{Windows,PanelSurfaces}.h` |
| Debug renderer atlas and shader | `Illumo/Assets/RendererDemo/*` |
| Production backend factory | `Illumo/Source/Rendering/OpenGL/CreateOpenGLBackend.*` (composed in `Engine/Illumo.cpp`) |
| Real graphics execution | `Illumo/Source/Rendering/OpenGL/*` |
| Headless backend | `Illumo/TestSupport/Include/Illumo/Testing/MockBackend.h` |
| Input, console, env, logging, system CLI | `Illumo/Source/Services/*` |
| OS entry and native save/load | `Illumo/Source/Platform/*` |
| WASM host, ABI decoders, runtime | `Illumo/Source/Wasm/*`, `Illumo/Include/Illumo/Wasm/*`, `cmake/IllumoWasm.cmake` |
| Guest SDK, guest-side engine, wire headers | `IllumoGuest/Include/IllumoGuest/*`, `IllumoGuest/Source/*` |
| IllumoGame package entry and platform adapter | `IllumoGame/Source/Wasm/*`, `IllumoGame/illumo.json` |
| IllEd package entry and platform seam | `IllEd/Source/Wasm/EditorApplication.cpp`, `IllEd/Source/IllEdPlatform.h`, `IllEd/illumo.json` |
| IllMeshViewer package entry and platform seam | `IllMeshViewer/Source/Wasm/ViewerApplication.cpp`, `IllMeshViewer/Source/MeshViewerPlatform.h`, `IllMeshViewer/illumo.json` |
| Runtime command line and capture mode | `Illumo/Source/Wasm/RuntimeApplication.cpp` |
| Tests | `Illumo/Tests/*`, `IllumoGame/Tests/*`, `IllEd/Tests/*`, `IllMeshViewer/Tests/*` |
| Canonical architecture | `docs/architecture-consensus.md` |
| Formal decisions | `docs/latex/sections/09-design-decision-log.tex` |

## Build and verification commands

Run from the repository root on Windows. The default Windows configure
builds `IllumoRuntime` and stages every app package (`apps/game`,
`apps/illed`, `apps/meshviewer`), which requires the pinned toolchain from
`tools/bootstrap-wasm.ps1` (or `python build.py wasm-tools`) in
`build-wasm-tools/` (`-DILLUMO_BUILD_WASM_RUNTIME=OFF` builds without them;
there are then no runnable applications).

```powershell
.\tools\bootstrap-wasm.ps1
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure
```

`build.py` passes `ILLUMO_BUILD_WASM_RUNTIME` explicitly (`--no-wasm` to
skip), because `option()` keeps a stale cached `OFF`. `python build.py
doctor` reports the toolchain, an `apps` check, and stale native pre-WASM
outputs (`IllumoGame.exe`, `IllEd.exe`, `IllMeshViewer.exe`,
`IllumoCapture.exe`, an old `game/` folder) to delete. `python build.py play
--app game|illed|meshviewer [-- runtime args]` builds the runtime and runs
one app (`--no-build` skips building). `python build.py test` builds every
executable CTest runs; `tools/test_build.py` covers the orchestrator.
Debug is the AddressSanitizer profile (`ILLUMO_ENABLE_ASAN`, default ON),
whose WASM guests also use explicit bounds checks; play and measure with the
`dev` profile (RelWithDebInfo) or `debug-noasan`, not Debug.

Package-level checks drive each real package through the generic host:
`IllumoGame.Wasm.GamePackage` (menu, setup, edit, step, save/load, 3D mode),
`IllumoGame.Wasm.GamePackageLanes` (real simulation lanes against a native
serial reference), `IllEd.Wasm.Package` (launch scene, preloaded atlas,
keyboard pan, Ctrl+S save in place), `IllEd.Wasm.ProjectPackage` (project
assets placed through the guest cache and saved into `/project`),
`IllMeshViewer.Wasm.Package` (launch mesh as a retained host mesh, skybox
cubemap), `IllMeshViewer.Wasm.ScenePackage` (a mounted package's scene opened
with `viewer_open`) and `IllumoGame.Wasm.CatalogMerge` (package rule
catalogs). `IllumoGame.Wasm.LaneParity`
and `IllumoGame.Wasm.LaneProtocol` cover lane exactness and the CSL1
decoders. The `Illumo.Wasm.*` cases cover the sandbox, engine modes, the
manifest decoder, ABI decoders and host services; `Illumo.Runtime.Help`,
`Illumo.Runtime.InvalidCaptureFrame`, `Illumo.Runtime.InvalidBenchFrames` and
`Illumo.Runtime.CaptureScriptNeedsCapture` cover the runtime command line.
`--capture-script` drives a capture to a later screen (menus, overlays) for
visual checks. Real-GPU capture is checked with `python
tools/verify_capture.py build-workspace/Release/IllumoRuntime.exe
--output-dir <dir>`. Performance is measured with `--bench-frames`,
`IllumoGame.Sim.RunnerBench` and `IllumoGame.Wasm.PackageBench` (label
`IllumoBenchmark`; see `docs/frame-capture.md`).

Focused test work:

```powershell
cmake --build build --config Release --target IllumoTests IllumoGameTests IllEdTests IllMeshViewerTests
ctest --test-dir build -C Release -N -L IllumoWorkspace
build/Release/IllumoTests.exe --list
build/Release/IllumoGameTests.exe --list
build/Release/IllEdTests.exe --list
build/Release/IllMeshViewerTests.exe --list
build/Release/IllumoTests.exe --run <Illumo.exact-name>
build/Release/IllumoGameTests.exe --run <IllumoGame.exact-name>
build/Release/IllEdTests.exe --run <IllEd.exact-name>
build/Release/IllMeshViewerTests.exe --run <IllMeshViewer.exact-name>
```

Clang/LLVM coverage:

Headless tests cover typed/generational MockBackend resources,
Renderer/token/style/asset flow, retained scene hierarchy, painter-correct
primitives and animation, rulesets,
CellContext, CellGameModule commands and file-backed save/load, Canvas
domain/fade/dirty behavior, input, environment/logging, SysCmdLine, and
CommandLine/GLString/SplashText tokens. `ILLUMO_ENABLE_COVERAGE=ON` adds the
Clang/LLVM `IllumoCoverage` target, an 85% production-line gate, and an HTML
report. They do not prove that the live OpenGL window, native dialogs, or
non-Windows ports work. Run a proportional manual smoke test for those paths.

```powershell
cmake -S . -B build-coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DILLUMO_BUILD_DOCUMENTATION=OFF -DILLUMO_ENABLE_COVERAGE=ON
cmake --build build-coverage --target IllumoCoverage
```

Documentation, formatting, and clang-tidy:

```powershell
./docs/build.ps1
clang-format -i <modified-cpp-or-header-files>
```

`ILLUMO_ENABLE_CLANG_TIDY` defaults to ON and attaches LLVM `clang-tidy` to
first-party C++ compilation. `.clang-tidy` treats diagnostics as errors.
Vendored, generated, and third-party sources are excluded. Disable with
`-DILLUMO_ENABLE_CLANG_TIDY=OFF` or `python build.py build --no-tidy`.
Workspace `IllumoTidy` is the batch Ninja/Clang compile-database run.

```powershell
cmake -S . -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DILLUMO_BUILD_DOCUMENTATION=OFF -DILLUMO_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy --target IllumoTidy
```

`python build.py tidy` configures the same tree under `build-workspace-tidy`.

The default workspace build compiles both runners, runs every granular
`Illumo.*` and `IllumoGame.*` case through `IllumoRunTests`, and builds
`IllumoDocs` when PowerShell and
`latexmk` are available. Headless tests do not prove the live OpenGL window,
native dialogs, or non-Windows ports. Use Debug or Release GUI smoke tests and
sanitizers when the affected risk requires them. When static analysis is
requested beyond `IllumoTidy`, report the extra checks and translation units.

## Project-wide invariants

- Illumo owns process entry, platform services, BuildInfo, SysCmdLine, logging
  lifetime, DebugModule composition, and the frame loop. IllEd supplies
  editor defaults/CLI metadata and its required editor-module factory.
  Illumo must not depend on Game, Rulesets, or IllEd.
- Content layering: core `Illumo` never includes `<Illumo/Content/...>`;
  `Illumo::Content` depends only on `Illumo::Illumo` (never on
  `Illumo/Source/Wasm` or a product) and keeps nlohmann private. It is the only
  owner of `.ilsc`, `illumo.json` and `.ilpk` code; guests link its mirror
  `IllumoGuestContent`. `SceneGraph` still never serializes itself. Engine
  tools reach the file tree only through `IFileTreeSource`.
- Guests address files by virtual path (`/app`, `/engine`, `/packages/<id>`,
  `/project`, guest-local `/local`) or opaque dialog grants; host paths never
  cross the ABI. Package mounts are read-only; only `--project` is writable,
  and it refuses `*.wasm` and `illumo.json`. Package manifests are
  `illumo.json`. Scene asset references are package-relative or explicit
  absolute virtual paths.
- IllumoGame executes only inside `IllumoGame.wasm`; IllEd and IllMeshViewer
  likewise run only as `IllEd.wasm` and `IllMeshViewer.wasm`, reaching files
  and dialogs through their `IllEdPlatform` / `MeshViewerPlatform` seams.
  `IllumoRuntime` and `Illumo/Source/Wasm` stay product-agnostic: no Game,
  Rulesets, IllEd or IllMeshViewer includes, no product-specific imports or
  opcodes, no native product fallback. The guest composes
  its own `IllumoContext` (`GuestModuleApplication`); nothing about it crosses
  the ABI, which carries only copied envelopes, services and frames. Package
  manifests request budgets; the runtime grants at most its ceilings, and
  launch options are never persisted. Frame schema changes, new capabilities
  or new imports require decoder/deny tests with the change.
- `IllumoContext` is a non-owning pointer bag frozen after engine startup;
  the one exception is `fileTree`, which the module owning a file tree (the
  WASM host) publishes during Start and withdraws on Exit.
  Failed optional modules remain inactive; a failed required module rolls back
  every accepted module and fails startup. Each product supplies one required
  module; `DebugModule` is optional.
- Production consumers include only `<Illumo/...>` headers. Test-only support
  is exposed separately by `Illumo::TestSupport`.
- Runtime window, input, module, rendering, and OpenGL work is main-thread
  affine unless a documented subsystem contract explicitly provides workers.
- Game and Rulesets do not issue raw OpenGL calls or depend on OpenGL types.
- Production drawables append `RenderCommand` tokens to the backend-neutral
  `Renderer`; `IBackend` executes them. Any pointer carried by a command must
  remain valid until synchronous queue submission returns.
- Directional shadows use one Renderer-owned depth pass over camera-relevant
  World casters before color rendering. Direct `MeshVisual` drawables and
  SceneGraph attachments contribute bounds and depth tokens to the same fitted
  light-space matrix and shared map; invalid camera reconstruction falls back
  to all casters, and per-object shadow framebuffers are forbidden.
- `Rendering::Scene` is a non-owning list rebuilt each frame. `SceneGraph`
  separately owns persistent SoA state and derived preorder/bounds/query caches.
  `SceneGraphDrawable` emits ordered snapshot attachments through the token
  path. Handles remain graph-ID-plus-slot-plus-generation; no node addresses,
  attachment ownership, ECS storage, or scene serialization cross this seam.
- Snapshot views expire when their ring slot is republished, on graph teardown,
  or when attachment content/lifetime is invalidated. Validate before every
  callback; an expired view must never call a stale attachment.
- `SparseCellGrid` is the production domain: signed 64-bit coordinates,
  non-background 16x16 sparse chunks, and configurable infinite non-toroidal
  or finite toroidal evolution.
  `CanvasView` is a bounded world-space presentation. Legacy `CellGrid` and
  `Canvas` remain compatibility fixtures, not a second production path.
- Save writes preserve sparse format version 4 and loads remain compatible
  with versions 4, 3, and 2 plus the legacy dense format unless migration is
  explicitly authorized.
- Ruleset transitions and palettes must remain deterministic. Update factory
  selection, known-mode validation, console help/completion, source lists, and
  focused tests together when ruleset availability changes.
- Windows is the only supported and currently verified platform. Linux sources
  and CMake are repaired for Ubuntu 24.04 x86_64 (X11/XWayland, gtkmm-3
  dialogs) and must not be described as supported until native configure,
  compile, launch, dialog, render, and shutdown smoke exist on that host.
  The WASM runtime is Windows x64 only, so Linux currently builds only the
  engine libraries and native test suites and has no runnable applications.
  macOS is not targeted; its platform scaffold has been removed. See
  `docs/packages/platform-linux.md`.
- Keep generic engine/services independent of game-domain policy. Keep raw
  platform and OpenGL details behind their existing boundaries.
- Do not add or replace a third-party dependency without user approval and a
  licensing, maintenance, build, and deployment assessment.
- `CommandQueue` reserves 2,048 commands, grows to a configurable 65,536
  default ceiling, and reports high-water/rejected counts. Do not remove the
  ceiling or hide rejection metrics.
- Product UI remains primitive-composed through `GameVisual`; do not introduce
  a separate retained widget tree for the current console and labels.

## Code, documentation, and generated material

Follow `docs/contributing.md`: avoid `auto`, namespaces, and recursion; use the
documented names; run the Mozilla-based `clang-format` on every modified C++ or
header file. Prefer explicit ownership and narrow dependencies. Owning types
must define or delete copy/move operations deliberately.

Use comments for non-obvious intent and invariants. Put current architecture,
workflows, rationale, and diagrams under `docs/`; cross-reference rather than
duplicating long narratives in guidance. Architecture changes update
`docs/architecture-consensus.md` and the matching LaTeX chapter. A closed
decision also receives a new formal decision-log entry.

Edit source inputs, not generated outputs: do not hand-edit CMake/build output,
LaTeX auxiliaries, PDFs, source dumps, ZIPs, or copied runtime assets. Dated
session records and superseded decisions are provenance; preserve them rather
than rewriting history when current implementation changes.

## Definition of done

A change is complete only when its scope is reviewed for accidental edits and:

- affected targets build and relevant exact tests pass;
- behavior changes pass the full Release build and labeled CTest suite above;
- configured formatting, analysis (`IllumoTidy` when the change warrants
  clang-tidy), sanitizer, coverage, benchmark, or manual checks relevant to
  the risk pass, with environment and limitations reported;
- new serious diagnostics are resolved and unrelated pre-existing failures are
  recorded without opportunistic fixes;
- matching canonical documentation is synchronized in the same change;
- the final handoff states what was verified, what was not, and why.

## Nested guidance

Subsystem rules live in:

- `Illumo/Source/Engine/AGENTS.md`, `Illumo/Source/Foundation/AGENTS.md`, and
  `Illumo/Source/Scene/AGENTS.md`, `Illumo/Source/Gui/AGENTS.md`, and
  `Illumo/Source/Platform/AGENTS.md` plus
  its Windows and Linux child guidance;
- `IllumoGame/Source/Game/AGENTS.md` and
  `IllumoGame/Source/Rulesets/AGENTS.md`;
- `IllEd/Source/AGENTS.md` and `IllEd/Tests/AGENTS.md`;
- `IllMeshViewer/Source/AGENTS.md` and `IllMeshViewer/Tests/AGENTS.md`;
- `Illumo/Source/Services/AGENTS.md`, `Illumo/Tests/AGENTS.md`, and
  `IllumoGame/Tests/AGENTS.md`;
- `Illumo/Source/Rendering/AGENTS.md` plus
  `Illumo/Source/Rendering/OpenGL/AGENTS.md`,
  `Illumo/Source/Rendering/Primitives/AGENTS.md`; test-only MockBackend guidance
  lives in `Illumo/TestSupport/AGENTS.md`.

Child guidance specializes this file and does not weaken it.

Repository skills under `.agents/skills/` apply only to this checkout family.
Do not copy or install them into a user-level skill directory unless the user
explicitly requests broader reuse.

## Maintaining guidance

Update this file or a nested `AGENTS.md` only when a task changes a durable
invariant, convention, boundary, command, or required workflow. Do not use
guidance as a class catalog, issue tracker, session log, or current-state
architecture narrative. Preserve user-authored policy unless explicitly
superseded.
