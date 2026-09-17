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
| F10 cubemap reload | Implemented and tested | Shared initial/reload RGBA decode, all six canonical dependencies, polling, revision/failure/stale-work preservation; Release and Debug ASan regressions pass. Backend GPU readback passes; fresh full Release 347/347. |
| F11 texture unit state | Implemented and tested | GPU sequence binds A on 0, B on 1, updates A, rebinds B on 1, and reads B pixels unchanged; upload restores active unit and unpack state. |
| F12 pass target restoration | Implemented and tested | Explicit screen target before clear and ordinary layers; unknown framebuffer cache at submission start. Layer-token regression, real OpenGL cross-submission binding/viewport check, full Release 348/348 pass. |
| F13 clear semantics | Implemented and tested | Independent color/depth pass tokens and defaulted depth value; all four combinations checked by tokens and GPU buffer readback. Final full Release 349/349; public aggregate compatibility smoke passes. |
| F14 font renderer lifetime | Implemented and tested | Weak lifetime identity; address reuse, alternating renderers, retirement and successive GPU text captures. |
| F15 host pass ownership | Implemented and tested | Separate fallbacks preserve lifecycle overrides; transitions detach callbacks. Release 351/351, Debug ASan and tidy passed. |
| F16 fullscreen state | Implemented and tested | Window owns resulting setting; host reports it without inversion. Release 351/351 and tidy passed. |
| F17 viewer key edges | Implemented and tested | Queued Press actions; repeated O/F/R/G/X/B, repeat suppression, console gating and independent instances. |
| F18 selection drag offset | Implemented and tested | Initial constraint-plane offset preserved; stationary 2D/3D press/hold/release stays clean. |
| F19 3D picking | Implemented and tested | Nearest transformed bounds; parent scale, elevation, rotation and perspective rays. |
| F20 scene numeric range | Implemented and tested | Range checks before float/int narrowing; transactional rejection and finite boundary round trips. |
| F21 imported IDs | Implemented and tested | Opaque imported names; collision-aware allocation. Final Release 357/357 and tidy passed. |
| F22 camera range | Complete | Shared coordinate policy, checked input/metadata, safe cache jumps; 359/359 Release tests. |
| F23 coordinate endpoints | Complete | Bounded clipboard loops and guarded elementary endpoints; 359/359 Release tests. |
| F24 plaintext detection | Complete | Explicit format routing and no stale Ctrl+V fallback; 361/361 Release tests. |
| F25 RLE byte states | Complete | Braced state tokens round-trip all 256 values; 362/362 Release tests. |
| F26 registry validation | Complete | Strict transactional catalog validation and safe counts; 363/363 Release tests. |
| F27 generation failures | Implemented and tested | Successful count only, paused explicit retry, transactional elementary publication and cached-source invalidation. Full Release 366/366; focused Debug ASan and tidy pass. |
| F28 InputManager ownership | Implemented and tested | Four deleted copy/move operations; public-header static assertions and Release input lifecycle/context tests pass. |
| F29 allocator alignment | Implemented and tested | Actual-address alignment and aligned backing; 256/4096-byte growth/reuse/cap tests. Release 367/367, ten Debug ASan allocator cases, tidy 131 TUs pass. |
| F30 starter focus | Implemented and tested | Capture gates controls, held keys require release, instance-owned edges. Generated Release 212/212 including ConsoleCapture; main Release 367/367; two template TUs pass tidy. |
| F31 CMake minimum | Implemented and tested | Consistent 3.25 minimum; actual portable 3.25.0 Ninja/Clang Release configure/build and 367/367 CTest pass; generator tests pass. |
| F32 generated docs | Implemented and tested | Full generated default build with PowerShell/LaTeX present passes; explicit missing-source ON rejected. Main PDFs and Release 367/367 pass. |
| F33 reserved names | Implemented and tested | Case-insensitive rejection before writes; preservation tests, generated full build and default/custom configurations pass. Shared staging prerequisite repaired; main Release 367/367. |
| F34 coverage inventory | Complete | All registered runners measured; 85.19% gate, 372 Debug and 370 Release tests pass. LLVM profile mismatch warning retained as a measurement limitation. |
| F35 tidy inventory | Complete | Main batch 134 files and generated AuditApp batch 89 files pass; inventory and failure-propagation regressions pass. |
| Rules/platform ownership | Complete | Product loader and startup composition; 373 Release, 375 coverage tests, four custom-catalog startup scenarios pass. |
| Wrapped dependency exposure | Complete | GLM-only public include tree; consumer checks, generated application build, minimum-CMake synchronization test and 373 Release tests pass. |
| Current architecture prose | Complete | README, canonical prose, package maps, book, and chart inventory reflect three applications, four runners, world rendering/offscreen passes, and multi-item attachments. Historical decisions preserved. |

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

F10 completion supersedes the pending work in the backend checkpoint above.
AssetManager now routes source-aware jobs, shares initial/reload decoding,
initializes cubemap revision to one, tracks all canonical face dependencies,
and preserves prior ready resources after decode or backend failure. Explicit
path reload rejects empty input rather than matching unused dependency slots.
Initial cubemap acquisition stays synchronous for compatibility; reloads use
the CPU queue and main-thread pump. Release and Debug ASan tests cover six path
reloads, sixth-face timestamp polling, cross reload, missing/nonsquare faces,
backend rejection/recovery, release during queued work, and kind/slot reuse.
The final full Release build passed 347/347 tests in 40.85 seconds in the fresh
build-f10-validation tree. The original build tree produced repeated PDB/lib/
object access failures; fresh configure/build resolved validation without
changing source build flags. Documentation was rebuilt separately and rendered
page 12 was inspected. IllumoTidy passed 131 translation units; formatting and
diff checks passed. Earlier backend GPU before/after face readback remains
applicable. Actual GPU out-of-memory injection and non-Windows validation were
not performed. No remaining F10 review findings. F12 is the next open finding.

F12 completion: frame setup now binds the screen before its clear. Ordinary
layers emit their framebuffer and full-window viewport before drawing, so a
custom offscreen World pass cannot capture UI/Debug output. GLDevice starts
its framebuffer binding cache unknown, preserving duplicate suppression only
after a real bind in that submission. OrdinaryLayerTargetRestore checks initial
clear and all three draw targets/viewports. Existing prefix/shadow-pass tests
were updated to assert the new explicit initial screen bind. A real OpenGL
regression binds offscreen in one submission and verifies screen framebuffer
and viewport restoration in the next. Full Release passed 348/348 in 36.93
seconds in build-f10-validation. IllumoTidy (131 TUs), explicit GPU-tool tidy,
formatting and diff checks pass. Documentation rebuilt and rendered page 11
was inspected. Debug ASan was not repeated for this binding-state-only change;
real Release GPU evidence covers the affected native behavior. F13 remains
open: clear-mask/depth-value semantics are a separate next fix.

F13 completion: pass clear flags emit independent color-only/depth-only tokens,
with the requested depth value. RenderCommand appends a defaulted depth member
after the existing union to preserve positional aggregate source compatibility;
public-header smoke checks that form and trivial copyability. The existing
no-argument pushClearDepth remains and delegates to depth one. OpenGL explicitly
sets the depth value for depth, screen and all-buffer clears. PassClearMasks
checks all four combinations. GPU readback confirms unchanged buffers, depth
0.25, and a later default depth clear restoring 1.0. Final Release passed
349/349 in 37.94 seconds; GPU regression passed against the final token layout.
IllumoTidy (131 TUs), explicit GPU-tool tidy, formatting and diff checks pass.
Independent review's aggregate-initialization issue was corrected. Documentation
rebuilt and rendered page 11 was inspected. Debug ASan was not repeated for this
clear-state change; real Release OpenGL covers its native execution. F14 is next.

## F14 renderer lifetime identity design

Give each Renderer an opaque shared identity owned solely by that renderer;
expose weak identity for non-owning caches. Font keys atlas enrollments by weak
ownership identity, prunes expired entries, and checks backend validity before
reuse. No cache entry owns or dereferences an old Renderer. Alternating live
renderers retains one enrollment each. Null lookup preserves the last handle
only while its renderer lifetime remains live. GPU resources retain existing
backend ownership and are released at backend shutdown. Test deterministic
placement reuse at one address, alternating live renderers, explicit texture
retirement, and repeated real text captures. This is a bounded cache identity
repair, not a general renderer ownership framework.

F14 completion: Font now caches atlas handles by weak renderer lifetime identity
instead of a raw renderer address. Expired entries are pruned; explicit resource
retirement triggers enrollment again. Renderer invalidates its identity before
backend shutdown. The focused regression covers placement reuse at the exact
same address with an unrelated aliased handle, alternating live renderers without
repeated enrollment, null lookup after destruction, and retired texture recovery.
It passes in Release and Debug ASan. Three successive real OpenGL text captures
produce visible, identical glyph pixels while retaining the same Font. The full
Release suite passes 350/350 tests. IllumoTidy passes all 131 translation units;
the GPU tool also passes its separate tidy check. Independent review found no
blockers. Documentation was rebuilt to 68 pages and rendered page 12 inspected.
Formatting and diff checks pass. Non-Windows execution was not tested. F15 is
the next open finding.

## F15 host default and application pass design

Scene retains separate per-layer host defaults and application overrides.
Existing SetLayerPasses installs an override; an empty list, ClearLayerPasses,
or ResetDefaultPasses removes the override and reveals the current default.
passesIn and hasCustomPasses continue describing the effective render sequence.
The additive SetDefaultLayerPasses API updates only fallback descriptors. The
host uses it exclusively for motion blur; setting changes cannot erase client
passes installed in Start, Update or dispatch. Standalone Scene behavior remains
unchanged without installed defaults. No resource ownership, threading or token
execution changes are needed. Separate fallback storage avoids fragile identity
inference from pass names or copies. Validate lifecycle overrides, blur toggles
and parameter changes under overrides, restoration, full Release and tidy;
document the precedence contract and review independently.

Review identified callback lifetime across required-module replacement. A
transition resets all application overrides before and after retiring required
modules, and after rejected replacement startup; host defaults survive. Because
Scene has no per-module pass ownership, optional modules must reinstall their
overrides after transition. Tests cover successful and rejected replacement.

F15 validation checkpoint: the final full Release build passed 351/351 tests
in 48.97 seconds. ScenePipelineOverrides covers Start, Update, dispatch, blur
toggles/parameter changes, clear/empty/reset restoration, successful and rejected
transitions, and reinitializing the same host. The host resets its blur cache
when constructing a new Scene. IllumoTidy passed all 131 translation units.
Independent review found no remaining blockers after transition detachment.
Canonical docs and decision D-RP1 were rebuilt; pages 11 and 12 were rendered
and inspected. Formatting and diff checks pass. The focused regression also
passes in Debug ASan, including callback retirement and reinitialization. Live
native-window and non-Windows checks were not repeated for this Scene/host
coordination change; rendering and lifecycle are exercised with MockBackend.
F15 is complete; F16 is the next open finding.

F16 implementation: remove the second fullscreen inversion in the global F11
handler. The window remains responsible for publishing its resulting state;
the host reports that state. CountingWindow now models the native environment
update, with a rejection mode. GlobalHotkeys checks entering, leaving, rejected
entry, and each resulting console message. IllumoTidy passed 131 translation
units; documentation rebuilt and rendered page 10 was inspected. Formatting and
diff checks pass. Full Release passed 351/351 in 37.13 seconds. No native fullscreen
monitor transition or additional ASan run was performed for this state-only
host change; the unchanged native toggle was inspected and its contract modeled.
F16 is complete; F17 is the next open finding.

F17 completion: replace block-local static latches with queued Press handling
through the existing MeshViewerAction dispatcher. Hold/repeat and Release do
not trigger shortcuts; unrelated events are retained. RepeatedShortcuts checks
three cycles of O/F/R/G/X/B, cancel/reopen via a headless dialog stub, console
suppression/recovery, unrelated keys, and a second instance. Full Release passed
352/352 in 67.15 seconds. IllumoTidy passed 131 translation units. Documentation
rebuilt and rendered page 10 was inspected; formatting and diff checks pass.
Native file-dialog interaction and additional ASan were not run for this input
dispatch change. The dialog stub verifies calls and cancellation, not OS UI.
Independent review found no correctness issues. F18 is the next open finding.

F18 completion: body selection records initial constraint-plane hit minus object
origin, matching the subsequent drag calculation. Failed initial intersection
retains selection without starting a drag. BodyDragOffset verifies off-center
press/hold/release without movement or dirty state in 2D and 3D, followed by a
real drag that moves and marks dirty. The existing movement epsilon remains;
no new pointer-distance threshold changes small deliberate drags. Full Release
passed 353/353 in 61.40 seconds. IllumoTidy passed 131 translation units.
Documentation rebuilt and rendered page 10 inspected; formatting and diff
checks pass. Additional ASan and native mouse interaction were not repeated
for this arithmetic/state fix; headless input exercises both coordinate modes.
F19 is the next open finding.

## F19 ray-picking design

Add EditorDocument::pickRay and route 3D body selection through screenToWorldRay.
Intersect transformed local primitive bounds using slab intervals, preserving
the original ray parameter through inverse transforms so nearest-hit comparison
works with nonuniform and parent scale. Bounds match EditorAttachment recipes
(including maximum-radius spheres and thin 3D rectangles). Empty nodes retain
their small selection proxy. Ignore nodes hidden/disabled by themselves or an
ancestor; skip singular/nonfinite transforms and invalid rays. Ties preserve
reverse document-order priority. Keep existing 2D pick and ground-plane placement.
3D selection must work even when the ray cannot intersect the ground. This is
oriented-bound picking, not triangle-exact mesh picking or a spatial index.
Test nearest order, elevation, rotation, parent/nonuniform scale, hidden parents,
singular transforms and real perspective screen rays. Run full Release, tidy,
independent review and synchronize canonical docs before closing F19.

F19 completion: pickRay transforms rays into each eligible node's local space
without renormalizing their direction, intersects recipe-aligned bounds and
selects the smallest nonnegative parameter. Inherited hidden/disabled state,
singular/nonfinite transforms and invalid rays are excluded. The module no
longer gates 3D body picking on ground intersection. RayPicking verifies nearest
order under unequal scale, elevated/rotated hits and misses, parent scale and
reflection, hidden/disabled ancestry, singular matrices and invalid/backward
rays. PerspectivePicking uses actual module screen rays, including a horizontal
ray with no ground hit and an elevated rotated nearest body. Full Release passed
355/355 in 67.11 seconds; IllumoTidy passed 131 translation units. Independent
review found no blockers. Canonical documentation rebuilt and rendered page 10
inspected; formatting and diff checks pass. Native interactive selection and
additional ASan were not run for this geometry calculation change. Bounds-based
picking is intentional; triangle-exact picking and a spatial index are outside
F19. F20 is the next open finding.

F20 completion: readFloat rejects values outside finite float limits before
casting. readInt checks JSON signed/unsigned storage against int bounds before
conversion, preventing wrapped version/color acceptance. NumericRanges covers
positive/negative overflow in camera angles/zoom, position, scale, quaternion
and extent fields, integer wrap cases including UINT64_MAX, destination
preservation, and accepted float-limit/subnormal round trips. Full Release
passed 356/356 in 38.90 seconds after an approved Windows SDK access retry.
IllumoTidy passed 131 translation units. Documentation rebuilt and rendered
page 25 inspected; formatting and diff checks pass. No additional ASan or GUI
run was needed for parser-only range checks. F21 is the next open finding.

F21 implementation: load resets the allocation cursor without parsing imported
IDs. allocateId checks every candidate, explicitly wraps UINT_MAX to one and
returns empty after a complete occupied cycle; createNode rejects empty IDs
without mutation. ImportedIdAllocation passes with maximum/long IDs, leading
zeros, arbitrary strings, repeated creation and reload. IllumoTidy passed 131
translation units. Documentation rebuilt and rendered page 25 inspected;
formatting and diff checks pass. The first full Release run passed 356/357,
with CommandLine.OpenTokens failing its batched UpdateBuffer assertion. Its
exact CTest rerun passed unchanged; the final full Release rerun passed 357/357
in 39.99 seconds.
Full generated-ID-space exhaustion is defined by the bounded loop but was not
materialized in tests. No additional ASan or GUI run was performed for this
document ID-allocation change.
F21 is complete; F22 is the next open finding.

## F22 camera-coordinate safety design

Evidence: CanvasView::worldToCell casts floor(world/16+0.5) directly to int64;
syncVisibleRegion then adds view half-spans, padding and alignment. Camera
commands and sparse metadata currently accept arbitrary finite doubles.
tryScrollCache also subtracts two distant signed cache origins before checking
whether scrolling is useful. Repair all of these paths together, including
runtime pan/zoom and cursor conversions, without changing sparse cell storage.

Use one product-owned coordinate policy shared by command, codec and view:
reserve a conservative 2^32-cell margin from signed endpoints (more than any
int-sized view/cache span), reject unsupported explicit camera input before
mutation, and safely handle direct out-of-contract view/cursor values. Preserve
normal rounding. Guard opposite-end cache jumps before signed subtraction.
Check dimension arithmetic before relying on the margin. Runtime pan/zoom must
not leave camera state outside the policy. Tests must cover huge finite values,
endpoint-adjacent values, accepted boundary positions, opposite-end jumps and
transactional metadata rejection; existing NaN-only tests are insufficient.
F22 completion: CanvasCoordinatePolicy provides checked conversion and the
shared finite navigation bound. Camera commands and sparse v2/v3 metadata
reject unsupported positions before mutation; save preflight preserves the
destination when camera metadata is invalid. Product pan/zoom validate pending
targets using read-only Camera target getters. Cursor editing rejects invalid
samples. CanvasView preserves its previous cache for invalid direct positions,
uses overflow-safe ceiling division, and checks overlap before subtracting
distant cache origins. Its legacy worldToCell helper returns zero for invalid
input; editing uses the checked helper instead.

CameraCoordinateBounds covers huge finite input, accepted positive/negative
boundaries, padded caches and opposite-end jumps. CameraInputBounds covers
rejected zoom targets through later interpolation and existing-save preservation.
Command and v2/v3 metadata regressions cover both axes and signed endpoints.
Full Release passed 359/359 in 40.60 seconds; IllumoTidy passed 131 translation
units. Independent static review found no blockers. Documentation rebuilt and
rendered pages 18-19 were inspected; formatting and diff checks pass. Initial
validation caught incorrect setter calls in new tests; these were corrected
before the successful runs. Native mouse dragging and additional sanitizers
were not run; pending pan arithmetic was reviewed, while zoom was exercised
headlessly. The bound ensures arithmetic safety, not extreme-coordinate visual
precision. F22 is complete; F23 is the next open finding.

## F23 endpoint arithmetic design

Capture and fill iterate validated local widths/heights, adding bounded offsets
to selection origins; neither loop increments a signed endpoint. Tiny endpoint
selections remain valid for copy/fill/cut. Elementary evolution clips target X
to representable coordinates and treats out-of-domain neighbors as background.
A source at the maximum Y cannot produce a representable next row and returns
failure before grid mutation. Normal and finite-torus stepping retain their
existing semantics. Regression coverage includes both endpoints, source history,
and revision/content preservation on rejected last-row advance. Full Release,
tidy, focused tests and documentation validation are required before completion.

F23 completion: capture/fill use bounded local offsets, including copy/cut at
INT64_MIN/MAX. Elementary stepping clips its X range, checks terminal X before
increment, guards both neighbor offsets and rejects maximum-Y advancement
before mutation. Focused CellClipboardOperations and ElementarySpaceTime pass
with endpoint regressions. Full Release passed 359/359 in 40.54 seconds;
IllumoTidy passed 131 translation units. Independent review found no blockers.
Canonical documentation rebuilt and rendered page 19 was inspected; formatting
and diff checks pass. No native GUI or additional sanitizer run was performed
for these bounded arithmetic changes. Allocation-failure transaction handling
remains F27. F23 is complete; F24 is the next open finding.

## F24 pattern routing design

Autodetection recognizes leading ! plaintext comments and skips # RLE comments;
RLE headers, runs, row separators, p-tokens and inline terminators identify RLE.
Ambiguous multiline cell-only text uses plaintext rows. PatternFormat provides
explicit RLE/plaintext routing through command and clipboard import APIs while
preserving default autodetection for existing callers. Clipboard paste parses
the current text and rejects malformed or occupancy-empty results without
falling back to its retained pattern. Failed imports preserve retained state.
Validate commented gliders, compact RLE headers, ambiguous rows, explicit
command routing and failed/empty clipboard input followed by a valid paste.

F24 completion: PatternFormat selects explicit parsers through both command
paths. Detection ignores comment contents and preserves plaintext row semantics
for ambiguous cell-only input. Ctrl+V parses into a local pattern and rejects
malformed/occupancy-empty clipboard text before placement; failed import leaves
the retained buffer unchanged. Console paste intentionally uses that buffer
and retains rotate/flip behavior. PatternFormatRouting and
ClipboardRejectsStaleFallback pass, including the commented glider reproduction,
explicit command differences and recovery after failed input. Full Release
passed 361/361 in 39.08 seconds; IllumoTidy passed 131 translation units.
Independent review found no blockers. Documentation rebuilt and rendered page
19 was inspected; formatting and diff checks pass. Tests use the deterministic
clipboard stub; native OS clipboard integration and additional sanitizers were
not repeated for this parser/routing fix. F25 is the next open finding.

## F25 RLE state encoding design

Emit project-extension p{N} tokens for byte states 2..255; retain o/b for 0/1.
Parse braced decimal states 0..255 with checked accumulation and required closing
brace. Preserve legacy one-digit pD/PD syntax without changing following run
counts (p23o means state 2 followed by three live cells). Previously emitted
unbraced multi-digit states cannot be recovered unambiguously; do not silently
reinterpret existing valid streams. Binary RLE remains unchanged. Test all 256
values with repeated runs and rows, adjacent states/run counts, old syntax and
malformed/out-of-range tokens; require full Release, tidy and documentation.

F25 completion: both exporter run-flush paths emit braced state tokens; parsing
checks byte-range accumulation before arithmetic and requires a nonempty decimal
value plus closing brace. Legacy one-digit tokens retain their following run
counts. RleByteStates verifies dimensions, positions and states for every byte
value across repeated runs/rows, mixed adjacent tokens, legacy syntax and invalid
numbers. Full Release passed 362/362 in 38.11 seconds; IllumoTidy passed 131
translation units. Independent review found no blockers. Documentation rebuilt
and rendered page 19 was inspected; formatting and diff checks pass. No native
GUI or additional sanitizer run was performed for this bounded parser change.
Historical ambiguous exports cannot be auto-recovered; the documented braced
encoding prevents new ambiguity without reinterpreting valid legacy streams.
F25 is complete; F26 is the next open finding.

## F26 ruleset validation design

Validate life-like grammar (B/S, S/B, legacy numeric S/B), 0..8 neighbor arrays,
byte palettes and elementary rule numbers before registration. Reject B0 and
elementary rules whose empty neighborhood births a cell, because the current
sparse representation cannot evolve a changing background. Direct registration
returns bool and rejects unsupported definitions. JSON loading strictly parses
the whole document, updates a temporary catalog and swaps only after every
entry passes; malformed later entries or trailing JSON leave the catalog intact.
Guard direct LifeLikeRuleSet counts above eight before shifting. Tests cover
invalid input types/ranges, parser failures, direct registration, rollback and
valid definitions; full Release, tidy, review and synchronized docs are required.

F26 completion: the registry rejects unsupported background-birth definitions,
invalid masks/families, malformed grammar and numeric type/range violations.
registerRule returns bool; strict JSON parsing and temporary catalog publication
prevent malformed files from partially registering definitions. Life-like
transitions reject counts above eight before shifting. RegistryValidation covers
B0, arrays/types/large indices, elementary bounds, palette bounds, later-entry
rollback, trailing garbage/second JSON values, direct registration and every
invalid byte neighbor count. Full Release passed 363/363 in 36.56 seconds;
IllumoTidy passed 131 translation units. Review identified non-strict stream
parsing; replaced it with json::parse and verified the added regression. No other
review blockers remained. Documentation rebuilt and rendered page 19 inspected;
formatting and diff checks pass. No additional GUI or sanitizer run was performed
for catalog parsing and bounded shifts. F27 is the next open finding.

## F27 generation failure execution plan (complete)

Objective: failed work never increments completed-generation counts, disappears
silently, or exposes partial elementary output. Preserve synchronous publication,
one outstanding worker generation, sparse save formats and existing catch-up
dropping for successful/overdue work.

1. Manual stepping returns the completed count, stops at the first failed advance
   and reports requested versus completed work. Prior successful generations stay
   committed; the failed generation does not count.
2. Async completion failure invalidates the spare mirror, pauses in edit mode,
   logs an actionable retry message and retains one explicit pending retry. Run
   can schedule it without charging another time step. A successful manual step
   also satisfies the retry. No automatic failing busy loop is introduced.
3. Stage elementary output, including retained history, in isolated state. Check
   every changed setCell outcome and catch allocation failure before a no-throw
   publication. Preserve source/destination cells and revision on failure,
   including advanceFrom. Reuse existing state/delta contracts rather than
   introducing another simulation owner or publication path.
4. Add bounded deterministic failure injection at elementary writes, plus
   endpoint rejection tests through manual/async product paths. Verify counts,
   diagnostics, paused state, retry and direct-source rollback.
5. Run focused tests, full Release/IllumoWorkspace, configured tidy, relevant
   Debug ASan tests, independent review, and docs build/render QA. Benchmark the
   elementary path if staging adds full-history copying; measure and report that
   tradeoff before closing this finding. Preserve all unrelated audit edits.

Design resolution: reuse the retained inactive chunk map for elementary history
and destination staging, followed by the existing single publication operation.

F27 implementation checkpoint: manual completed-count reporting and paused async
retry behavior are implemented. Elementary advancement now stages source history
and writes into the retained inactive map; source-relative journal comparison
precedes one publication. advanceFrom no longer changes revision before success
and clears its borrowed binding on exceptions. Instance-owned failure injection
throws after a selected number of changed staged cells. Focused product and
domain regressions pass, including injected worker failure, prior delta retention,
external-source rollback/binding cleanup, empty-source replacement, wrapped-row
retry and one revision per generation.

ElementaryHistoryBench (MSVC Release, Windows, Intel i7-10700K, three warmups,
64 repeated advances from a fixed source) measured 0.001255 ms/generation without
history and 0.390766 ms with 1024 rows x 128 occupied historical cells plus one
source cell. This measures the complete current elementary kernel, including
history discovery/staging; it is not a before/after speed comparison or GPU test.
F27 completion: full Release passed 366/366 in 35.99 seconds. IllumoTidy passed
131 translation units. Debug ASan passed ElementarySpaceTime,
ElementaryTransactions, SimulationFailureReporting and NegativeChunkMapping.
The strengthened cache regression explicitly enables linked candidate sources,
proves unchanged-source reuse, then checks invalidation and byte identity after
four elementary replacements. Review identified the necessary topology-version
invalidation and this test activation requirement; both are resolved.

Full-suite endpoint testing exposed an optimized MSVC neighbor-lookup failure.
Debug ASan passed; temporarily disabling getCell or floorDivide inlining also
made Release pass. The permanent floorDivide expression divides a nonnegative
magnitude, shifting before negation to preserve INT64_MIN. Boundary tests verify
positive, negative, non-power-of-two and invalid divisors. This is evidence for
an optimization-sensitive failure, not a confirmed compiler defect diagnosis.
All diagnostic probes and compiler annotations were removed. Documentation was
rebuilt and pages 19-20 visually inspected; formatting and diff checks pass.
No new GUI smoke was run for this domain/failure-reporting change. Existing
unused-parameter and ASan incremental-link warnings remain unrelated. F29 is
the next open finding; F28 was completed earlier.

## F29 allocator alignment execution

Scope: ArenaAlloc, ChainedStackAlloc, ChainedPoolAlloc and allocator tests.
Align actual pointers with std::align; own aligned backing storage with matching
delete, upgrading only empty storage and aligning new chunks for the request.
Typed pool chunks use std::allocator<T> raw storage. Preserve four chunks,
live addresses, bulk Clear and stack LIFO semantics. Reject invalid alignment
and overflowing size arithmetic. Stack mark insertion failure restores the old
chunk/count/offset; pool free-list reservation precedes chunk ownership.

OverAlignment verifies 256- and 4096-byte types, full-size chunks through the
cap, every pool slot, three Clear/reuse cycles, LIFO unwind, mixed live storage,
invalid alignment and arithmetic overflow. Initial focused Release passed;
independent review found no blockers. Full Release passed 367/367 in 36.13
seconds. All ten allocator cases passed Debug ASan; IllumoTidy passed 131
translation units. Documentation rebuilt; pages 24-25 inspected. Formatting
and diff checks pass. The test padding warning was eliminated by making the
aligned payload occupy its complete size. Existing ShaderPreprocessor unused
helper and ASan incremental-link warnings remain unrelated. No GUI check or
performance claim applies to the allocator contract repair. F30 is next.

## F30 starter console capture execution

Gate all five raw-polled controls while the console is open, without stopping
animation. Track held/blocked keys per module; a key held during capture must
be released before it can act after close. Preserve held R reset and arrow speed
behavior outside capture. Test each key while captured, held through close,
fresh press after release, and two independent module instances. Instantiate a
real AuditCube workspace and build its tests, since the main workspace does not
compile templates. Full main Release passed 367/367 in 37.95 seconds; explicit
clang-tidy checks of both changed template translation units pass. Independent
review found no blockers. Generated full Release passed 212/212 in 16.87 seconds,
including AuditCube.Module.ConsoleCapture and all four starter cases. Both
generated application executables built. This checkpoint explicitly disabled
generated documentation because F32 remains open. README describes capture and
release behavior; formatting and diff checks pass. Existing GuiKit and
ShaderPreprocessor warnings remain unrelated. No new native GUI or sanitizer
run was performed for this input-gating change; headless tests exercise the raw
down-state policy but do not prove native console rendering. F31 is next.

## F31 minimum CMake execution

Raise every current first-party entry point and generated root to 3.25,
matching add_subdirectory SYSTEM support. README and generated README declare
the requirement; generator tests assert root/template minima. Kitware's portable
3.25.0 Windows archive was verified against its release SHA-256 listing, then
used with Ninja and Clang 22.1.8. Actual minimum-version Release configure and
full build pass, including 367/367 CTest cases in 36.99 seconds. All five
project-generator tests pass. Independent review found no stale active minimum;
diff checks pass. Existing unused member/parameter/local/helper and constructor
order warnings are recorded in build-cmake-min-validation/f31-build.log; no
runtime source was changed for this checkpoint.
Documentation and clang-tidy are disabled only in this version-compatibility
build; their own workflow verification remains separate. No vendored minimum
or historical document was rewritten.

## F32 generated documentation execution

The engine derives the default PDF option from its build script and two LaTeX
entry points. Generated source-copy workspaces omit these inputs and now default
OFF. Explicit ON without inputs fails during configure with an actionable
message; an existing cached OFF choice remains respected. The main repository
keeps its available documentation target. README and generated README explain
the behavior and the opt-in integration command.

The new generated-default-build integration case runs only when
ILLUMO_TEST_GENERATED_BUILD=1, requires LaTeX/PowerShell, creates a complete
workspace, configures without documentation overrides, builds the default
Release target including its tests, verifies the OFF cache, then checks explicit
ON rejection. All six generator tests including this integration passed in
109.656 seconds. Independent review found no blockers. Main full Release with
documentation enabled passed 367/367 in 35.63 seconds; both PDF targets ran
successfully and were already up-to-date. Diff checks pass. Existing Perl locale
warnings remain unrelated. No runtime source changed, so no additional sanitizer,
GUI or C++ analysis run was needed. F33 is next.

## F33 reserved project names execution

Reject case-insensitive copied infrastructure, generated engine/editor/vendor/
CMake/CTest targets and Windows device names before provenance, mkdir or copying.
Validate direct template instantiation too, and use full identifier matching to
reject trailing newlines. Preserve IllumoGame and normal custom identifiers.
Tests verify existing destination bytes and directory contents stay unchanged
even with force, missing destinations stay absent, and valid names remain valid.
Review caught shader/default-file stage target names; both are now reserved.

The integration run exposed a prerequisite shared-output staging failure:
concurrent runtime targets attempted to copy THIRD_PARTY_NOTICES.md to the same
Release destination and one copy failed. Shared runtime and shader stages now
form a dependency chain per output directory; product-specific default-file
seeds are deliberately excluded so building one product cannot seed another
product's settings. This bounded build-order repair preserves target names and
payloads. Default-file seeds separately lock their shared destination's
check-and-copy using a lock in the build directory, preserving first successful
seed behavior and existing user files without adding cross-product dependencies.
Re-run the parallel generated integration and main full build, and
verify representative accepted names configure before closing F33.

F33 completion: seven generator cases including full parallel generated build
and representative IllumoGame/Game_123 configurations passed in 135.160 seconds.
The final quick suite ran eight cases, with only the already-run full integration
skipped; concurrent eight-process seeding and all reserved-name regressions pass.
Full main Release passed 367/367 in 39.74 seconds. Generated project references
confirm TestsShaderStage -> CaptureRuntimeStage -> GameRuntimeStage ->
IllEdRuntimeStage ordering (each later stage depends on its predecessor).
Review found no remaining blocker. README covers validation and shared staging;
diff checks pass. Existing configuration/locale warnings remain unrelated.
No C++ runtime code changed; GUI/sanitizer checks do not apply. Cross-build-tree
manual sharing of one output directory was not tested. F34 is next.

## F34 coverage inventory execution

Derive coverage discovery dependencies and a per-configuration binary manifest
from the existing runner registration. Both the main and generated roots use
one helper after their subdirectories register. Keep the 85% gate and existing
headless exclusions. Reject coverage with testing disabled or missing binaries.
Generate HTML before enforcing the gate so failures remain inspectable.

Fresh Ninja/Clang 22.1.8 Debug coverage built all four runners and passed 369
tests; the complete denominator measured 84.82%, below 85%. Added behavioral
tests for editor property commands, panel toggles, and new-document/camera reset.
Generated-workspace regression configurations with and without the editor
verify manifest membership, Ninja discovery dependencies, and testing-disabled
rejection. Nine generator cases passed (one unrelated full-build integration
skipped) in 41.399 seconds. Re-running coverage and full Release before closure.
Coverage uses its own tree with clang-tidy disabled; the changed test translation
unit passes a separate clang-tidy invocation with the configured policy.

F34 completion: final combined coverage passed 372/372 Debug cases in 105.26
seconds and measured 85.19% across 30,442 production lines. Inventory output
contains all four binaries and editor/mesh-viewer production sources. LLVM
reported 287 functions with mismatched profile data even in this fresh build;
this is not a warning-free coverage measurement. No exclusions or gate were
relaxed. Full Release passed 370/370 in 40.73 seconds. Final read-only review,
formatting and diff checks pass. Documentation rebuilt; changed PDF pages 15
and 69 were rendered and visually checked. Existing compiler/LaTeX warnings
remain unrelated. No production C++ changed; additional GUI or sanitizer runs
were not needed for this build/test checkpoint. F35 is next.

## F35 batch analysis inventory execution

Replace the hardcoded product/folder regex with parsed compile-database entries
resolved against the source root. Exclude sources outside the workspace,
vendored thirdparty components, and the configured binary tree. Preserve each
compilation variant and its original command/arguments; save the selected
database and invoke LLVM against that exact inventory. Count unique files and
compilation entries separately. Keep CMake as the command entry point and the
existing clang-tidy policy unchanged.

Focused Python tests pass for custom names, capture/native test tools, relative
paths, workspace spaces, vendor/generated/external exclusion, multiple compile
variants, selected-database forwarding, and nonzero analyzer exit propagation.
Run the full main batch and a fresh generated AuditApp batch before closure.

F35 completion: the main LLVM batch passed all 134 selected files, including
CaptureMain.cpp, CaptureGpuTests.cpp, and CloseWindowTests.cpp. Fresh generated
AuditApp configure and batch passed 89 files, including all five application
source/test files. The saved databases verify exact membership. Two focused
Python tests passed; nine generator cases ran with seven passing and the two
opt-in integrations skipped (18.068 seconds). This checkpoint separately ran
the real generated tidy integration. Full main Release passed 370/370 tests in
35.95 seconds and rebuilt the book. Changed PDF pages 69-70 were rendered and
checked. Final review and diff checks pass. No production C++ changed; GUI,
sanitizer and repeated coverage runs were unnecessary. Concrete architecture
and current-state documentation mismatches remain before the overall goal can
be closed.

## Rules catalog ownership correction design

The report explicitly calls for catalog-location/I/O policy to move out of
Rulesets into product composition. RuleSetRegistry currently mixes native
executable discovery, file reads, transactional JSON validation and factories.
Keep the latter two in Rulesets; add Game/RuleCatalogLoader for file reads and
ordered discovery. Reuse EnvVars::ApplicationConfigPath's parent directory as
the existing engine-owned executable-directory source, avoiding native calls
in Game. The required-module factory initializes the shared catalog once before
constructing either menu or direct-game modules. Application metadata remains
declarative. No new worker, dependency, file format, or engine API is introduced.

Preserve built-ins first, then the first valid catalog in executable/current/
current-IllumoGame order; missing, inaccessible, or malformed candidates fall
through. Failed parses never publish partial definitions. Explicit registry
instances become deterministic built-in-only values; their product-facing
loadFromFile/loadFromDefaultLocations methods move to RuleCatalogLoader, and
loadFromText is the domain API. This bounded API migration is part of the
report-authorized ownership correction, not a general registry redesign.

Verify strict parser regressions, directory precedence and fallback with
isolated fixtures, no ambient filesystem reads during registry construction,
full Release/CTest, clang-tidy, and refreshed coverage. Update nested ownership
guidance plus canonical game documentation and the formal decision log.

Catalog ownership completion: RuleCatalogLoader owns file reads and ordered
fallback; RuleSetRegistry contains no filesystem/stream/native includes and
constructs built-ins only. Strict parser regressions now use catalog text.
Loader tests verify precedence, valid-empty stopping, missing/malformed fallback,
transactional rejection and factory usability. StartupComposition confirms
availability before required-module Start. The opt-in Python integration copies
the Release runner into an isolated directory and verifies custom-catalog menu
and direct-game startup, both executable-side success and malformed-file/cwd
fallback (four child processes); all pass. Run it with
ILLUMO_CATALOG_TEST_BINARY pointing to the Release IllumoGameTests executable.

Final full Release passed 373/373 in 36.06 seconds. Final LLVM Debug coverage
passed 375/375 in 106.13 seconds at 85.23%; the existing 287-function profile
mismatch warning remains a measurement limitation. The full 136-file tidy batch
passed, and the subsequently added startup test passed a focused tidy invocation.
No serious new diagnostics. Canonical game/architecture prose, nested ownership
guidance and D-GC1 were updated; the book rebuilt and pages 22-23/62 were rendered
and checked. No GUI or additional sanitizer run was needed for this sequential
file-loading responsibility correction. Public engine APIs and catalog formats
are unchanged; the product registry C++ migration is documented above. Final
diff checks pass. Dependency exposure and current-state prose remain open.

## Public dependency exposure correction design

Public math/camera contracts intentionally expose GLM types and <glm/...>.
Exporting their current parent directory also exposes unrelated vendor headers.
Keep vendored sources and public types unchanged; prepare a GLM-only include
directory under the build tree with CMake. configure_file COPYONLY tracks source
content changes without touching identical outputs; CONFIGURE_DEPENDS tracks
membership changes, and stale generated headers are removed from that dedicated
directory. Only .h/.hpp/.inl files are exposed. Existing private includes/link
dependencies remain private. No dependency addition or version change occurs.

Verify a consumer linked only to Illumo::Illumo can include all public headers
and cannot discover stb/json/glfw/glew/Tracy through their old vendor-root paths.
Test configure-time copy, content update, new/deleted headers and unchanged
timestamps with a temporary minimal GLM fixture. Run full Release, actual
minimum-CMake configure, and generated-project consumer build. Update canonical
dependency documentation and keep vendor source/license packaging unchanged.

Public dependency completion: CMake exports only the generated GLM header tree
beside first-party public headers. The main output directory contains only glm.
ConsumerSmoke rejects the old stb, tinyobjloader, JSON, GLFW, GLEW and Tracy
include paths while compiling all supported public headers. Review corrected
the stb probe to the actual stb/stb_image.h path; final MSVC rebuild/CTest and
focused Clang analysis pass. Header synchronization regressions pass under
CMake 4.3.3 and actual minimum 3.25.0, including build-triggered content/add/delete
updates and unchanged timestamps.

Full main Release passed 373/373 in 35.45 seconds. A freshly generated
MathConsumer workspace configured, built its application and consumer smoke,
and passed ConsumerSmoke CTest. Existing compiler/vendor warnings remain
unrelated. Canonical dependency prose, generated README policy and D-DEP1 were
updated; book rebuilt and changed PDF pages 62/68 rendered and checked. No
runtime implementation, dependency version, vendored source or license payload
changed, so extra GUI, sanitizer and repeated coverage runs were unnecessary.
Final diff checks pass. Current-state architecture prose and final whole-report
completion audit remain open.

## Current-state documentation reconciliation

Corrected the report's documentation drift in README, architecture consensus,
package maps, and matching book sections: three applications and four test
runners; world meshes, cubemaps and effective offscreen pass sequences; one
borrowed attachment per node with multiple visual items allowed per host.
IllEd document geometry uses MeshVisual in both world modes, while GameVisual
composes UI. The simulator's optional render3dTest graph remains separate from
cell storage. Coverage prose now follows registered runners. Runtime staging
is described as copying inputs without pruning stale outputs.

The chart inventory now includes the viewer/editor, all runners, live elementary
rules, and world rendering. Narrow chart labels and wrapping were corrected
after visual inspection. Historical decision entries were preserved.

Verification: docs/build.ps1 passed; changed book pages 5-7, 11-13, 68 and 70-71
and chart pages 1, 3, 5 and 8 were rendered and visually checked. Existing
unrelated LaTeX warnings remain. git diff --check passes. No runtime code was
changed in this step, so no repeated C++ build, GUI, sanitizer, or coverage run
was needed. Final whole-report completion audit remains pending.

## Completion audit checkpoint (2026-09-14)

Three independent read-only reviews checked the original triggers against the
current implementation and regression bodies. No remaining original-trigger
implementation defect was found. They identified missing retained GPU/native
execution evidence and incomplete visible input checks; these are tracked as
validation work rather than silently treated as passes.

Refreshed authoritative logs under build-f10-validation:

- completion-release.log: full Release build, 373/373 CTest, 40.22 seconds.
- completion-gpu.log: current IllumoCaptureGpuTests, zero failures; upload bounds,
  mixed-channel cubemaps/replacement, texture units, framebuffer/viewport,
  clear masks, and successive font/renderer captures.
- completion-native-close.log: Release native close/cancel/failed-save/save/discard.
- completion-asan.log: eight exact Debug ASan regressions (OBJ bounds, command
  reentrancy, malformed config, atomic files, over-alignment, worker lifecycle,
  semantic memo invalidation, elementary transactions), all passed.
- completion-asan-native-close.log: Debug ASan native close rerun from the staged
  runtime directory, passed. The earlier run appended to completion-asan.log
  used Testing as cwd and emitted missing-shader errors; that native-rendering
  limitation is superseded by this correctly staged rerun.
- completion-generator.log: all nine generator cases passed in 214.442 seconds,
  including full generated default build and coverage configurations. The initial
  combined Python module invocation used the wrong import path; the documented
  unittest discovery command above corrected the invocation. Public-math and
  tidy selection cases passed in that initial run, but its overall exit failed
  on the generator import and must not be called an all-green tooling run.

Visible Computer Use observations: staged Release IllEd entered fullscreen on
F11 and returned to its window on the second F11. A disposable rectangle retained
its displayed position after stationary off-center selection. Alt+F4 and the
title-bar X each displayed the unsaved-changes dialog; clicking Cancel preserved
the scene, and Don't Save closed it. IllMeshViewer reopened its native Open
dialog after verified cancellation; repeated G turned its grid off and on.
The generated MathConsumer Debug starter displayed a working console, accepted
G/Space/R text, retained its grid, and continued rotating. A brief fresh G after
closing the console did not reliably exercise its polled down-state controls.
The automation API has no held-key/down-state duration control, and its drag
did not establish a reliable held-button frame. Do not count these as completed
native drag or held-key capture checks. All disposable UI sessions were closed.

Coverage diagnosis is recorded in build-f34-coverage/completion-coverage-
denominator-audit.json and completion-coverage-functions-audit.json. LLVM 22.1.8
reported 287 duplicate mapping warnings for 114 distinct names, all present in
the actual export. An empty-profile mapping baseline contains one additional
eligible Logger::setConsoleToStderr line: 30,443 versus 30,442 actual lines.
Conservatively retaining the 25,946 measured covered lines and counting that
extra line as uncovered yields 85.22813%, still above the unchanged 85% gate.
This proves the threshold despite the mapping quirk, not a warning-free export.
The --dump diagnostic crashed inside LLVM after printing its records; successful
ordinary JSON exports and the baseline comparison are the evidence used here.

Remaining completion work: native frame-interleaved input validation for body
drag, perspective selection, and starter capture/release; final format/diff and
requirement-by-requirement evidence handoff. No new production change was made
in this checkpoint. Future charter milestones remain outside defect repair.

## Final completion (2026-09-14)

All 35 numbered defects and the three concrete architecture/documentation
mismatches are repaired. The finding ledger maps each original trigger to its
implementation and regression evidence; checkpoint counts above are historical.
The final independent read-only comparison found no additional original-trigger
defect. Future charter milestones still require separate design work.

The remaining native checks passed in a Debug AddressSanitizer harness linked
to the current IllEdCore and actual SpinningCube template source. Real Windows
mouse/key events were held across update/render frames, and GLFW state was
asserted before product behavior was checked. The final run exited zero:

- 2D and 3D off-center stationary press/hold/release preserve position and clean
  state; subsequent held movement changes the body and marks the scene dirty;
  release ends dragging.
- A perspective click selects the nearer elevated, rotated cube even when the
  farther cube was inserted first.
- Space, G, R, Up and Down are captured by the real console, remain blocked when
  it closes while they are held, and act after release and a fresh press.

Evidence: build-f10-validation/completion-native-input.log. Initial probe runs
exposed harness activation overlap, missing arrow scan codes, and an implicit
camera fixture; the final harness waits for activation to settle, supplies scan
codes, and explicitly sets the same camera and extent as BodyDragOffset. These
were fixture corrections, not production fixes. The temporary target hook was
removed by a successful normal configure, disposable source inputs were removed,
and the test windows closed on completion.

Final checks: full Release 373/373; full Clang coverage 375/375 with all four
runners; conservative coverage 85.22813% against the 85% gate; full tidy 136
first-party translation units; generated-project tidy 89 translation units;
nine generator tests and three public-math/tidy helper tests pass. Actual CMake
3.25.0 configure/build/test, fresh generated default builds, GPU readbacks,
native close handling, focused ASan tests, and rendered documentation evidence
are recorded above. All 90 modified/new C++ headers and sources pass
clang-format --dry-run --Werror; final git diff --check passes.

Limits: no race detector is available in the configured Windows toolchain, so
F01 relies on constructor-order evidence plus lifecycle stress, not an ASan
claim of race detection. Linux/macOS, power-loss durability, exhaustive GPU
fault injection and performance are not claimed. Existing compiler/vendor and
LaTeX warnings remain; the LLVM duplicate mapping/one-line denominator quirk is
qualified above. No dependencies were replaced and no Git mutations were made.
