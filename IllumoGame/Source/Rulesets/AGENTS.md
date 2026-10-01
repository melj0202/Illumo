# Rulesets subsystem guidance

This file specializes the repository `AGENTS.md` for
`IllumoGame/Source/Rulesets/`.

## Scope and boundaries

Rulesets define deterministic cellular transitions, neighbor-counting masks,
state validation, and presentation palettes. They may use Foundation and the
ruleset interface, but must remain independent of Game orchestration,
Rendering, Services, Engine, platform APIs, and OpenGL.

## Required invariants

- Rule transitions are pure functions of current state and their declared
  neighborhood contract. Keep the
  cached 256x9 transition table equivalent to direct evaluation. Production
  rules are compiled from validated `RuleSetDefinition` data into `DataRuleSet`.
- Directional von Neumann rules receive neighbors in north, east, south, west
  order. Preserve that ordering in dense and sparse evaluators; do not collapse
  Turmite, lattice-gas, rule-table, or sandpile input into an unordered
  histogram.
- `von_neumann_table` rules keep Golly `@TABLE` semantics: first matching
  transition wins, repeated variables bind, unmatched neighborhoods keep their
  center, and Golly state 0 swaps with background state 1. Keep table text
  verbatim against its published source.
- Extended-range kernels count the state returned by
  `getExtendedCountedState`; neighborhood shape membership comes only from
  `RuleSet::extendedNeighborhoodContains`.
- Binary rules encode state `0` as alive and `1` as dead.
- `WeightedKernel` (Lenia) rules sum integer tap weights times
  `getKernelLevel` exactly and map that potential through a compiled growth
  table. Compile Lenia tables only from correctly rounded IEEE operations (no
  platform `exp`/`pow`) so lanes, the control store and the native oracle
  agree bit for bit, and reject growth that raises empty space.
- Wireworld encodes head `0`, empty `1`, tail `2`, and conductor `3`; only
  states declared by its counting mask contribute to neighbors.
- `evalCell` supplies a stable palette for every valid state. A palette change
  is user-visible behavior and must be tested with transition behavior.
- Rule 90 and Rule 184 are elementary 1D space-time rules (`NeighborhoodKind::Elementary1D`).
  Keep the 256x9 Moore table for Moore rules; do not express 184 as a popcount.
- Do not add allocation, I/O, rendering, mutable global state, or concurrency
  to transition evaluation.

## Rule definitions and compatibility

`RuleFamilyDefinition` owns the family ID, simulation model, state count,
labels, and colors. `RuleSetDefinition` owns stable rule identity, transition
parameters, and exactly one required `familyId`; it must not duplicate family
state metadata. The staged `IllumoGame/families.json` and
`IllumoGame/rulesets.json` are the source of truth for shipped definitions.
Family models include `life_like`, `generations`, `moore_table`, `cyclic`,
`species_life`, `larger_than_life`, `hodgepodge`, `turmite`, `lattice_gas`,
`dominance`, `von_neumann_table`, `sandpile`, `lenia`, and `elementary_1d`. Family catalogs use schema 1 and ruleset
catalogs schema 3;
unversioned and schema-v1/v2 legacy rule catalogs normalize into separated
definitions in memory. Validation must be transactional and preserve state `1`
as a quiescent background and state `0` as the only Moore-counted state. Rule
90/184 continue through the existing elementary 1D history path.

Game's `RuleCatalogLoader` owns file discovery and layers working-directory
`families.user.json` and `rulesets.user.json` overlays onto one valid base pair.
The registry is text-only, starts empty, and must not discover files or call
native APIs. Preserve existing rule IDs because saves reference them. New
transition semantics should extend the normalized definition/compiler only
when an existing family cannot express them; do not add a C++ class for a rule
that its data family can already describe. Keep parity references and tests
for legacy implementations until compatibility coverage no longer needs them.

## Documentation and verification

Use `docs/packages/source-layout.md`,
`docs/latex/sections/07-game-and-rules.tex`, and the formal decision log. Run
exact ruleset tests plus sparse serial/parallel identity and
persistence round trips. Include adversarial neighbor counts and every valid
state. Update this file only for durable Rulesets contracts.
