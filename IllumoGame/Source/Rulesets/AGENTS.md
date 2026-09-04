# Rulesets subsystem guidance

This file specializes the repository `AGENTS.md` for
`IllumoGame/Source/Rulesets/`.

## Scope and boundaries

Rulesets define deterministic cellular transitions, neighbor-counting masks,
state validation, and presentation palettes. They may use Foundation and the
ruleset interface, but must remain independent of Game orchestration,
Rendering, Services, Engine, platform APIs, and OpenGL.

## Required invariants

- `nextState` is a pure function of current state and neighbor counts. Keep the
  cached 256x9 transition table equivalent to direct virtual evaluation.
- Binary rules encode state `0` as alive and `1` as dead.
- Wireworld encodes head `0`, empty `1`, tail `2`, and conductor `3`; only
  states declared by its counting mask contribute to neighbors.
- `evalCell` supplies a stable palette for every valid state. A palette change
  is user-visible behavior and must be tested with transition behavior.
- Rule 90 and Rule 184 are elementary 1D space-time rules (`NeighborhoodKind::Elementary1D`).
  Keep the 256x9 Moore table for Moore rules; do not express 184 as a popcount.
- Do not add allocation, I/O, rendering, mutable global state, or concurrency
  to transition evaluation.

## Adding or changing a ruleset

New rules for supported families (`LifeLikeRuleSet` for binary B/S,
`Elementary1DRuleSet` for Wolfram 1D) are cataloged in `rulesets.json` and
automatically discovered by `RuleSetRegistry`. When introducing a new family or
specialized transition logic, implement the family `.h`/`.cpp` pair, update
`RuleSetRegistry`, source lists, palette behavior, and focused domain tests
together. Preserve existing mode names used in persisted files unless a
compatibility migration is authorized.

## Documentation and verification

Use `docs/packages/source-layout.md`,
`docs/latex/sections/07-game-and-rules.tex`, and the formal decision log. Run
exact ruleset tests plus sparse serial/parallel identity and
persistence round trips. Include adversarial neighbor counts and every valid
state. Update this file only for durable Rulesets contracts.
