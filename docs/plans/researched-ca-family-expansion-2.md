# Researched Cellular-Automata Family Expansion II

## Objective

Add another broad set of visually active cellular-automata families whose
mechanics are materially different from Life-like counting: multilevel
chemical excitation, directional mobile agents, lattice-gas particles, and
many-species dominance. Give every shipped rule a deterministic starter that
demonstrates its behavior immediately.

## Research basis

- Gerhardt, Schuster, and Tyson's Hodgepodge Machine models excitable media
  with a healthy state, a maximally ill state, many intermediate infection
  levels, neighborhood averaging, and a configurable infection increment.
  Dewdney's contemporary description records the transition equations and
  the classic `n=100`, `k1=2`, `k2=3` experiment:
  https://people.sc.fsu.edu/~jburkardt/classes/ysp_2003/dewdney_hodgepodge.pdf
- Moreira and Gajardo formalize Turmites as directional grid agents over an
  `n`-symbol tape. Each symbol selects a left/right turn, the visited symbol is
  incremented, and the agent advances one cell. Their examples include the
  classic `RL` Langton ant plus `RRL` and `RRLL` multi-color rules:
  https://www.ci2ma.udec.cl/pdf/pre-publicaciones2/2016/pp16-19.pdf
- Hardy, Pomeau, and de Pazzis introduced a deterministic two-dimensional
  lattice gas with exact particle dynamics; Toffoli and Margolus document the
  collision/streaming cellular-automaton formulation:
  https://doi.org/10.1103/PhysRevA.13.1949
  https://people.csail.mit.edu/nhm/cam-book.pdf
- Vukov, Szolnoki, and Szabo study five-species cyclic dominance in which each
  species has two prey and two predators and spatial interactions form moving
  interfaces and spiral arms:
  https://arxiv.org/abs/1308.0964

## Scope

- Add `Hodgepodge`, `Turmite`, `LatticeGas`, and `Dominance` family models.
- Extend the rule contract with directional von Neumann neighborhoods while
  keeping full-state Moore histograms for Hodgepodge and dominance.
- Add validated, round-trippable family-specific parameters.
- Add a serial correctness-first sparse directional evaluator and dense
  compatibility evaluation.
- Add deterministic chemical, turmite-swarm, particle-cloud, and species-soup
  starters.
- Ship classic and high-activity rules with vivid state palettes.
- Add focused transition, sparse-boundary, torus, starter, catalog, and
  serialization tests; synchronize canonical documentation.

## Non-goals

- Probabilistic Monte Carlo scheduling, random transitions, or coordinate/time
  dependent rules.
- Hexagonal FHP gas, continuous fluid claims, or quantitative hydrodynamic
  validation.
- Multiple internal Turmite controller states, arbitrary turn angles, or a
  generic Turing-machine file format.
- Optimizing the new directional path into the radius-one candidate/frontier
  fast paths before correctness and behavior are established.

## Design

`RuleSet` gains a four-entry directional neighbor array ordered north, east,
south, west and a `VonNeumannDirectional` neighborhood kind. `DataRuleSet`
implements Turmites and HPP gas from that preserved direction data.

Turmite families encode `n` tape colors followed by `4n` agent states. State 1
is tape color zero/background; state 0 maps tape color one so the sparse-grid
background contract remains intact. An agent cell writes the next tape color,
turns according to the rule word, and moves. Simultaneous arrivals annihilate
deterministically rather than depending on evaluation order.

HPP gas encodes four directional occupancy bits into 16 cell states, with the
empty bitmask mapped to background state 1. Each source applies the standard
head-on collision (north/south to east/west and vice versa), then particles
stream into the target from its four neighbors. This preserves particle count
without adding generation parity.

Hodgepodge maps conceptual healthy level zero to sparse background state 1 and
conceptual infection level one to state 0. Intermediate and ill levels retain
their numeric states. The full Moore histogram supplies infection counts and
weighted sums.

Dominance families reserve state 1 for empty space and map all other states to
species ordinals. A rule declares prey offsets and an invasion threshold; a
cell changes to its locally strongest predator when that predator reaches the
threshold. This is a deterministic synchronous CA inspired by, but not
claimed to reproduce, the paper's stochastic Monte Carlo schedule.

## Compatibility and risk containment

All new JSON keys are additive under schema 3. Existing families retain their
current execution paths and serialized representation. New evaluators use the
same transactional inactive-map publication and changed-chunk journal as the
other correctness-first full-neighborhood paths. No engine, rendering,
persistence-format, or threading boundary changes.

## Ordered milestones

1. Extend rule/family contracts, parsing, validation, and serialization.
2. Implement and test Hodgepodge and dominance neighborhood transitions.
3. Implement directional dense/sparse evaluation.
4. Implement and test Turmites and HPP lattice gas.
5. Add deterministic starters and shipped family/rule catalogs.
6. Update workshop summaries and canonical documentation.
7. Format, run focused tests, full Release build/CTest, `IllumoTidy`, docs,
   and final diff review.

## Validation record

- JSON catalogs parsed successfully with PowerShell `ConvertFrom-Json`.
- Focused Release tests passed for
  `IllumoGame.Rules.DirectionalChemicalFamilies`,
  `IllumoGame.CellGame.ResearchedStarterSeeds`, and the preceding researched
  interaction coverage.
- The complete `IllumoGameTests.exe` binary passed after formatting.
- `cmake --build build --config Release` passed, including documentation and
  all integrated workspace tests.
- `ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure`
  passed: 409/409 tests.
- `cmake --build build-tidy --target IllumoTidy` passed across 140 first-party
  source files.
- `git diff --check` passed.
- No interactive OpenGL smoke test was run; the automated checks validate
  rules, sparse evolution, registry/workshop data flow, starter construction,
  and documentation, but do not visually certify the live palette or motion.
