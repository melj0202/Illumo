# Codebase audit remediation

## Objective and authority

Resolve every concrete defect in the 2026-09-13 codebase audit (revision
`b6571e4c`, original report in the user's temporary directory), with evidence
for each finding. The user authorized the complete repair effort after F01.
The original report remains historical evidence. Current code, root/nested
guidance, architecture consensus, and design book determine implementation
constraints. The starting tree contains only the F01 source/test changes.

## Scope and constraints

Repair F01-F35 and the concrete architecture/documentation mismatches. Future
charter capabilities (physics/audio integrations, a corridor game, generic
object registration, Linux support) remain design milestones, not defects in
implemented capabilities. Preserve the library/product boundary, synchronous
token payload lifetime, main-thread GPU ownership, sparse v3 compatibility,
and deterministic transactional simulation. Do not commit or modify Git state.
Do not add dependencies without approval. Lead owns all edits and design;
read-only helpers may gather evidence.

## Design and sequence

1. Startup, asset bounds, queued callback lifetime: F01, F03-F06. Validate
   before memory access, publish only fully constructed state, detach command
   batches and identify registrations so retirement cancels pending callbacks.
2. Simulation and persistence: F02, F07-F09, F20-F27. Compare transition
   semantics, check numeric ranges before conversion, preserve prior files on
   errors, and commit state only after successful work.
3. Rendering and interaction: F10-F19, F28-F30. Preserve texture kinds and
   exact GPU state; establish explicit pass defaults, renderer lifetime
   identity, input edges/capture, and non-copyable ownership.
4. Build evidence: F31-F35. Align minimum syntax, generated build defaults,
   reserved names, and complete coverage/tidy source selection.
5. Synchronize canonical current-state prose and package descriptions; audit
   all findings against final source and validation, then close the plan.

Prefer small fixes within existing owners over replacement frameworks. Changes
to host close negotiation, pass ownership, persistence replacement, and renderer
lifetime identity will receive concrete designs in this plan before edits.
New optional APIs should preserve existing consumer behavior; invalid input
must return observable failure without partial mutation. No format migration
is planned. Each milestone remains separable for review/containment; avoid
mixing unrelated changes in a single editing step. Never reset user work as a
rollback mechanism.

## Verification

Add targeted regressions that exercise reported triggers, then build affected
targets. Run full Release build and labeled IllumoWorkspace CTest at coherent
checkpoints; clang-format all changed C++ and run configured clang-tidy.
Use sanitizer probes for memory hazards and real GL/native checks for findings
whose correctness cannot be established by token tests. Record unsupported or
blocked checks honestly; stress tests do not prove absence of races. Test
generated configure/build with the advertised CMake minimum. Verify coverage
and tidy inventory, not just tool exit status. Measure performance only when a
fix introduces material hot-loop cost or supports a performance claim. Rebuild
and inspect documentation after LaTeX changes. Review final diff and every
finding's acceptance evidence before declaring completion.

## Finding ledger

| Finding | Status | Required evidence |
|---|---|---|
| F01 worker startup | Implemented | Constructor starts after member initialization; 128-cycle lifecycle regression and publication test pass. Full Release 336/336; tidy 128 TUs. Race detector unavailable in configured Windows build. |
| F02 semantic memo invalidation | Implemented and tested | SparseMemoRuleSemantics restores warm neighborhoods, switches both different-tag and same-tag semantics; Release and Debug ASan pass. |
| F03 OBJ bounds | Implemented and tested | AttributeBounds rejects invalid/missing position/normal/UV references, huge indices, and invalid later shapes without partial output; valid relative references pass. Release and Debug ASan pass. |
| F04 callback enqueue/retirement | Implemented and tested | Reentrancy tests enqueue growth, deferred nested execution, self/later retirement, replacement, clear, and exception recovery. Release and Debug ASan pass. |
| F05 PBO rectangle bounds | Implemented and tested | TextureUploadBounds covers layout/overflow; explicit GPU test rejects report rectangle without pixel changes and verifies full/partial padded PBO uploads plus direct edge upload. |
| F06 cubemap channels | Implemented and tested | CubemapDecodedChannels checks exact RGBA bytes from native 1/2/3/4-channel faces/crosses in Release/Debug ASan; real GL checks six face readbacks and raw 2-channel rejection. |
| F07 config preservation | Implemented and tested | FailedLoadPreservation checks malformed/array/null/invalid legacy/trailing garbage/second object, explicit save and destruction, failed reload, repair, missing file, and unreadable directory. Release and Debug ASan pass. |
| F08 native close guard | Implemented and tested | Host/editor regressions, canceled approval in both module orderings, Release and Debug ASan native WM_CLOSE/SC_CLOSE checks pass; full Release 345/345. |
| F09 atomic saves | Implemented and tested | Failure injection preserves prior bytes through write/flush/close errors; collision cleanup and 244-character basename replacement pass in Release/Debug ASan. Both codecs replace existing files and round-trip; full Release 345/345. |
| F10 cubemap reload | In progress | Backend replacement implemented; six-face GPU readback and rejected replacement preservation pass. AssetManager source-kind/dependency routing remains pending. |
| F11 texture unit state | Implemented and tested | GPU sequence binds A on 0, B on 1, updates A, rebinds B on 1, and reads B pixels unchanged; upload restores active unit and unpack state. |
| F12 pass target restoration | Pending | Offscreen World followed by ordinary UI restores screen/viewport. |
| F13 clear semantics | Pending | All color/depth combinations and depth value. |
| F14 font renderer lifetime | Pending design | Address reuse and successive text captures. |
| F15 host pass ownership | Pending design | Start/Update client overrides survive host defaults. |
| F16 fullscreen state | Pending | Persist/log actual toggled state. |
| F17 viewer key edges | Pending | Press-release-press for every affected shortcut. |
| F18 selection drag offset | Pending | Stationary off-center click does not move/dirty object. |
| F19 3D picking | Pending | Raised/rotated bounds and nearest perspective hit. |
| F20 scene numeric range | Pending | Reject nonrepresentable floats/integers transactionally. |
| F21 imported IDs | Pending | Maximum/long IDs followed by creation retain uniqueness. |
| F22 camera range | Pending | Finite overflow and viewport margins rejected. |
| F23 coordinate endpoints | Pending | Bounded clipboard/elementary stepping at signed endpoints. |
| F24 plaintext detection | Pending | Commented plaintext, explicit formats, failed clipboard. |
| F25 RLE byte states | Pending | All 256 states round-trip unambiguously. |
| F26 registry validation | Pending | Reject B0/invalid shifts and malformed definitions transactionally. |
| F27 generation failures | Pending | Failure injection; successful count only and no partial publication. |
| F28 InputManager ownership | Implemented and tested | Four deleted copy/move operations; public-header static assertions and Release input lifecycle/context tests pass. |
| F29 allocator alignment | Pending | Over-aligned arena/stack/pool and growth/reuse. |
| F30 starter focus | Pending | Held keys, typing, history, and return from capture. |
| F31 CMake minimum | Pending | Consistent minimum and actual minimum configure/build. |
| F32 generated docs | Pending | Actual generated default build with documentation tools present. |
| F33 reserved names | Pending | Case-insensitive rejection before copying. |
| F34 coverage inventory | Pending | Four products' runners and production objects in coverage. |
| F35 tidy inventory | Pending | Generated application and capture tools selected. |

## Open decisions and risks

Host close, pass ownership, renderer identity, and atomic replacement designs
must stay bounded and backward compatible. Native platform and GPU validation
may require runtime permission. Failure injection should use narrow existing
seams where possible. The remaining audit defects can cause unrelated tests or
sanitizer checks to fail until their milestones are repaired; record rather
than conceal those failures.

## Checkpoint 1 evidence (2026-09-13)

- Full `cmake --build build --config Release` passed, including labeled CTest:
  342/342, 36.45 seconds (Illumo 154, IllumoGame 136, IllEd 39, viewer 13).
- Configured Debug MSVC AddressSanitizer targets built. Exact regressions for
  OBJ references, command reentrancy, cubemap channels, failed configuration,
  semantic memo changes, and runner lifecycle passed. The strict JSON fix was
  rebuilt and its Debug regression rerun afterward. This is not race detection.
- `IllumoCaptureGpuTests` Release: zero failures with actual hidden OpenGL
  contexts. Tests cover report rectangle rejection, full and partial PBO pixels
  with distinguishable padded rows, direct upload and unpack-state restoration,
  texture-unit cache sequence, and six raw cubemap faces. Existing capture
  failure/lifecycle checks also passed. This explicit executable stays outside
  the default headless suite and now links its existing GLEW dependency directly.
- Independent read-only review caught non-strict JSON trailing-input acceptance
  and weak uniform-color stride fixtures; both were repaired and rerun.
- Documentation book rebuilt to 66 pages; modified pages 13, 21, and 22 rendered
  for inspection. Existing Perl locale, LaTeX box/float warnings, optional
  FreeType dependency discovery warnings, and Debug ASan incremental-link
  warning remain. No new serious compiler diagnostics.
- Final `IllumoTidy` passed on 128 selected first-party translation units.
  Because F35 remains open, `CaptureGpuTests.cpp` was checked separately with
  clang-tidy against `build-capture-tidy`; it passed. `clang-format --dry-run
  --Werror` on every modified C++ file and `git diff --check` passed.
- No forced PBO-busy/map-failure injection or race detector was run. No
  performance improvement is claimed. The full report remains unfinished:
  F08-F10, F12-F27, F29-F35 and architecture-matrix reconciliation remain open.

## F08 close negotiation design

Native close currently exits at the runner's raw window-flag query before the
editor can show its existing confirmation. Add an optional main-thread
`IModule::OnCloseRequested()` callback that accepts by default, plus an explicit
host `processCloseRequest()` operation. Keep `shouldClose() const` a raw query.
The runner uses the explicit operation before terminating. Only started modules
participate; failed startup or failed required-module transition remains terminal.

On deferral, clear the native window close flag through an explicit window
cancellation operation so Cancel does not reopen confirmation on the next frame.
Implement cancellation for the GLFW window and in-tree test windows. Preserve
source compatibility for existing windows with a default method; document that
custom windows used with deferring modules must implement cancellation.

Editor acceptance uses a one-shot approved-exit flag. Dirty native close opens
the existing confirmation once; repeated requests preserve an already open
action. Save failure/Cancel stays open. Successful Save or Discard sets approval
before requesting close, so dirty Discard cannot reopen the prompt. Toolbar and
native requests converge on the same action logic. Approval is consumed during
negotiation and expires when Update resumes, including when an earlier module
vetoed before the editor was consulted. No engine dependency on
editor types and no concurrent callbacks are introduced.

Alternatives rejected: putting editor-specific checks in the runner; callbacks
inside a const state query; treating Discard as document mutation; resetting the
native flag without a subsequent approved-close path. Regression tests must
cover host default acceptance/deferral/retired modules and editor clean/dirty,
Cancel, failed Save, successful Save, and Discard. Native X/Alt+F4 and both Debug
and Release smoke remain required before F08 closes. Update lifecycle docs and
formal decision log with implementation.

F08 validation: final full Release build passed all 345 workspace cases in
51.28 seconds. Focused editor and host regressions passed, including Debug
ASan. Explicit Windows/OpenGL regression passed in Release and Debug using
WM_CLOSE and WM_SYSCOMMAND/SC_CLOSE, covering Cancel, failed Save, successful
Save, and Discard. These were native messages, not physical mouse/keyboard
gestures. Debug was rerun from build/Debug after a root-directory invocation
reported missing relative shader assets; the corrected run passed without
those diagnostics. Workspace IllumoTidy passed 131 translation units; the
explicit native test also passed a separate clang-tidy check. Formatting and
git diff --check passed. Existing Perl locale and Debug ASan incremental-link
warnings remain. No non-Windows smoke test was performed.

## F09 save publication design

Both codecs currently truncate before encoding and can report success before
close reports failure. Add a narrow engine-owned `AtomicFile::write` operation:
exclusively create a unique sibling with C++23 `noreplace`, stream the product's
unchanged encoding, check writer/flush/close, then replace the destination.
On failure remove only the staging file created by this call. The Windows
platform implementation uses same-directory `MoveFileExW(REPLACE_EXISTING)`;
no cross-volume copy fallback. Unsupported POSIX scaffolds use rename and
remain unvalidated. This prevents incomplete publication on reported I/O errors;
it does not claim power-loss durability or preservation of all destination ACLs.

A private per-call finalization seam permits injected flush/close failures and
deterministic staging collisions without global fault state or product API
changes. Tests assert old bytes survive each failure, replacement succeeds, and
owned staging files are removed while colliding preexisting files survive.
JSON encoding and sparse record layout stay in their respective products.
FrameCapture keeps its separate no-overwrite output contract. No file format
migration, background I/O, or dependency addition is introduced.

F09 validation: bounded independent sibling basenames preserve valid long
destination components; a 244-character basename replacement regression first
verifies the old file exists. Windows extended-path syntax isolates this test
from legacy MAX_PATH restrictions. All atomic-file regressions passed in
Release and Debug ASan. The final full Release build passed all 345 workspace
cases in 38.16 seconds, including both product codec replacements and editor
failed-save dirty-state preservation. IllumoTidy passed 131 translation units;
formatting and diff checks passed. Independent review found no remaining F09
defects. The design book rebuilt to 67 pages; rendered platform and decision
pages 23 and 59 were inspected without clipping or overlap. Existing LaTeX,
Perl locale, and ASan incremental-link warnings remain. POSIX behavior and
power-loss durability were not verified and are not claimed.

## F10 cubemap reload design

Preserve a texture's source kind (2D, six faces, or cross) and canonical source
dependencies in its asset entry and CPU job. Decode all six faces before any
GPU replacement. Add a narrowly scoped optional backend ReplaceCubemap method
(default rejection for external backends), exposed through Renderer. Replacement
preserves the handle and texture kind, and retires the old resource only after
the complete replacement succeeds. Reject a cubemap passed to ReplaceTexture.
Track every face for explicit path reload and timestamp polling. Failed reload
keeps Ready state, prior revision and resource, with an observable error.
Initial and reload decode paths share orientation and RGBA normalization.
Keep the existing worker/main-thread pump ownership and stale-result guards.
Tests cover kind rejection, six-face pixels before/after replacement, missing
or mismatched faces, dependency reload, stale requests and failed publication.

F10 backend checkpoint: Renderer forwards the optional backend cubemap
replacement operation. GLBackend rejects kind changes and publishes only a
constructed cubemap; GLTexture validates the GPU size limit, verifies all face
allocations, and restores cube binding and unpack state. MockBackend tracks
cubemap kind through destruction and slot reuse. Release CubemapReplacement
and CubemapDecodedChannels pass. Real OpenGL six-face before/after readback and
oversized-replacement preservation pass. Full Release: 346/346 in 36.12 seconds.
IllumoTidy passed 131 translation units and the explicit GPU tool passed its
separate tidy check. Independent review found no blockers. Oversize rejection
is preflight evidence, not an injected real GPU allocation failure; dedicated
nondefault cubemap unpack-state regression remains useful. F10 is not closed:
AssetManager source kinds, six canonical dependencies, shared decoding,
revision initialization, path reload and polling, failure/stale-result tests,
and final canonical documentation synchronization remain to implement.
