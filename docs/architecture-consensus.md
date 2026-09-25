# Illumo — Architecture consensus (unified)

**Status:** Single living document — **authoritative for later sessions**  
**Last updated:** 2026-09-15

This file **merges and supersedes** scattered design memory into one coherent story. Read this first; treat external PDFs and old agenda notes as **history** (§2).

| Source | Role in this document |
|--------|------------------------|
| Desktop “Main agenda for Illumo” | Product wishlist → §7 scorecard |  
| `Illumo_Architecture_Decisions.pdf` (ChatGPT) | CA / OOP-practice design review → purpose, ownership, input, rules, SYCL |
| `Illumo_Engine_Architecture_Decisions.pdf` (ChatGPT) | Aspirational 2D engine → what we kept vs discarded |
| `gpt_illumo_arch_assessment.pdf` (ChatGPT) | Assessment of *current* tree → risks, direction |
| Grok architecture reviews + local code review | Strengths, bugs, debt |
| In-repo LaTeX design notes + decision log | Formal decision IDs (D-\*) |
| `docs/current-issues.md` | Product/correctness punch list |
| **Current code under `Illumo/`, `IllumoGame/`, `IllEd/`, and `IllMeshViewer/`** | Current-state evidence; resolve conflicts with approved intent explicitly |

**Rule:** If this document and the code disagree, **code wins** until this file is updated in the same change set.

Optional deeper reading (not required to resume work):

- `docs/latex/architecture-map.tex` → `docs/output/architecture-map.pdf` — landscape chart-only package/class map
- `docs/latex/illumo.tex` — canonical prose-book PDF entrypoint (chapters under `sections/`)
- `docs/latex/sections/09-design-decision-log.tex` — append-only formal decision prose
- `docs/scene-graph-v2-design.md` — compiled hierarchy and snapshot contract
- `docs/scene-graph-v2-plan.md` — implementation and validation record
- `docs/sessions/2026-08-04-illumo-console-and-documentation.md` — this session's implementation record

---

## Table of contents

0. [One-line summary](#0-one-line-summary)  
1. [Project purpose and scope](#1-project-purpose-and-scope)  
2. [Historical sources (how to read old notes)](#2-historical-sources-how-to-read-old-notes)  
3. [Overall assessment](#3-overall-assessment)  
4. [Strengths (keep these)](#4-strengths-keep-these)  
5. [Canonical architecture (code truth)](#5-canonical-architecture-code-truth)  
6. [Locked decisions (catalog)](#6-locked-decisions-catalog)  
7. [Early agenda vs today](#7-early-agenda-vs-today)  
8. [Known product / correctness issues](#8-known-product--correctness-issues)  
9. [Architectural debt](#9-architectural-debt-do-when-it-hurts)  
10. [Recommended work order](#10-recommended-work-order)  
11. [Core design principles](#11-core-design-principles-merged)  
12. [Open questions (remaining)](#12-open-questions-remaining)  
13. [How to use this document](#13-how-to-use-this-document)  

---

## Charter milestones (2026-09-11)

The optional [frame profiler](frame-profiler.md) adds an engine-owned,
bounded 120-frame main-thread timing collector and a DebugModule pie overlay.
Update, rendering, presentation, limiter, and remaining time partition each
sampled loop body. CPU submission is separated from backend presentation;
GPU and asynchronous worker execution are excluded. DebugModule borrows the
collector explicitly and emits ordinary GameVisual tokens. See decision D-PROF1.

IllEd 3D body selection uses nearest forward ray intersections with transformed
local bounds, including hierarchy and nonuniform scale. Hidden/disabled ancestry
and invalid transforms exclude candidates. Ground-plane intersection is used
for placement, while 3D selection uses the screen ray directly; 2D picking is
unchanged. Empty nodes retain a small proxy and picking remains bounds-based.

Scene pass precedence is explicit: nonempty application `SetLayerPasses`
overrides win over host `SetDefaultLayerPasses` fallbacks. Clearing or resetting
overrides restores current host defaults. Motion-blur configuration updates
fallbacks only, preserving passes installed in Start, Update, and dispatch.
See decision D-RP1.

Audit repairs add explicit close negotiation: the runner calls
`Illumo::processCloseRequest()`, and started modules may defer through optional
`OnCloseRequested()`. The window flag is cleared on deferral so editor Cancel
continues normally. Default acceptance preserves other products; failed required
module transitions remain terminal. IllEd reuses Save/Discard/Cancel for native
and toolbar exit. Product saves use `AtomicFile` sibling staging and verified
write/flush/close before replacement; formats remain unchanged. See decisions
D-L1 and D-IO1 and the [remediation ledger](codebase-audit-remediation-plan.md).

[Charter direction](charter-direction.md) separates later physics, object
lifetime, tooling and Linux requirements from current contracts. The owner
approved baseline and independent rendering; older CA-only deferrals do not
exclude those concrete consumers.

InputManager now validates non-reused manager-local IDs, reuses retired storage
among 32 live slots and selects neutral input after active retirement.
CellGameModule releases its registration on Exit and rejects exhausted startup
before domain allocation. Generated standalone projects record source provenance
in engine-provenance.json, distinguishing clean, dirty and unknown identity.

[FrameCapture](frame-capture.md) provides bounded hidden-context OpenGL
rendering without starting the runtime host. Scene and direct producers use the
same renderer. Strict capture errors, backend readback and first-frame depth
state are explicit; normal application defaults remain. The former
`IllumoCapture` CLI is folded into `IllumoRuntime --capture`, which reads back
a presented frame of a running app and prints one JSON result (D-E14, §5.12).

## 0. One-line summary

The simulator's Edit-mode Cell paint drawer is a module-owned GameVisual using
GuiKit rounded surfaces and UiTheme colors. Closed, it is a round glass bubble
peeking over the footer with an up chevron and a paintbrush whose bristles
carry the current paint; hover swells it. Clicking the bubble morphs it into
the drawer on two springs (the width leads, the height follows and bounces),
and the drawer's labels fade in once it has formed; the drawer's header morphs
it back. The morph, bubble hover and row emphasis follow reducedUiMotion. State swatches read the active family's
palette and select the existing paint path; right-click remains erase. Fitted
drawing and pointer coordinates agree, and palette gestures capture pointer
input through release. The drawer anchors above the controls-hint band,
fits the remaining height, and leaves footer input to the hints. The opaque
footer covers the bubble's lower half and any morph overflow. On entering
Edit, the controls hint bar leads the palette by a short stagger and both pop
up on jelly springs that bounce past their slots (the footer continues below
the screen edge so a lifted bar never opens a gap); on exit, the palette leads
the bar and both ease away without ringing. The canvas inset tracks the
visible portion of the moving footer, clamped so it never bounces. Reduced UI motion snaps this transition. The palette travels by
its visible height plus the full footer height before it is removed. See [game controls](packages/game.md).

**The workspace separates the reusable `Illumo` static library from in-tree
applications. Illumo owns the generic application runner, platform
entry/dialogs, BuildInfo, SysCmdLine, host, services, rendering, persistent
scene hierarchy, assets, and module lifetime. `IllumoGame` owns CA policy and
`.csim` persistence (with legacy `.illumo` loading). The optional
`Illumo::Content` layer owns the one scene format every program reads
(`.ilsc` format 2, instantiated by `SceneInstance`), `.ilpk` packages, and the
host virtual file tree on which apps, content packages and mods mount
(D-E18 to D-E24). `IllEd` is the SceneGraph world editor that authors those
scenes and packs projects into packages. Production simulator state remains signed-coordinate
`SparseCellGrid`; `SceneGraph` is the retained world hierarchy consumed by
IllEd, not CA cell storage; rendering remains an enroll-once token stream
through `IBackend`; dense `CellGrid`/`Canvas` remain compatibility fixtures
only.**

---

## 1. Project purpose and scope

### 1.1 Primary purpose (still true)

From the CA design review and the original spirit of the repo:

- **Practice OOP and systems programming** through a production-quality,
  reusable foundation rather than a speculative all-purpose framework.
- The main learning sandbox is **cellular automata** (GoL family, multi-state rules, editor, camera, console).
- Illumo should support future downstream projects without importing their
  domain policy; IllumoGame is the first current consumer, not the limit of the
  library's intended reuse.
- Custom allocators, Tracy, backend interfaces, etc. are **useful experiments**, not requirements that must dominate the design.
- Do **not** rewrite everything for an imagined perfect architecture. Refactor where coupling produces **real friction**.

### 1.2 What the project grew into

It is no longer “one Game of Life file.” It is a reusable runtime/rendering
library plus a **small CA framework / app**:

- Multiple rule sets, edit/run modes, save/load hooks  
- Token-based OpenGL presentation + headless tests  
- Engine-shaped packaging (App / Engine / Game / Rendering / Services)

That growth is **acceptable**. Architecture should add bounded facilities for
current or credible downstream consumers, not a fantasy feature checklist.

### 1.3 Explicit non-goals (for now)

- Production AAA renderer or full multiplayer engine  
- Full ECS / archetypes / cached structural queries  
- `SceneGraph` as the primary sparse-cell storage or retained product-UI path
- Second real graphics API (Vulkan/Metal) as a near-term deliverable  
- Linux parity before Windows remains solid; macOS is not targeted
- SYCL or a second compute backend before the sparse product path is correct and documented  
- Aggressive batching/instancing/render graphs without profiling evidence  

### 1.4 Allocators (from CA design PDF — retained policy)

Retain arena / stack / pool allocators as learning code and optional utilities. Do **not** force them into every service. Appropriate uses:

| Allocator | Type | Fit |
|-----------|------|-----|
| `ArenaAlloc` | bump + chained chunks, bulk `Clear` | Frame scratch, transient command data, parsers, load buffers |
| `ChainedStackAlloc` | bump + LIFO `Deallocate` (destructor runs) | Nested temporary lifetimes |
| `ChainedPoolAlloc<T>` | fixed-size free list, up to 4 chunks | Many same-sized objects with stable slots |
| `MallocAlloc` / `IAllocator` | thin `malloc`/`free` | Baseline interchangeable heap |

Headless allocator coverage lives in `Illumo/Tests/TestAllocators.cpp`
(`Illumo.Alloc.*` CTest cases).

Live consumers:

| Site | Allocator | Role |
|------|-----------|------|
| `CommandLine` parse / complete / dispatch | `ArenaAlloc parseArena` | Token and chain staging for one command session |
| Nested alias expansion | `ChainedStackAlloc aliasExpandStack` | LIFO expanded text frames |
| `Renderer::RenderScene` | `ArenaAlloc frameArena` | Immediate-drawable pointer list per frame |
| `CellGameModule::LoadCellGame` | standard temporary vectors | Validated sparse/legacy load state |
| `SparseCellGrid` | standard authoritative hash map + retained inactive map, node handles, flat index/vector | Unbounded 16×16 chunks plus allocation-reusing generation output, separate stored/counting masks, and per-target candidate/halo selection |

Arena and stack allocations align actual addresses and use aligned backing
storage for stronger requests without relocating live allocations. Typed pool
storage honors element alignment. The four-chunk cap, bulk reset and LIFO
contracts remain unchanged; invalid alignments and overflowing sizes are
rejected before use. Allocation failures may throw.

A general-purpose allocator (mimalloc-class) is a different product; do not reinvent it.

---

## 2. Historical sources (how to read old notes)

Three generations of “design truth” got mixed in chat history. This section untangles them so later sessions do not re-implement superseded plans.

### 2.1 Desktop agenda (“Main agenda for Illumo”)

Early product wishlist:

| Item | Intent |
|------|--------|
| On-screen text / fonts | UI |
| In-game command line | Tools |
| Console drawn in the GL window | UI |
| More rule sets | Content |
| Infinite canvas in **16×16 chunks** + hash map | Scale |
| Mouse drag pan | UX |
| SYCL (or similar) for large cell counts | Scale / learning |

**Outcome today:** UI/tools/pan/most rulesets and the sparse chunk canvas are done. **SYCL remains deliberately deferred.** Full scorecard → §7.

### 2.2 CA design review (`Illumo_Architecture_Decisions.pdf`)

Themes that **still guide** the project:

| Theme | Guidance |
|-------|----------|
| Ownership | Explicit Engine/App object; reduce uncontrolled global lookup |
| Input | Thin layer: GLFW callbacks → InputManager → application logic |
| Boundaries | Contain GLFW/OpenGL behind wrappers; no GL in sim/UI domain |
| Sim vs view | Separate simulation from presentation; double-buffer generations |
| Rules | Families (life-like / elementary / multi-state); JSON for **data**, not executable behavior |
| Parallel | Parallelize **simulation data** last—after serial works |
| SYCL | Optional compute backend; never a hard engine dependency |
| Refactor plan | Ownership → input extract → controllers → contain GL → rule families → double-buffer → then parallel |

**Principle:** *abstract real volatility; hard-code stable structure; ownership explicit.*

**Closer to current Illumo than the “engine passes” PDF.**

### 2.3 Engine design PDF (`Illumo_Engine_Architecture_Decisions.pdf`)

Aspirational **general 2D engine** design (July 2026). Valuable as boundary thinking; **not** the current product plan.

| Proposed | Status in Illumo |
|----------|----------------|
| Illumo owns services; context as view | **Kept** |
| Backend owns GPU resources + ID tables | **Kept** (IBackend / GL registries) |
| Modules as subsystems | **Kept** |
| RenderPipeline with Opaque / Transparent / UI / Debug **passes** | **Not built** |
| MeshDrawCommand-style typed pass queues | **Not built** — we use tagged-union `RenderCommand` stream |
| Minimal EntityTable (transform, mesh, texture) | **Archived** (D-E3) |
| Transform hierarchies + fixed-tick mesh interpolation | Persistent hierarchy is now **implemented by D-E8** for future projects; fixed-tick mesh interpolation remains deferred. CA cells still use visual fade rather than graph nodes. |
| Full ECS (sparse-set sketches) | **Deferred forever-until-pain** |
| Multi-backend, render graphs, heavy batching | **Explicitly deferred** in that PDF too |

**Do not** re-introduce pass objects or entity mesh tables “because the engine
PDF said so.” The retained graph is the bounded D-E8 design; token stream plus
the per-frame drawable list remains the render-submission boundary.

### 2.4 Later assessments (Grok + ChatGPT on the *current* tree)

Agreement across reviews:

- The explicit library/product split is **appropriate**
- The engine-owned application runner consumes a declarative IllumoGame module
  factory; token path + MockBackend are real strengths
- The per-frame Scene-as-list is correct; the dead pointer-based graph and
  EntityTable should stay gone. D-E8 adds a separate handle-based retained
  graph rather than reviving either experiment.
- Main risks: native platform gaps and explicit resource/failure handling at
  rendering and service boundaries; the renderer now bounds queue/resource growth explicitly
- Highest value: product correctness + one canonical sparse-domain/bounded-view model in docs — **not** ECS or multi-pass

---

## 3. Overall assessment

Illumo is a **coherent reusable library currently consumed by one sibling
product**:

```
Illumo Platform/Runner    (entry, system CLI, chrono loop, Debug module)
  → IllumoGame definition (CA defaults/CLI metadata + module factory)
  → IllumoGameCore        (Game, Rulesets, persistence policy)
  → Illumo::Illumo        (generic services + module lifetime)
  → Illumo Scene          (persistent hierarchy + transform extraction)
  → Illumo Rendering      (frame list, drawables, tokens, OpenGL)
  → IBackend              (GLBackend | test-only MockBackend)
```

This is currently a source and target boundary inside one repository, not yet
an installable SDK or stable DLL ABI. A downstream CSim repository remains a
separate packaging and dependency-boundary validation. The broader reuse goal
authorizes evidence-backed generic facilities, not speculative framework work.

**Verdict:** Functional and reasonably testable. Residual risk is mostly **product correctness holes** and **documented debt**, not a need for ECS or a full rewrite.

---

## 4. Strengths (keep these)

| Area | Consensus |
|------|-----------|
| **Declarative composition seam** | Illumo's runner owns process/system sequencing; `CreateIllumoApplication` supplies CA defaults/CLI data and a required-module factory without creating an Illumo-to-Game dependency (D-E7). |
| **Module lifecycle** | `Start` / `Update` / `DispatchDrawables` / `Exit` is clear. |
| **Render split** | Enroll once; emit tokens per frame; backend executes (D-R1–D-R8, D-R10). |
| **Rulesets** | Strategy hierarchy; pure `nextState` + `evalCell`; double-buffered generation (D-P3). |
| **Scene model** | Persistent SoA SceneGraph, compiled preorder and bounds, incremental identity/journal and query BVH (D-E12); SceneGraphDrawable consumes immutable snapshots in the unchanged frame list (D-R25). |
| **Tests** | Independent `IllumoTests`, `IllumoGameTests`, `IllEdTests`, and `IllMeshViewerTests` runners, plus consumer-header smoke, exact process-isolated cases, `IllumoWorkspace` aggregation, combined Clang/LLVM coverage (D-T1), and compile-time `clang-tidy` (D-T3). Coverage dependencies and binary inputs derive from the registered runners, including generated applications and optional editor runners. |
| **Debt hygiene** | Dead experiments under `archive/` rather than half-live. |

---

## 5. Canonical architecture (code truth)

### 5.1 Packages

| Package | Role |
|---------|------|
| **Illumo/Source/Engine/** | Generic application runner, host, modules, and frozen `IllumoContext`; supported contracts are under `Illumo/Include/Illumo/Engine`. |
| **Illumo/Source/Scene/** | Persistent nodes, hierarchy, cached transforms, subtree state, and render attachment extraction; supported contracts are under `Illumo/Include/Illumo/Scene`. |
| **IllumoGame/Source/Game/** | CA definition/config, module factory, domain + presentation (`SparseCellGrid`, `CanvasView`, `CellGameModule`, `CellContext`); dense Canvas types are compatibility-only. |
| **IllumoGame/Source/Rulesets/** | CA rules (GoL family, Wireworld, …). |
| **Illumo/Source/Rendering/** | Reusable world-mesh and 2D front end, cubemaps, offscreen passes, managed assets, Scene list, tokens, and private OpenGL implementation; supported contracts are under `Illumo/Include/Illumo/Rendering`. |
| **Illumo/Source/Content/** | Optional `Illumo::Content` layer above the engine: virtual paths, `illumo.json` manifests, `.ilpk` archives, the virtual file tree and its console/asset/tree adapters, package mounting, and the `.ilsc` format 2 codec plus `SceneInstance` (D-E18). Guests link the serial-safe mirror `IllumoGuestContent`. Core Illumo never includes it. |
| **IllEd/Source/** | SceneGraph world editor over a `SceneInstance` document: patch history, selection set, shortcut table, gizmos, typed inspector, clipboard, hierarchy, asset browser and project commands (D-E25); shipped as `IllEd.wasm` (`Wasm/`), with `IllEdCore` as native test oracle (D-E14). |
| **IllMeshViewer/Source/** | Mesh and scene viewer (`.obj`, `.ilsc` through `SceneInstance`), camera, configuration, input, and product UI; shipped as `IllMeshViewer.wasm` (`Wasm/`), with `IllMeshViewerCore` as native test oracle (D-E14). |
| **Illumo/Source/Audio/** | Sound effects (D-E28): the `IAudio` seam, `AudioDecoder` (WAV/FLAC/MP3 to float clips) and the native `AudioDevice` mixer, all over vendored miniaudio kept private here; supported contracts are under `Illumo/Include/Illumo/Audio`. Guests compile only `AudioDecoder`. |
| **Illumo/Source/Services/** | Generic log, env, input, system CLI, branded console UI, and allocators. |
| **Illumo/Source/Foundation/** | BuildInfo, macros, and implementation support; public aliases/utilities are under `Illumo/Include/Illumo/Foundation`. |
| **Illumo/Source/Platform/** | OS entry, public SaveLoad implementation boundary, and native dialogs. |
| **`Illumo/Assets/`** | Runtime files outside `Illumo/Source/`; there is no `Source/Assets` package. |
| **Illumo/Tests, IllumoGame/Tests, IllEd/Tests, IllMeshViewer/Tests** | Four independent library and product runners. |

`Illumo::Illumo` exports supported public headers and a GLM-only generated include
directory. GLM types and `<glm/...>` paths are intentional public math contracts;
the rest of the vendor tree stays private. CMake tracks GLM header content and
membership, preserves unchanged timestamps, and removes stale generated headers.
Consumers that need another vendor API declare that dependency explicitly
(D-DEP1). Vendored source files and versions are unchanged.

House style (D-008 / `docs/contributing.md`): avoid `auto`; avoid namespaces (prefer static classes/structs); no recursion; third-party via PR — unless a later decision waives.

### 5.2 Ownership and lifetime

- **Illumo** owns long-lived services with `unique_ptr` (window, renderer, camera, env, input, scene, command line, …).  
- **SceneGraph** owns node slots, hierarchy links, and transform caches. It
  borrows render attachments, which remain consumer-owned. Detach/invalidate
  snapshots before changing or retiring their content; emitted payloads outlive
  synchronous queue submission.
- **IllumoContext** is a **non-owning** pointer bag frozen as a public source
  contract (D-E5/D-E6); modules validate their required fields at `Start`.
- **Illumo's runner** registers DebugModule and invokes the consumer-supplied
  required-module factory. The factory definition remains in IllumoGame.
- Do not grow the bag for convenience; a third module with different needs should take **explicit constructor dependencies**.  
- Prefer explicit references for domain logic over “look up everything from the bag.”

### 5.3 Module contract

```
class IModule {
  virtual bool Start(IllumoContext* context) = 0;
  virtual void Update(double dt) = 0;
  virtual void DispatchDrawables(Scene* scene) = 0;
  virtual void Exit() = 0;
};
```

| Hook | Responsibility |
|------|----------------|
| Start | Bind inputs and allocate game state; return `false` if initialization is incomplete |
| Update | Simulation / input |
| DispatchDrawables | Contribute what should draw this frame |
| Exit | Teardown module-owned state |

`IllumoContext` exposes `IModuleHost*` allowing active modules to request deferred
runtime module transitions (`RequestTransition`). Transitions take place at frame
boundaries, safely tearing down the outgoing module via `Exit()`, draining
input/char queues to prevent click/key bleed, clearing `Scene` drawables, and invoking
`Start()` on the incoming module.

The application definition supplies `createRequiredModule` (creating `MainMenuModule`
by default, or `CellGameModule` if direct launch was requested via environment/CLI);
the engine runner registers `DebugModule` as optional in Debug. Optional overlay
modules update before the required product module so console/FPS/demo input is
global across the main menu, settings, and cell canvas; they still dispatch
after it so overlays draw on top. A required
failure rolls back every accepted module and fails startup; an optional failure
remains inactive. Startup and rollback exceptions are contained and logged
(D-B1/D-E7/D-E9).

### 5.4 Frame loop (production)

```
Illumo Platform::main
  → CreateIllumoApplication()         // consuming product definition
  → RunIllumoApplication(argc, argv)  // engine logger/CLI/loop ownership
  → Illumo(IllumoConfig{application name, config path})
  → application.applyDefaults()       // product defaults
  → SysCmdLine::ParseCommandLine      // engine parser + product option metadata
  → Illumo::initialize()              // fallible window/backend factories
       backend = CreateOpenGLBackend(window)
       backend->Initialize()          // exactly once, owned by Illumo
       renderer = Renderer(..., unique_ptr<IBackend>)
  → application.createRequiredModule(&environment) → product module
  → addModule(required module, Required)
  → addModule(DebugModule, Optional)  // Debug / RelWithDebInfo
  → startModules()                    // rollback on required failure
  loop:
    update(dt)   // applyPendingModuleTransition → InputManager → Camera
                 // → optional overlays.Update → required.Update
                 // → discard leftover key/char events
    render()                            // single production path (D-R13)
      scene.ClearDrawables()          // Scene = per-frame FrameRenderList (D-E4)
      required.DispatchDrawables then optional overlays

      renderer.BeginFrame()
      renderer.RenderScene(scene, camera)   // tokens, then optional immediate fallback
      renderer.EndFrame()                   // swap (GL)
```

There is **no** env-gated alternate product frame path. `Renderer::RenderProofQuad`
remains for headless token e2e tests only.

Illumo construction loads only generic host defaults. The engine runner invokes
the product defaults callback, then its own parser consumes standard window
flags plus product-provided option/help descriptors before host initialization.
`--help` and `--version` return explicit process results; library code does not
call `std::exit`.

Typical combined draw order: an optional Renderer-owned directional-shadow
depth pass over camera-relevant World casters, then configured World color passes,
followed by UI splash + console + editor cursor + selection outline + optional
inspector HUD, then Debug FPS (Debug/RelWithDebInfo via DebugModule).

At the start of `RenderScene`, `Renderer` captures the active window dimensions
and primary camera MVP once for that extraction. Matching `GameVisual` instances
consume those frame values instead of querying the window and recomputing the
same matrix independently; direct token emitters retain their local fallback.
`CanvasView` retains upload-rectangle scratch storage. `MeshVisual` retains
dynamic handles for procedural geometry and uploads only dirty ranges; immutable
model geometry is reference-counted once by `AssetManager`, while each visual
keeps a non-owning mesh handle and per-instance state. `SceneGraph` retains
compiled arrays and reusable snapshot buffers. These caches do not retain or replay command queues.

`RenderWindow` defaults to swap interval one. The persisted `vsync` environment
value can select synchronized or uncapped presentation and is reapplied only
when it changes, so Debug's generic `toggle vsync` command works live. The FPS
overlay labels synchronized swap-completion cadence as `Paced FPS` and reports
main-loop submissions separately; uncapped mode reports paced FPS as off rather
than presenting CPU submissions as monitor output (D-P32).

#### 5.4.1 Compiled scene hierarchy and snapshots (D-E12/D-R25)

`SceneGraph` owns persistent nodes in parallel slot arrays and exposes only
`SceneNodeHandle` identities (graph ID, slot, generation). Intrusive ordered
parent/child/sibling links are authoritative. A structural revision lazily
compiles preorder, parent indices, subtree ranges, and depths. Local TRS edits
set one dirty bit and lower a watermark; one forward pass resolves affected
world transforms. Authoritative world queries walk only ancestors and do not
consume dirty state. Matrix setters explicitly decompose to TRS; shear and
projective input are lossy.

Nodes carry nonunique interned names, an opaque uint64 payload, enabled/visible
state, and an ordered list of borrowed attachments. Names return the first
preorder match. A 4,096-entry sequence journal lets consumers update bindings;
an expired cursor signals a full resynchronization. No node addresses, file
format, component system, or application update callbacks enter this API.

Nonzero attachment bounds revisions cache local AABBs; zero means uncacheable.
World boxes transform each attachment before union, and a backward pass refits
subtree boxes. Unknown/invalid bounds fail open during rendering. Hidden
subtrees skip their compiled range. Camera-missed bounded subtrees skip camera
tests but retain snapshot entries for potentially relevant off-camera shadows.
`raycast`, ordered ray candidates, and `queryBounds` use a lazy binned-SAH BVH;
failed builds use the equivalent linear path. Warm queries still poll attachment
revisions in O(n); the index accelerates intersection work, not that polling.

`SceneGraph` no longer derives from `DrawableBase`. `SceneGraphDrawable` borrows
it and publishes one `SceneSnapshotView` per renderer frame, keyed by renderer
lifetime and frame serial. Collection, depth, and color consume immutable world
matrices, bounds, handles, and borrowed attachment pointers. The graph retains
two reusable snapshot buffers. Views expire on slot reuse, graph teardown, or
attachment invalidation/destruction. The adapter checks validity before every
callback and reports an expired frame rather than calling retired content.
Ordinary transform/state edits after extraction affect the next snapshot.

The Renderer fits the shared directional light after caster collection, so
shadow relevance is computed from snapshot bounds in the depth pass. This is
an intentional adjustment to the original proposal's early shadow flag; it
preserves the established shared-shadow policy and off-camera casters. Owners
must detach or invalidate snapshots before changing/freeing borrowed content,
and emitted token payloads must still outlive synchronous submission. Mutation
is rejected inside extraction/bounds callbacks. All scene work stays on the
main thread. The `sceneSnapshotExtraction` environment value defaults to on;
zero temporarily selects guarded direct traversal for containment.

IllEd's `EditorDocument` owns the runtime graph and is its edit gateway. Stable
file IDs become graph names; recipe indices use user data. Render bindings
persist across transform/recolor edits, journal overflow resynchronizes bindings,
and serialization exports hierarchy order from the graph. Picking keeps the
editor's exact transformed local-box narrow phase after graph broad-phase
queries. Since 2026-09-23 the document wraps a Content `SceneInstance` over
`.ilsc` format 2 (D-E19, D-E25), and IllumoGame's `render3dTest` scene and
IllMeshViewer scenes load through the same instance; CA storage and
primitive-composed UI stay separate. Generic `WorkerPool` is available without importing CA policy; scene
parallelism and a separate culling index remain measurement-gated.


### 5.5 Rendering architecture (shipped)

**Mental model:** Game never issues draw `gl*` for the live path. It enrolls resources and emits **tokens**.

```
Game / Rulesets / UI
    |  (no gl* for draw submission)
    v
Drawable::AppendCommands(Renderer*)
    |  acquire/enroll typed handles + push tokens
    v
CommandQueue<RenderCommand>     // tagged union payloads
    |
    v
IBackend::SubmitCommandQueue
    |
    +-- GLBackend + GLDevice   (handle resolve, PBO upload, bind tracker)
    +-- MockBackend            (tests)
```

| Piece | Job |
|-------|-----|
| **Modules** | Choose what should appear this frame; place drawables into Scene layers. |
| **Scene** | Per-frame non-owning drawable pointers in ordered layers (World → UI → Debug), with ordinary screen rendering or effective custom pass sequences per layer. |
| **RenderStyle** | Generational registry on `Renderer`: shader handle + `PipelineState` defaults. Canvas, UiText, Console, Shape, Sprite, and Skybox are registered built-ins. Canonical Shape/Sprite programs position with `uMVP` only (`WorldLook`); overlay chrome supplies a Y-down screen ortho, world objects supply camera view-projection times node world; Skybox positions at the far plane with translation-stripped view-projection. |
| **Camera** | Default orthographic vec2 pan/zoom for CA XY picking; `ProjectionType::Perspective` plus `lookAt` for 3D views. Restoring orthographic preserves 2D pan/zoom/`ScreenToWorld`. Read-only pending position/zoom access lets products validate interpolated navigation targets. CA navigation and sparse camera metadata reserve a `2^32`-cell endpoint margin: finite world axes satisfy `abs(axis) <= 16 * (2^63 - 2^32)`. This protects view arithmetic without restricting sparse storage. |
| **Primitives / GameVisual** | Value-type shapes/sprites/text on a `GameVisual` host for overlay/painter UI and the CanvasView world quad. Parent + local `Transform2D`, atlas regions/flips, integer draw order, stable insertion order, and adjacent-only batching preserve painter semantics. Pixel-space rebuilds conservatively reject quads outside the logical viewport. An optional top-left logical-pixel clip rejects wholly excluded quads before upload and brackets partial content with a nested, intersected scissor that restores any outer clip (D-R23). Dynamic quad buffers start at 1,024 and grow to a configurable 65,536 default ceiling. |
| **MeshVisual** | World mesh host and `ISceneRenderAttachment`: colored lines/triangles, textured quads (sprites), optional billboard facing, managed immutable mesh handles, conservative attachment bounds, and optional directional lighting plus a depth-only shadow pass and object motion blur. Lighting, shadows, tint, caster distance, and motion blur are per-instance CPU state emitted as `WorldLook` uniforms. A node borrows an ordered list of attachments; several visuals/nodes may bind the same managed mesh without another upload. Procedural batches remain visual-owned dynamic buffers (D-R21/D-R24/D-E11). |
| **SkyboxVisual** | World cubemap host and `ISceneRenderAttachment`: unit cube geometry rendered with `RenderStyleId::Skybox` at the far depth plane (`xyww`), translation-stripped view matrix `projection * mat4(mat3(view))`, seamless cubemap sampling (`IBackend::CreateCubemap`, `AssetManager::acquireCubemapFromCross`), and optional tint color. Consumed by 3D viewers (e.g. `IllMeshViewer`). |
| **Primitive UI & GUI Kit** | `GuiKit`, `GuiDialog`, `GuiMenuShell`, and `GridAtlas` (`Illumo/Include/Illumo/Gui/`) supply stateless drawing/layout helpers, reusable modal dialogs, shared overlay behavior, and atlas UV mapping on top of `GameVisual` and `UiTheme`. `GuiMenuShell` holds the behavior every overlay repeats: `GuiEasing` curves and approach helpers, `GuiMenuAnimator` reveal/selection/value-pulse/ambient/caret clocks with reduced motion, `GuiPanelLayout` virtual-resolution fitting (`fit` for centered panels, `viewport` for docked bars) plus row-window arithmetic, and `GuiPointerTracker` virtual-space pointer sampling with press and release edges. The tool apps add `GuiToolStyle` (the plain palette and flat tool widgets), `GuiPanelDock` (dock columns, splitters, title-bar pop-out and tear-off, saved layouts) and `GuiPanelPlacement`/`GuiPanelPointer` (a panel's rectangle, surface and pointer), D-UI7. `CommandLine`, `GLString`, `ExitConfirmDialog`, `EditorConfirmDialog`, the IllumoGame menus, the IllEd panels and the IllMeshViewer panels compose these primitives without introducing a retained widget hierarchy. |
| **Drawable** | Content handles; `bindStyle` then content tokens via `AppendCommands`. Immediate `Draw()` only if AppendCommands returns false (tests/stubs). |
| **Renderer** | Backend-neutral: owns style table; frame setup; walk layers; submit. Depends only on `IBackend*` (D-R11). |
| **IBackend** | Allocates typed slot+generation handles; validates create/replace/destroy/query operations; queues and submits. GPU objects live in backend registries. Supports 2D textures and 6-face cubemaps (`CreateCubemap`, optional `ReplaceCubemap`) with seamless filtering and clamp-to-edge wrap. Replacement preserves texture kind and publishes a complete resource before retiring the previous one. |
| **AssetManager** | Canonical-path texture/cubemap/shader/static-mesh cache with reference counts and stable typed handles. Texture/shader assets retain fallback resources, synchronous or one-worker CPU loading, cubemap extraction, dependency tracking, render-thread `pump`, explicit reload, and Debug polling. Static meshes synchronously decode through `MeshLoader`, enroll one immutable backend mesh per canonical path/options key, expose immutable draw metadata, and are destroyed on final release; mesh hot reload remains deferred (D-R17/D-R24). |
| **ShaderPreprocessor** | Backend-neutral GLSL preprocessor: resolves `#include` directives against virtual in-memory module registry (`<illumo/...>`) and disk paths, injects compile-time `#define` macros, enforces `#pragma once` & recursion guards, preserves `#version` at line 1, emits `#line` markers, and discovers transitive include dependencies. |
| **Composition (`Illumo::initialize`)** | Calls fallible window/backend factories, initializes the backend exactly once, transfers `unique_ptr<IBackend>` to Renderer, and calls `ensureBuiltinStyles()`. |
| **GLBackend / GLDevice** | Real OpenGL under `Rendering/OpenGL/`: handle registries, execute tokens, PBO texture updates, bind-state tracking, blend-func-on-enable (D-R5). Each `GLShaderProgram` owns its uniform-location cache, so cache hits do not construct combined program/name strings and replacement or destruction retires cached locations with the program. |
| **MockBackend** | Exposed only by `Illumo::TestSupport`: records creates + command order for headless tests. |

**Layers vs passes:** layers are ordered composition buckets. Ordinary layers
draw to the screen; a layer's effective pass sequence can select offscreen
targets, viewports, and independent clear settings. Application overrides take
precedence over host defaults. This explicit pass mechanism is not a render graph.

**Acquire/enroll (rare):** backend `Create*` returns non-convertible
`MeshHandle`, `ShaderHandle`, or `TextureHandle` values with slot+generation;
managed file textures, shaders, and immutable model meshes normally come from
`AssetManager`. `MeshVisual` borrows a managed handle; the acquiring owner
retains/releases it. Canvas cache growth replaces its texture through the same
validated handle and destruction releases the GL texture, PBOs, and fences
before recycling that handle.
**Per frame:** growable `CommandQueue` reserves 2,048 tokens and grows to a
configurable 65,536 default ceiling while tracking high-water and rejected
counts. `RenderCommand` remains a 72-byte tagged union on the supported 64-bit
Windows build (bind, uniform, update texture/buffer, draw, clear, viewport,
scissor state, pipeline). Matrix-uniform tokens borrow renderer-owned values
copied into retained 128-matrix chunks; those chunks reset only after command
submission, remain allocated for reuse, and use a matching default
65,536-value safety ceiling so rejected token storms cannot grow side storage
without bound.
`Renderer::pushClipRect`/`popClipRect` retain a per-frame scissor stack, so
primitive clips intersect and restore an existing product-owned scissor rather
than disabling it. Low-level `pushScissor` remains available for explicit
state emission.
**Not current (old engine PDF):** separate Opaque/Transparent/UI pass *objects*, MeshDrawCommand-only world draws, entity mesh tables.

**Production pure-token drawables (D-R10):** CanvasView/GameVisual, Cursor,
ConfigurationMenu, CommandLine, GLString, and SplashText. Dense Canvas remains a compatibility
fixture.

**Token migration phases 0–6:** complete for planned scope (proof → Scene → Canvas → UI → dead-path cleanup → Mock inject).

### 5.6 Sparse domain + selectable topology + bounded view (canonical — code wins)

The live path is intentionally a full replacement of the finite dense runtime:

| Layer | Type | Contents |
|-------|------|----------|
| **Domain** | `SparseCellGrid` | signed 64-bit cells in a hash map of non-background 16×16 chunks; `0 x 0` selects the infinite non-toroidal domain, while positive chunk dimensions select a finite torus with canonical wrapped cells |
| **Simulation** | `SimulationRunner` + two `SparseCellGrid`s | one worker generation may be in flight; the main thread reads only the published grid and publishes completions at frame boundaries. Incremental completions apply `SparseGenerationDelta` before the former display grid is reused. Broad completions and one-revision journals of at least 2,048 presentation chunks carry a lightweight replacement marker: the spare advances directly from the immutable published grid and updates its own authoritative nodes in place, avoiding a full snapshot and mirror pass. Overdue whole steps are dropped without a backlog, and edits, persistence, ruleset changes, manual stepping, and shutdown drain first. `SparseCellGrid::advance` retains the transactional totals/frontier/candidate/halo/node-reuse paths described below. |
| **View** | `CanvasView` | visible dimensions remain diagnostics while sampling uses a globally aligned cache padded by two chunks per side; motion inside it copies retained texels and resamples only newly exposed strips when the aligned origin shifts, otherwise it changes only the MVP. Near LOD is one texel/cell; far LOD is an integer density level with immediate coarsening and 80% refinement hysteresis. One-revision changed chunks map to deduplicated exact or overview cache bins, with a complete cache resample when those bins cover at least a quarter of the cache; LOD/resize, non-aligned jumps, revision gaps, palette change, torus wrap, and replacement refill the bounded cache. Complete overview resamples walk occupied cells and snap-convert sampled RGB in one pass. Exact-cell CPU RGB fades; density overviews and newly revealed cells snap. |
| **GPU** | `CanvasView` + `GameVisual` | one reusable nearest-filtered RGB staging texture and one world-space quad, a dynamic `Pos3Color3Uv2` mesh drawn with the built-in Canvas style (the guest records it as a Canvas batch; no frame schema change). Its shader (`Illumo/Shader/canvas_frag.glsl`) reads cells with `texelFetch` and, once a cell spans about 4-12 screen pixels, draws each as an LED in a matrix: a softly raised rounded-square key in a dark housing coloured by a distance-weighted blend of the neighbouring cells (continuous across borders, so no seams) with a faint world-fixed grain. A lit key has a hot core that runs toward white (a squircle falloff, so no diagonal creases), a gentle bevel lit from the upper left and a light drop shadow onto the housing; very close, the emitter chip shows at its centre. Lit LEDs throw light into the housing around them and brighter neighbours bleed onto darker keys. Lit and brighter-than-self are judged by luminance, so it needs no knowledge of the ruleset's background colour; smaller cells and density overviews stay flat texels. The player's cell look (D-UI12) rides in the quad's otherwise unused colour attribute as (glow / 2, LED keys, grid lines): it scales the glow and spill, turns the LED styling off for flat pixels, and adds a one-pixel dark line on cell borders once cells span about 6-14 pixels, with no extra uniform or frame change. Finite worlds add a screen-thickness-stable themed outline around the canonical rectangle so wrap edges remain visible without tiling the world. Dirty 16×16-texel tiles merge into at most eight update rectangles or their AABB. Uploads through 64 KiB are direct; larger requests use a non-waiting three-PBO/fence ring with direct fallback. |

Rulesets provide stateless `nextState` and `evalCell` functions. Each ruleset's
complete 256-state by 9-neighbor transition table is built once before worker
dispatch and indexed by sparse and compatibility dense loops. Production does
not depend on `CellGrid*`; the old dense `CellGrid`/`Canvas` and their
toroidal `calcGeneration` entry remain only for compatibility tests during the
transition. The sparse map has no fixed chunk-count cap and preserves all
multi-state byte values. Finite topology is centered on the origin in whole
chunks, wraps both axes during reads, writes, and neighbor evaluation, and
rejects mixed zero/positive dimensions. Presentation clips to the centered
canonical rectangle and leaves camera space outside it background-colored;
only neighbor evaluation wraps across the rectangle's opposite edges.

Negative chunk coordinates use centralized floor division/modulo. Chunk output
is sorted by `(chunkY, chunkX)` for deterministic saves and tests. The view
visits only chunks intersecting its cache bounds. The cache is globally aligned
and extends two 16-cell chunks beyond the visible viewport on every side, so
sub-cache pan/zoom changes only the camera MVP and aligned origin shifts copy
retained CPU texels while resampling only the newly exposed strips. Overview
strips initialize their bounded output bins to the background and accumulate
only occupied cells from intersecting sparse chunks instead of probing every
source cell. The grid publishes a
revision-scoped changed-chunk list for edits and completed generations. Direct
grid edits conservatively map changed chunks, while published generations map
exact changed-cell masks to deduplicated cache bins. Far-zoom bins reset
together, accumulate in one occupied-chunk traversal, and finalize once,
avoiding per-bin hash probes and full overview refills for sparse broad
changes. Dense exact-cell marking stops once it covers a quarter of the cache,
then the complete bounded cache is resampled. Revision gaps, non-aligned jumps, palette changes,
whole-grid replacement, LOD changes, torus wrap, and resize use a complete bounded refill.
Far LOD coarsens immediately to fit and refines only at 80% budget occupancy.
A retained active-texel set
makes fade ticks and zero-speed snaps proportional to colors still changing;
reapplying an unchanged zero fade speed does no scan. Exact-cell LOD keeps that
fade; density overviews snap color changes. Dense exact-cell one-revision bins
that cover at least a quarter of the cache resample the complete bounded cache;
bin marking stops once that threshold is reached. At far zoom it limits
the active texture to `max(CanvasX/Y, window / 4)` texels and accumulates
palette density into each overview texel. This presentation budget neither
caps chunks nor discards simulation cells. Rendering never creates per-chunk
GPU resources: only the bounded view texture and quad enter the token stream.
For small required dimension increases, the CPU and GPU texture allocation
reserves 50% headroom; nearby smooth-zoom sizes reuse it. Dirty 16x16-texel
tiles merge horizontally and vertically into at most eight rectangles when
their combined area is at most half the enclosing AABB, otherwise the AABB is
used. `GLTexture` uploads rectangles through 64 KiB directly; larger requests
select a signaled slot from a three-PBO ring without waiting and fall back to
direct upload when all slots are busy or mapping fails. A replacement keeps the
same opaque handle while `GLBackend` destroys the old texture, PBOs, and fences,
and `CanvasView` explicitly releases the handle during destruction.

Sparse stepping keeps result installation serial. Each chunk carries separate
compact masks for stored non-background cells and cells that contribute to
neighbor counts, plus cached popcounts for both masks. The authoritative and
inactive maps each maintain aggregate stored-cell, counted-cell, and
candidate-preferred-chunk totals. Edits, assignment, clear, local patches, and
complete map swaps update those totals in the same transaction as the chunk
data. `advance` therefore reports population statistics and checks whether any
chunk prefers candidate evaluation without scanning every allocated chunk. If
any source chunk has fewer than 48 counted cells, the grid
creates exact affected targets through a retained power-of-two open-addressed
index. Generation stamps make old slots logically empty without clearing the
table, and each slot points into contiguous scratch containing a 256-bit
candidate mask plus 256 neighbor counts. Target discovery checks the four edge
masks and corner bits directly. Candidate counters are initialized when a bit
is first enrolled, avoiding a 256-byte clear for every target. The direct
serial path keeps source-centric accumulation for locality. During large or
forced parallel preparation, discovery records each target's direct 3x3 source
references beside its neighbor counts. Workers claim retained coarse target
ranges and count from those references without repeated chunk-map lookups. For
direct dual-grid generations, an object-local topology epoch changes only when
chunk presence or counted edge/corner participation changes. When the same
source grid returns with the same epoch, its target index and source references
are reused exactly; only candidate masks and counts are rebuilt. An
index-aligned destination pointer also avoids output-map lookup and stale-node
scanning when the destination topology is unchanged. In-place output preserves
the node addresses that make this reuse safe. Candidate-index lookup probes for an existing target before considering
growth; only a new insertion that would cross the 75% load boundary resizes the
retained flat table. Each target independently uses those
candidates when its counted-neighbor contributions are below the calibrated
threshold, or a direct 18×18 halo otherwise. Counting-dense centers skip
neighbor-count scratch preparation and go straight to halo evaluation.
Dense-majority frontiers skip candidate scratch construction.
Frontiers with at least 2,048 targets also skip scratch and evaluate as halo.
Thus mixed dense/sparse worlds do
not inherit one global decision, and dense Wireworld conductors remain candidate
work because only heads contribute neighbor counts. Worlds whose source chunks
are all densely counted bypass scratch construction. Both capacities are
retained, so a stable-width colony neither reallocates candidate records nor
sorts or binary-searches chunk addresses. Halo/core evaluation uses up to eight
reusable workers once there are at least 32 targets. Complete target discovery
uses a retained generation-stamped flat index and retained address/result
vectors rather than a generation-local hash set, target sort, and disposable
buffers. Evaluation extracts each 18-bit counted row directly from the 3x3
chunks' counting masks and reduces it through three rolling horizontal rows;
no 18x18 byte halo is materialized and cell states are read only for the target
core. Halo targets may use an exact, on-demand memo keyed by all 324 states in
that 18x18 neighborhood. The bounded four-way cache is sharded by the main and
worker slots, so evaluation needs no locks; a hash selects candidates but a
full key comparison is required for a hit. It samples 16 targets per shard,
activates at a 25% hit rate, and cools down for 32 generations after
unprofitable sampling or three active generations below 10%. An exact comparison
of the complete transition table invalidates every entry when semantics change;
instance revisions, rule names, and C++ types are not semantic identities. Candidate-only and
sub-32-target generations bypass it. Coarse mixed/candidate
evaluation uses up to four automatic workers once there are at least 16,384
work cells. Preparation uses about eight retained ranges per worker, capped at
256 target chunks per range; evaluation ranges contain roughly 2,048 work cells.
Both avoid one atomic claim per mostly empty target chunk. All-candidate small worlds keep
the original direct serial output loop. Both
evaluators build transactionally into a retained inactive map. Nodes from the
prior inactive generation are extracted into a retained handle vector after its
aggregate statistics reset once. Sparse candidate output acquires and rekeys a
node first, then constructs the result directly in mapped storage; dense halo
output retains its complete-array bulk-copy path. Target-sized bucket headroom
is preserved because exact output reservation measured slower. Allocation occurs
only when output exceeds the previous node high-water count. A persistent `SimulationRunner` advances
the spare grid while the main thread reads the published grid. Completion
publishes only at a frame boundary with a sparse generation delta; the former
display grid consumes that delta before becoming the next worker grid. Only one
generation may be outstanding and no backlog is formed. Excess whole-step debt
is dropped while fractional remainder is retained. Pause/edit, persistence,
ruleset changes, manual stepping, and shutdown drain first. Status computes
achieved TPS from published completions and retains rolling 256-sample p50/p95/
max values for worker generations and their mirror/advance/capture stages,
cache refills, requested upload bytes, and upload rectangles. Full sparse
replacements publish only a marker; `advanceFrom` reads the published map and
writes the spare grid's map directly, so no replacement records are captured or
mirrored. Incremental catch-up uses the prior grid's own
changed-address journal and a retained incoming-address index, so an incoming
record overwrites its final state once instead of first replaying an obsolete
intermediate snapshot.

The inactive map also preserves the prior generation for changed-region
stepping. Edits and committed generations record exact per-chunk state-change
and counting-change bitmasks in retained generation-stamped flat sets. On the
next step, each changed chunk enrolls itself; a neighboring chunk is enrolled
only when a counting-status change touches the shared edge or corner. A
state-only change cannot affect a neighbor's transition. Sparse local sources build
exact candidate masks only for those targets and choose candidates or halos from
counted-neighbor contribution work. The grid compares target/source bookkeeping,
candidate construction, neighbor contributions, and evaluation cells with a
complete-path estimate derived from cached population totals. It patches the
retained prior map only when the frontier estimate is no greater; broad changes
use the complete adaptive path. An empty frontier returns without visiting any
chunk. Tracking retains at most 16,384 changed chunks so a large generation
does not discard the journal; evaluation still chooses frontier versus complete
from estimated work. Overflowing that tracking cap, or allocation failure, is
what invalidates frontier bookkeeping. Ruleset type changes
invalidate current chunks before the next step.

### 5.7 Rules and encoding

`RuleSetRegistry` starts empty and transactionally compiles catalog text into
data-backed rules. The shipped `families.json` (schema 1) owns family IDs,
simulation models, state counts, state labels, and colors. The separate
`rulesets.json` (schema 3) owns stable rule IDs, display names, one required
`family_id`, transition parameters, and an optional deterministic starter
strategy. Life-like, Generations, Moore-table, cyclic, colorized-Life,
Larger-than-Life, Hodgepodge, Turmite, lattice-gas, dominance, von Neumann
rule-table, sandpile, Lenia, and elementary 1D definitions compile to the
existing `DataRuleSet`;
the elementary rules retain their separate history path. Older unversioned,
schema-v1, and schema-v2 rules catalogs are normalized into compatible family
and rule definitions in memory.

Game's `RuleCatalogLoader` selects a valid base catalog pair beside the
executable, in the working directory, or in its `IllumoGame` subdirectory. It
then layers the working-directory `families.user.json` and
`rulesets.user.json` overlays as one validated pair. The required-module factory
loads the shared catalog once before menu/direct-game construction. Engine-owned
configuration-path discovery supplies the executable directory. Independent
registries require explicit text/file loading; Rulesets has no filesystem or
native API dependency (D-GC1, D-GC2, D-GC4).

`RuleFamilyDefinition` owns cell schema and appearance. `RuleSetDefinition`
owns transition behavior and refers to exactly one family ID; the typed
`RuleFamily` value identifies the evaluator contract. Registry compilation
resolves the pair into the immutable runtime `RuleSet` view used by Game and
`SparseCellGrid`. F1, New Simulation, status, console output, and `CellContext`
carry the family/rule pair. New startup configuration stores both values while
legacy `ModeString` inputs derive the family from the selected rule.
Version-4 `.csim` saves store both IDs; version-3, version-2, and legacy
dense saves derive the family from their saved rule ID. The loader still
accepts existing `.illumo` filenames; the extension change does not alter save
magic or the binary format.

**Active rules**: Game of Life, Seeds, Brian's Brain, Highlife, Day & Night,
Life Without Death, Wireworld, Rule 90, Rule 184, Star Wars, Nova Trails, two
Excitable Waves thresholds, Prism Rush, Chromatic Storm, Crystal Domains,
Elemental Surge, Aurora Conflict, Immigration, QuadLife, Bosco/Bugs,
Bugsmovie, Globe, Banners, Transers, Fireworks, the classic 14-color cyclic
automaton, Hodgepodge Classic and Spiral Bloom, Langton's Ant plus RRL and RRLL
Turmites, HPP Gas, and two five-species RPSLS thresholds; Langton's, Byl's,
two Chou-Reggia, SDSR, and Evoloop self-replicating loops; Sandpile Mandala,
Binary Star, and Critical Avalanche; Griffeath's 313, Lava Lamp, Stripes,
Squarish Spirals, Cyclic Spirals, and Turbulent Phase; Comet Rockets, Rainbow
Tides, Neon Coral, and Bubble Swarm trailing Larger-than-Life rules; and 45
classic Golly/MCell Life-like and Generations rules (Maze, Coral, Anneal,
Diamoeba, Replicator, Frogs, Lava, Swirl, Bombers, Xtasy, Thrill Grill, and
others); and six Lenia species (Orbium, Orbium bicaudatus, Gyrorbium,
Scutium, Discutium, Paraptera). They compile to the common `DataRuleSet`;
life-like and Generations definitions use neighbor masks, while Wireworld and
the five-phase excitable-media family use explicit Moore transition tables.
The twelve-state Prismatic Ecology and nine-state Elemental Court use cyclic
interaction: a cell advances by a coprime cycle step when enough neighbors hold
that successor state. Their histogram kernel expands occupied chunks by one,
counts every byte-valued neighbor state, and reuses the transactional sparse
output/change-journal machinery without altering optimized binary paths
(D-GC5, D-GC10). A multi-state medallion seed gives every kind immediate interaction.
The classic cyclic rule follows Fisch, Gravner, and Griffeath's 14-color,
random-initial-condition experiment. Immigration and QuadLife use the same
B3/S23 population geometry as Life while inspecting all parent colors;
Immigration inherits the majority species and a three-color QuadLife birth
selects the absent fourth species. Both dense compatibility and sparse
production use the full-state histogram contract.

Larger-than-Life rules carry radius, optional center counting, inclusive birth
and survival intervals, and square, circular, or diamond (von Neumann range)
neighborhood shape. Range is bounded to 16 and B0 is rejected. Families with
more than two states add Golly's C-state decay trail: an active cell outside
its survival interval walks states 2..C-1 back to background, and trail cells
neither count nor accept births. The correctness-first sparse evaluator
expands each occupied chunk by the necessary chunk radius, copies each target
chunk plus its margin into a canonical halo window once, counts the state named
by `RuleSet::getExtendedCountedState` (state 0 for Larger than Life), and
commits through the existing transactional output/change journal. It does not
enter the radius-one candidate, halo, or memo fast paths (D-GC6), but its
target chunks share the worker pool (D-GC10). Shipped parameters match Golly's documented Bosco/Bugs,
Bugsmovie, and Globe examples.

Cyclic rules accept an optional `radius` (1..16) and `neighborhood`
(`square`, `circular`, `diamond`). Radius-one square rules keep the Moore
histogram path and their serialized form; wider rules are Griffeath's
long-range cyclic automata on the extended-range kernel, where each cell counts
its successor state and the threshold is bounded by the neighborhood size.
An optional `inert_background` removes state 1 from the cycle: the void never
advances and the phases 0, 2..n-1 cycle among themselves, so a seeded soup
stays inside its dish instead of invading the infinite background at up to the
neighborhood radius per generation. The shipped Griffeath rules set it; the
older full-cycle rules keep their behavior (D-GC8).

`von_neumann_table` families (at most 12 states) store rules as Golly
`@TABLE` text: `var` lines plus comma or compact `C,N,E,S,W,C'` transitions
with `none`, `reflect_horizontal`, `rotate4`, `rotate4reflect`, or `permute`
symmetry. Compilation produces a dense `n^5` lookup with Golly's semantics:
the first matching transition wins, a variable repeated within one transition
binds to one value, and unmatched neighborhoods keep their center. Table and
RLE starter text use Golly numbering, whose quiescent state 0 swaps with
Illumo background state 1. `sandpile` families encode heights 0..7 in the
background-zero form followed by pulsed grain-source phases; a cell holding
four or more grains topples one to each von Neumann neighbor, and the phase-0
source emits four without depleting (D-GC8).

`lenia` families (3..256 states) carry Bert Chan's continuous Lenia as
quantized intensity levels: background state 1 is level 0, states 2..n-1 are
levels 1..n-2, and state 0 is the full level n-1, so the value of a cell is
level/(n-1). Rules carry kernel radius R (1..16), time steps T, growth centre
mu and width sigma, one to four kernel ring peaks (beta), and polynomial,
exponential, or step kernel core and growth shapes. Compilation quantizes the
normalized ring kernel to integer tap weights (4096 per unit) and G(u)/T to a
fixed-point level delta per potential bin (16,384 bins, 1/256 level), using
only correctly rounded IEEE operations (a deterministic exp), so the control
store, every simulation lane, and the native oracle build bit-identical tables.
`NeighborhoodKind::WeightedKernel` evaluates the exact integer potential
sum of weight times level; the next level is
clip(level + delta, 0, n-1) rounded to nearest, matching Lenia's P-quantized
update. Growth that would raise empty space at zero potential is rejected so
the infinite canvas stays sparse. The sparse evaluator expands source
chunks by one chunk, reads each target through a radius-R halo window of
levels, and publishes through the same transactional journal as the other
correctness kernels; lanes partition it like any radius-16 rule. Species
starters use `lenia_rle` (Lenia's RLE, values 0..255 scaled onto levels);
parameters and cells come from Chan's MIT-licensed `animals.json` (D-GC9).

Hodgepodge rules expose 101 visible chemical levels. Healthy state 1 is the
sparse background; state 0 encodes infection level one, and numeric states
2..100 retain their conceptual levels. The Moore histogram supplies infected
and ill counts plus the weighted neighborhood sum for the Gerhardt--Schuster--
Tyson transition. RPSLS dominance uses that same histogram with five species,
two prey offsets per species, and a deterministic synchronous invasion
threshold (D-GC7).

Turmites, HPP gas, rule tables, and sandpiles use
`NeighborhoodKind::VonNeumannDirectional`. Its four
entries retain north/east/south/west identity rather than collapsing neighbors
into counts. Turmite state is `n` tape colors plus `4n` agent states;
simultaneous arrivals annihilate deterministically. HPP's 16 states encode four
particle-direction bits. Opposing pairs collide into the perpendicular axis
before all particles stream. The sparse evaluator expands source chunks by one,
reads each target through a one-cell halo window, and publishes through the
same transactional map and change journal as the histogram and extended-range
paths.

The histogram, directional, extended-range, and weighted-kernel (Lenia) models
share one chunk-parallel driver (D-GC10). Target discovery stays serial; each
target chunk is then evaluated independently from a per-worker halo window on
the grid's worker pool (up to eight workers once target cells times per-cell
neighbor reads reach 131,072, so wide kernels parallelize from two targets);
the change journal and the inactive-map publication then run serially in target
order, so the worker count never changes a generation. Elementary 1D computes
its next row in 65,536-cell blocks, splitting a block into 2,048-cell ranges
across the pool when it holds at least 16,384 cells, and writes the row
serially. The WASM control store and lanes compile the pool serially
(`ILLUMO_SERIAL_GUEST`); in the package, lanes supply the parallelism.

Rules may request deterministic glider, single-cell, wire, active-soup,
phase-soup, species-soup, excitable-break, Turmite-swarm, particle-cloud, or
exact `rle` starters; an RLE starter is validated against its family when the
rule compiles and is stamped centered on the origin. Omitted data keeps a
model-appropriate default. This replaces the former assumption that a Life
glider is meaningful for every Moore-count rule: Generations and
Larger-than-Life begin from reproducible active soups, cyclic systems from
mixed phases, and species rules from all declared live kinds.
The four-phase Generations family shares its state schema between Star Wars and
Nova Trails; the excitable-media rules share a five-state
excited/resting/refractory palette while varying their activation threshold.
Rule 90 / 184 use
`NeighborhoodKind::Elementary1D` and a serial space-time `SparseCellGrid`
advance (D-G2).
Catalog registration rejects B0 and odd elementary rules because sparse storage
requires a stable background. Strict JSON loading validates all entries before
publishing a replacement catalog; neighbor indices are 0..8, and elementary
numbers/palette channels are 0..255. Direct life-like counts above eight return
background without shifting.
Failed manual generations do not count; async failure preserves the published
world, pauses with a diagnostic, and retains one explicit retry without another
time-step charge. Elementary history and next-row writes stage in the existing
inactive chunk map and publish once, preserving live state on staging failure.
External-source binding is cleared on every exit, including empty-source advances.
At signed X endpoints, elementary rules use background for unrepresentable
neighbors and omit unrepresentable children. A source row at maximum signed Y
rejects advancement before changing grid contents or revision. Clipboard
capture/fill iterate bounded offsets, permitting small endpoint selections.
Pattern detection honors plaintext comments and retains plaintext rows for
ambiguous cell-only text. Explicit `rle`/`plaintext` commands choose their named
parser. Ctrl+V rejects invalid or occupancy-empty clipboard text without pasting
a previous internal pattern; console `paste` explicitly uses the retained buffer.
RLE exports preserve binary `o`/`b` and use project-extension `p{N}` byte-state
tokens. Parsing preserves old single-digit `pD` meanings; ambiguous historical
unbraced multi-digit exports require explicit correction rather than guessing.

```
class RuleSet {
  virtual unsigned char nextState(unsigned char cell,
                                  unsigned char aliveNeighbors) const;
  virtual unsigned char nextStateFromNeighborhood(
    unsigned char cell,
    const NeighborStateCounts& neighborStateCounts) const;
  virtual unsigned char nextStateFromDirectionalNeighborhood(
    unsigned char cell,
    const DirectionalNeighbors& neighbors) const;
  // State counted by extended-range kernels (0, or a cyclic successor).
  virtual unsigned char getExtendedCountedState(unsigned char cell) const;
  virtual unsigned char nextStateFromExtendedCount(
    unsigned char cell,
    unsigned int aliveCount) const;
  // Lenia: integer taps, per-state levels, exact potential -> next state.
  virtual const std::vector<KernelTap>& getKernelTaps() const;
  virtual std::uint32_t getKernelLevel(unsigned char state) const;
  virtual unsigned char nextStateFromPotential(
    unsigned char cell,
    std::uint64_t weightedSum) const;
  virtual void evalCell(const unsigned char& target,
                        unsigned char dest[3]) const; // palette colors
};
```

| Encoding | Values |
|----------|--------|
| Binary / life-like | `0` = alive, `1` = dead; neighbor count treats value `0` as live |
| Multi-state (e.g. Brian's Brain) | `≥2` additional states (e.g. dying = 2) |
| **Wireworld** | `0` head, `1` empty, `2` tail, `3` conductor (head = 0 reuses head-neighbor counting) |
| Cyclic ecology | `1` remains sparse background; all declared values can trigger their predecessor |
| Colorized Life | `1` background; every other declared value is a live species |
| Larger-than-Life | `0` active, `1` background, `2..C-1` decay trail; radius 1..16 extended neighborhood |
| Long-range cyclic | as cyclic ecology, or with an inert `1` void and phases `0, 2..n-1`; the successor is counted over a radius 1..16 shape |
| Von Neumann table | Golly table states with `0` and `1` swapped; `1` quiescent background |
| Sandpile | `1` empty; `0` one grain; `2..7` grains; `8..` pulsed source phases (`8` drops) |
| Hodgepodge | `1` healthy; `0` level one; `2..100` infection/ill levels |
| Turmite | `n` tape states followed by `4n` directional agent states |
| HPP gas | remapped 4-bit north/east/south/west particle occupancy; `1` vacuum |
| Dominance | `1` empty; all other states are competing species |
| Lenia | `1` level 0 (empty); `2..n-1` levels 1..n-2; `0` full level n-1; value = level/(n-1) |

**Generation path:**

1. Route full-state histograms, directional von Neumann, extended-range, and
   weighted-kernel (Lenia) rules to their isolated correctness kernels.
   Standard radius-one count rules continue below.
2. Read the cached stored/counting totals and candidate-preferred chunk count.
   If the retained changed frontier is empty, return immediately without
   visiting allocated chunks. Otherwise enroll each changed chunk and only the
   neighbors touched by its exact counting-change edge/corner bits. For
   sparse local sources, build target-local candidate masks and select candidate
   or halo evaluation independently.
3. Compare exact frontier preparation/evaluation work units with a complete-path
   estimate derived from cached totals. Evaluate and patch the frontier when it
   is no more expensive; otherwise use the complete path. Retain at most 16,384
   changed addresses between generations so tracking can resume after a dense
   burst; the work comparison, not that cap, selects evaluation.
4. Use the cached candidate-preferred count to select the complete
   adaptive or all-dense path.
5. If any source chunk is counting-sparse, derive affected target addresses
   from source counting-mask edges/corners and insert them into the retained
   generation-stamped flat index. Initialize neighbor counts only as candidate
   bits are enrolled. Build serial scratch source-by-source, or prepare large
   and explicitly parallel workloads target-by-target through the reusable
   pool. Select candidate or direct 18×18 halo evaluation separately
   for each target from its actual counted-neighbor contribution work.
   Counting-dense centers skip neighbor-count scratch preparation.
   Dense-majority frontiers and frontiers with at least 2,048 targets skip
   candidate scratch construction.
   Large mixed work sets use coarse work-count ranges in the worker pool.
6. If every source chunk is counting-dense, bypass candidate scratch. Build the
   expanded target set in its retained generation-stamped flat index, retain
   address/result vector capacity, and evaluate serially or through the bounded
   pool for 32+ targets. Construct rolling neighbor rows directly from chunk
   counting masks without a temporary 18×18 byte halo.
7. For at least 32 halo targets, sample an exact sharded 18×18-state memo. Use
   it only after the observed hit rate pays for key construction, and bypass or
   cool it down on unique neighborhoods. Invalidate on transition changes.
8. Build the complete next map serially using retained buckets and recycled
   chunk nodes. Construct sparse candidate output directly in mapped node
   storage; keep dense halo output as a bulk array copy. Compare to the
   authoritative map, then swap transactionally only when contents differ.
   Empty space is implicit, so births never wrap.

Before evaluation, the active ruleset lazily materializes all 2,304 transition
results. Hot loops share that immutable table, eliminating virtual dispatch and
repeated rule branches after the first use. Rules stay free of rendering and
input. `evalCell` supplies palette/RGB colors only.

**Implemented in D-GC2/D-GC3:** data-driven rule families compile into the
common `DataRuleSet`; a typed family selector and independent ruleset identity
make customization explicit without changing serialized IDs:

```json
{ "family": "life_like", "birth": [3], "survive": [2, 3] }
```

JSON describes **data**, not executable behavior. Multi-state (Wireworld, Brian's Brain) stay separate families.

### 5.8 Simulation vs rendering

1. Simulation advances on a **tps × speedFactor** clock. One due generation is
   guaranteed; a measured 4 ms slice gates the optional second generation.
2. Rendering runs every frame.  
3. Double-buffer prevents reading a half-written generation.  
4. Visual fade is **presentation**, not simulation state.  
5. Modes in `CellGameModule`: `NORMAL | EDIT | EXIT` (enum + switch; save/load are registered commands, not frame states).
6. F1 opens the Release-visible `ConfigurationMenu`; while open it owns product
   input and blocks simulation/editor mutation.

Fixed-tick **mesh transform interpolation** (engine PDF) is **not** required for the CA product.

### 5.9 Input

Intended flow:

```
GLFW callbacks / poll → InputManager → module / controller logic
```

**Today:** InputManager holds key/mouse state and contexts. `DebugModule` consumes Grave and open-console editing first as a global Debug overlay; product modules then read remaining events and yield while the console is open. Unconsumed key/char events are discarded at the end of the host update. Not every behavior is extracted into tiny controller classes—acceptable.

`CellGameModule` owns a primitive-composed F1 settings overlay in every build.
Its persisted `editHints` option defaults on and controls a wrapping bottom
legend in Edit mode, including Wireworld brush keys. The flat menu-themed footer uses muted text
and shows selection actions only when selected. Its opaque bottom band is
excluded from canvas drawing and pointer input; disabling hints releases it.
Canvas clipping uses backend-neutral scissor tokens without shifting camera
coordinates and restores scissor state before drawing the UI. Selection drags finish
on mouse release; releasing Shift first does not start painting. Painting,
erasing, or leaving Edit clears the selection while preserving the copied
pattern. Clipboard hotkeys act only in Edit; explicit console commands remain
mode-independent. Modal overlays and the console hide the legend and selection
outline and interrupt active paint/selection drags.
Its settings sit in Simulation, Video, Audio and General tabs with shared
Apply/Discard/Exit footer buttons, across six tabs (Simulation, Canvas,
Video, Audio, Controls, General). It edits family and its filtered ruleset,
world chunk width/height, TPS, simulation speed, start paused, cell style
(LED keys or flat), cell glow, grid lines, fade speed, simulation inspector,
edit hints, VSync, fullscreen, restart-only MSAA, FPS cap, the corner FPS
counter and memory readout, sound volume, zoom sensitivity, invert zoom,
keyboard (arrow) pan speed, UI scale, reduced menu motion, software cursor,
autosave interval and confirm clearing (D-UI12). Numeric
settings are sliders over fixed stops (keyboard steps or pointer drag, with
typed digits as an exact-entry shortcut); UI scale is a slider from Auto
(sized to the window, D-UI11) through 1x-4x, and MSAA is a segmented picker. FPS cap uses the existing engine `fps` setting and
frame pacer: 0 (the slider's far right) disables software limiting and VSync
remains independent.
Both main-menu and in-game Apply persist display preferences and apply fullscreen
immediately. Main-menu F1 opens the same settings overlay. Positive dimensions apply finite toroidal topology;
`0`/`0` or `inf`/`inf` applies infinite topology. Topology changes drain the
worker and intentionally start a fresh centered world before persisting values.
Larger high-contrast labels, readable ruleset names, keycap hints, and a
selected-setting explanation keep the Release surface legible. Q and its Exit
action open a confirmation overlay; confirming requests window closure so the
Illumo application runner performs normal engine shutdown.
F2 opens the separate Ruleset Workshop in the canvas. Family and transition
drafts are independent, and every ruleset remains bound to one family. Family
edits own cell-state names, colors, and state count; ruleset edits own identity
and transitions. The family-aware form previews a representative transition
and state palette, and imports or exports family/rule packages. Save & Apply
validates and persists the family before its referencing rule, drains the
simulation, rejects state-schema changes that invalidate live cells, then
activates the pair. Cyclic families expose successor threshold and coprime cycle
step controls with a successor-neighbor preview. Moore transition tables remain
JSON-edited rather than expanding the UI into a large matrix editor.
Animation remains local value state: the overlay eases into place, rows reveal
in sequence, selection glides, and changed values pulse without adding widgets
or blocking input. The mouse wheel scrolls the viewport without changing the
selected row; an offscreen selection has no visible highlight. Keyboard navigation
keeps the selected row visible, including Page Up/Down and Home/End. Rows retain
readable height; drawing and pointer conversion share a fitted UI
scale. A held opening click is consumed until release.

The menus share one "living glass" visual language (D-R26). Panels, cards and
buttons are soft-shadowed glass: a lit rim over a vertical-gradient face, a
breathing outer glow, and a cyan-to-violet accent hairline with a travelling
glint. Overlays sit on a radial scrim rather than a flat backdrop. Motion is
liquid and a little bouncy (D-UI8). Selection is a drop of liquid
(`GuiKit::drawLiquidSelection`): its head and tail are two springs, so the head
pours toward the new row and overshoots while the tail lags, the drop necks in
the middle and tapers to a smaller tail like a teardrop, bulges as the tail
catches up, and keeps its momentum when redirected mid-flight; at rest it is an
ordinary rounded pill, and a sheen sweeps it on arrival. Panels and rows drop
in on springs and bounce just past their resting places. Rows lean in (label
slide, tile growth, glow) on jelly-like emphasis springs; toggle knobs stretch
like droplets as they boing across; count chips, footer buttons and scrollbar
thumbs travel on springs; changed values bloom and spring the way they moved.
Every glass panel (title screen, settings, canvas setup, Ruleset Workshop and
the rounded pause/exit dialogs) swivels toward the pointer through
`GuiPanelTilt`: layers shift by depth (glass least, then footer, body, header,
and accents such as the glider motifs most), the glass takes the tilt for its
shadow, glare and edge light, the layout origin carries the body shift so hit
testing follows the drawing, and a panel swings level when it is not the
pointer's target.
Footers use keycap hints. The title screen runs a real Immigration Life world
behind the glass: a Gosper gun streams cyan gliders, coral acorns churn, and a
deterministic generator launches gliders and spaceships in from the edges every
few seconds. The world is reseeded when it grows past 1,400 chunks or after 15
minutes, and it never replaces the player's saved ruleset preference. A radial
vignette and three drifting soft glows, which squeeze and swell like lava-lamp
blobs, lean against the pointer (parallax), a soft spotlight follows it lazily
and smears along its motion, the glass panel swivels toward the pointer (its
layers shift by depth, from the glass up through the rows, title and the
nearest glider motif; the shadow slides away, a glare follows the pointer and
the facing edges catch the light via `GuiGlassStyle::tiltX`/`tiltY`; rows are
hit where they are drawn, and the panel swings level behind an overlay), the
panel dims while an overlay is open, the CSIM
letters fall in like drops and bounce as they land, then float on a slow
swell, a real glider walks the 7x7 motif torus (`CellMotif`, shared with canvas
setup) whose newborn cells pop in as bouncing beads, menu icons animate with
emphasis, and a pressed row wobbles like jelly while a splash (a ripple ring
and teardrop droplets, `GuiKit::drawSplash`) leaps from the press. Actions are
never delayed by feedback animation, and overlays close immediately (a closed
overlay emits no commands). The pause dialog, the F2 workshop, the cell-paint
drawer, the inspector card, the hamburger button, the edit-hint footer and the
corner EDIT/NORMAL badge (`ModeBadge`, a glass pill mirroring the hamburger
button that drops in as a bead, stretches into a pill, holds, then melts
away; it replaced the simulator's `SplashText` label) use the same chrome; the
drawer and the hint footer stay opaque.
`GuiKit` supplies the chrome: `drawRoundedRect` (three rectangles plus packed
two-wedge corner quads), `drawRoundedGradientRect`, `drawRoundedBand` (the
shared core of outlines, soft shadows and glows), `drawSoftShadow`,
`drawSoftGlow`, `drawVignette`, `drawSheen`, `drawGlassPanel`
(`drawRoundedPanel` forwards to it), `drawLiquidSelection` (non-overlapping
slices across the travel, so translucent faces stay even), `drawSplash` and
keycap hints. Rounded corners subdivide by radius: 15-degree steps through
radius 15, finer beyond (up to 24 per quarter), so large radii such as the
paint bubble stay round. Line icons use `drawPolyline` (mitered joins and
optional square caps, one quad per segment) and `drawChevron`, never
separately drawn line segments, whose butt ends leave notches at corners.
The canvas chrome (paint drawer, inspector, hamburger and its hint) follows
the menus: card faces, the liquid drop for the chosen brush, keycap hints,
spaced-caps eyebrows and label/value rows.

CSim's type is Kikuta, a variable-weight face (D-UI9). The host font service
resolves `kikuta:<weight>[:<glyphs>]` (weight 1..1000, an optional subset of
up to 32 printable ASCII glyphs) by instancing the face's `wght` axis
(`FontFaceOptions`) and rasterizing the printable ASCII Kikuta lacks
(`# $ % * + < = > ^ _` and braces, among others) from Space Mono of a similar
weight; the guest ABI is unchanged. `CSimTypeface::install` runs during package
bootstrap: Kikuta 400 becomes the default font and weights 400/600/800/1000 at
the 32 px raster become the UI `FontWeightRamp`. A ramp maps any weight to the
two loaded samples around it; `TextPrimitive::heavyFont`/`heavyBlend` overlay
the heavier sample at the fraction between them with interpolated advances, so
weight animates continuously from a handful of atlases. `GuiKit`'s
`drawEmphasizedText`, `drawEmphasizedTextCentered` and `measureEmphasizedText`
map a row or button's emphasis spring onto that ramp (400 at rest, 800 focused,
heavier through the jelly overshoot) in the title rows, settings, canvas setup,
Ruleset Workshop rows and action buttons, and the glass dialogs; without an
installed ramp (IllEd, the viewer, the native test oracle) they draw plain
text. The CSIM title uses its own ramp rasterized for just those four glyphs
at eight weights from 200 to 1000, packed at 60-unit steps across the
resting word's 590..810 breath so blended weights never show a soft double
edge (eight per size keeps all three raster sizes inside the guest's 32-font
budget), and each letter also squashes and stretches
(`TextPrimitive::stretchX`/`stretchY`, scaled about the run's left edge and
baseline so feet stay on the line). Letters fall thin and tall and splat heavy
and squat on impact (the landing spring's overshoot carries them past their
700 rest), a weight swell rolls through the word with the bob, and letters
near the pointer pool heavier and puff up. Once the word has landed, one
letter at a time strikes a random pose every 0.55--1.55 s on its own springs:
it flexes black and squat, slims to a hairline and grows tall, or hops
(thinning in flight, squashing heavy as it dips back past the line, with a
glow pooling on the underline beneath it), holds briefly and springs back
while its neighbours get jostled; now and then a wave of hops ripples out from
it across the word. Wider letters shoulder their neighbours
aside, but a hop's own flight and impact weight and squash shape only the
hopping letter about its cell's center; laid out, that ringing spring shook
the word sideways. Clicking the word sends a staggered hop through it; poses rest while an
overlay is open. Reduced motion holds the title still at rest weight. The developer console follows the default font.

Fades target the same
color at zero alpha (`UiTheme::transparentOf`) so straight-alpha blending never
darkens an edge. `GuiDialog` provides opt-in rounded presentation with a fitted
visual/pointer scale; its default flat presentation remains available to other
apps unchanged.
`MainMenuModule`, `ConfigurationMenu`, `NewSimulationMenu`, and
`RulesetWorkshopMenu` share one motion and layout vocabulary through
`GuiMenuShell` rather than repeating it: the reveal, springy panel and row
drops, the liquid selection span (head and tail springs with a squash
response), arrival sheen, press pulse and jelly wobble, directional value pulse
and nudge, ambient cycle, and caret blink come from `GuiMenuAnimator`; physical
motion comes from `GuiSpring` and `GuiSpringArray` (closed-form damped springs
that are stable at any frame time and snap exactly at rest) tuned from the
shared `GuiMotion` presets (`kJelly`, `kBoing`, `kLiquidHead`, `kLiquidTail`,
`kSwell`, `kGlide`, `kDrift`, `kSway`); the pointer tilt comes from
`GuiPanelTilt`; easing curves (`outCubic`, `outBack`,
`inOutCubic`) and clock-driven spring shapes (`springStep`, `wobble`) come from
`GuiEasing`; the fitted virtual
space, visible-row window, and wheel scrolling come from `GuiPanelLayout`; hover
and press edges come from `GuiPointerTracker`. Each overlay still owns its own
layout constants, rows, and `GameVisual` composition. The title screen keeps
its own slower entrance clock. The game advances pause-dialog time once per
frame, and submission refreshes the visual even while input yields to the
console. `reducedUiMotion` snaps every clock and spring and disables decorative
motion (sheen, ripple, splash, wobbles, bounces, title bob, parallax,
spotlight, background visitors, motif crossfades, dialog breathing), including
pause/exit transitions; it does
not change domain simulation or cell fading. `showInspector` loads at product
startup and applies to the existing inspector drawable.

The large main-menu title is drawn letter by letter from a separate font atlas
at 64, 128, or 256 pixels, selected to cover its fitted display size at the
letters' peak (overshoot) size without upscaling glyphs. Smaller labels retain
the default font atlas; resizing reuses the bounded title sizes.

New simulation and the menu console command `play` open a dedicated canvas
setup screen, independent of F1 configuration. It offers the rules catalog,
infinite or wrapping boundaries, width and height in 16-cell increments, and
empty or starter contents. Infinite mode disables dimensions while retaining
the finite draft. Create passes validated canvas values to CellGameModule;
Back or Escape discards the draft. Display and performance preferences are not
part of this payload. The screen fits all rows, respects reduced menu motion,
and consumes wheel input without changing selection or values. Glass cards,
spring-driven focus lighting, the liquid selection drop, springy directional
value nudges, a crossfading boundary badge, and a live glider on a 6x6 torus match
the main menu; reduced motion freezes the motif and snaps focus feedback.

Entering a new or loaded cell canvas plays a 0.9-second cellular wave. A
module-owned, screen-space GameVisual veil tiles the viewport with cells of
roughly 44 virtual pixels, opaque and dark at first. A wave spreads from the
center with a deterministic, per-cell ragged front: each cell fires cyan as the
wave reaches it (with a soft halo), cools to violet, then shrinks to a dot and
fades, revealing the world. Its cell count follows the viewport and UI scale,
and it retires after completion. It covers the canvas and HUD but remains beneath
settings and confirmation dialogs. The transition never changes camera or
simulation state and does not block input. Reduced menu motion and the 3D
diagnostic view skip it from the first frame. Returning to the main menu runs
the wave backwards over 0.48 seconds (cells regrow from the edges inward, fire,
and darken until the canvas is covered), then submits one module transition. Repeated return
requests do not restart it; product input is consumed during the accepted exit,
while the global console retains its input. Reduced motion returns immediately.
Both the pause-menu action and the console menu command use this path.

**D-E2:** InputManager must not depend on Game types.  
Callbacks should record events/state, not own game policy long-term (CA design PDF — still the direction of travel).

### 5.10 Developer console

The Debug/RelWithDebInfo console is a global overlay on the main menu, settings, confirm
dialogs, and cell canvas. It separates general tooling from product behavior:

- `CommandLine` owns help, environment-variable inspection/editing, validated
  finite timing/display settings, console history, alias macro management, multi-command chaining, and application exit.
- `CellGameModule` registers simulation, canvas, camera, ruleset, and save/load
  commands through `CommandRegistry`; registry metadata drives help and Tab
  completion.
- Registered commands execute in a detached batch. Reentrant enqueue is deferred
  to the next top-level dispatch; retirement cancels unstarted callbacks and
  clearing cancels the batch remainder. The non-copyable registry and input
  manager retain instance-local ownership. See [Services](packages/services.md)
  for exception and configuration recovery contracts. Failed configuration
  loads preserve live values and original bytes, disabling teardown saves until
  a successful reload.
- Save always writes version 4 sparse records (magic/version, family, ruleset, camera,
  topology, deterministic sorted canonical chunks). Load validates into
  temporary state, accepts versions 4, 3, and 2 plus the prior dense format,
  treats older formats as infinite, imports legacy cells centered at the origin,
  derives family IDs for pre-v4 saves, then restores the validated pair/camera
  and rebuilds the bounded view.
- `vid_restart` is not advertised: safely recreating an OpenGL context requires a
  complete resource re-enrollment design, so the old no-op now reports that limit.
- Editing supports measured caret placement, selection, Home/End, Delete,
  Ctrl+word movement/deletion, Ctrl+A, Ctrl+C/X/V (selection or whole input;
  pasted line breaks become `;`), quoted arguments, history, and horizontal
  input scrolling. PageUp/PageDown scroll output by a page, Ctrl+Up/Down by a
  line, and Ctrl+Home/End jump to either end; a scrolled-back view stays
  anchored while new output arrives.
- The console is deliberately plain (D-UI4): its own flat terminal palette
  instead of `UiTheme`, a header, output anchored above the input line, a
  one-line status bar (FPS and frame time, line counts, active filters, scroll
  position, and the usage or completion hint), and a completion list for
  ambiguous Tab. There is no full-screen shade, shadow, or pulsing accent; only
  the open/close slide animates.
- Output entries record a level, a timestamp, and a repeat count; identical
  consecutive lines collapse to one entry with `(xN)`. The buffer holds 2,048
  entries and command recall 256. `filter <text>|off` and
  `loglevel trace|info|warning|error` hide entries without discarding them;
  `timestamps` adds a dmesg-style gutter; `copy [<n>|all]` and
  `savelog <file>` export output; `exec <file>` runs a command script (blank
  lines and `#` comments skipped, 1,000 lines, nested depth 8); `!!`, `!<n>`,
  and `!<prefix>` recall history; `help <word>` searches names and
  descriptions. `bind <F1-F12> <command>` runs a command on a function key
  whether or not the console is open (F3, F5, F6, F11 stay host-reserved);
  `DebugModule` consumes only bound keys. `add <var> <n>` and
  `cycle <var> <values...>` adjust settings (useful behind binds);
  `watch`/`unwatch` pin up to eight variables to a live strip under the
  header; `writeconfig <file>` saves aliases, binds, watches, and view
  settings as an `exec` script.
- Ctrl+R starts a shell-style reverse history search (repeat for older
  matches, Enter runs, Right accepts for editing, Escape cancels). Clicking an
  echoed command in the output recalls it. Warning and error lines carry a
  thin severity mark, filter matches are highlighted, and the status bar
  counts lines that arrived while scrolled back. While the console is closed,
  errors and warnings raise a small top-right count badge (`alerts` toggles
  it); opening the console acknowledges them.
- Multi-command chaining splits on `;` (preserving quotes and escape sequences).
- Alias macro management (`alias`, `unalias`) expands user-defined command shortcuts (with recursion capped at depth 8) and integrates aliases into auto-completion.
- Inline ghost-text auto-suggestions display faint completion candidates after the caret; pressing Right-Arrow or Tab accepts the ghost text.
- Window mode supports switching between top-mounted and floating modes (via `console_mode [floating|mounted|toggle]` or double-clicking the console title bar). In floating mode, title-bar dragging repositions the window across the screen, and dragging the bottom-right corner grip handle (or running `console_size <W> <H> | reset`) dynamically resizes the console window with real-time UI bounds clipping (D-UI3). Those two commands capture the live `CommandLine` and are unregistered when it is destroyed.
- Dynamic parameter syntax hints dynamically render usage instructions in the status bar while typing known commands.
- Utility commands include `repeat <N> <command>`, `history [filter|clear]`, and the `sysinfo` telemetry dashboard. The simulation-provided `status` command reports simulation, canvas, ruleset, and camera state.
- Console chrome uses a single heap-backed batch with capacity for 8,000 UI
  quads. History wraps to panel width and scrolls by visual lines, sharing
  mounted/floating layout metrics (D-UI2). Wrap metrics are cached until
  history contents or panel wrap width change; only the visible window is
  tessellated; entries hidden by view filters wrap to zero lines. Settled
  composition is replayed until history, input, scroll, layout, caret phase,
  the reported FPS, or a watched value changes, so idle frames `DrawIndexed`
  without a new `UpdateBuffer` (D-P2). The closed-console badge replays the
  same way until its counts or the window size change.
- The console can pop out into its own OS window (D-UI5): the header's
  `pop out` button, `console_mode detached`, or dragging the floating title bar
  past the game window's edge. `dock`, closing the window, or `` ` `` from
  either window brings it back (dock reopens it in-game; the others return it
  closed). `DebugModule` owns the `GLFW_NO_API` `PixelWindow`, routes its
  keyboard/mouse events to the console, and presents a `SoftwareCanvas` raster
  of the console's `GameVisual` primitives (Windows GDI; Linux reports it
  unavailable). No second OpenGL context exists; while detached the console is
  not `isOpen`, so the game keeps its input and no in-game console draws.
- WASM guests compile the same console as a log bridge. Clipboard and file
  access are compiled out under `ILLUMO_SERIAL_GUEST`; the bridge forwards
  collapsed repeats, and the host restores the success level for guest
  `SUCCESS:` lines.
- `Logger` may mirror output into the console while services are alive. The host
  clears that non-owning logger context before destroying the services.
  `Illumo` hands the logger its settings as soon as they load, so the
  configured `logLevel` (runtime default 3, info) applies from window and GPU
  startup onward. Messages logged before the first console attaches are kept
  (at most 256, then counted) and replayed into it once; later detach/attach
  cycles never replay. Each session opens `log.txt` with its start time and
  build, and each file line carries a wall-clock timestamp.
- The logger is safe to call from background threads: file, terminal and
  backlog writes are serialized, but only the owner (main) thread forwards to
  the main-thread-affine console or reads the level from settings; other
  threads reach the file and terminal with the last level the owner read.
- Startup logs a report of the build and machine (`QuerySystemInfo`: OS,
  architecture, CPU, core/thread counts, physical memory), then the display
  mode, GPU, driver and context limits, audio output, package, mounts, guest
  budgets and granted capabilities. Subsystems log lifecycle milestones at
  info, behind-the-scenes detail at trace, and recoverable or silent failures
  as warnings or errors. Guests' `LogTrace` stays trace on the host. The
  runtime sends terminal log lines to stderr so stdout carries only the
  `--capture`/`--bench` JSON.

#### Process memory diagnostics

The optional Debug/RelWithDebInfo `DebugModule` owns one top-left `GLString`
diagnostics panel. `showFPS` / F3 / `fps` retain FPS-only control;
`showMemory` (default off) / `memory [on|off|toggle]` independently control
memory rows. Both settings persist through the existing environment service.
The console help, completion and `sysinfo` expose memory visibility.

An internal `DebugOverlayState` owns independent FPS and memory refresh clocks
and cached text; only changed content rebuilds the label. Memory sampling occurs
on enable and once per second while visible, on the main thread. Hidden memory
performs no queries. Failure replaces old values with `Memory: unavailable` and
retries normally. No background worker or allocator instrumentation is added.

The public platform value `ProcessMemoryStats` contains resident, lifetime-peak
resident and private-commit byte counts. `QueryProcessMemoryStats` returns false
and clears its output when unavailable. Windows implements it with
`K32GetProcessMemoryInfo`; existing non-Windows scaffolds return unavailable.
Win32 headers remain in Platform/Windows. Values cover the whole process and
are formatted in MiB with one decimal place; private commit is not resident
memory, and resident memory includes shared pages. GPU and system memory are
outside this diagnostic's scope.

The public platform value `SystemInfo` (operating system, architecture, CPU
name, physical cores, logical processors, total and available physical
memory) feeds the startup log. `QuerySystemInfo` never logs. Windows reads
`RtlGetVersion`, the registry's processor name and release, processor
relationships and `GlobalMemoryStatusEx`; other platforms report only the
logical processor count until a native port is verified.

### 5.11 Window / platform boundaries

- Semantic window ops (`shouldClose`, poll, swap, title, dimensions) justify thin wrappers even if one-liners — they hide GLFW types from the main loop.  
- OpenGL calls stay under `Rendering/OpenGL/` (+ window bootstrap).  
- Interfaces only where multiple implementations or third-party volatility are real (`IBackend`, `IRenderWindow`).  
- macOS is not a current target; no Cocoa or other Apple platform scaffold is retained.
- Fullscreen transitions preserve the windowed position and dimensions, enter
  the primary monitor at its current video mode, and restore the saved windowed
  bounds on exit.

### 5.12 Isolated WASM application runtime (D-E13 to D-E17)

IllumoGame, IllEd and IllMeshViewer are WASM packages, not native
executables (D-E14 extends D-E13 to every interactive client program). The
shipped layout is:

```text
IllumoRuntime.exe              generic host: loop, window, GL, Wasmtime sandbox
  envvars.json                 runtime settings (window, vsync, fps, overlays)
  apps/game/illumo.json        package manifest (see below)
  apps/game/IllumoGame.wasm    the whole product
  apps/game/CSimWorkerGuest.wasm  simulation lane worker (one store per lane)
  apps/game/families.json ...  packaged catalogs and first-run defaults
  apps/illed/illumo.json       IllEd.wasm; launchAccess "edit"
  apps/illed/Assets/IllEd/editor-ui-atlas.jpg      package-preloaded asset
  apps/meshviewer/illumo.json  IllMeshViewer.wasm; launchAccess "read"
  apps/meshviewer/Assets/Skybox/skybox-daylight.png  package-preloaded asset
  packages/                    extra packages (directories or .ilpk), mounted at
                               /packages/<id>
  storage/csim/                game settings, saves, user rule overlays
  storage/illed/               IllEd private storage
  storage/meshviewer/          IllMeshViewer private storage
```

Each package's `illumo.json` (`format: "ilpk"`, `format_version: 1`)
carries `id`, `version`, `title` (the window title) and `kind: "app"`; its
`app` section names `module`, optional `worker`, `launchAccess` (`"read"` or
`"edit"`), `metering` (`"fuel"`, the default, or `"epoch"`) and requested
`memoryMiB`, `fuelPerCall` (fuel only) and `deadlineMilliseconds`; with a
`worker`, `workers` (lanes), `workerMemoryMiB` and
`workerDeadlineMilliseconds`. Requests are clamped to host ceilings (lanes:
at most 8 and two fewer than the hardware threads). The decoder is
`decodePackageManifest` (`Illumo/Content/PackageManifest.h`). The manifests
live in the source tree as `IllumoGame/illumo.json`, `IllEd/illumo.json` and
`IllMeshViewer/illumo.json`; all three are epoch-metered, and the game requests
eight lanes. A package may be a directory or an `.ilpk` archive; the host
mounts it at `/app` of its virtual file tree (with `packages/`, `--mount`
and `--project` mounts beside it) and serves the guest's Package area and
module bytes from there (`docs/content-packages-and-scenes-design.md`). The old `game/` directory and `game.json` are gone.

- **Engine modes** (D-E15): compiled code depends on `WasmEngineOptions`
  (`meterFuel`, `explicitBounds`), which the host passes to the isolated
  compiler so artifacts always match the deserializing engine (a mismatch
  fails closed). Explicit guest bounds checks are selected by AddressSanitizer
  (`ILLUMO_ASAN`, `ILLUMO_ENABLE_ASAN=ON` in Debug), not by `_DEBUG`, because
  Windows ASan's shadow-page handler can recurse into Wasmtime's trap handler;
  every other build keeps signal-based traps. Epoch-only stores carry no fuel
  instrumentation and are bounded by their per-call deadline; mods stay
  fuel-metered. `--fuel n` forces metering for one launch.

- **Host** (`Illumo/Source/Wasm`): `WasmGameModule` snapshots input, pumps
  generic services (textures, fonts, files, dialogs, clipboard, display and
  its system-cursor visibility, console trampolines, jobs, audio) and submits
  validated `IRF1` frames through
  `WasmFrameRenderer`. It contains no Game/Rulesets includes and no CA
  opcodes. The manifest requests memory, metering, deadline and lane budgets;
  the runtime clamps them to its ceilings. Launch options are cleared before
  command-line parsing so persisted settings never replay a package. When an
  update reports `GuestUpdateFlags::ServicesPending`, the host runs a second
  services exchange before the frame, so requests queued by that update
  (compute lanes especially) start while the frame renders. `WasmFrameStats`
  (exchange timings) and `WasmFrameCounters` (batches, inline bytes, texture
  and mesh writes, slot allocations) are shown by the host-owned
  `wasm_stats` console command.
- **Runtime command line** (`Illumo/Source/Wasm/RuntimeApplication.cpp`):
  `--app <name>` runs `apps/<name>` (default `game`; not combinable with
  `--package` or `--game`). `--open <file>` hands one document to the app: the
  guest sees only the base name (the `ILS1` startup record `GuestLaunch`), and
  the host grants the file as the selection `launch`, writable when the
  manifest says `launchAccess: "edit"`. `--capture <new .png>` renders until
  `--capture-frame <n>` (default 60), reads back the presented backbuffer
  through a `Renderer::setBeforePresent` hook, writes the PNG, prints one JSON
  line and exits 0/1 through `IllumoApplicationDefinition::exitCode`; it
  replaces the removed `IllumoCapture` executable ([frame-capture.md](frame-capture.md)).
  `--capture-script <file>` runs bench-script steps first so a capture can
  reach a later screen; the capture frame counts from the script's end, and the
  saved frame is forced opaque to match the presented window.
  `--bench-frames <n>` (with `--bench-warmup` and `--bench-script`) runs a
  scripted, timed session and prints one JSON line of frame and exchange
  statistics (see the same document).
  The runtime works from its own directory wherever it is started, relative
  command-line paths resolve against the invocation directory, and the window
  title comes from the manifest through `IRenderWindow::setTitle`.
- **Guest engine** (`IllumoGuest`): `GuestModuleApplication` builds a
  guest-local `IllumoContext` (snapshot window, `InputManager` fed from the
  snapshot, storage-backed `GuestEnvironment`, `Camera`, `Renderer` on
  `GuestRecordingBackend`, `Scene`, a `CommandRegistry` mirrored to host
  console trampolines, a `CommandLine` whose lines forward to the host
  console, and a module host applying transitions at frame boundaries). It
  renders with `Renderer::RenderScene`; `IBackend::BeginLayer` tags World and
  UI batches. Product close requests travel as update flag bit 0; the host
  still asks `illumo_guest_close`. It also sets `context.assetManager`:
  `AssetManager` reads bytes through an `IAssetSource`
  (`Illumo/Rendering/AssetSource.h`), served in the guest by
  `GuestPackageAssets` from the files a product lists in `packageAssets()`
  (preloaded before bootstrap). Under `ILLUMO_SERIAL_GUEST` (defined for all
  guest code) `AssetManager` has no worker thread, completes loads in
  `pump()`, and shader files are unavailable (shaders are host policy);
  natively `FileAssetSource` keeps filesystem behaviour and hot reload. Every
  app's package `envvars.json` supplies first-run defaults before
  `applyDefaults`, `launchFile()` exposes the `--open` document, and
  `GuestDocuments` (`IllumoGuest/Documents.h`) gives document products
  open/edit/save dialogs and read/write by opaque location. Dialog requests
  are version 2 (adds `edit`, an open whose selection stays writable for
  save-in-place); results add a base-name-only `label`.
- **Product** (`IllumoGame/Source/Wasm`): the package application bootstraps
  storage settings plus packaged defaults, reads catalogs through
  `CSimCatalogBootstrap`, installs `GuestCSimPlatform`, loads its sound cues
  when granted `Audio` (section 5.13), and starts `MainMenuModule`. Shared Game sources use `CSimPlatform` for dialogs, file
  transfers, clipboard and user-catalog writes; the native oracle completes
  synchronously, the guest on a later update. IllEd
  (`IllEd/Source/Wasm/EditorApplication.cpp`) and IllMeshViewer
  (`IllMeshViewer/Source/Wasm/ViewerApplication.cpp`) follow the same shape
  through their `IllEdPlatform` and `MeshViewerPlatform` seams. IllEd's
  save/open/close-confirm are asynchronous, its document keeps an opaque
  location plus a display label, and editing input is held while a transfer
  is in flight. Scenes load in three steps: collect references, fetch them
  into the guest asset cache (`GuestSceneFetches`), then instantiate. The
  viewer parses loose meshes with `MeshLoader::loadFromMemory` and opens
  scenes from any mount (`viewer_open`). `IllEdCore` and
  `IllMeshViewerCore` remain only as native test oracles.
- **Packages and files** (D-E20 to D-E24): the host mounts `/engine`, `/app`
  (plus overlays), `/packages/<id>` (from `packages/` and `--mount`) and a
  writable `/project` (`--project`) on one `VirtualFileSystem`. File protocol
  v2 adds the `Mounted` area, `List`, `Stat`, `Import` and `Pack`, 1 MiB
  mounted blocks and 16 guest tasks; `ProjectFiles` (bit 9) gates writes,
  Import and Pack and is offered only with a project. The host `vfs` command
  explores the tree, and in debug builds `files` browses it
  (`IllumoContext::fileTree`). `GuestVfsAssets` serves guest asset bytes
  from pinned preloads, held fetch sets and `/local` entries under an LRU
  budget. IllumoGame merges `/packages/<id>/csim/*.json` catalogs before the
  player's overlays.
- **Frames**: schema version 2 adds the world camera, line and depth-tested
  batches, lit meshes (normals plus per-batch lighting) and shadow casters.
  The guest records its own shadow pass against a virtual depth target to
  learn which meshes cast; the host registers the casters, fits its shared
  shadow pass to the guest camera (`Renderer::setNextWorldViewProjection`)
  and draws lit meshes through the built-in `LitMesh` style. Schema version
  3 (`IllumoGuest/Frame.h`) adds retained host meshes (`CreateMesh`,
  in-order `WriteMesh` chunks of at most 1 MiB, `ReleaseMesh`; a batch naming
  a `mesh` id draws `indexCount` indices from `firstIndex` with no inline
  geometry), cubemaps (`CreateCubemap`, six square RGBA faces in +X,-X,+Y,-Y,
  +Z,-Z order, drawn by the `Skybox` batch style through the built-in skybox
  style with a rotation-only view projection) and a per-batch blend flag. The
  guest recorder retains static meshes of at least 64 KiB; dynamic and small
  meshes stay inline. The host validates a complete mesh (layout stride,
  finite positions, index range) when its last byte lands, enrolls a static
  GPU mesh, and charges the bytes to the owner's 256 MB resident budget.
  Cubemaps cannot be sampled as 2D textures or written through frames.
  Motion blur and MSAA stay host policy. Version 1 and 2 packets remain
  valid and keep their historical blend defaults.
- **Frame schema v4** (D-E16): dynamic 2D meshes (Shape, Sprite, Canvas; at
  most 4 MiB per buffer) are retained on the host too. `CreateMesh` with the
  `dynamic` flag returns a mesh that is ready and zero-filled at once; the
  frame's trailing `meshWrites` section (at most 4,096 writes and 32 MiB)
  patches it in place before any of the frame's batches draw. The host
  validates each write (whole vertices with finite positions and Canvas
  colors, whole indices inside the vertex capacity) into a CPU shadow and
  uploads only dirty spans before the next draw. The guest records only the
  span that actually differs from its mirror, draws a dynamic mesh inline
  until its host copy exists, and turns a mesh inline for good (converting
  that frame's by-reference draws) if it changes again after being drawn in
  the same frame. Remaining inline batches use one grow-only host mesh slot
  pool per style, so batch reordering no longer rebuilds GPU meshes. Lit
  meshes stay inline or static-retained. Version 1 to 3 packets remain valid.
- **Simulation lanes** (D-E17): with a `worker` and the Jobs grant the game
  asks `JobLanes` once at startup; the host compiles one isolated worker
  store per lane (in parallel, while menus run) and answers only when all are
  ready, so serial generations continue meanwhile. Each lane owns interleaved
  bands of eight chunk rows plus a one-row halo (`SimulationLanePartition`),
  speaks the `CSL1` protocol (`IllumoGame/Source/Wasm/SimulationLanes.*`:
  multipart sync, advance with halo patches, owned-change replies) and
  advances its rows exactly with the unchanged `SparseCellGrid` kernels;
  `SparseCellGrid::applyChunkPatches` keeps each lane's frontier journal
  sound across halo updates. The control store (`SimulationLaneCoordinator`
  behind the guest `SimulationRunner`) merges owned changes into the spare
  grid as one exact delta and publishes it through the unchanged
  mirror/publication path, launches the next generation from the halos
  before merging (pipelining), and resynchronizes lanes whenever the
  published world, rule or topology changed. Drains cannot wait inside one
  frame: `SimulationRunner::canBlock()` is false with lanes, so edits, loads,
  ruleset changes, saves and exit retire the outstanding generation (the
  displayed world is what they act on) and stale replies are dropped by
  session and epoch. Lanes are used once serial generations exceed 1 ms and
  dropped when all lanes together work under 0.5 ms; radii above 16 stay
  serial, as does everything after a lane failure. Elementary 1D rules
  partition by chunk column: the coordinator supplies the global source row
  (CSL1 version 2), lanes write only their own columns of the next row, and
  the lanes' owned-column source rows merge into the next one (D-E29).
  The legacy whole-world `CSW1` worker remains for parity tests in
  the same worker module.
- **Limits**: the pinned runtime is Windows x64 only, so Linux has no
  runnable applications; it builds the engine libraries and native test
  suites.
- **Verification**: `IllumoGame.Wasm.GamePackage`, `IllEd.Wasm.Package` and
  `IllMeshViewer.Wasm.Package` drive the real packages through the generic
  host; `IllEd.Wasm.ProjectPackage` places project assets and saves into
  `/project`, `IllMeshViewer.Wasm.ScenePackage` opens a mounted package's
  scene, and `IllumoGame.Wasm.CatalogMerge` merges package catalogs;
  `IllumoGame.Wasm.GamePackageLanes` runs the game on real lanes and
  compares its save with a native serial reference;
  `IllumoGame.Wasm.LaneParity` checks every rule, both topologies and 1-3
  lanes (one-row bands, edits, retirement) plus large 4-lane worlds against
  serial generations, and `IllumoGame.Wasm.LaneProtocol` the CSL1 decoders and
  lane rejections; `Illumo.Wasm.RetainedResources`,
  `Illumo.Wasm.FrameValidation`, `Illumo.Wasm.FrameFailures` and the guest
  `Illumo.Wasm.SdkContract` cover retained, dynamic and pooled meshes;
  `Illumo.Wasm.EngineModes` and `Illumo.Wasm.Manifest` cover engine options
  and manifest decoding; `Illumo.Runtime.Help`,
  `Illumo.Runtime.InvalidCaptureFrame` and `Illumo.Runtime.InvalidBenchFrames`
  cover the command line; `tools/verify_capture.py` is the real-GPU capture
  check. Performance: `IllumoGame.Sim.RunnerBench` (native reference) and
  `IllumoGame.Wasm.PackageBench` (label `IllumoBenchmark`). Plans and ledgers:
  [wasm-apps-cutover-plan.md](wasm-apps-cutover-plan.md) and
  `.agent/wasm-runtime-performance-plan.md`.

### 5.13 Audio (D-E28)

Sound effects are an engine service with one product-facing seam, `IAudio`
(`Illumo/Include/Illumo/Audio/Audio.h`): register a decoded `AudioClip` once
(`createSound`, at most 256 sounds), then `play` it by `SoundHandle`
(slot+generation, stale handles ignored) with a `SoundPlayback` volume, pan
and pitch; every play is its own voice, at most 32 mix and the oldest is
replaced. `stopAll` and a master volume complete the surface. Calls are
main-thread affine. It is published as `IllumoContext::audio` and, like
`panelSurfaces`, may be absent, so products must also work silently.

- **Library** (vendored miniaudio 0.11.25, `Illumo/thirdparty/miniaudio-0.11.25`,
  public domain or MIT-0): no miniaudio type crosses a public header.
  `AudioDecoder` (`AudioClip.h`) decodes WAV (integer or float), FLAC and MP3
  bytes to interleaved float samples at the source rate, mixing wider sources
  to stereo, and bounds clips at 3 Mi samples. It uses only miniaudio's
  memory decoder entry points, so no stdio file loader is linked, and
  `AudioDecoder.cpp` is the single miniaudio implementation unit.
  `AudioDevice` is the native `IAudio`: a miniaudio engine on the default
  output device (its own mixer thread), sounds kept at their source rate and
  converted per voice, and a headless mode whose `render()` pulls the mix for
  tests.
- **ABI** (`Audio` capability, bit 11; `GuestService::Audio`, 18): offered only
  when the host has an output. `GuestAudioRequest` version 1 carries
  `Create` (a guest-chosen sound id plus float samples), `Destroy`, `Play`,
  `StopAll` and `SetVolume`; unused fields must be zero, values are range
  checked, and a malformed request fails the exchange. Requests complete in
  the same exchange with no payload, and the guest queue discards Audio
  completions (like Log), so plays never accumulate against the outstanding
  bound. Unknown or duplicate ids, a full table, a missing output or the
  64 MiB per-guest sample budget are rejections that leave the guest
  running. `WasmGameServices` releases a guest's sounds, voices and master
  volume when it retires.
- **Decoding stays in the sandbox**: guests decode their own package files
  (as textures are decoded guest-side) and send only validated samples;
  the host never parses a guest's encoded audio. `IllumoGuestRendering`
  compiles `AudioDecoder` with miniaudio's device, thread, engine and node
  graph code disabled, and the game module's imports are unchanged.
- **Host ownership**: `RuntimeApplication` creates one `AudioDevice` unless
  the launch is `--capture` or `--bench-*`; a machine without an output logs
  `Audio disabled` and runs silently. `WasmGameModule::setAudio` borrows it;
  `RuntimeModule` declares it first so it outlives the guest.
- **Guest** (`IllumoGuest/Audio.h`): `GuestAudio` implements `IAudio` over the
  service with guest-local handles and never-reused wire ids;
  `GuestModuleApplication` publishes it when granted.
- **CSim**: `CSimSounds` maps ten cues (program start, menu hover, select,
  back and error, canvas enter and exit, the canvas EDIT/NORMAL mode switch,
  voiced only when the mode actually changes, and the paint drawer's expand
  and collapse) to `Sounds/*.wav` in the package,
  with per-cue mix levels scaled by the `soundVolume` setting (0-100,
  default 80, a F1 settings row that previews each step). Menus and dialogs
  without an `IllumoContext` fire cues through its installed bank, as they
  use `CSimPlatform::current()`; with no bank cues are silent but counted,
  which the native tests read. The WAV sources in `IllumoGame/Assets` are
  deliberately not tracked in git (`IllumoGame/.gitignore` ignores the whole
  directory, owner decision); CMake stages the ones present, and a package
  without them plays silently. Tests: `Illumo.Audio.*`,
  `Illumo.Wasm.AudioServiceDecoder`, `Illumo.Wasm.AudioServices`,
  `Illumo.Wasm.GuestAudio`, `IllumoGame.Sounds.Bank`,
  `IllumoGame.ConfigurationMenu.SoundVolume` and
  `IllumoGame.Wasm.GamePackageAudio` (the real package through the host with
  a recording output). Record: [audio-subsystem-plan.md](audio-subsystem-plan.md).

---

## 6. Locked decisions (catalog)

Full formal prose also lives in `docs/latex/sections/09-design-decision-log.tex`. Append new IDs there **and** update this table.

### 6.1 Structure and style

| ID | Decision |
|----|----------|
| **D-001** | Package rename: Core→Game, System→{App,Engine,Services}, Init+Util→Foundation, AssetLoaders→Assets. |
| **D-002** | Math aliases in `MathTypes.h` (not `Math.h` — Windows CRT collision). |
| **D-004** | Modules: Start / Update / DispatchDrawables / Exit. |
| **D-005** | Each CA variant is a `RuleSet` subclass (factory in CellContext; registry later optional). |
| **D-006** | Modes = `CellState` enum in module (not archived State class hierarchy). |
| **D-008** | House style: avoid `auto`; no namespaces; no recursion; third-party via PR. |
| **D-N1** | Historical unified Illumo identity; superseded for product/executable/test identity by D-N2 and for product-visible identity/new save filenames by D-N4, while retained for the library, repository, host, and legacy `.illumo` loading. |
| **D-N2** | Historical `IllumoGame` simulator identity; superseded for product-visible branding by D-N4 while retained for technical targets and test namespaces. |
| **D-N3** | `IllEd` is the in-tree world-editor application identity (`IllEd.exe`, `.ilsc`, `IllEd.*` tests). The `IllEd.exe` executable identity is superseded by D-E14 (`IllEd.wasm` in `apps/illed`). |
| **D-N4** | CSim is the simulator's product-visible identity and `.csim` is the canonical save extension; technical `IllumoGame` identifiers and legacy `.illumo` loading remain. |
| **D-B1** | `DebugModule` is composed by the engine runner in Debug and RelWithDebInfo; Release must neither compile nor register it. Refined by D-E7. |
| **D-CLI1** | Services own generic console mechanics; `CellGameModule` registers domain commands and help/completion metadata. |
| **D-UI1** | Console editing and caret placement use measured text geometry; one enlarged batch must fit a full help page. |
| **D-UI2** | Console history wraps and scrolls by visual lines using shared mounted/floating layout metrics. |
| **D-UI3** | Console can be mounted or floating; floating mode supports title-bar drag and corner resize. |
| **D-UI4** | Console is a plain tool with its own palette (not `UiTheme`), levelled/timestamped/collapsing output, view filters, scripts, log export, clipboard, and F-key bindings. |
| **D-UI5** | Console can pop out into a separate `GLFW_NO_API` window drawn by `SoftwareCanvas` and presented by the platform; no second GL context. |
| **D-UI6** | `GuiTextEdit` (UTF-8 caret, selection, clipboard) and `GuiFileTree` (non-recursive flattening of asynchronous listings) join `Illumo/Gui`; IllEd's inspector and asset browser and DebugModule's keyboard-only `files` browser use them. |
| **D-UI7** | Tool UIs (IllEd, IllMeshViewer) use the plain `GuiToolStyle` look and one `GuiPanelDock` of detachable panels: left/right columns with splitters, title-bar hide/pop-out/dock, tear-off past the window edge, layout saved in the `panelLayout` setting. Panels are content renderers over a `GuiPanelPlacement` (rectangle plus surface) read through `GuiPanelPointer`, never knowing whether they are detached. Chrome keeps the tool metrics; `fontSize` scales panel content. No retained widget tree. |
| **D-UI8** | Glass menus move like liquid: the travelling selection is a drop with head and tail springs (`GuiMotion::kLiquidHead`/`kLiquidTail`) and a squash response, drawn by `GuiKit::drawLiquidSelection` (full-width head, neck, teardrop tail; a rounded rect at rest); panels and rows drop in on springs, presses and stepped values wobble, every glass panel swivels toward the pointer through `GuiPanelTilt` (depth-layered shift, `GuiGlassStyle` tilt shadow/glare/edge light, layout origin and hit testing shifted to match), and screens tune springs from `GuiMotion` presets. |
| **D-UI10** | CSim draws its own mouse pointer (`SoftwareCursor`: exact tip, spring lean against sideways motion, holographic afterimage, press squish and splash, drawn in the menu glass palette with an edge glow) and hides the system cursor while it does; the persisted `softwareCursor` option is the F1 settings Software cursor toggle. Display wire version 2 appends `hideSystemCursor`; the host hides the main window's cursor (`IRenderWindow::setSystemCursorHidden`), still decodes version 1 (which leaves the cursor alone) and answers each request in its own version, and `WasmGameServices::cancel` restores it. `GuestModuleApplication::updateOverlay`/`dispatchOverlay` host overlays that outlive module transitions. |
| **D-UI11** | The `uiScale` setting accepts fractional factors and `auto` (or 0). `UiScale` (`Illumo/Rendering/UiScale.h`) resolves it against the window being laid out: automatic is 1x at 1280x720 and grows in 0.25 steps with the tighter axis ratio, clamped to 1x-4x; explicit positive factors pass through. `Renderer` resolves it per frame (so a resize takes effect next frame) and the console uses the same rule. Display wire version 3 carries the scale in hundredths (0 = automatic, else 100-400); the host still decodes versions 1-2 and answers them with the whole factor in effect. CSim's F1 UI scale is a slider (Auto, 1x-4x in quarter steps up to 3x, then halves) and new CSim settings default to `auto`. |
| **D-UI12** | CSim adds Canvas and Controls settings tabs. `SimulatorSettings` owns the new keys, defaults and ranges for the canvas and main menu: `startPaused` (canvases open in EDIT; off opens them running), `cellStyle` (`led`/`flat`), `cellGlow` (0-2), `gridLines`, `showFPS`/`showMemory`, `zoomStep` (wheel zoom per notch, 0.01-0.5), `invertZoom`, `panSpeed` (arrow-key pan in screen pixels per second, 0 off), `autosaveMinutes` (writes `autosave.csim` to private storage; 0 off) and `confirmClear` (`clear_canvas` opens the confirmation dialog; `clear_canvas yes` skips it). The cell look travels to the host Canvas shader in the cell quad's colour attribute (glow / 2, LED keys, grid lines), which `GLMesh` now binds for `Pos3Color3Uv2`; no uniform or frame schema changes. The FPS/memory readout is CSim's own `PerformanceOverlay`, drawn by the guest over every screen because the engine's `DebugModule` overlay exists only in development builds; memory is the store's linear memory. |
| **D-UI13** | In-application restart for settings read only at startup (today MSAA). `IRenderWindow::requestRestart()` is a close request that remembers a relaunch; a deferred close (`Illumo::processCloseRequest`) drops it, and `RunIllumoApplication` relaunches after the engine and logger have shut down (`RelaunchCurrentProcess`: `CreateProcessW` with the original command line on Windows; `posix_spawn` of `/proc/self/exe` on Linux, unverified). Guests ask with `GuestUpdateFlags::RequestRestart` (4) beside `RequestClose`; `WasmGameModule` honours it only for a Display-granted guest when the runtime allows it (`setRestartAllowed`, off for captures, benchmarks and tests), else it is an ordinary close. Display wire version 4 appends `msaa` (saved to the host `msaa` for its next window) and `activeMsaa` (the running window's samples, `IRenderWindow::getMsaaSamples`, unknown for capture windows); before it, CSim's MSAA setting never reached the host. After Apply, CSim compares the applied MSAA with `activeMsaa` and, when they differ, asks "Restart CSim?" (Later / Restart now) in the canvas and the main menu. |
| **D-DOC1** | Established one first-party documentation tree; refined by D-DOC2. |
| **D-DOC2** | Canonical technical documentation remains under `docs/`; `illumo.tex` is the prose book and `architecture-map.tex` the chart pack. Root/nested `AGENTS.md` and `.agent/` are operational-guidance exceptions. |
| **D-T1** | Independent compile-efficient test runners expose exact cases; `IllumoWorkspace` aggregates all registered runners and combined Clang/LLVM coverage enforces at least 85% production line coverage across their linked production code. |
| **D-T2** | Introduced workspace `IllumoTidy`; superseded for default-build invocation by D-T3. |
| **D-T3** | First-party C++ runs `clang-tidy` during the default build (`ILLUMO_ENABLE_CLANG_TIDY` ON). Disable with `-DILLUMO_ENABLE_CLANG_TIDY=OFF` or `python build.py build --no-tidy`. `python build.py tidy` remains the batch Ninja/Clang compile-database run. |

### 6.2 Rendering (D-R\*)

| ID | Decision |
|----|----------|
| **D-003 / D-R\*** | Token submission via IBackend + RenderCommand; game must not permanently depend on raw GL for draw. |
| **D-R1** | `RenderCommand` = simple tagged union (C-style), not byte-stream compiler / `std::variant`. |
| **D-R2** | Each drawable emits tokens via `AppendCommands`; Renderer owns frame setup + submit. |
| **D-R3** | Historical v1 raw-handle decision; superseded by typed generational lifecycle in D-R16. |
| **D-R4** | Migration phases 1–6 done and Windows GL primary; shutdown-only resource lifetime was superseded by D-R16/D-R17. |
| **D-R5** | On blend enable, always set `glBlendFunc` (console panel transparency). |
| **D-R6** | Archive dead render queue / SceneObject graph helpers; Scene + Renderer is the path. |
| **D-R7** | MockBackend + headless tests; no Vulkan/Metal yet. |
| **D-R8** | Inject `IBackend*` into Renderer always; production composition owns backend via `CreateOpenGLBackend` + ownership transfer; tests inject MockBackend. |
| **D-R9** | String-named uniforms = debt if a second *real* GPU API appears. |
| **D-R10** | Production drawables pure-token; hybrid `Draw()` for tests/stubs only. |
| **D-R11** | `Illumo::initialize` constructs through `CreateOpenGLBackend`, owns the one fallible backend `Initialize` call, and transfers ownership; `Renderer` never includes OpenGL types. |
| **D-R12** | Historical fixed-queue policy; superseded by bounded vector growth in D-R16. |
| **D-R13** | Single production frame path in `Illumo::render`; `RenderProofQuad` is test-only. |
| **D-R14** | Scene layers (World/UI/Debug) + Renderer-owned built-in `RenderStyle` table. Layers group ordered drawables; D-R22 adds the shared shadow depth pass and D-RP1 permits configured per-layer offscreen/post passes. |
| **D-R15** | Render primitives (`Shape`/`Sprite`/`Text`) composed on a `GameVisual` host; product drawables embed/compose via GameVisual. |
| **D-R16** | Typed slot+generation resource/style handles; validated replace/destroy/query; stale operations log and no-op. CommandQueue reserves 2,048, grows, and rejects only at a configurable 65,536 default ceiling. |
| **D-R17** | AssetManager owns canonical-path texture/shader caching, references, one CPU worker, stable fallbacks, render-thread pump/replacement, explicit reload, and Debug 500 ms timestamp polling. |
| **D-R18** | Painter-correct 2D stream: parent/local transforms, normalized pivots, atlas regions/flips, stable cross-type draw order, adjacent-only batching, bounded dynamic quad buffers, and caller-updated passive sprite animation. |
| **D-R19** | Superseded by D-E6: the sibling IllumoGame consumer establishes the explicit library boundary. Future downstream repositories still require install/package validation. |
| **D-R20** | Product UI is composed from `GameVisual` shapes/text with shared value-only `UiTheme` styling. Keep console, label, and splash behavior in their existing owners; do not introduce a retained widget tree. Soft chrome (gradients, glows, shadows) stays primitive-composed per D-R26. The developer console keeps its own plain palette instead (D-UI4). |
| **D-R21** | One world look (`uMVP`). Sprites are textured quads; 2D vs 3D is the camera projection. `MeshVisual` is the world object host; `GameVisual` remains overlay/painter composition. |
| **D-R23** | Pixel-space `GameVisual` geometry culls wholly excluded quads against the logical viewport or an optional clip before upload. Partial clips use nested, intersected scissor tokens that restore the prior state. D-E11 separately governs world-space `MeshVisual` and SceneGraph bounds. |
| **D-R24** | `AssetManager` reference-counts immutable static meshes and canonical file/options cache entries. `MeshVisual` borrows the managed `MeshHandle` and draw metadata, retaining only per-instance transform/tint/lighting state; procedural geometry remains visual-owned and dynamic. Automatic instancing remains a measured follow-up. |
| **D-R25** | SceneGraphDrawable consumes one immutable graph-owned snapshot per renderer frame. Two reusable buffers, explicit invalidation, and per-callback validation protect borrowed content. Shared shadow relevance follows caster collection; token/backend contracts remain unchanged. |
| **D-R26** | `GameVisual` adds `ShapeKind::GradientQuad`: convex, fan-ordered quads with one color per vertex (`addGradientQuad`/`addGradientRect`/`addGradientTriangle`), carried by the existing shape vertex format and shader. Soft UI chrome (gradients, glows, soft shadows, sheens, vignettes) is composed from these in `GuiKit`; no new shader, blur pass, or widget tree. Menu motion uses `GuiMenuShell` springs and curves. |
| **D-R27** | Frame schema v5 adds up to eight UI-only surface batch lists with revisions (`same` when unchanged, one quota across the frame). The host replays a changed surface through `Renderer::renderOffscreen` into a per-window target and reads it back through a two-deep asynchronous stream (`IBackend::requestFramebufferReadback` / `takeFramebufferReadback`) for a `PixelWindow` present, one frame late. No second GL context (D-UI5). |
| **D-007** | Enroll resources outside the per-frame stream (frame queue = bind/draw/update). |
| **D-WW1** | Wireworld: ruleset-aware seed + sticky head/tail/conductor brush keys. |
| **D-C2** | `CellGrid` domain + `Canvas` presentation; rulesets depend only on `CellGrid`. |
| **D-C3** | Replace the finite production path with signed-coordinate `SparseCellGrid` chunks plus bounded `CanvasView`; retain dense types only as compatibility fixtures. |
| **D-C4** | `CanvasView` is a nearest-filtered, world-space quad with exact cell texels at normal zoom and cursor-aligned world-cell editing. |
| **D-C5** | At far zoom, `CanvasView` uses a revision-gated density overview capped at roughly four screen pixels per texel; this visual budget does not cap sparse simulation chunks. |
| **D-C6** | Keep `0 x 0` as the infinite sparse world; positive chunk dimensions select a finite torus. Configure it through the Release F1 overlay, reset on topology change, and persist it in sparse saves (introduced in v3; current v4 via D-GC4). |

`MeshVisual` is the world mesh host and scene-graph attachment (D-R21/D-R24). It
tessellates colored lines/triangles and textured quads, clones Shape/Sprite
styles with depth-tested pipelines, and emits `uMVP = cameraVP * nodeWorld *
local` (billboard replaces local rotation with camera axes). Lit triangle
meshes also emit `uLightDir`, `uLightColor`, `uAmbientColor`,
`uLightSpaceMatrix`, `uShadowMap`, and shadow filter uniforms from drawable
lighting state. Before color rendering, direct MeshVisual drawables and visible
SceneGraph attachments contribute retained caster descriptors to Renderer.
The first camera-visible caster deterministically selects light direction. The
camera frustum is reconstructed from inverse MVP and conservatively extruded
toward that light by `shadowCasterDistance` (100 world units by default).
Only intersecting caster bounds participate in the shared light fit and depth
pass; a second fitted-light-frustum test rejects provably irrelevant depth
emission. Invalid camera reconstruction falls back to all casters. All
receivers sample the same map, so separate objects cast onto one another.
Maximum requested map size, minimum radius, and light distance cover the
participating set. Per-receiver
bias, slope scale, normal offset, and PCF remain drawable state. Lit meshes
retain the previous `uMVP` and emit `uPrevMVP`,
`uMotionBlurEnabled`, `uMotionBlurAmount`, and `uMotionBlurMax`; the lit
vertex shader stretches trailing vertices toward that previous clip pose so
object and camera motion smear the silhouette. Shadow map size, ortho radius,
light distance, bias, slope scale, normal offset, PCF, and motion-blur amount
are the same CPU-side state. MeshVisual does not read EnvVars; IllMeshViewer
persists those values and calls the setters. It does not own a camera, model
loader, or material system. For imported/static geometry it stores a non-owning
managed `MeshHandle`, immutable index count, and per-instance tint; the
acquiring owner controls the `AssetManager` reference. This lets multiple nodes
reuse one upload without introducing automatic instance aggregation. Overlay
chrome stays on `GameVisual` with a screen ortho `uMVP` so HUD does not pan with
the world camera.

IllumoGame's persisted `render3dTest=1` flag replaces `CanvasView` with the
package scene `Scenes/render3d-test.ilsc`, instantiated through
`SceneInstance` (the game animates its `orbit` and `child` nodes by id), and
switches the product `Camera` to perspective look-at while the flag is on. It is a rendering smoke path only;
simulation keeps running and the normal ortho canvas returns when the flag is
disabled.

### 6.3 Performance (D-P\*)

| ID | Decision | Note |
|----|----------|------|
| **D-P1** | Dirty visual path — idle frames skip full recolor/upload. | Still current |
| **D-P2** | Primitive UI batch: CommandLine remains one draw; settled frames skip `UpdateBuffer`. GLString caches geometry, including optional panel chrome. | Still current |
| **D-P3** | Double-buffer `calcGeneration` + sparse dirty AABB. | Still current; refined by D-P5 |
| **D-P4** | Originally: R8 + palette + dirty-rect PBO; drop dual float RGB. | **Partially superseded:** dirty-rect PBO + bind tracker kept; **RGB fade display restored** as live presentation (see §5.6) |
| **D-P5** | Single-pass dirty AABB + `CellGrid` front/back swap (no full memcpy). | 2026-08-06 |
| **D-P6** | Fade loop hoists + packed dirty-rect PBO staging (keep CPU RGB fade). | 2026-08-06 |
| **D-P7** | Optional row-parallel `calcGeneration` (≥512² auto, override for tests). | 2026-08-06 |
| **D-P8** | Reusable bounded worker pool for large sparse target sets; normal mode drops excess catch-up debt. | Worker pool current; scheduling refined by D-P26 |
| **D-P9** | Occupancy-masked cell candidates for sparse chunks; full chunk halos remain the dense fallback. | 2026-08-08 |
| **D-P10** | Replace per-world-cell candidate hash nodes with retained contiguous per-chunk masks and neighbor counts. | 2026-08-08 |
| **D-P11** | Replace candidate-address sorting and binary searches with a retained generation-stamped open-addressed flat index. | 2026-08-08 |
| **D-P12** | Reuse next-generation chunk-map buckets and nodes transactionally through a retained inactive map and node handles. | 2026-08-08 |
| **D-P13** | Parallelize large candidate evaluation with retained coarse ranges balanced by candidate-cell count. | 2026-08-08 |
| **D-P14** | Retain changed chunks and patch only their neighbor frontier; settled worlds perform no simulation evaluation. | 2026-08-08 |
| **D-P15** | Track stored and neighbor-counting masks separately and select candidate versus halo work independently per target chunk. | 2026-08-08 |
| **D-P16** | Cache all 256x9 rule transitions and reduce dense halos with a rolling three-row neighbor stencil. | 2026-08-09 |
| **D-P17** | Maintain transactional chunk population aggregates so settled stepping and complete-path selection do not scan all allocated chunks. | 2026-08-09 |
| **D-P18** | Replace the 64-target frontier cutoff with bounded adaptive work comparison and per-target candidate/halo frontier evaluation. | 2026-08-09; tracking cap 16,384 on 2026-08-21 |
| **D-P19** | Derive candidate boundaries from masks, initialize counters lazily, and use target-owned worker preparation for large workloads. | 2026-08-09 |
| **D-P20** | Retain complete-halo targets/index/results and build rolling neighbor rows directly from chunk counting masks. | 2026-08-09 |
| **D-P21** | Publish revision-scoped changed chunks for dirty-tile sampling and retain active fading texels. | Refined for every LOD by D-P24; overview snap 2026-08-21 |
| **D-P22** | Gate the optional second synchronous generation by measured frame cost and report requested versus achieved TPS. | Superseded by D-P26 |
| **D-P23** | Grow canvas texture capacity geometrically and destroy replaced/released GL textures and PBOs. | Refined by D-P25 |
| **D-P24** | Separate the visible viewport from a padded aligned camera cache with stable integer LOD and incremental overview bins. | 2026-08-09; aligned cache scroll and overview snap on 2026-08-21 |
| **D-P25** | Merge dirty texel tiles into bounded rectangles and use direct small uploads plus a non-waiting three-PBO/fence ring. | 2026-08-09 |
| **D-P26** | Publish one evidence-gated worker generation from dual sparse grids at frame boundaries with no backlog. | 2026-08-09 |
| **D-P27** | Retain exact state/counting change masks and enroll neighboring frontier targets only at affected edges or corners. | 2026-08-11 |
| **D-P28** | Use bounded, sharded, evidence-adaptive exact memoization for repeated 18×18 halo states. | 2026-08-11 |
| **D-P29** | Eliminate duplicate inactive-delta snapshots, skip superseded incremental mirror writes, and rebuild broad mirrors with recycled chunk nodes. | 2026-08-11 |
| **D-P30** | Link candidate targets to source chunks during discovery and prepare them through retained coarse ranges. | 2026-08-11 |
| **D-P31** | Probe duplicate candidate enrollments before growth checks and construct sparse results directly in recycled nodes. | 2026-08-16 |
| **D-P32** | Default to configurable synchronized presentation and distinguish paced swap cadence from CPU submissions. | 2026-08-16 |

### 6.4 Engine shape (D-E\*, D-C\*, D-F\*)

| ID | Decision |
|----|----------|
| **D-E1** | Historical App-owned module registration; refined by D-E7 while Engine still knows only `IModule`. |
| **D-E2** | InputManager has no Game types. |
| **D-E3** | EntityTable archived; cells are not entities. |
| **D-E4** | Rendering `Scene` remains a non-owning per-frame drawable list; D-E8 adds a separate retained graph rather than changing this type. |
| **D-E5** | Freeze IllumoContext; validate at Start; third module → explicit deps. |
| **D-E6** | One repository contains the `Illumo` static library and sibling `IllumoGame` product. Public headers live under `Illumo/Include/Illumo`; no install package, DLL ABI, or second repository is implied. Its App/Platform ownership clause is superseded by D-E7. |
| **D-E7** | Illumo owns platform entry/dialogs, BuildInfo, SysCmdLine, logging lifetime, DebugModule composition, and the frame loop. IllumoGame contains only CA Game/Rulesets/configuration metadata and its required-module factory. |
| **D-E8** | Illumo owns an additive persistent `SceneGraph` with generational graph-local handles, deterministic hierarchy/transform state, and borrowed token render attachments. |
| **D-E9** | Debug `DebugModule` is a global overlay: optional modules update before the required product module and dispatch after it. Product input yields while the console is open. |
| **D-E10** | *Superseded by D-E18/D-E19.* `.ilsc` v1 was the editor-owned UTF-8 JSON scene interchange; SceneGraph does not serialize itself (still true). |
| **D-E11** | Attachments may report conservative local AABBs; SceneGraph exposes world bounds and linearly camera-culls color extraction. Renderer retains and filters directional-shadow casters against a camera-frustum extrusion, failing open on invalid data. No spatial index or subtree cache is introduced. |
| **D-E12** | SceneGraph v2 uses intrusive SoA/TRS state, lazy preorder, revision bounds, ordered attachments, interned names/payloads, a bounded journal, and derived query BVH. IllEd edits the graph incrementally. Supersedes the storage and linear-only limits of D-E8/D-E11; no ECS or persistence migration. |
| **D-E13** | IllumoGame ships only as `IllumoGame.wasm`, hosted by the generic `IllumoRuntime.exe`. A guest-side `GuestModuleApplication` composes the `IllumoContext` inside the store; product I/O uses `CSimPlatform`; frame schema v2 carries the world camera, lit meshes and shadow casters. `IllumoGameCore` is only the native test oracle. Supersedes D-E7's IllumoGame seam (§5.12). |
| **D-E14** | Every interactive client program is a WASM package in `apps/<name>/` (`app.json`: id, module, worker, title, `launchAccess`, requested budgets) run by the one generic `IllumoRuntime.exe`: `game`, `illed` (`IllEd.wasm`) and `meshviewer` (`IllMeshViewer.wasm`). `--app` selects the package, `--open` grants one launch document (read or edit per manifest; the guest sees only its base name), and `--capture` folds the removed `IllumoCapture` into the runtime. Frame schema v3 adds retained host meshes, cubemaps/`Skybox` batches and a per-batch blend flag. IllEd and IllMeshViewer reach I/O through `IllEdPlatform` / `MeshViewerPlatform`; `IllEdCore` and `IllMeshViewerCore` are only native test oracles. Extends D-E13; `FrameCapture` and `IllumoCaptureGpuTests` remain (§5.12). |
| **D-E15** | Compiled WASM depends on `WasmEngineOptions` (fuel metering, explicit bounds) passed from the host to the isolated compiler; mismatched artifacts fail closed. Explicit guest bounds checks follow AddressSanitizer (`ILLUMO_ENABLE_ASAN`), not `_DEBUG`. Manifests choose `metering` `fuel` (default) or `epoch`; first-party packages are epoch-only, mods stay metered, `--fuel` forces metering. Build profiles `dev` (RelWithDebInfo) and `debug-noasan` sit beside the `debug` sanitizer profile (§5.12). |
| **D-E16** | Frame schema v4: dynamic 2D meshes are host-retained, created ready and patched by per-frame `meshWrites` applied before the frame's batches; the guest sends only differing spans and falls back to inline geometry when a mesh changes after a by-reference draw in the same frame. Inline batches use per-style grow-only host slot pools. v1-v3 stay valid (§5.12). |
| **D-E17** | IllumoGame generations run on up to eight isolated simulation lanes (`CSimWorkerGuest.wasm`, `LaneJob`/`JobLanes` services) owning interleaved eight-row chunk bands with one-row halos; the control store merges exact deltas, pipelines the next generation, and publishes through the unchanged path. Drains retire the outstanding generation instead of waiting (`SimulationRunner::canBlock()`). Lanes engage above 4 ms serial generations; elementary 1D rules, radii above 16 and failures stay serial. Completes milestone 6 of the game cutover (§5.12). |
| **D-E29** | Lanes engage above 1 ms serial generations and leave under 0.5 ms of summed lane work (measured by `IllumoGame.Wasm.PackageBench`); elementary 1D rules take lanes partitioned by chunk column with a coordinator-supplied source row (CSL1 version 2). Supersedes the threshold and elementary exclusion of D-E17. |
| **D-E18** | Optional `Illumo::Content` layer owns virtual paths, manifests, `.ilpk`, the virtual file tree, `.ilsc` and `SceneInstance`; core never includes it, it never depends on Wasm or a product; guests link `IllumoGuestContent`. Supersedes D-E10. |
| **D-E19** | `.ilsc` format 2 is the one scene format (clean break from v1): settings with one environment, an asset table with package-relative or absolute virtual references, strict core components, verbatim namespaced data, canonical output; `SceneInstance` is the live incremental loader. |
| **D-E20** | `.ilpk` is a bounded ZIP subset (stored/deflate, CRC-32, name and size checks); the host writer deflates through vendored `stb_image_write`. |
| **D-E21** | One host `VirtualFileSystem`: `/engine`, `/app` with overlays, `/packages/<id>`, writable `/project`; immutable mount tables, case-exact lookups, no whiteouts, host paths never disclosed. |
| **D-E22** | `illumo.json` (kinds `app`, `content`, `mod`) replaces `app.json`; `--app`/`--package` accept a directory or `.ilpk`. Refines D-E14. |
| **D-E23** | File protocol v2: `Mounted` area, `List`, `Stat`, `Import`, `Pack`, 1 MiB mounted blocks, 16 guest tasks; `ProjectFiles` capability (bit 9) only with `--project`. |
| **D-E24** | `GuestVfsAssets` replaces `GuestPackageAssets`: pinned preloads, held fetch sets, `/local` entries, LRU budget; `GuestSceneFetches` adds OBJ material libraries. |
| **D-E25** | IllEd edits a format 2 document over `SceneInstance` with patch history (merged drags, count/byte caps, cursor-based dirty), a selection set, one shortcut table, gizmos, typed inspector, clipboard fragments, asset browser and project commands. |
| **D-E26** | SceneGraph gains `setParent(node, parent, insertBefore)` for sibling order; nothing else in v2 changes. Refines D-E12. |
| **D-E27** | `IPanelSurfaces` (`IllumoContext::panelSurfaces`) gives products extra top-level windows for detached panels: surface 0 is the main window, keys stay in one queue with `focused()`. Guests get it through the `Windows` capability (bit 10, granted opportunistically when the host can present windows), `GuestService::Window` (17) and input v2; `WasmPanelWindows` owns the windows. Hosts without windows (Linux, `--capture`, `--bench-*`, headless) omit it and products stay docked. |
| **D-E28** | Sound effects through `IAudio` (`IllumoContext::audio`), implemented natively by `AudioDevice` over vendored miniaudio 0.11.25 (private to `Source/Audio`). Guests get it through the `Audio` capability (bit 11, granted only with an output; never for `--capture`/`--bench-*`) and `GuestService::Audio` (18, fire-and-forget, completions discarded); guests decode their own files with `AudioDecoder` and send only float samples, bounded per clip and per guest. Extends D-E27's capability pattern. |
| **D-C1** | Canvas dual role intentional until scale forces split. |
| **D-C2** | **Refines D-C1:** extract `CellGrid` domain; `Canvas` extends it for view/GPU. |
| **D-C6** | Configurable infinite or finite toroidal sparse topology, Release F1 configuration, and topology persistence (current sparse save v4; D-GC4). |
| **D-G1** | Editor patterns (RLE/plaintext/stamps/clipboard) are a side path; the sparse world-save format is maintained separately (current v4; D-GC4). |
| **D-G2** | Elementary 1D rules use a serial space-time advance, not the Moore 256×9 table. |
| **D-GC2** | Shipped and custom rules compile from versioned data; F2 edits staged drafts (catalog layering and save version are updated by D-GC4). |
| **D-GC4** | Families own cell schemas and palettes; every ruleset references one family; F1/runtime/saves carry both IDs, with v4 writes and legacy derivation. |
| **D-GC5** | Cyclic families use full Moore neighbor-state histograms with threshold/coprime-step rules in an isolated serial sparse kernel. |
| **D-GC10** | Histogram, directional, extended-range and Lenia kernels, and elementary 1D rows, evaluate on the grid worker pool with per-worker scratch and serial in-order publication (native builds; the guest pool stays serial). |
| **D-F1** | MacroDefs / Windows.h include toxicity deferred until real pain. |
| **D-F2** | Builds are versioned `vYY.MM_B`. `VERSION.txt` holds the release (`YY.MM`, changed only by `python build.py version --set`); `B` counts first-parent commits since it last changed. `cmake/IllumoVersion.cmake` stamps `BuildInfo` and the staged `illumo.json` manifests on every build; CSim's main menu shows `v26.09_12` from its manifest, and logs and `--version` add the commit and a dirty flag. |

---

## 7. Early agenda vs today

| Agenda item | Status |
|-------------|--------|
| Render text / fonts | **Done** (FreeType + GLString / SplashText) |
| Command line | **Done** (validated built-ins plus module-registered simulation, canvas, camera, ruleset, and file commands) |
| Console on GL screen | **Done** (token UI, advanced editing, measured caret, scrolling, full-help capacity test) |
| More rulesets | **Done** — Wireworld live; 90/184 space-time |
| Infinite 16×16 chunk canvas | **Done** — sparse signed-coordinate chunks with separate stored/counting masks, per-target candidates or dense 18×18 halos, bounded view, editing, camera, persistence, and tests |
| Mouse pan | **Done** (camera controls) |
| CPU large-grid parallel | **Done where measured** — bounded reusable workers for large candidate and halo work; discovery/merge remain deterministic and serial |
| SYCL / GPU simulation | **Not done** — optional and deferred |

**Reading of the agenda:** UI/tools, sparse chunks, and bounded CPU parallelism
are live. GPU/SYCL work remains optional and must be justified by product need
and current measurements.

---

## 8. Known product / correctness issues

From local code review / `docs/current-issues.md` (fix when touching related code; not architecture rewrites). Architecture itself is sound for the normal paint/sim loop.

### 8.1 Bugs (high signal)

| # | Severity | Issue |
|---|----------|--------|
| 1 | ~~bug~~ | **Resolved and strengthened by D-E6:** optional rejection destroys the module immediately; required rejection rolls back accepted modules and fails startup; exceptions are contained. |
| 2 | ~~bug~~ | **Resolved 2026-08-06:** Wireworld left-paint uses a sticky brush selected with `1`/`H` (head), `2` (empty), `3`/`T` (tail), `4` (conductor); right-click still clears to empty. |
| 3 | ~~bug~~ | **Resolved 2026-08-06:** Startup seed is ruleset-aware — GoL-family glider for binary rules; Wireworld plants a horizontal conductor with a head+tail electron. |

### 8.2 Closed test gaps

Wireworld now has explicit two-head birth and three-head no-birth truth-table
coverage. File-backed save/load tests cover version 4 family/ruleset and
topology round trips, version 3 and version 2 compatibility, ruleset
restoration, dimension overlap,
missing/truncated/invalid files, extension fallback, and dialog cancellation.
Finite Life/Wireworld seams and Release settings behavior have focused
headless coverage. Native dialog and live window UI still need platform smoke
tests.

### 8.3 Assessment-only risks (structural)

From `gpt_illumo_arch_assessment.pdf` and later boundary-consolidation work:

- Command queue reserves **2,048** tokens, grows to a configurable **65,536**
  default ceiling, logs one rejection warning per frame, and exposes high-water
  and rejection counts (D-R16); raw pointer payloads must still stay alive
  until submit
- CMake configuration is centralized through shared target helpers; the
  repository root is the canonical `IllumoWorkspace` entry point and adds the
  sibling `Illumo` and `IllumoGame` projects. Workspace `clang-tidy` runs on
  first-party compiles by default (D-T3); `IllumoTidy` is the optional batch
  compile-database run.
- Runtime configuration is one `envvars.json` beside IllumoGame; CMake seeds
  it from tracked `IllumoGame/envvars.json` only when absent, so launch working
  directories cannot select or overwrite a different configuration. The F1
  product menu updates supported simulation, topology, and display values in
  both Release and Debug.
- Docs historically described **R8+palette** while code used **RGB fade** — this consensus file + §5.6 is the resolution; keep LaTeX chapters aligned  
- ~~Renderer.h constructed GLBackend~~ — **resolved D-R11:** composition root injects `IBackend` via `CreateOpenGLBackend`; `Renderer.h` no longer includes OpenGL types  
- Hybrid token + immediate Draw path remains for stubs  
- Native file dialogs and live OpenGL/fullscreen behavior still require manual
  smoke tests; headless MockBackend coverage does not prove them.
- Deferred boundary work (do when it hurts): further CanvasView presentation/
  upload separation, capability-oriented module contexts instead of the frozen bag
  (D-E5), rename `Scene` → `FrameRenderList` only if the name causes real
  confusion, logger global removal, and further capability-oriented contexts.

### 8.4 Looks fine (do not “fix” as bugs)

- Wireworld encoding (head = 0) and head-neighbor counting  
- Fade order for paint/sim path  
- `rebuildPalette` → full dirty refresh on ruleset switch  
- SceneObject removal has no leftover live call sites  
- Pure-token production drawables  

---

## 9. Architectural debt (do when it hurts)

| Item | Notes |
|------|--------|
| String uniforms | D-R9 — GL-shaped until second real backend |
| Hybrid Draw path | Tests/stubs only; production is tokens |
| Command queue ceiling | **D-R16:** vector growth from a 2,048 reserve to a configurable 65,536 default ceiling; one warning per rejecting frame plus high-water/rejection metrics |
| CMake duplication | Resolved 2026-08-02 with shared source lists/settings; no forced package libraries |
| Renderer ↔ backend | **D-R11:** `CreateOpenGLBackend` at composition; Renderer is `IBackend*`-only; Mock inject for tests |
| Sparse sim + bounded view memory | Sparse simulation scales with stored and counted cells; mixed targets independently use candidates or halos, dense counted chunks use at most eight reusable workers, and presentation scales with the configured visible view |
| MacroDefs + Windows.h | D-F1 deferred |
| IllumoContext growth | Frozen; third module = explicit deps |
| Additional rule families | Add only when current Moore/elementary data forms cannot express a needed rule |
| GPU/SYCL acceleration | Optional after bounded CPU parallel benchmark / product need; CPU sparse stepping is the production baseline |
| File asset formats | `MeshLoader` imports OBJ to CPU `MeshData` behind a replaceable loader. `AssetManager` synchronously caches/enrolls immutable mesh resources by canonical path plus geometry options, while `MeshVisual` borrows handles; material binding, mesh hot reload, and game-object persistence remain future work. |

---

## 10. Recommended work order

### A. Correctness first

1. Failed `Start` → do not run that module’s Update/Dispatch — **done 2026-08-06**.
2. Wireworld seed + mouse head-placement UX — **done 2026-08-06**.
3. Keep this file and LaTeX “current state” sections aligned with **RGB fade CanvasView** (no stale dense-production or GPU-R8 claims).

### B. Hygiene / boundary consolidation

4. CMake source/config consolidation — **completed 2026-08-02**.
5. Command-queue overflow policy (log once per frame) — **D-R12, done 2026-08-06**.
6. Backend injection at composition root — **D-R11, done 2026-08-06**.

### C. Boundary consolidation (2026-08-07 arc — done)

7. `SparseCellGrid` domain + bounded `CanvasView` production migration — **D-C3**.
8. Single production frame path; remove `UseTokenProof` product bypass — **D-R13**.
9. Mode splash module ownership (no file-scope `stateSplash` global).

### D. Only if product or learning goals require it

10. Font atlases, UTF-8 text layout, and nine-slice UI. General pixel
    viewport/rect clipping is complete in D-R23; text shaping remains future work.
11. Chunked tilemaps, high-volume particle culling, and particle emitters.
12. Multiple cameras, offscreen targets, compositing, and post-processing.
13. Keep the established Illumo public boundary narrow; add install/export or
    shared-library ABI work only for a real distribution requirement.
14. Non-string uniforms / second real backend (OpenGL factory already at composition).
15. ~~Data-driven rules and the F2 Ruleset Workshop — D-GC2; typed family and ruleset identity separation — D-GC3.~~
16. Narrow `IllumoContext` into capability bags only when a third module needs different deps (D-E5).

### Explicitly deferred (engine PDF + consensus)

- SceneGraph ECS components, serialization, update callbacks, retained UI,
  parallel scene stages without a measured gate, and render-thread payloads
- Render graphs, multi-backend, global transparent texture sorting
- Multithreaded command generation  

---

## 11. Core design principles (merged)

1. **Reusable foundation, concrete consumers** — generalize Illumo around
   demonstrated project needs while keeping product policy downstream.
2. **Ownership explicit** — Illumo owns system/runtime behavior; IllumoGame owns
   only CA policy and supplies its module through a declarative factory; context
   does not own.
3. **Boundaries over cleverness** — abstract volatility (GLFW, GL, future compute); keep the stable sparse-domain/bounded-view split explicit.
4. **Sim produces complete state; render observes** — double-buffer; no draw mid-generation.  
5. **Tokens for draw submission** — enroll once, emit commands, backend executes.  
6. **Parallelize data transformations last** — deterministic serial ownership first; adaptive cell candidates and bounded CPU chunk evaluation are justified only by measured pressure.
7. **Archive superseded experiments** — do not revive the old pointer-based
   graph, EntityTable, or pass objects beside their replacement contracts.
8. **Simplest architecture that preserves the boundaries you care about** (engine PDF principle, applied to the CA product).  
9. **Code wins over docs** — update this file when consensus shifts.  
10. **One technical-documentation tree** — current prose, LaTeX, decisions,
    package maps, and session records live under `docs/`; root/nested
    `AGENTS.md` and `.agent/` are operational exceptions (D-DOC2), and
    generated PDF output is not source.

---

## 12. Open questions (remaining)

Most design questions from the LaTeX open list are **resolved** (see §6). Still open or only lightly decided:

| Topic | Working answer |
|-------|----------------|
| Resource ownership long-term | Typed generational handles validate explicit replace/destroy operations; `AssetManager` reference-counts textures, shaders, cubemaps, and immutable model meshes, while the resizeable canvas explicitly replaces/releases its texture and PBO ring. |
| Linux validation | Linux sources and CMake are repaired for Ubuntu 24.04 x86_64 X11/XWayland with gtkmm-3 dialogs (`docs/packages/platform-linux.md`). Keep Linux unsupported until native configure, compile, launch, dialog, render, and shutdown smoke exist on that host. macOS is not targeted and its scaffold has been removed. |
| Tracy CI policy | Debug-oriented; no strict CI policy yet. |
| When to introduce SYCL / GPU simulation | Only after a current benchmark and explicit product or learning goal justify a second compute path. Sparse chunks and bounded CPU workers are already live. |

Resolved highlights (do not re-open without a new decision ID):

- Token payload shape → D-R1  
- Handles → D-R3  
- Who emits tokens → D-R2  
- Per-frame render-list role → D-E4; persistent scene hierarchy → D-E8
- IllumoContext growth → D-E5  
- Canvas domain vs view → D-C1/D-C2 superseded for production by D-C3
  (`SparseCellGrid` + bounded `CanvasView`); dense types are compatibility-only
- String uniforms → D-R9 debt  
- Backend injection → D-R11  
- Command queue overflow → D-R12  
- Single production frame path → D-R13  
- Canvas upload dirty rects → D-P1 / PBO path  
- Module registration → D-E1  
- InputManager Game deps → D-E2  
- EntityTable → D-E3 archived  
- CPU color fade → **restored** (RGB display)  
- Wireworld → implemented  
- MacroDefs toxicity → D-F1 deferred  
- Library/repository naming → D-N1 (`Illumo`); simulator product identity and
  save extension → D-N4 (`CSim`, `.csim`)

---

## 13. How to use this document

| When | Action |
|------|--------|
| New task | Read root/closest `AGENTS.md`, then **this file** for current architecture. |
| Deep dive | Then LaTeX design notes / decision log. |
| Architecture change | Update **this file** + append a decision log entry in `docs/latex/sections/09-design-decision-log.tex`. |
| Code lands | Update “code truth” sections here if behavior changed. |
| Significant session | Add a dated implementation/verification record under `docs/sessions/`. |
| Rebuild the PDF | Run `docs/build.ps1`; the default Windows CMake build runs `IllumoDocs` when PowerShell and `latexmk` are available. |
| Old PDFs / agenda | Treat as **history** (§2); do not re-implement superseded engine plans by default. |
| Bug triage | §8 first; architecture is not the problem until proven otherwise. |

---

### Appendix A — Source map (where code lives)

| Concern | Typical location |
|---------|------------------|
| Application runner / frame loop | `Illumo/Source/Engine/Application.cpp` |
| Game definition / required module factory | `IllumoGame/Source/Game/IllumoGameApplication.cpp` |
| Public library API | `Illumo/Include/Illumo/*` |
| Host / services / modules | `Illumo/Source/Engine/Illumo.cpp` plus public Engine headers |
| Persistent scene hierarchy | `Illumo/Include/Illumo/Scene/*`, `Illumo/Source/Scene/*` |
| CA module / modes | `IllumoGame/Source/Game/CellGameModule.*`, `CellContext.h` |
| Sparse domain cell storage | `IllumoGame/Source/Game/SparseCellGrid.*` |
| Bounded world-space view + fade | `IllumoGame/Source/Game/CanvasView.*` |
| Compatibility dense storage | `IllumoGame/Source/Game/CellGrid.*`, `Canvas.*` |
| Rules | `IllumoGame/Source/Rulesets/*` |
| Tokens / Renderer / resources | `Illumo/Include/Illumo/Rendering/*`, `Illumo/Source/Rendering/*` |
| 2D primitives, animation, and debug 3D | `Illumo/Source/Rendering/Primitives/*` |
| Debug renderer assets | `Illumo/Assets/RendererDemo/*` |
| GL execute | `Illumo/Source/Rendering/OpenGL/*` |
| Mock/test support | `Illumo/TestSupport/Include/Illumo/Testing/*` |
| Console / system CLI | `Illumo/Source/Services/CommandLine.*`, `SysCmdLine.cpp` |
| Sound effects | `Illumo/Include/Illumo/Audio/*`, `Illumo/Source/Audio/*`, `IllumoGuest/Include/IllumoGuest/Audio.h`, `IllumoGame/Source/Game/CSimSounds.*` |
| Platform bootstrap/dialogs | `Illumo/Source/Platform/*` |
| Headless tests | `Illumo/Tests/*`, `IllumoGame/Tests/*` |
| Dead experiments | `archive/dead-engine/`, `archive/dead-render/`, `archive/old-states/` |

### Appendix B — What not to do next

- Do not restore the archived raw-pointer `SceneObject` or EntityTable. Extend
  the handle-based `SceneGraph` only for a concrete consumer contract.
- Do not build Opaque/Transparent/UI pass objects unless profiling or a real multi-genre product appears.  
- Do not make SYCL a hard dependency of Illumo or Game.  
- Do not document R8-only presentation as current without verifying
  `CanvasView.cpp`, `RendererStyles.cpp`, and the active style binding.
- Do not expand IllumoContext casually for a third module.  

---

### 2026-09-19 report corrections

See [the finding ledger](report-remediation-plan.md) for verification. The host
accepts at most one required module before startup; deferred transitions retain
the first non-null request and log competing requests. Input callback queues are
bounded and release polling uses frame edges. Console parsing preserves Windows
paths on execution and callbacks detach before console destruction. Logger's
unique process instance defaults to an executable-adjacent file.

GameVisual resources remain bound to their first renderer lifetime; foreign
emission fails explicitly. Geometry truncation and command rejection mark the
frame incomplete and prevent presentation. Backend queue rejection/high-water
metrics survive pass resets. Failed GPU mesh/texture enrollment does not publish
handles; failed replacement preserves prior resources and oversized vertex
updates fail before a GL write. Native GPU names live only in private GL types.

RuleSet and its data factory are storage-independent. Dense CellGrid/Canvas,
legacy reference rules, and DenseRuleEvaluator compile only into tests. The
fixture evaluator owns its worker setting and includes Elementary 1D history
advancement over toroidal dense storage. Windows pickers use wide APIs and return
UTF-8; product codecs explicitly convert UTF-8 filesystem paths. The subsequent
macOS removal decision retires that scaffold rather than maintaining a port.

---

*End of unified consensus. Prefer this one coherent story over re-deriving from multiple ChatGPT PDFs, agendas, and chat threads.*
