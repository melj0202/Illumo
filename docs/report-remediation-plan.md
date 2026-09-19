# Architecture report remediation - 2026-09-19

## Objective and authority

Verify all 31 numbered findings in the supplied architecture report against the
live checkout, repair confirmed defects, reconcile stale contracts, and record
evidence per finding. The user's goal authorizes remediation; the report's
read-only statements describe its original audit. No Git mutations are authorized.

## Baseline and constraints

The initial dirty tree contains CommandLine callback cleanup, its regression
test, and services/consensus documentation, plus an untracked `.grok/` tree.
Preserve this work and validate it. Source files, tests, CMake and canonical
documentation govern current behavior; generated PDFs are rebuilt from LaTeX.
Keep main-thread affinity, borrowed attachment/resource lifetimes, token rendering,
sparse v4 persistence compatibility, and product/runtime separation. No new
dependencies, scene parallelism, render graph, or unrelated redesign.

## Design and sequence

1. Services/host: bound callback queues and expose overflow counts; use the
   existing edge state for release queries; unify argument parsing while retaining
   literal Windows separators; verify console callback retirement; reject duplicate
   required registrations and competing transitions explicitly; remove destructor
   lifecycle callbacks. Keep the public host signature and deterministic first
   accepted transition. Fix logger path/lifetime without replacing call sites.
2. Rendering: bind primitive resources to their creating renderer identity;
   validate GPU enrollment and buffer capacities; propagate incomplete geometry
   and queue rejection through frame status, suppress failed presentation; contain
   implementation-only GPU interfaces behind the backend boundary.
3. Rules: move dense evaluation and its mutable worker policy into test support;
   remove dense-grid coupling from production rules and registry; preserve fixture
   coverage including Elementary 1D parity. Separate dense adapter CMake sources.
4. Platform/scene: UTF-8/wide Windows dialog conversion and adequate path storage;
   apply the shared dialog specification to macOS; clarify that appending attachments affects subsequent extraction while
   removal/replacement/content invalidation retires existing views.
5. Documentation: correct findings 18-27, product-neutral loop descriptions,
   ownership maps, supported debug configurations, schema families, and stale
   compatibility comments. Correct platform-specific CLI usage without adding
   unrequested options or silently changing unknown-option compatibility.

Changes to internal source contracts migrate every in-tree consumer together.
Prefer additive diagnostics and existing cleanup paths over new ownership models.
Where a report claim is inaccurate, record evidence rather than manufacturing a
code change. Milestones remain independently reviewable in the working-tree diff;
contain failures within their subsystem and preserve existing user edits.

## Verification

Add focused regressions for lifetime, capacity, transitions, parsing, and fixture
parity. Run affected targets, full Release build and IllumoWorkspace CTest,
clang-format checks, configured clang-tidy, and documentation generation. Use
existing hidden-context capture checks for real OpenGL; sanitizers for lifetime
changes when configured tooling permits. Windows/macOS picker interaction and
native macOS compilation require their actual UI/host; name any remaining limits.
No performance improvement is claimed; no speculative benchmark project.

## Finding ledger

All 31 findings have been checked against the live implementation. Changes and
focused evidence are recorded below; aggregate validation follows the table.

| # | Finding | Disposition / evidence |
|---|---|---|
| 1 | GameVisual renderer ownership | Fixed: GameVisual binds to the first renderer lifetime, rejects foreign emission, and avoids expired-owner destruction. `Illumo.GameVisual.RendererOwnership`. |
| 2 | GPU allocation and vertex update validation | Fixed: validate mesh/texture storage before publishing handles; preserve old resources on failed replacement; bound vertex writes. Real OpenGL `IllumoCaptureGpuTests` covers invalid creation/replacement and oversized updates. |
| 3 | Competing module transitions | Fixed: first non-null deferred transition wins; competing/null requests are rejected and logged. `Illumo.Host.ModuleTransitionSuccess`. |
| 4 | DebugModule double Exit | Fixed: DebugModule destructor no longer calls Exit. Host owns successful cleanup; false Start receives no Exit, throwing partial Start is cleaned once. Host failure/exception and Debug overlay tests. |
| 5 | Console Windows-path parsing | Fixed: execution uses the public parser and matching chain grammar; preserves drive/UNC paths, quoted trailing separators, empty arguments, and escaped separators. `Illumo.CommandLineCore.Headless`. |
| 6 | Console callback lifetime | Existing user fix preserved and verified: destructor unregisters captured callbacks. `Illumo.CommandLine.UnregistersConsoleCommands`. |
| 7 | Input event capacity | Fixed: callback char/key queues cap at 256 each, preserve accepted order, and expose cumulative drop counters. `Illumo.InputManager.HeadlessLifecycle`. |
| 8 | Truncated geometry reported complete | Fixed: geometry truncation marks every affected frame incomplete, including cached geometry; failed frames do not present. `Illumo.GameVisual.DynamicCapacity`. |
| 9 | Queue overflow frame status | Fixed: backend rejection/high-water metrics reach Renderer; overflow fails the frame and prevents presentation. Rejected resource uploads remain pending for retry. `Illumo.Renderer.QueueOverflowStatus`. |
| 10 | Release edge polling | Fixed: release queries consume frame-edge state, with Release followed by None while idle. Input lifecycle regression. |
| 11 | Required-module cardinality | Fixed: reject a second required module before startup; retain the existing void registration API and diagnostic log. Host transition/cardinality regression. |
| 12 | Dense RuleSet production coupling | Fixed: RuleSet and registry no longer reference CellGrid; dense evaluation moves into test-only DenseRuleEvaluator. All in-tree consumers migrated; full rules/simulation suites. |
| 13 | Dense Elementary 1D evaluation | Fixed: dense Elementary1D branch preserves history and wraps finite coordinates; worker override belongs to each evaluator. `IllumoGame.Rules.ElementarySpaceTime` compares sparse wrap/history, empty input, and data-backed Rule 90. |
| 14 | Windows Unicode dialog paths | Fixed: wide Windows dialog APIs, strict UTF-8 conversion, 32768-wide-character buffer. Both product codecs use explicit UTF-8 filesystem conversion and contain conversion errors. Unicode/malformed-path codec tests; interactive picker/long-path acceptance remains a manual validation limit. |
| 15 | macOS dialog specification | Superseded by the user-requested removal of the macOS scaffold. The initial specification fix is retired with those files; see the follow-up below. |
| 16 | Attachment-add snapshot expiration | Resolved conflicting guidance: append preserves captured attachment values; detach/replacement/content invalidation expires views. Scene AGENTS now matches consensus and implementation. `Illumo.SceneGraph.SnapshotLifetime` verifies append and subsequent extraction. |
| 17 | Public GPU object interfaces | Fixed: native GPU IDs moved from public resource interfaces into private concrete GL classes. All workspace targets compile; backend-neutral header test retained. Intentional source API tightening; handle APIs remain the consumer contract. |
| 18 | D-R14 pass description | Corrected D-R14 to describe shared shadow depth and per-layer offscreen/post passes; formal correction records the later authoritative contract. |
| 19 | Root sparse save version | Corrected root guidance to sparse v4 writes with v4/v3/v2/dense reads, matching current codec and compatibility tests. |
| 20 | Drawable production list | Corrected Drawable production examples to CanvasView and GameVisual; dense Canvas remains a fixture. |
| 21 | Debug/RelWithDebInfo descriptions | Corrected D-B1, engine package, contributing, and LaTeX runtime text to Debug plus RelWithDebInfo, matching CMake. |
| 22 | IllumoConfig factory description | Corrected engine/LaTeX config description: public name/path only; factory injection is private test access. |
| 23 | Product-neutral frame loop | Corrected canonical and LaTeX frame loop to product-provided application definition and required module. |
| 24 | Dense Canvas comments | Corrected Canvas comments to test-only dense compatibility adapter and removed obsolete production composition advice. |
| 25 | SaveLoad ownership | Corrected Services guidance/package ownership: Platform owns SaveLoad public contract and implementations. |
| 26 | Snapshot design sketch | Corrected snapshot design and Phase 4 record: graph-local handle, boundsValid/cameraVisible, two publication buffers, off-camera retained casters, later shadow relevance. |
| 27 | LaTeX schema family list | Updated LaTeX family list and neighborhood contracts to current schema; rebuilt generated documentation. Also escaped pre-existing x86_64 text that prevented LaTeX compilation. |
| 28 | Logger path and ownership | Fixed: Logger defaults beside the executable, accepts explicit paths for tests/embedding, uses unique_ptr lifetime, and reports file-open failure. `Illumo.Logger.LevelsAndSinks`; existing shutdown sequencing preserved. |
| 29 | CLI usage executable suffix | Fixed: .exe appears only in Windows default help. Existing supported flags and unknown-option compatibility deliberately preserved and documented. |
| 30 | Unused AllSets include | Fixed: CellContext includes RuleSetRegistry directly instead of AllSets. |
| 31 | Dense adapter production source list | Fixed source-list coupling: dense Canvas/CellGrid and legacy C++ reference rules compile only into the test runner. Report claim of guaranteed final binary inclusion was too strong: static archive membership alone does not prove linker inclusion. |

## Validation results and remaining limits

- Complete final Release build passed, including IllumoGame, IllEd, IllMeshViewer,
  and the automatically invoked labeled workspace CTest suite: **450/450**.
  Command: `cmake --build build --config Release`.
  Log: `build/report-build-final.log`.
- Debug targets built with the configured MSVC AddressSanitizer. Focused host,
  renderer/primitive, console, input, logger, snapshot, atomic-file, elementary
  rule, and Unicode codec regressions passed: **80/80**.
  Logs: `build/report-build-asan.log`, `build/report-asan-tests.log`.
- Real OpenGL allocation/replacement/buffer/lifecycle and scene-shadow parity
  checks passed in Release and Debug ASan: **0 failures** each.
  Logs: `build/report-gpu-release.log`, `build/report-gpu-asan.log`.
- `tools/verify_capture.py` passed **11 real-GPU invocations** covering
  direct/scene pixels, first-frame depth occlusion, invalid input, shader errors,
  and safe output publication. Log: `build/report-capture.log`;
  images/JSON: `build/report-capture-proof/`.
- `cmake --build build-workspace-tidy --target IllumoTidy` passed for **151
  first-party translation units**, with no diagnostics after correcting pointer
  comparisons. Log: `build/report-tidy.log`.
- `clang-format --dry-run --Werror` passed for all changed C++/header/Objective-C++
  files, including the new evaluator. `git diff --check` passed.
- `docs/build.ps1` rebuilt both PDFs. Extracted text confirms the complete schema
  family list and dense boundary; rendered pages 23, 30, and 31 were inspected for
  the corrected family/service/platform content. Log: `build/report-docs-final.log`.
- Independent final read-only reviews found no remaining concrete correctness
  defects in the rendering or host/services/platform changes. The final diff
  preserves the initial console-lifetime edits and contains no generated outputs.

Existing unrelated MSVC warnings remain in GuiKit (unused parameters) and
TestShaderPreprocessor (unused helper). ASan links report the expected disabled
incremental-link warning. Documentation builds retain existing locale, float,
list-layout, and box warnings; the affected rendered content remains readable.
No new dependency was added.

Interactive Windows picker/long-path acceptance has not been exercised.
Automated Unicode codec round trips do not substitute for those UI checks.
macOS was subsequently removed from scope and its scaffold deleted. No performance, non-Windows support, or race
detector claim is made. Existing user changes and the untracked `.grok/` tree
remain preserved; no staging, commit, or other Git mutation was performed.

## Follow-up: macOS scaffold removal

The user subsequently requested removal of macOS files because a port is not
currently planned. Finding 15 is superseded by removal of the seven first-party
platform files and the dedicated package map. CMake rejects Apple targets;
current platform documentation and diagrams now reflect the reduced scope.
No macOS runtime validation is outstanding for this removed implementation.
Windows/Linux source paths, vendored portability code, and historical records
are retained. Windows configure and full Release build passed, including
450/450 workspace tests (`build/macos-removal-configure.log` and
`build/macos-removal-build.log`). Both PDFs rebuilt successfully; the updated
platform diagram and removal decision were visually checked. Header formatting
and `git diff --check` passed. A reference scan found no active references to
the deleted macOS source files or package map.
