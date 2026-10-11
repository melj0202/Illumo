---
name: illumo-performance-audit
description: Measure and diagnose Illumo simulation, presentation, upload, and frame-pacing performance. Use for lag, low TPS or FPS, slow sparse generations, expensive camera movement, texture-upload stalls, VSync confusion, Tracy analysis, or requests for more performance improvements. Diagnose first; implement optimizations only when the user asks for changes.
---

# Illumo Performance Audit

Measure first, isolate the responsible lane, and preserve simulation semantics.

## Preserve the performance contracts

Before proposing or implementing work, verify these live invariants:

- `SparseCellGrid` uses signed 64-bit coordinates and sparse 16x16 chunks.
- Candidate, halo, frontier, serial, and worker paths remain deterministic and equivalent.
- Stored-cell and neighbor-counting masks retain their distinct meanings.
- `SimulationRunner` permits one outstanding generation, publishes at frame boundaries, and does not create a catch-up backlog.
- Spare-grid reuse and `SparseGenerationDelta` remain correct across state mutation, ruleset changes, persistence, manual stepping, and shutdown.
- `CanvasView` retains bounded cache, LOD, dirty-region, and upload behavior.
- Paced swap completion and CPU submission rates remain separately reported.

## Capture a reproducible baseline

Record commit and branch, dirty state, build type, compiler, CPU/GPU, display refresh, window mode, VSync setting, ruleset, topology, pattern, live population, allocated chunks, target TPS, camera, zoom, fade state, run duration, and warm-up. Prefer Release builds, repeated runs, medians, and tail behavior over a single best result.

## Measure the correct lane

### Simulation

Use status metrics plus `Illumo.Sim.MicroBench` and `Illumo.Sim.SparseMicroBench`. Compare representative dense, sparse, settled, finite-torus, and multistate workloads. Attribute time to candidate preparation, halo preparation, evaluation, publication, delta consumption, or other observed phases before changing code.

### Presentation and upload

Observe cache refills, exact or overview bins, fade work, upload bytes, dirty rectangles, direct uploads, PBO use, and fallback. Compare camera motion inside the cache, cache exits, zoom/LOD transitions, palette changes, replacement, and dense visible activity.

### Pacing

Compare persisted `vsync=1` and explicit profiling mode `vsync=0`. Treat monitor-paced swap completions separately from CPU submissions. Use Tracy or another configured profiler to locate waits and hot paths; do not infer a CPU bottleneck from paced FPS alone.

## Make bounded improvements only when authorized

1. Form one falsifiable hypothesis from measured evidence.
2. Prefer removing measured work, allocation, copying, hashing, synchronization, or uploads over adding speculative frameworks.
3. Keep small workloads on the simpler serial path when worker overhead dominates.
4. Preserve deterministic equivalence tests for every optimized path and all relevant rulesets and topologies.
5. Avoid redesigning the sparse model, renderer, threading contract, or presentation cache without explicit architectural authorization.

## Validate and report

Repeat the identical baseline and report workload, environment, median, variance, tails, and limitations. Run focused equivalence tests, the allocation gates, relevant GUI smoke checks, and a final diff review; the full Release suite runs before the push (root `AGENTS.md`, "When to run what"). Compare builds with `tools/perf_bench.py` rather than single runs. If an optimization lacks a repeatable win or weakens correctness, remove only edits made by the current agent within the authorized task using scoped patches. Preserve pre-existing work, and do not use Git rollback operations without explicit authorization. Separate observed bottlenecks, implemented changes, rejected hypotheses, and unmeasured follow-ups.
