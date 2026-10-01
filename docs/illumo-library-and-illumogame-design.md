# Illumo library and IllumoGame extraction

**Status:** Corrected boundary approved on 2026-08-18

**Architecture decisions:** D-E6, D-E7, and D-N2

**Baseline:** clean `main`; 172/172 Release CTest cases passed before migration

## Objective and measurable end state

Split the current monolithic cellular-simulator executable into two sibling
CMake projects without changing simulation, rendering, persistence, or runtime
threading behavior:

- `Illumo` is a static source-level library with an explicit public include
  tree and the CMake alias `Illumo::Illumo`. It owns the application runner,
  process entry, system command-line parser, build metadata, native platform
  services, and generic runtime staging.
- `IllumoGame` is the Windows cellular-simulator product and the first
  source-tree consumer of that library. Its production source contains only
  Game, Rulesets, CA configuration, and the declarative game-module factory.
- Library and game production sources are compiled once into their owning
  static targets rather than repeated in application and test executables.
- `IllumoTests` and `IllumoGameTests` independently verify their owning
  projects, while an `IllumoWorkspace` CTest label and coverage target verify
  the complete repository.
- The generated product is `IllumoGame.exe`; its window, command-line help,
  version output, and console branding say `IllumoGame`. (Superseded
  2026-09-22: IllumoGame ships only as the `IllumoGame.wasm` package hosted by
  `IllumoRuntime.exe`; see [wasm-game-cutover-plan.md](wasm-game-cutover-plan.md).)

Success requires Release and Debug builds of all four primary targets, all
pre-existing tests rehomed under their owning prefix, new boundary and failure
tests, the existing 85 percent aggregate coverage gate, rebuilt documentation,
and a proportional Windows OpenGL/native-dialog smoke test.

## Current-state evidence and problem

The live renderer already has a suitable reusable boundary:

```text
Drawable::AppendCommands -> Renderer -> CommandQueue -> IBackend
                                                     -> OpenGL or Mock
```

App chooses modules, Engine owns long-lived services and accepted modules, and
Game consumes only backend-neutral rendering and service contracts. The source
tree nevertheless builds one `Illumo` executable and recompiles shared
production sources directly into `IllumoTests`.

A mechanical `Source/Game` move did not establish the requested boundary:

- `Engine/Illumo.cpp` applies CA defaults and parses simulator arguments.
- `Services/CommandLine.cpp` owns TPS, simulation-speed, and fade commands and
  hard-codes product branding.
- The first extraction placed `SysCmdLine`, `BuildInfo`, the frame loop,
  platform entry points, and native dialogs in IllumoGame even though they are
  system responsibilities of the Illumo runtime.
- `IllumoContext` contains a game-specific validation helper.
- Window and OpenGL startup terminate the process on failure, which is not an
  acceptable library contract.
- Headers are exposed through broad private source include roots rather than an
  intentional consumer surface.

## Scope and non-goals

### In scope

- Sibling `Illumo/` and `IllumoGame/` projects under the existing workspace.
- Static targets, public headers, source ownership, dependency direction,
  fallible host startup, product naming, tests, build tooling, runtime staging,
  documentation, and durable agent guidance.
- Moving CA configuration, rules, persistence behavior, simulation commands,
  and CA-specific command-line metadata into IllumoGame while keeping parser,
  process, platform, and dialog implementation in Illumo.
- Preserving the Debug-only renderer showcase as an Illumo library module that
  IllumoGame optionally composes.

### Non-goals

- No shared DLL, installed package, stable binary ABI, or separate repository.
- No ECS, scene graph, render graph, new backend, or new dependency.
- No change to sparse generation algorithms, published-grid threading,
  rendering tokens, resource-handle semantics, UI architecture, or performance
  targets.
- No save migration: version 3 writes, version 3/2/legacy reads, `ILLUMO3`
  magic, and `.illumo` remain unchanged.
- No Linux or macOS repair or support claim.

## Target and source ownership

| Target | Ownership |
|---|---|
| `Illumo` / `Illumo::Illumo` | Engine host and module contracts, application runner, Foundation including BuildInfo, generic Services including SysCmdLine, Platform entry/dialog implementations, Rendering/OpenGL, generic console/UI, AssetManager, DebugModule |
| `IllumoGameCore` | Game, Rulesets, CA configuration and CLI metadata, save-format behavior/orchestration, product commands, and the game-module factory |
| `IllumoGame` | Executable assembled from the engine-owned platform entry object and `IllumoGameCore`; no platform or system implementation source |
| `IllumoTests` | Library contracts using public headers plus private library test hooks |
| `IllumoGameTests` | Rules, simulation, presentation, persistence, product CLI and module integration |

Illumo owns the vendored dependencies, shaders, fonts, renderer-demo assets,
and third-party notices. A source-tree CMake staging helper attaches those
runtime files to a consuming executable. IllumoGame owns and seeds
`envvars.json` without overwriting an existing runtime copy.

Illumo supplies the platform dialog contract and selected native
implementation. IllumoGame calls that public system service with game-owned
file-format labels and defaults. The game test runner may provide a
deterministic replacement without owning a production platform implementation.

## Public contracts

### Headers and CMake

Supported consumer headers live under `Illumo/Include/Illumo/` and are included
as `<Illumo/...>`. They cover the host/application/module API, BuildInfo,
SysCmdLine configuration, the platform dialog contract, and the Foundation,
Services, and backend-neutral Rendering types needed by modules. Concrete
platform and OpenGL implementation headers remain private.

`Illumo::TestSupport` exposes MockBackend and test-only helpers to downstream
headless tests without making them production API. A dedicated consumer compile
smoke target receives only Illumo's public usage requirements.

The project remains a source-level contract with global C++ type names; this
migration does not add a C++ namespace or promise ABI stability.

### Host configuration and lifecycle

`IllumoConfig` supplies the visible application name and environment-file
path. Host construction loads the environment and applies only generic
window/presentation/logging defaults. The environment is available before
initialization so IllumoGame can apply product defaults and argument overrides.

Illumo owns logging setup/teardown, DebugModule composition, the visible
`std::chrono` frame loop, initialized services, and accepted module lifetimes.
IllumoGame supplies a declarative application definition containing its name,
CA defaults callback, CA-specific CLI metadata, and required game-module
factory. The engine-owned platform entry passes that definition to the generic
runner.

Module registration records whether a module is required or optional.
`startModules()` removes failed optional modules. A failed required module
rolls back already accepted modules and returns failure. The application
definition's module factory is registered as required; engine-owned
DebugModule is optional and remains absent from Release compilation and
composition.

`initialize()` returns failure instead of terminating the process. A fallible
window factory creates GLFW and the context; a fallible OpenGL factory performs
GLEW/backend initialization exactly once. Production backend ownership moves
to Renderer with `std::unique_ptr<IBackend>`. A private test hook substitutes
deterministic factories for failure-path tests without widening production API.
Shutdown remains idempotent and safe after partial initialization.

### Configuration and commands

Illumo owns defaults for window dimensions, VSync, fullscreen, FPS overlay,
and log level. IllumoGame owns canvas dimensions, ruleset, TPS, simulation
speed, and fade speed.

Illumo's CommandLine keeps generic editing, history, help dispatch,
environment, fullscreen, FPS, close, quit, and renderer/tool commands. It
receives the application name for panel/help text. Illumo's SysCmdLine owns
argument traversal, validation, standard window options, help/version output,
and continue/success/failure results. IllumoGame supplies data describing its
canvas options and ruleset help, and registers its CA console commands. The
engine-owned platform entry returns the parser/runner process status without
calling `std::exit`.

## Compatibility and consequences

- The old executable target `Illumo` is intentionally not retained because
  that name now denotes the library. Build and run tooling targets
  `IllumoGame`.
- Runtime `envvars.json` remains beside the executable and retains existing
  keys and values. Renaming the executable does not rename this file.
- Test names move by owner: library cases retain `Illumo.*`; product cases use
  `IllumoGame.*`. No duplicate compatibility aliases are registered.
- Root `cmake -S .` is the canonical workspace configuration.
  `cmake -S Illumo` configures the standalone library and its tests.
- Debug Tracy/diagnostic definitions are centralized so library, game, and
  their final executables use one coherent configuration.
- Windows remains supported. Platform implementations and stale Linux/macOS
  scaffolding live under Illumo; source presence does not make the latter
  supported or validated.

## Alternatives rejected

- **Expose the existing Source tree:** lower churn, but it makes every internal
  header accidentally public and fails to establish a maintainable library.
- **Keep one combined test runner:** compile-efficient, but it cannot prove the
  library independently and hides reversed dependencies.
- **Move directories but leave product policy in Services/Engine:** creates a
  nominal library that still knows the simulator.
- **Keep App, the loop, and Platform in IllumoGame:** leaves system concerns in
  the game project and contradicts the corrected boundary. Illumo instead owns
  the generic runner while the game supplies only declarative policy and its
  required module factory.
- **Keep hard process exits:** prevents consumers from handling startup failure
  or guaranteeing cleanup.
- **Installable package or DLL:** adds packaging and ABI work without a current
  requirement.

## Ordered implementation milestones

1. Record this design and formal decisions; retain a clean pre-migration test
   baseline.
2. Create the Illumo static target and public header tree, make startup
   fallible, and add public-consumer and lifecycle tests.
3. Create IllumoGameCore and IllumoGame, keep only Game/Rulesets/configuration
   and the declarative game factory there, and eliminate every Game/Rulesets
   dependency from Illumo.
4. Split and rename test cases, update discovery/coverage/build tooling, and
   stage runtime assets/config/licenses.
5. Synchronize README, package maps, architecture consensus, LaTeX, decision
   log, AGENTS guidance, and generated PDFs.
6. Format, build, test, cover, smoke, and review the complete diff.

Each milestone must preserve one authoritative implementation; temporary
compatibility source lists may exist only while changing CMake and must be
removed before the milestone is complete.

## Rollback and containment

The migration is grouped by the milestones above so target creation, source
moves, test moves, and documentation can be reviewed independently. The old
monolithic source list is not deleted until the new library and game targets
compile. If a milestone cannot be completed, restore that milestone's scoped
file moves and CMake edits rather than shipping both production paths.

Git staging, commits, branches, and pushes remain user-owned. No persisted
format, external data, or installed component is mutated by this migration.

## Verification record

### Baseline

- [x] Worktree clean on `main` before implementation.
- [x] Release CTest baseline: 172/172 passed on 2026-08-18.

### Corrected-boundary completion evidence

- [x] Standalone Illumo configure plus Release and Debug library/test builds.
- [x] Workspace configure plus Release and Debug builds of IllumoGame and both
      test runners.
- [x] `IllumoGame/Source` contains no App, Platform, SysCmdLine, BuildInfo,
      native SDK, frame-loop, or entry-point implementation.
- [x] Every prior case rehomed; new public API, branding, ownership, required
      module, and initialization-failure cases pass.
- [x] Full `IllumoWorkspace` CTest label passes.
- [x] Combined LLVM coverage meets the existing 85 percent line gate.
- [x] `IllumoGame --help` and `--version` return successfully without opening a
      window; invalid dimensions return failure.
- [x] Runtime shaders, fonts, renderer-demo assets, notices, licenses, and
      non-overwriting `envvars.json` are beside the executable.
- [ ] Release and Debug Windows GUI/OpenGL smoke covers startup, edit/run,
      settings, console, renderer demo, save/load dialogs, and clean shutdown.
- [x] Modified C++ is clang-formatted; documentation builds; `git diff --check`
      and final scope review pass.

Validation results and any justified deviations are appended here as the
milestones complete.

After the corrected ownership implementation on 2026-08-18, standalone Illumo
Release and Debug suites passed 89/89 each, and root Release and Debug
`IllumoWorkspace` suites passed 188/188 each. The combined Clang/LLVM report
passed at 85.94 percent production-line coverage. `build.py` combined listing
and exact prefix dispatch, engine-owned help/version and dimension parsing,
fresh runtime staging hashes, modified-source formatting, documentation builds,
visual PDF review, and final diff checks also passed.

The corrected Release executable created a live OpenGL window with the
`IllumoGame` title. Further automated UI interaction was blocked by a Windows
Security firewall prompt that the verification tooling is prohibited from
dismissing. The process was stopped without changing the firewall setting, so
the Debug/Release interaction, renderer-demo, native-dialog, and clean-shutdown
row remains deliberately unchecked pending a user-performed smoke pass.

Before the corrected ownership request, automated verification on 2026-08-18
passed the standalone `Illumo` Release
and Debug suites (84/84 each) and the root Release and Debug
`IllumoWorkspace` suites (183/183 each). The combined Clang/LLVM report passed
at 85.79 percent production-line coverage. CLI identity/error cases, split
test-runner listing and exact dispatch, persistence compatibility fixtures,
and staged assets/licenses/configuration also passed. The coverage tool
surfaces 57 conservative hash-zero/no-profile-record warnings for inline
functions; no instrumented test or profile was lost. Manual Windows Debug and
Release smoke verified the IllumoGame title and startup, OpenGL presentation,
editing and simulation, settings, the branded console, renderer demo, native
Save As and Open dialogs, and normal clean shutdown. Both native dialogs were
opened and cancelled without writing user data. Those results establish the
behavioral baseline for the correction but do not close the revised ownership
checklist above.

The coverage denominator explicitly excludes the concrete `RenderWindow`
implementation together with the OpenGL implementation. Both require a live
native context and were absent from the pre-extraction headless test binary;
keeping them out preserves the existing headless-testable production gate
rather than rewarding unreachable zero-coverage lines. `IRenderWindow` and the
rest of the backend-neutral public contract remain measured. Live Windows
window/OpenGL behavior is validated by the separate required smoke matrix.
