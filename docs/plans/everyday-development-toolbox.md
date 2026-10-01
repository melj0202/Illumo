# Everyday Development Toolbox execution plan

## Objective and approved design

Implement the user-approved terminal Development Tools submenu: CTest explorer,
read-only diagnostics/source previews, complete profile selection, and existing
artifact shortcuts. Success means finding a failure, inspecting it, and rerunning
the relevant tests without leaving the dashboard. Browsing never builds.

## Evidence and boundaries

`build.py` owns the standalone standard-library CLI/dashboard, live progress,
Windows input and process-job cleanup. Generated projects copy this single file.
CTest exposes 335 workspace cases with labels and isolated working directories.
Its generated include can fall back to another configuration, which the explorer
must reject. Preserve CMake authority, all existing uncommitted work, public CLI
defaults, synchronous terminal ownership and current cancellation guarantees.
No C++ changes, external dependencies, editor integration, graphics/performance
tooling, source writes from previews, Git operations or automatic artifact builds.

## Implementation milestones

1. Model effective dashboard profiles and versioned per-run JSON metadata beside
   existing logs. Bind metadata to checkout/build directory/configuration and
   preserve incomplete runs. Add internal child commands for explicit inventory
   preparation and selected CTest execution; normal CLI output stays compatible.
2. Reject missing/mismatched configuration discovery; use CTest JSON inventory,
   escaped anchored selection batches and local uncompressed Test.xml results.
   Default to building discovery targets; expose Run Existing explicitly.
3. Add bounded, scrollable toolbox views and raw text input distinct from
   navigation. Tests select details, with separate run actions; hover never runs.
4. Parse common compiler/CMake diagnostics and show current source/log context.
   Add profile picker/session overrides/explicit saving and existing artifacts.
5. Validate, document and record evidence below.

## Interfaces and risks

Internal child request/result JSON is versioned, file-backed and checked before
execution. It carries names/options, never a shell command. Sidecars identify the
source root, build directory, configuration, effective settings, command working
directory, status, exit code, elapsed time and structured test results. Existing
logs without sidecars remain readable but cannot supply trusted reruns.

One dashboard action owns the terminal and child process tree at a time. All
operations use the existing progress runner; cancelled/failed preparation must
not produce a trusted successful test result. Snapshot fresh CTest XML per batch
before another batch replaces it. Preserve isolated test directories/timeouts.
Profile switches discard session overrides; manual changes never silently save.
Source previews may differ from historical logs and are labelled accordingly.

## Verification and containment

Extend Python tests for input/search/hover/scrolling, profiles, schema validation,
inventory/configuration rejection, selection batching, XML failures/cancellation,
history isolation, diagnostics and missing files. Exercise representative cases
from all projects, then serial full Release build and workspace CTest. Verify
generated-project compatibility, native console input and process cleanup, and
Windows artifact handler dispatch. Rebuild and inspect changed PDF pages.
No new sanitizer/benchmark work is warranted for this Python tooling feature.
Review the final diff; retain logs under ignored build directories. If a tool or
inventory is unavailable, surface an actionable error instead of guessing or
running a broader selection. CLI commands remain a containment path.

## Results

- Completed all five milestones in `build.py`, `tools/test_build.py`, README,
  and the canonical LaTeX build notes. Existing uncommitted work was preserved;
  no CMake, C++, dependency, or Git state changes were made.
- Discovery now uses only targets actually declared through the CMake discovery
  helper; the earlier filename heuristic incorrectly invented a discovery target
  for the optional GPU executable. Exact configuration files and single-config
  cache build type are checked before inventory/execution.
- Full serial Release build passed with `python build.py build --profile release
  --no-docs --parallel 1`, including all 335 workspace tests. A separate full
  Run Existing operation through the toolbox passed all 335 and recorded complete
  results across three bounded CTest batches. Default preparation plus one test
  from each of Illumo, IllumoGame, IllEd, and IllMeshViewer also passed.
- Final Python discovery ran 79 tests: 77 passed and two native-console checks
  were skipped under pipes. Both native checks then passed in an isolated Windows
  terminal (mouse/hover, keyboard and repeated search text, input-mode restoration,
  and cancellation). Process ownership/startup-gate/descendant cleanup tests pass.
- A disposable live CTest fixture verified a failed test with regex characters,
  exact selection without its similarly named neighbor, preserved working
  directory/timeout, structured failure and duration, and a successful rerun.
  Unit tests additionally cover cancellation/incomplete results, empty selection,
  foreign history/configuration, missing source, command-relative diagnostics,
  profiles/overrides, scrolling/hover and explicit activation, and refreshed
  project-label removal.
- Generated-project tests passed with and without editor tools. A copied
  `build.py` ran independently with a different product and no tools modules.
  A full compilation of a newly generated project was not needed for this
  Python-only change and was not run.
- Artifact tests verify existence checks and no-build dispatch; a real Windows
  default-handler invocation successfully opened the selected Release build
  directory (desktop handler access required leaving the filesystem sandbox).
- `docs/build.ps1` passed. Build-note PDF pages 65 and 66 were rendered and
  visually inspected. Existing HarfBuzz configure and unrelated LaTeX layout
  warnings remain; no new warning in the changed build-note section was found.
- Final `git diff --check` passed. Validation logs, sidecars, batch XML, and PDF
  previews remain in the ignored `build-orchestrator-logs` directory. Automated
  native input and dispatch checks do not constitute a complete manual mouse
  walkthrough or inspection of every external artifact-handler application.
