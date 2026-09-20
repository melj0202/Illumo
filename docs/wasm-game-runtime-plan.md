# Whole-game WASM migration execution plan

Status: design and dependencies approved; implementation in progress.
Design: [wasm-game-runtime-design.md](wasm-game-runtime-design.md).
Baseline inspected: `6050a373`, `release/v26.09`, clean before proposal files.

## Objective

All IllumoGame production behavior executes under isolated WASM, using a generic
native Illumo host that also runs another game and an isolated sample mod. No
native game fallback, CA-specific host imports or native simulation shortcuts.

## Milestones and exit evidence

| Step | Work | Exit evidence |
|---|---|---|
| 0 | Approve design/dependencies; prove pinned Windows C++ guest/runtime compatibility; capture native baselines | C++23/STL/exception/allocation/SIMD smoke; two isolated stores; interruption/cleanup; dependency license/hash/CRT assessment; baseline results |
| 1 | Define versioned ABI, guest SDK skeleton, manifest and generic native adapter | Minimal unrelated guest loads/updates/exits; unsupported ABI/capability fails; no public Wasmtime types |
| 2 | Implement resource tables, bounded event/service messages, execution limits and teardown | Cross-owner/stale-handle, malformed-memory, trap/unload/late-callback tests; native bridge ASan |
| 3 | Build guest rendering/UI helpers and copied frame/instance adapter | Packet fuzzing; painter/text/clip/upload/shadow parity; guest-memory destruction after acceptance; GPU capture |
| 4 | Port all CSim rule/grid/codec/pattern/view logic into WASM | Actual WASM state-hash and save-format parity; no native CA imports; memory/quota failure tests |
| 5 | Replace SimulationRunner/pool boundary with WASM worker jobs | Responsive control guest; one outstanding generation; revision/epoch validation; full-state resync and publication tests; measured copy/compute cost |
| 6 | Port application definition, menus, commands, settings, dialogs/clipboard, catalogs and transitions | Full CSim play/edit/save/load/workshop/close flows through guest; legacy path/config migration; native GUI smoke |
| 7 | Expose game-owned mod API and second game template | Visible isolated mod behavior; failing mod leaves base game alive; second package on identical host |
| 8 | Optimize within guest boundary as required and cut over shipping target | Native linkage audit; full Release/CTest/tidy/ASan; GPU/UI acceptance; performance/capacity gates |
| 9 | Synchronize architecture/guidance/notices and handoff | Reviewed final diff, rebuilt docs, reproducible build/run instructions and complete verification ledger |

Steps may overlap only where their contracts are stable. A stub or rules-only
guest does not close steps 4-8. The lead owns design, edits and integration;
read-only helper reviews can verify bounded contracts independently.

## Constraints

- Preserve the engine/product boundary and main-thread native graphics execution.
- All CSim algorithms and product policy stay in guest code, including helpers
  dispatched as jobs. No native `SparseCellGrid` behind a generic-looking opcode.
- Preserve save-format compatibility, topology, deterministic rules and UI input
  precedence. Source API migration is intentional; behavior regressions require
  explicit discussion.
- Native reference test targets may retain product code during migration. The
  shipping native target and all its libraries may not.
- Preserve CMake authority and separate native/WASM build trees. No dependency
  auto-download without a pinned verified source. No commits/branches/pushes.
- Existing editor/viewer tools remain native consumers. Guest memory and host
  resource quotas must be visible, bounded and tested.

## Validation strategy

Use current exact native cases as behavioral specifications and run corresponding
guest tests in the selected runtime. Extend the CTest workspace label with guest
contract/parity cases. Add a target dependency/source manifest check, module import
inspection, isolation adversaries, decoder fuzzing, native ASan, GUI smoke and
capture parity. Build both Debug and Release where lifetime/input behavior differs.

Performance gates and workloads are in design section 13. Record results with
the baseline configuration. Failure of a performance gate means more work or an
explicitly revised target, not a native shortcut. Native coverage alone cannot
prove guest behavior; preserve its gate and report guest evidence independently.

## Failure containment / rollback

Before cutover, retain the existing shipping build as the reference while new
guest targets are incomplete. New targets must not silently fall back to native
game code. Each milestone must leave unrelated native consumers buildable.
Keep protocol/version changes explicit. Trap/unload immediately revokes handle
authority and registrations, discards uncommitted frames and cancels pending
requests where possible, independent of guest shutdown. Accepted-frame and
in-flight-operation leases defer physical resource destruction. Already accepted
file operations have separate completion semantics, not implicit rollback.
Preserve the last valid published frame/world where the protocol permits. Never
reuse a trapped mutable instance as if its last operation were transactional.

## Open gates

- Selected Wasmtime/WASI SDK pair must pass C++ exception and allocator tests.
- Serialized worker deltas and wasm32 memory limits must support measured CSim
  workloads; parallel work strategy remains conditional on those measurements.
- Final ABI field layouts, capability profiles and bounded packet schemas are
  frozen and tested in steps 1-3 before migrating all product consumers.
- Design and dependency addition approved by the user's "proceed" on 2026-09-19.
  Continue within the approved boundary without requesting approval again.

## Verification ledger

2026-09-19, implementation started:

- User approved the full design, Wasmtime/WASI toolchain dependencies and
  isolated worker model. Milestone 0 compatibility verification is in progress.
- Native Release baseline: all 450 workspace tests passed. Sparse benchmark
  output is retained in the ignored `build-wasm-baseline/sparse.txt`.
- SHA256-verified Wasmtime 48.0.2 C API/CLI and WASI SDK 34.0 Windows x64
  downloaded into ignored `build-wasm-tools`; explicit repeatable bootstrap
  is `tools/bootstrap-wasm.ps1`. No global installation or PATH change.
- Added an opt-in `ILLUMO_BUILD_WASM_RUNTIME` migration target. Seven native
  sandbox tests pass with a compiled C++23 guest: constructors/STL/exceptions/
  SIMD/allocation, separate state, trap retirement, fuel, epoch interruption,
  memory growth/range checks, rejected imports and invalid modules.
- Production grid, rules and pattern sources compile in a separate WASI CMake
  tree. A serial guest scheduler selects existing kernels. Native/WASM domain
  state-hash parity passes for every shipped rule, both topologies, three
  workloads and eight generations. This is intermediate correctness evidence,
  not the worker performance gate or whole-game completion.
- WASI imports are explicitly enumerated: empty environment, bounded clock
  outputs, denied file descriptors/preopens, and trapping process exit. The
  Wasmtime WASI linker is never enabled. Game service capabilities remain open.
- Observed existing Clang warning: unused `kCountedState` in
  `RuleSetRegistry.cpp`. Formatting/tidy/ASan/full post-change Release and
  interactive acceptance remain open, as do the shipping cutover milestones.
- Shared `IllumoCodecStreams.cpp` now builds natively and as WASM. All 35 rules
  pass identical v4 save-byte and restore comparisons across the parity matrix.
  Existing native save/load, malformed-save, overlapping-load and direct-codec
  tests pass after the split.
- Independent runtime review exposed unbounded in-process compilation and two
  weak security assertions. Compilation now runs in a bundled, suspended-then-
  job-contained Windows helper with a private inherited mapping, a 1 GiB default
  process-memory ceiling and 30-second default deadline. Only that helper's
  serialized artifact is deserialized; user-supplied native artifacts are never
  accepted. Fuel and epoch tests now assert distinct trap classifications;
  forbidden-import test uses the actual WASI signature.
- Ten sandbox tests now pass, including asynchronous persistent worker requests,
  one outstanding job, copied responses, terminal worker failure, wire bounds,
  compiler deadline, exception unwinding and catchable allocation failure.
- Targeted configured clang-tidy passed for the runtime, worker and Windows
  compiler containment sources after fixing one pointer-conversion diagnostic.
  Full post-change Release build/CTest is running. The worker is still generic;
  CSim worker protocol/control integration and all render/service migration remain
  open. No shipping target has been cut over.
- Full Release validation at the domain/worker checkpoint passed 461/461 tests.
  The subsequent font-layout split and lifecycle/rendering additions require a
  fresh final full build. The complete CSim-facing source surface compiles into
  a WASI archive, but this archive is not linked into a runnable game.
- Debug ASan exposed recursive exception dispatch between Wasmtime's Windows
  vectored handler and ASan's lazy shadow-page handler. Debug now uses explicit
  guest bounds checks (`signals_based_traps=false`, with the associated required
  Spectre settings disabled); Release retains normal fault-based traps and
  mitigations. Compiler and host share the exact configuration. This is a
  sanitizer profile, not a production security configuration or performance run.
- Fifteen native ASan tests pass: the original runtime/worker tests plus actual
  WASM lifecycle sequencing and capability negotiation, owner/type/generation
  resource authority, retained-frame leases, transactional packet decoding,
  deterministic malformed-packet mutations, and copied 2D Renderer submission.
  The current packet schema supports Shape/Sprite/Canvas triangle batches;
  world-instance/shadow records, guest resource requests, font transfer, service
  completion routing and application integration remain open. MockBackend tests
  do not establish GPU pixels or complete game behavior.

2026-09-19, control and presentation bridge checkpoint:

- Added the reusable guest recording backend, asynchronous texture/font/log
  services, copied font metrics, input snapshot adapter and guest-local window
  values. The existing Renderer and GameVisual execute in an actual WASM
  reactor. A captured OpenGL frame shows text, clipping, sprites and painter
  order. This is the SDK presentation fixture, not the CSim application.
- An unrelated Paddle game and separate palette mod run in distinct stores.
  Wrong-role and faulting mods are revoked while the base game continues.
- Added generic asynchronous jobs to the control service route. An actual
  control guest submits work, consumes completion on later frames, then
  survives a trapping worker. Eight focused Release host/frame/service tests
  pass at this checkpoint. The new SDK regression fixture is being built.
- Fixed input snapshots to use processed Press/Hold state and overlay masks.
  Review also identified ordered texture writes, release-queue backpressure,
  failed replacement retention, font retry/cache retirement and command-prefix
  publication risks. Fixes are implemented; targeted regression validation is
  in progress. Shared native font/input/environment splits still need the full
  Release suite, updated ASan and configured analysis.
- Full CSim control composition, asynchronous product persistence/dialogs,
  console routing, worker publication/mutation coordination, 3D instances,
  performance acceptance, dependency-notice closure and shipping cutover remain
  open. The shipping IllumoGame target still runs its native reference code.

2026-09-20, host clipboard/console/dialog checkpoint:

- Display, clipboard, console registration/listen, and native-dialog grants
  now have host-side service tests. Dialog success yields a selected-file
  capability name rather than a host path. Console trampolines unregister on
  guest retirement. CSim still has to adopt these services in a runnable
  control guest; the shipping executable remains native.

2026-09-19, historical proposal preparation (before approval):

- Read live product build/factory, simulation, rendering, service and lifetime
  paths; reconciled three read-only helper analyses against repository evidence.
- Checked upstream Wasmtime C API, release/license and WASI SDK compatibility
  documentation. No dependency installed, downloaded into the project or linked.
- Prepared design and this execution plan. No production/CMake/guidance changes.
- No new builds, runtime smoke tests, performance measurements or acceptance
  tests have run. All implementation milestones remain open.
