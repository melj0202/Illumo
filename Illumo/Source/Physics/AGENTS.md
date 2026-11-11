# Illumo physics wrapper guidance

This directory implements the optional physics wrapper (`PhysicsWorld.cpp`),
compiled into `IllumoPhysics` (`Illumo::Physics`) on the host and
`IllumoGuestPhysics` in WASI guest builds. Public contracts live in the
separate include root `Illumo/Physics/Include/Illumo/Physics/`
(`PhysicsTypes.h`, `PhysicsWorld.h`).

## Invariants

- Jolt Physics (`Illumo/thirdparty/jolt-5.6.0`, `Illumo/cmake/IllumoJoltDeps.cmake`)
  is private to `PhysicsWorld.cpp`. Never link `illumo_jolt` or
  `Illumo::Physics` into `Illumo::Illumo` or `Illumo::Content`, and never place
  physics headers under `Illumo/Include/`. `IllumoPublicHeaderSmoke` enforces
  that neither `<Jolt/Jolt.h>` nor `<Illumo/Physics/PhysicsWorld.h>` is visible
  to core `Illumo` consumers.
- Feature macros (`JPH_CROSS_PLATFORM_DETERMINISTIC`, `JPH_NO_DEBUG`, plus WASI
  or Debug ASan switches) participate in `JPH_VERSION_ID` and must come from
  `illumo_jolt` so `PhysicsWorld.cpp` and Jolt agree on ABI and object layouts.
- WASI builds compile `illumo_jolt` with `-DJPH_PLATFORM_WASM
  -Wno-builtin-macro-redefined -U__wasm_simd128__ -msimd128 -fno-exceptions` and
  omit `Semaphore.cpp`, `JobSystemThreadPool.cpp` and
  `JobSystemWithBarrier.cpp` so no `<immintrin.h>` x86 shim or forbidden WASI
  syscall import is pulled into `.wasm` modules.
- Process-wide Jolt allocator and `JPH::Factory::sInstance` registration is
  reference-counted across live `PhysicsWorld` instances (mutex-guarded on the
  host, serial under `ILLUMO_SERIAL_GUEST`).
- Steady-state `PhysicsWorld::step` runs on `JPH::JobSystemSingleThreaded` and
  `JPH::TempAllocatorImpl` (falling back to `TempAllocatorMalloc` only when
  Debug ASan defines `JPH_DISABLE_TEMP_ALLOCATOR`), with pre-reserved reverse
  lookup and sensor overlap buffers so simulation steps do not heap-allocate.
- GLM `Quaternion` uses `(w, x, y, z)` constructor order whereas `JPH::Quat`
  uses `(x, y, z, w)`; always convert through the helper functions in
  `PhysicsWorld.cpp`.

## Verification

`Illumo.Physics.*` (`IllumoPhysicsTests`), `Illumo.PublicHeaders.ConsumerSmoke`,
`Illumo.Build.NoExceptions`, `Saturn.*` (`SaturnTests`) and
`Saturn.Wasm.Package`. Record: `docs/jolt-physics-plan.md`.
