# Jolt Physics wrapper and Saturn physics execution plan

Status: completed (M1-M4 verified).
Tier 2 plan (`.agent/PLANS.md`: wrapped external dependency + new optional
subsystem + end-to-end Saturn integration).

## 1. Objective and measurable end state

- Jolt Physics `v5.6.0` (`e77f175595e64cb44218cc9d9d56fc365ad0e36a`, MIT) is
  vendored under `Illumo/thirdparty/jolt-5.6.0/` (`Jolt/` library tree and
  `LICENSE` only; upstream samples, tests, assets and build scripts omitted),
  inventoried in `THIRD_PARTY_NOTICES.md` and `Illumo/thirdparty/README.md`,
  and staged to `licenses/JoltPhysics-LICENSE.txt`.
- **Zero Jolt contact with `Illumo`:**
  - Core `Illumo` (`Illumo::Illumo`), `Illumo::Content`, `IllumoRuntime`,
    `IllEd`, `IllMeshViewer` and `Playground` neither compile, link, nor
    include Jolt or `Illumo::Physics`.
  - Public wrapper headers live in a separate include root
    `Illumo/Physics/Include/Illumo/Physics/` (`PhysicsTypes.h`,
    `PhysicsWorld.h`) rather than `Illumo/Include/`, so targets linking only
    `Illumo::Illumo` or `Illumo::Content` cannot even `#include` physics
    headers (`__has_include(<Illumo/Physics/PhysicsWorld.h>) == 0` in
    `IllumoPublicHeaderSmoke`).
  - `illumo_jolt` / `illumo_guest_jolt` are linked `PRIVATE` to
    `IllumoPhysics` (`Illumo::Physics`) and `IllumoGuestPhysics`. Zero
    `<Jolt/...>` headers, `JPH_*` macros or `JPH::*` symbols leak to any
    consumer (`__has_include(<Jolt/Jolt.h>) == 0` verified at compile time in
    `PublicHeaderSmoke.cpp`, `TestPhysics.cpp`, and `TestSaturnBehaviours.cpp`).
- `PhysicsWorld` provides deterministic single-threaded rigid-body, static,
  kinematic and sensor simulation (`Plane`, `Box`, `Sphere`, `Capsule`),
  raycasts, sensor overlap tracking, and swept capsule character traversal
  (wrapping `JPH::CharacterVirtual` with collide-and-slide, stairs step-up,
  ground-sticking and dynamic body pushing) backed by a preallocated
  `TempAllocatorImpl` arena (zero steady-frame heap allocations).
- `Saturn` links `Illumo::Physics` natively (`SaturnCore`) and
  `IllumoGuestPhysics` in WASI (`Saturn.wasm`), runs a fixed-timestep
  simulation with pose interpolation in `SaturnWorld` / `SaturnScene`, exposes
  typed `saturn.collider`, `saturn.rigidbody` and `saturn.player` schemas in
  `Saturn/behaviours.json`, and grounds `Scenes/world.ilsc` with an analytical
  plane collider on `slab` and swept-capsule physics + gravity + jump on
  `player`.

## 2. Current-state evidence

- `Saturn` (`Saturn/CMakeLists.txt`, `Saturn/GuestTargets.cmake`) compiles
  shared logic in `SaturnCore` natively for `SaturnTests` and into
  `Saturn.wasm` via `IllumoGuest`.
- `IllumoGuest/CMakeLists.txt` compiles WASI reactor modules with C++23,
  `-msimd128`, `-fno-exceptions` and `ILLUMO_SERIAL_GUEST` (no OS threads,
  semaphores or stdio filesystem imports).
- `Jolt/Core/Core.h` auto-detects `JPH_PLATFORM_WASM` only under
  `__EMSCRIPTEN__` and includes x86 `<immintrin.h>` when `__wasm_simd128__` is
  defined (requiring Emscripten's SSE shim headers, which WASI SDK 34 omits).
  Passing `-DJPH_PLATFORM_WASM -Wno-builtin-macro-redefined -U__wasm_simd128__`
  with `-O2 -msimd128` selects Jolt's portable C++ vector path while keeping
  LLVM's WASM `simd128` code generation, and omitting `Semaphore.cpp`,
  `JobSystemThreadPool.cpp` and `JobSystemWithBarrier.cpp` avoids `<semaphore.h>`
  entirely while `JobSystemSingleThreaded` executes deterministically.
- `Illumo/` currently carries uncommitted edits for `GroundPlaneVisual` in
  `Illumo/CMakeLists.txt`, `IllumoGuest/CMakeLists.txt`, `SceneDocument.*`,
  `SceneInstance.*`, `EditorDocument.cpp` and `EditorInspector.cpp`; all must
  remain intact.

## 3. Scope and non-goals

- In scope: vendoring Jolt `v5.6.0`, `IllumoJoltDeps.cmake`, `Illumo::Physics`
  + `IllumoGuestPhysics`, `IllumoPhysicsTests`, license notices/staging, and
  end-to-end Saturn fixed-step physics (`saturn.collider`, `saturn.rigidbody`,
  `saturn.player`, `Scenes/world.ilsc`, native + WASM package tests).
- Non-goals: modifying `Illumo::Illumo` or `Illumo::Content`, changing `.ilsc`
  format 2.0, adding host/guest WASM ABI physics opcodes, ragdolls/vehicles/
  soft-bodies/hair in the public wrapper, or collider gizmos in `IllEd`.

## 4. Constraints and invariants

- First-party C++ obeys `docs/contributing.md`: no `auto`, no namespaces, no
  recursion, no exceptions (`try`/`catch`/`throw`), Mozilla 80-column
  `clang-format`, and clean `clang-tidy`.
- `Saturn.wasm` imports only the host's permitted WASI reactor stubs.
- Steady-state `PhysicsWorld::step` allocates nothing on the heap (preallocated
  `TempAllocatorImpl` and pre-reserved contact buffers).
- Behaviour field names in `Saturn/behaviours.json` obey `plainName`
  (`[a-z][a-z0-9_]*`, snake_case).

## 5. Proposed design and alternatives considered

- **Wrapper placement:** Optional peer targets `IllumoPhysics` (`Illumo::Physics`)
  and `IllumoGuestPhysics` with headers in `Illumo/Physics/Include/Illumo/Physics/`
  and implementation in `Illumo/Source/Physics/PhysicsWorld.cpp`.
  - *Alternative rejected:* Linking Jolt into `Illumo` or putting headers in
    `Illumo/Include/`. Rejected per explicit owner requirement that Jolt must
    not directly touch `Illumo`.
  - *Alternative rejected:* Host-side physics over WASM services. Rejected
    because synchronous character sweeps, raycasts and per-tick pose writeback
    belong with the guest's simulation authority.
- **Determinism & threading:** Both native `illumo_jolt` and WASI
  `illumo_guest_jolt` compile with `JPH_CROSS_PLATFORM_DETERMINISTIC` and step
  with `JPH::JobSystemSingleThreaded`, making native `SaturnTests` a faithful
  oracle for `Saturn.wasm`.
- **Saturn integration:** `SaturnWorld` (in `SaturnCore`) owns a `PhysicsWorld`,
  fixed-step accumulator (`1/60` s, max 4 substeps) and pose interpolation
  table (`prev`/`curr` world poses written back to `SceneInstance`).
  `saturn.collider`, `saturn.rigidbody` and `saturn.player` register/unregister
  with `SaturnWorld` through `SceneBehaviour` lifecycle hooks (`start`,
  `valuesChanged`, `stop`), avoiding `SceneInstance`'s single-observer limit.

## 6. Affected public contracts, compatibility, and migration

- Existing `Illumo::Illumo` and `Illumo::Content` contracts are untouched.
- New opt-in targets: `Illumo::Physics` (native) and `IllumoGuestPhysics`
  (WASI guest).
- `Saturn/Scenes/world.ilsc` remains `.ilsc` 2.0: `slab` gains
  `{ "type": "saturn.collider", "shape": "plane" }` (`3 nodes 2 behaviours`).

## 7. Ownership, lifetime, threading, error, and platform implications

- Process-wide Jolt allocator and `JPH::Factory::sInstance` initialization in
  `PhysicsWorld.cpp` is reference-counted across live `PhysicsWorld` instances
  (mutex-guarded on host, serial under `ILLUMO_SERIAL_GUEST`), allowing
  multiple concurrent or sequential `PhysicsWorld` instances in tests cleanly.
- Generational handles (`PhysicsBodyHandle`, `PhysicsCharacterHandle`) reject
  stale or cross-slot access cleanly without exceptions.
- MSVC Debug ASan builds pass `$<$<CONFIG:Debug>:/fsanitize=address>` and
  `$<$<CONFIG:Debug>:JPH_DISABLE_TEMP_ALLOCATOR;JPH_DISABLE_CUSTOM_ALLOCATOR>`
  consistently across `illumo_jolt` and `IllumoPhysics` so STL container
  annotations and ASan redzones match.

## 8. Ordered implementation milestones

| # | Milestone |
|---|---|
| M1 | Vendor Jolt `v5.6.0` (`Illumo/thirdparty/jolt-5.6.0/`), add `Illumo/cmake/IllumoJoltDeps.cmake`, update `THIRD_PARTY_NOTICES.md`, `Illumo/thirdparty/README.md` and `IllumoRuntime.cmake` license staging |
| M2 | Implement `Illumo::Physics` (`PhysicsTypes.h`, `PhysicsWorld.h`, `PhysicsWorld.cpp`, `Source/Physics/AGENTS.md`) and `IllumoGuestPhysics`; add `IllumoPhysicsTests` and `PublicHeaderSmoke` isolation guards |
| M3 | Implement `SaturnWorld` fixed-step + interpolation and physics-backed `saturn.collider`, `saturn.rigidbody` and `saturn.player` behaviours; wire `SaturnScene`, `SaturnProgram`, `behaviours.json` and `Scenes/world.ilsc` |
| M4 | Format, lint (`clang-tidy`), build Release workspace + WASM packages, run `IllumoPhysicsTests`, `SaturnTests`, `Saturn.Wasm.Package`, `Illumo.Build.NoExceptions` and `Illumo.PublicHeaders.ConsumerSmoke`; record results |

## 9. Build, test, static-analysis, and sanitizer strategy

- Build Release targets with `ILLUMO_ENABLE_CLANG_TIDY=ON` in `build-workspace`.
- Run `IllumoPhysicsTests` (rigid body fall/rest, kinematic motion, raycast,
  sensor trigger overlap, `CharacterVirtual` ground support, wall slide and
  dynamic crate push, stale-handle rejection, header-leak `#error` check).
- Run `SaturnTests` (schema match, world load, isometric WASD movement, gravity
  landing, wall collide-and-slide, jump arc, dynamic body push) and
  `Saturn.Wasm.Package` (real `Saturn.wasm` inside `WasmProgram`).
- Run `Illumo.Build.NoExceptions` and `Illumo.PublicHeaders.ConsumerSmoke`.

## 10. Rollback or containment strategy

- All engine additions are isolated to `Illumo/thirdparty/jolt-5.6.0/`,
  `Illumo/cmake/IllumoJoltDeps.cmake`, `Illumo/Physics/Include/`,
  `Illumo/Source/Physics/` and `Illumo/Tests/TestPhysics.cpp`, with additive
  target blocks at the end of `Illumo/CMakeLists.txt` and
  `IllumoGuest/CMakeLists.txt`. Removing those targets restores the prior build
  without touching any uncommitted `GroundPlaneVisual` work.

## 11. Open questions

- Resolved with owner: engine-level `Illumo::Physics` + `IllumoGuestPhysics`
  consumed end-to-end by `Saturn`, with strict isolation so Jolt never directly
  touches `Illumo`.

## 12. Validation log

- **Build (`Release`, `ILLUMO_ENABLE_CLANG_TIDY=ON` + WASI SDK 34 guest sub-build):**
  - Built `illumo_jolt`, `IllumoPhysics`, `IllumoPhysicsTests`,
    `IllumoPublicHeaderSmoke`, `SaturnCore`, `SaturnTests`,
    `IllumoGuestPhysics`, `Saturn.wasm`, `SaturnPackage`,
    `SaturnWasmPackageTests` and `IllumoRuntime` with zero compiler warnings and
    zero `clang-tidy` diagnostics.
  - Verified `licenses/JoltPhysics-LICENSE.txt` and `apps/saturn/Saturn.wasm`
    staged under `build-workspace/Release/`.
- **Isolation verification:**
  - `IllumoPublicHeaderSmoke` enforces `!__has_include(<Jolt/Jolt.h>)`,
    `!__has_include(<jolt-5.6.0/Jolt/Jolt.h>)` and
    `!__has_include(<Illumo/Physics/PhysicsWorld.h>)` on `Illumo::Illumo`.
  - `TestPhysics.cpp` and `TestSaturnBehaviours.cpp` enforce
    `!__has_include(<Jolt/Jolt.h>)` and
    `!__has_include(<jolt-5.6.0/Jolt/Jolt.h>)` on `Illumo::Physics` consumers.
- **CTest (`13/13` passed):**
  - `Illumo.Build.NoExceptions` (`729` first-party C++ files clean)
  - `Illumo.PublicHeaders.ConsumerSmoke`
  - `Illumo.Physics.LifecycleAndHandles`
  - `Illumo.Physics.RigidBodyFallAndRest`
  - `Illumo.Physics.KinematicRaycastAndSensor`
  - `Illumo.Physics.CharacterGroundSlideStairsAndPush`
  - `Saturn.Behaviours.SchemaMatchesRegistry`
  - `Saturn.World.Loads`
  - `Saturn.World.PlayerMoves`
  - `Saturn.World.GravityAndJump`
  - `Saturn.World.ColliderWallAndRigidBody`
  - `Saturn.KeyToggle.ConsoleCapture`
  - `Saturn.Wasm.Package` (executes `Saturn.wasm` inside `WasmProgram`: world
    load, `3 nodes 2 behaviours`, isometric `D` movement, `Space` jump and
    gravity landing on `slab`, zero WASI import leaks)
