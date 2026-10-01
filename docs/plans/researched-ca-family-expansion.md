# Researched Cellular-Automata Family Expansion

## Objective and measurable end state

Expand IllumoGame with documented cellular-automata families whose shipped
rules produce sustained, visibly distinct behavior from suitable initial
conditions. The completed change will add:

- colorized Life (Immigration and QuadLife) with interacting live species;
- Larger-than-Life rules with configurable range, center counting, and square
  or circular neighborhoods;
- published Generations examples spanning 3 through 24 states;
- the classic 14-color cyclic cellular automaton studied by Fisch, Gravner,
  and Griffeath;
- deterministic, model-appropriate starter patterns stored as ruleset data.

Success requires catalog round trips, workshop preservation, dense
compatibility where applicable, sparse infinite and toroidal correctness,
focused behavioral tests, the complete Release suite, formatting, and
IllumoTidy.

## Current-state evidence and research basis

The live registry separates family-owned states/palettes from rule-owned
transitions. `DataRuleSet` already supports radius-one totalistic rules and
full Moore state histograms, while `SparseCellGrid` routes histogram rules
through an isolated complete-chunk evaluator. The current startup path uses a
Life glider for every non-elementary, non-histogram rule, which is unsuitable
for Generations and excitable media.

Primary and project-authoritative references:

- Golly's official Generations documentation defines the state-cycle model
  and lists Banners, Bloomerang, Brian's Brain, Cooties, Fireworks, Frogs,
  Lava, Star Wars, Sticks, Transers, and Xtasy with canonical parameters.
- Golly's official Larger-than-Life documentation defines range, state count,
  optional center counting, survival/birth intervals, neighborhood shape, and
  published examples including Bosco/Bugs, Bugsmovie, and Globe.
- Fisch, Gravner, and Griffeath's 1991 paper defines cyclic competition and
  specifically studies a random 14-color system on a 256 by 256 torus.
- The January 1979 BYTE article's Quad-Life description defines majority-color
  births and the fourth-color result when three distinct parents meet.

## Scope and non-goals

In scope:

- registry data contracts, compilation, serialization, and legacy-safe
  defaults;
- `DataRuleSet` transition behavior for species and extended-range families;
- a contained sparse extended-range evaluator;
- deterministic starter generation;
- shipped catalogs, canonical architecture/package documentation, and tests.

Non-goals:

- Lenia, SmoothLife, or other continuous-valued convolution systems;
- Margolus/block cellular automata, lattice gases, asynchronous updates, or
  probabilistic rules;
- a new renderer, storage domain, retained UI system, or GPU simulation path;
- generalized Golly rule-file import or every neighborhood Golly supports.

## Constraints and invariants

- State value 1 remains the sparse background; state 0 remains the counted
  active state for historical rules.
- Existing catalog schemas and rules remain loadable with unchanged behavior.
- New background transitions are quiescent; B0 extended-range rules remain
  unsupported because they cannot be sparse on an infinite plane.
- Simulation stays deterministic and backend-neutral.
- Extended range is intentionally bounded to 16 cells and initially supports
  square and circular neighborhoods only.
- Finite topology remains toroidal and uses canonical wrapped cell lookup.
- Workshop cloning, applying, importing, and exporting must preserve all new
  fields even before every parameter receives a dedicated control.

## Proposed design and alternatives

Add `SpeciesLife` and `LargerThanLife` family kinds. Species Life reuses the
existing full-state Moore histogram route: all non-background states count as
live, survival preserves species, and births choose a majority species;
QuadLife selects the absent fourth species when three distinct parents meet.

Larger-than-Life receives a dedicated `ExtendedRange` neighborhood contract
with radius, shape, center-inclusion, and count-based transition methods. The
sparse evaluator expands occupied source chunks by the required chunk radius,
counts live cells directly, and emits transactional result chunks through the
existing retained output machinery. This deliberately favors correctness and
containment over integrating the radius-one candidate/memo fast paths.

Each ruleset gains a seed strategy. The catalog can request a glider, wire,
single cell, deterministic active soup, deterministic phase soup, species
soup, or excitable wave break. Missing seed data maps to the historical
kind-based default, preserving older files.

Rejected alternatives:

- Encoding species Life as a 256 by 9 table cannot inspect parent colors.
- Approximating wide neighborhoods with repeated radius-one generations
  changes the rule and its dynamics.
- Adding continuous or block-partitioned models to the byte-state synchronous
  kernel would require new domain contracts and exceeds this feature.
- Nondeterministic random starters would make failures and demonstrations hard
  to reproduce.

## Contracts, ownership, threading, and compatibility

Definitions remain value-owned by `RuleSetRegistry`; `DataRuleSet` copies the
compiled definition and family. No pointer lifetime or subsystem ownership
changes. The new extended evaluator is serial in this milestone and runs
behind `SimulationRunner` exactly like existing sparse evaluation. Existing
files omit the new fields safely. New files remain schema-version 3 because
the added keys are optional and additive.

## Ordered implementation milestones

1. Add rule/family contracts, parsing, validation, serialization, and focused
   registry tests.
2. Implement species transitions and dense/sparse behavioral tests.
3. Implement extended-range dense/sparse evaluation and topology tests.
4. Replace heuristic startup selection with rule-owned deterministic seeds and
   test nontrivial evolution.
5. Add researched families/rules and catalog assertions.
6. Synchronize architecture and package documentation.
7. Format, build, run focused tests, full Release CTest, IllumoTidy, and review
   the final diff.

## Verification and containment

Run exact registry, ruleset, sparse-grid, and module tests first. Then build
`IllumoGameTests`, run the complete executable, perform the Release workspace
build and labeled CTest suite, and run `IllumoTidy`. Sanitizers or coverage are
not required unless focused failures expose memory/lifetime risk. No live GUI
smoke test is authorized; visual behavior will be supported by deterministic
generation assertions rather than claimed as observed pixels.

The changes remain isolated behind new enum routes and optional definition
fields. Removing the new catalog entries and routes restores prior behavior
without persistence migration.

## Open questions and decisions

No user decision is required. The explicit request authorizes the broad
family expansion. Range 16, square/circular neighborhoods, and serial
extended evaluation are deliberate first-contract limits.

## Validation record

- Focused researched-interaction, starter-seed, cyclic-family, and catalog
  tests passed in the Release test binary.
- The complete `IllumoGameTests` executable passed.
- The Release workspace build completed successfully and its integrated test
  run passed all 408 tests.
- Explicit labeled Release CTest passed all 408 `IllumoWorkspace` tests.
- `IllumoTidy` passed across 140 first-party source files.
- The documentation build completed successfully and produced a 78-page PDF;
  only the existing LaTeX box and locale warnings were reported.
- No live OpenGL GUI smoke test, sanitizer run, or coverage run was performed.
