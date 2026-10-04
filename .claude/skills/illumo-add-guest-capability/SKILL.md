---
name: illumo-add-guest-capability
description: Add or change what a WASM guest can ask the Illumo host for - a new GuestService operation, GuestCapability bit, host import module, manifest flag, or frame/wire schema change - with the decoder and deny tests AGENTS.md requires. Use when a guest app (game, IllEd, mesh viewer, Playground, mods) needs a new host service, or when changing the guest ABI or frame format. Carries known traps.
---

# Add a guest capability or service

Binding rules (AGENTS.md): `IllumoRuntime` and `Illumo/Source/Wasm` stay
product-agnostic - no product-specific imports or opcodes. Only copied
envelopes, services and frames cross the ABI; host paths never do. **Frame
schema changes, new capabilities or new imports require decoder/deny tests in
the same change.** Read `docs/wasm-game-runtime-design.md` §6 (ABI, isolation)
and §9 (other services) first. A new capability is usually a decision: finish
with the `illumo-record-decision` skill.

## 1. Choose the shape

- **Service operation** (request/response through the guest service queue):
  most new features (files, dialogs, audio, windows, launch).
- **Capability bit**: when the operation must be grantable/deniable per
  guest.
- **Import module** (e.g. `illumo_profile`, defined in `WasmInstance.cpp`
  through `wasmtime_linker_define_func`): only for hot, synchronous calls
  where a queued request is too slow. Needs explicit justification.
- **Frame schema** (`IllumoGuest/Include/IllumoGuest/Frame.h`): only for
  per-frame render data; bump the documented version and keep older versions
  decodable where the file's comments say so.

## 2. Wire it through (typical service)

1. `IllumoGuest/Include/IllumoGuest/Services.h`: add the `GuestService`
   value at the end. **Update the decoder's upper bound**: validation rejects
   `operation > GuestService::<last>` (currently `LaunchApp = 19`), so a new
   value is silently rejected until that check moves.
2. `IllumoGuest/Include/IllumoGuest/Protocol.h`: if gated, add a
   `GuestCapability` bit (next after `Launch = 1u << 13u`) with a comment
   naming its service.
3. Host dispatch in `Illumo/Source/Wasm/WasmGameServices.cpp` (or the
   matching `Wasm*Services.cpp`): decode the payload strictly, check the
   capability, and reply `GuestServiceStatus::Rejected` on any failure. Never
   trust guest lengths, offsets or handles; validate against the guest's
   memory and `WasmResourceTable`.
4. Guest SDK wrapper in `IllumoGuest/Include/IllumoGuest/<Area>.h` plus
   `IllumoGuest/Source/`, and the product's platform seam if a product uses
   it (`IllEdPlatform.h`, `MeshViewerPlatform.h`, the IllumoGame adapter).
5. Runtime wiring and command-line options: `Illumo/Source/Wasm/RuntimeApplication.cpp`.

## 3. Granting

- **Every offered capability is effectively granted to every guest.** The
  real gate is a package manifest flag (pattern: `app.launchApps` in
  `Illumo/Source/Content/PackageManifest.cpp`, set only in IllEd's
  `illumo.json`). Add a manifest flag for anything sensitive, and a manifest
  decoder test.
- Manifests request budgets; the runtime grants at most its ceilings. Launch
  options are never persisted.
- Writable storage is only `--project`; it refuses `*.wasm` and
  `illumo.json`.

## 4. Tests (required)

Put them under `Illumo/Tests/Wasm/` beside the closest existing file
(`TestWasmLaunch.cpp`, `TestWasmFiles.cpp`, `TestWasmAudio.cpp`,
`TestWasmWindows.cpp`, `TestWasmFrame.cpp`, ...):

- **Decoder:** valid round trip; truncated, oversized, wrong-version and
  out-of-range operation payloads are rejected.
- **Deny:** a guest without the capability or manifest flag gets `Rejected`,
  and nothing happens on the host.
- **Package-level** when a real app uses it (e.g. `IllEd.Wasm.Package`,
  `IllumoGame.Wasm.GamePackage*`). Assert on effects: guest log lines never
  reach a package-test harness console.
- **SDK contract:** `Illumo.Wasm.SdkContract` runs inside one 10M-fuel
  `start()` call that is nearly full, so put new contracts in `update()`.
  Tracy builds use more fuel.

## 5. Traps

- The guest service queue caps at **32 outstanding requests**
  (`GuestServices::MaximumRecords`). Bursts (scene changes) saturate it, and
  log lines are dropped while it is full. Defer non-urgent requests to
  `pump()` and handle refusal; never assume a send succeeded.
- A dropped frame reaches the host with no compositions; guest state that
  assumes delivery (`same` compositions) must be reset (`dropVisuals()`).
- Allocation gates (`IllumoGame.Wasm.PackageFrameAllocations`,
  `LaneAllocations`): per-frame service paths must not heap-allocate after
  warm-up. Use retained buffers.
- No exceptions (D-F3): return errors; `illumoFatal` only for programming
  errors or exhausted memory or threads.
- Line endings are mixed (`WasmProgram.cpp` and `IllumoGuest/CMakeLists.txt`
  are CRLF); preserve each file's style.
- Host keys: F5 is reserved by the host (asset reload).

## 6. Finish

Build the guests and runtime, then run the focused `Illumo.Wasm.*` tests and
the affected package tests, then the full labeled suite (`illumo-verify-change`).
Update `docs/wasm-game-runtime-design.md` and the package docs, and record
the decision.
