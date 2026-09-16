# Cyclic multistate rules

## Objective and measurable end state

Add a data-driven cellular-automaton model in which many distinct cell states
interact directly. A cell advances around a configurable state cycle when a
threshold number of its Moore neighbors hold that cell's successor state.

The change is complete when shipped vibrant multi-state families can be chosen
in New Simulation and the Ruleset Workshop, edited and exported, simulated on
infinite and finite-toroidal sparse canvases, and covered by focused and full
workspace tests.

## Current-state evidence

- `RuleSet` exposes alive-neighbor-count and elementary-1D evaluation only.
- `RuleSetRegistry` compiles life-like, Generations, Moore-table, and elementary
  definitions. None can distinguish counts of several neighbor states.
- `SparseCellGrid` optimizes the existing state-zero counting contract. Its
  complete target discovery is therefore not correct for interactions between
  arbitrary states across chunk boundaries.
- The Ruleset Workshop already edits family palettes and model-specific rule
  parameters, making it the appropriate surface for the new threshold and
  cycle-step controls.
- Canonical architecture is described in `docs/architecture-consensus.md`,
  `docs/packages/game.md`, and `docs/latex/sections/07-game-and-rules.tex`.

## Scope and non-goals

In scope:

- a `cyclic` family model with 2-256 named, colored states;
- per-rule successor threshold and cycle step;
- dense compatibility and production sparse evaluation;
- Ruleset Workshop editing and preview;
- vibrant shipped families, rules, and multi-state initial seeds;
- registry, simulation, workshop, and catalog tests;
- canonical documentation and a decision-log entry.

Non-goals:

- changing the optimized kernels for existing models;
- GPU or raw-OpenGL simulation;
- a general expression language, arbitrary pairwise matrix, or per-state rule
  editor;
- persistence-format changes (saved canvases already store byte-valued states
  and identify rules by catalog ID);
- performance parity with the highly specialized binary/counting kernels in
  this first implementation.

## Constraints and invariants

- State value `1` remains sparse background storage. With no successor-state
  neighbors it must remain quiescent.
- Existing rules, catalogs, saves, and optimized sparse paths remain behaviorally
  unchanged.
- Transitions and palettes remain deterministic and backend-neutral.
- `SparseCellGrid` remains the production domain; `CellGrid` support remains a
  compatibility path.
- The cycle step must be non-zero, smaller than the family state count, and
  coprime with the state count so every declared state participates in one
  cycle. Threshold is in the inclusive range 1-8.
- Game and rules code issue no OpenGL calls. Runtime mutation and publication
  continue through existing grid ownership and generation boundaries.

## Design and alternatives

`RuleSet` gains a state-histogram neighborhood kind and a 256-entry neighbor
count value. The default histogram evaluator delegates to the existing
state-zero count behavior, while data-driven cyclic rules inspect only the
configured successor state's count. This preserves existing subclasses and
keeps the new public contract narrow.

`SparseCellGrid` uses an isolated correctness-first evaluator for this model.
It expands every occupied source chunk by one chunk in all directions, samples
the eight neighbors for every target cell, and reuses the existing transactional
output, change-journal, torus canonicalization, and publication machinery. The
existing candidate, halo, memo, and worker-pool paths remain untouched.

Alternatives considered:

- Encoding the rules as Moore tables was rejected because those tables see only
  state-zero neighbor counts and cannot express many-state interaction.
- A complete 256-by-256 pairwise matrix was rejected as difficult to author and
  unnecessary for the desired cyclic ecology.
- Reworking all sparse kernels around histograms was rejected because it would
  add risk and overhead to mature binary paths.

## Contracts, compatibility, and migration

Family JSON adds model `cyclic`. Rule JSON for that model adds integer
`threshold` and `step`. Older JSON remains valid and serializes identically.
No save migration is required. Unknown cyclic data continues to fail catalog
validation transactionally.

## Ownership, threading, errors, and platforms

Definitions remain value-owned by `RuleSetRegistry`; `DataRuleSet` owns compiled
copies. Histogram values are stack-local during evaluation. The initial sparse
kernel is serial and preserves the existing main-thread/direct-generation
ownership rules. Allocation or evaluation failure follows existing `advance`
failure behavior. The change is platform-neutral C++23, with Windows remaining
the only verified platform.

## Ordered milestones

1. Extend the normalized rule definition, parser, serializer, validation, and
   `DataRuleSet` evaluation contract.
2. Add dense compatibility and isolated sparse histogram evolution, including
   infinite and finite-toroidal behavior.
3. Add workshop controls, preview semantics, and a colorful multi-state seed.
4. Ship two multi-state families and several varied rules.
5. Add focused tests and synchronize architecture/package/LaTeX documentation
   plus the decision log.
6. Format, build, run focused tests, run full Release CTest, build docs, inspect
   the final diff, and record results below.

## Verification strategy

- Build `IllumoGameTests` in Release.
- Run focused registry/ruleset, sparse-grid, and workshop tests by exact name.
- Run the full Release workspace build and `IllumoWorkspace` CTest label.
- Run `clang-format` on every modified C++ header/source.
- Build documentation with `docs/build.ps1` after source changes.
- No sanitizer is initially required: the new code uses bounded arrays and
  existing value containers, but failures or review findings may raise that
  requirement.
- No benchmark claim will be made. Existing microbenchmarks will be run only if
  the implementation touches an optimized path or exhibits a regression.
- No interactive GUI smoke test will be performed without separate user
  authorization; headless tests cannot prove the live visual result.

## Rollback and containment

The histogram model is gated by a new neighborhood kind and family enum. It can
be removed without rewriting existing rule data or optimized kernels. Shipped
catalog entries are additive. If the sparse evaluator cannot be made correct
within the existing transactional output boundary, stop before publishing the
new catalog entries rather than weakening existing paths.

## Open questions

None require user input. The requested emphasis favors a generous set of named
states and highly saturated palettes. Threshold and coprime step provide useful
variation while keeping the workshop understandable.

## Validation results

- Implemented the `cyclic` model, full neighbor-state-count contract, dense
  compatibility evaluator, isolated sparse evaluator, workshop controls,
  multi-state starter seed, two families, and five shipped rules. No material
  design deviation was required.
- Focused `IllumoGame.Rules.CyclicMultistate`,
  `IllumoGame.CellGame.RulesetWorkshopFamilyControls`, and
  `IllumoGame.CellGame.CyclicMultistateSeed` tests passed.
- The complete `IllumoGameTests.exe` run passed with zero failing cases.
- Full Release workspace build passed; its `IllumoWorkspace` run passed all
  406 tests.
- `IllumoTidy` passed across 140 first-party source files.
- Documentation rebuilt successfully to the 77-page `docs/output/illumo.pdf`.
  Existing LaTeX box/locale warnings remain non-fatal.
- `git diff --check` passed.
- Sanitizer and coverage runs were not performed because the implementation
  uses bounded value arrays and existing transactional containers, and the
  focused plus full regression suite covered the new ownership-free path.
- No live OpenGL GUI smoke test was performed because interactive launch was
  not authorized. Headless verification proves transition behavior and token/UI
  structure, not the final animated appearance in a real window.
- Remaining risk: the cyclic sparse kernel is intentionally serial and samples
  complete expanded chunks. Large cyclic populations may be slower than the
  specialized binary kernels; no performance claim is made.
