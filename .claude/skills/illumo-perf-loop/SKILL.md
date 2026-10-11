---
name: illumo-perf-loop
description: Run an unattended Illumo performance loop in its own worktree - measure a baseline, then per experiment state a hypothesis and predicted gain, make one change, test it, re-measure, and keep it as a commit only if tools/perf_bench.py's gate passes. Writes a ledger of predicted vs actual. Use only when the user runs /illumo-perf-loop or explicitly asks for the perf experiment loop.
argument-hint: "<goal or area> [experiments=N] [suites=sim,render,...]"
disable-model-invocation: true
---

# Illumo performance loop

Running this skill authorizes, for this loop only: creating a linked worktree
and branch `perf/loop-<YYYY-MM-DD>` from the current release branch tip,
committing kept experiments on that branch, and discarding the current
experiment's own uncommitted edits in that worktree with
`git restore -- <files it touched>`. It does not authorize touching the main
checkout, merging, pushing, rebasing, or deleting the worktree or branch.

Read first: `illumo-performance-audit` (the performance contracts every
change must keep), the benchmark section of `docs/frame-capture.md`, and
`python tools/perf_bench.py --help`.

## 1. Set up

1. Main checkout: `git status --short` (it may be dirty; the loop does not
   touch it) and `git branch -vv` for the current release branch.
2. `git worktree add ../Illumo-perf-<date> -b perf/loop-<date> <release>`.
   Results go in `../Illumo-perf-<date>-results/` (outside the repo).
3. Configure and build in the worktree, reusing the main checkout's WASM
   toolchain:

   ```powershell
   cmake -S . -B build -DILLUMO_WASM_TOOLS=<main checkout>/build-wasm-tools
   cmake --build build --config Release --target IllumoTests IllumoGameTests IllumoGameWasmPackageTests
   ```

   The first build is a full one. Close other heavy programs; note the power
   plan and anything else running in the ledger.

## 2. Baseline and drift

1. Pick suites for the goal (`sim`, `topology`, `canvas`, `render`,
   `package`), plus one unrelated suite as a guard. `package` and
   `Sim.FrameLatencyBench` take 15-30 s per run.
2. Measure twice, 3 runs each:
   `python tools/perf_bench.py run --suite ... --build-dir build --runs 3 --out <results>/base-1.json`
   and `base-2.json`.
3. `compare base-1.json base-2.json`: any metric it calls better or worse is
   **drifting** on this machine. List those in the ledger; they can never be
   a target, and a regression on one is re-measured before it rejects.

## 3. Ledger

Create `docs/plans/perf-loop-<date>.md` in the worktree: objective,
environment (commit, CPU/GPU, config, power plan, background load), suites,
drifting metrics, and a table:

| # | Hypothesis | Target metric | Predicted | Actual (1 / 2) | Gate | Decision | Commit |

Write each row's hypothesis and prediction **before** editing code.

## 4. Each experiment (default 5, at most the requested number)

1. **Find the cost.** Read the hot path for the target; use Tracy
   (`docs/tracy-profiling.md`) or bench phase output where it helps. State
   one falsifiable hypothesis and a predicted gain with a number.
2. **Change one thing.** Keep determinism, sparse/lane equivalence, the
   allocation gates, rendering parity, no exceptions, and no new
   dependencies. No architecture changes (threading model, ownership, file
   formats, ABI) without asking; log the idea instead.
3. **Prove it is still correct.** Build the affected targets. Run the exact
   tests for the touched area, the matching parity tests (for example
   `IllumoGame.Wasm.LaneParity`, `IllumoGame.CanvasInf.IncrementalResampleParity`,
   `IllumoGpuTests backendparity` for render changes) and the allocation
   gates (`IllumoGame.Wasm.PackageFrameAllocations`,
   `IllumoGame.Wasm.LaneAllocations`). A failure gets one fix attempt;
   otherwise reject.
4. **Measure twice.** `exp-<n>-1.json` and `exp-<n>-2.json`, same suites and
   runs as the baseline, then:
   `python tools/perf_bench.py compare <base> exp-<n>-1.json exp-<n>-2.json --target "<metric substring>"`.
5. **Decide.**
   - `gate: PASS` and the target is not drifting: commit in the worktree
     (`git commit -F <message file>`, subject "Perf: <change> (<target>
     -x%)"). The new base is `exp-<n>-1.json`.
   - Otherwise reject: `git restore -- <files this experiment touched>`,
     then `git status --short` must be clean. If the only regressions are
     drifting metrics, measure once more before rejecting.
6. **Record** actual numbers from both measurements, the gate line, the
   decision and the commit hash in the ledger row. A rejected idea stays in
   the table with its numbers.

Stop early when two experiments in a row find nothing worth changing, or the
remaining ideas need an architecture decision.

## 5. Finish

1. Compare the original `base-1.json` with a fresh two-measurement run of
   the final tip, over every suite used.
2. Commit the ledger with a summary: predicted vs actual for every
   experiment, kept commits, rejected ideas, ideas that need a decision.
3. Report: branch, worktree path, kept commits with measured gains, the
   predicted-vs-actual table, and what was not checked (no live-window or
   GPU capture unless done; the full suite runs at push time through
   `/ship push`). Merging is the user's call (`/ship` from the worktree).
