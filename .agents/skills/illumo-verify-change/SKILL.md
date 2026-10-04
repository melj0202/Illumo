---
name: illumo-verify-change
description: Verify Illumo repository changes with evidence proportional to risk. Use when asked to validate a change, assess readiness, prepare a handoff, or decide which builds, focused tests, full CTest, formatting, coverage, benchmarks, documentation checks, and manual GUI checks are required. Do not treat verification as authorization to modify code, commit, push, or perform an interactive smoke test.
---

# Illumo Verify Change

Verify the requested outcome, not merely that a command returned zero.

## Establish the verification scope

1. Read the root and closest nested `AGENTS.md`, `README.md`, applicable implementation and tests, CMake configuration, and canonical documentation.
2. Inspect `git status --short`, the complete relevant diff, and the affected call paths. Preserve all pre-existing work.
3. Map each requested behavior and material risk to evidence. Review and diagnosis remain read-only unless the user separately authorizes changes.
4. Distinguish production paths from compatibility fixtures, test fallbacks, generated outputs, and unsupported platforms.

## Select proportional checks

Use repository-provided commands from the repository root. The full suite runs once, before a push (root `AGENTS.md`, "When to run what"):

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure
```

The default build only builds the runners (`ILLUMO_RUN_TESTS_ON_BUILD` is `OFF`); if a tree was configured with it `ON`, the build already ran the suite, so do not run `ctest` again.

Short of a push, build only the affected targets and run the exact cases and package tests that cover the change:

```powershell
cmake --build build --config Release --target IllumoTests IllumoGameTests IllEdTests IllMeshViewerTests
ctest --test-dir build -C Release -N -L IllumoWorkspace
build/Release/IllumoTests.exe --list
build/Release/IllumoTests.exe --run <exact-test-name>
```

The other runners (`IllumoGameTests`, `IllEdTests`, `IllMeshViewerTests`)
take the same `--list` and `--run` flags. Package-level cases such as
`IllumoGame.Wasm.GamePackage` live in separate package test executables
(e.g. `IllumoGameWasmPackageTests`); run them by name with `ctest -R`.

Add checks according to risk:

- Check formatting on every modified C++ source or header without rewriting it. Run `clang-format -i` only when the active task already authorizes edits to those files.
- Build documentation only when documentation writes are authorized and source changes make the PDF stale.
- Run coverage when the change creates meaningful untested production paths or the user requests the configured gate.
- Benchmark performance-sensitive changes against a reproducible baseline.
- Use configured sanitizers for material lifetime, memory-safety, undefined-behavior, or concurrency risk.
- Identify when Windows GUI/OpenGL smoke evidence is needed for rendering, native dialogs, input, windowing, or presentation behavior. Launch or control an interactive application only with explicit authorization.
- Require native configure, build, launch, and smoke evidence before claiming another platform works.

## Interpret the evidence honestly

- MockBackend and headless tests prove command and state behavior, not live pixels.
- A startup timeout or process survival is not a visual GUI test.
- A successful target proves only what that target builds or runs.
- A blocked, skipped, unavailable, or environment-dependent check remains unverified.
- Separate unrelated pre-existing warnings and failures from regressions introduced by the change.
- Do not infer performance, portability, or visual correctness from compilation alone.

## Complete the audit

1. Review `git diff --check`, `git diff --stat`, the complete relevant diff, and untracked files for accidental or generated material.
2. Tie every requirement to a test, inspection, benchmark, or explicitly named gap.
3. Report what passed, what failed, what was not run, and why. Include configuration, exact commands, and meaningful limitations.
4. Do not commit, push, stage, or alter code merely because verification found a defect; report it unless fixes were authorized.
