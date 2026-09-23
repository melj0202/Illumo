# Researched Cellular-Automata Family Expansion III

## Objective

Add cellular automata whose behavior is qualitatively beyond the existing
catalog: self-replicating loops defined by published rule tables, Abelian
sandpile fractals, Griffeath's long-range cyclic spirals, and Larger-than-Life
rules with decaying trails, plus a broad set of classic Life-like and
Generations rules. Every shipped rule starts from a starter that demonstrates
its behavior immediately.

## Research basis

- C. G. Langton, "Self-reproduction in cellular automata", Physica D 10
  (1984); J. Byl, "Self-reproduction in small cellular automata", Physica D 34
  (1989); J. A. Reggia, S. L. Armentrout, H.-H. Chou, and Y. Peng, Science 259
  (1993); H. Sayama's structurally dissolving (SDSR, 1998) and evolving
  (Evoloop, 1999) loops. The shipped tables and starting loops are the
  transcriptions distributed with Golly (`Rules/*.rule`,
  `Patterns/Loops/*.rle`), themselves derived from Eli Bachmutsky's 1999
  `loops.java`; the Evoloop and SDSR tables include Sayama's patches for
  undefined transitions. Golly's RuleTable semantics:
  https://golly.sourceforge.io/Help/formats.html#table
- P. Bak, C. Tang, and K. Wiesenfeld, "Self-organized criticality",
  Phys. Rev. Lett. 59 (1987): https://doi.org/10.1103/PhysRevLett.59.381
- R. Fisch, J. Gravner, and D. Griffeath, "Cyclic cellular automata in two
  dimensions" (1991): https://www.math.ucdavis.edu/~gravner/papers/cca.pdf,
  and the Griffeath rule names (313, Lava Lamp, Stripes, Squarish Spirals,
  Cyclic Spirals, Turbulent Phase) as catalogued by MCell.
- Golly's Larger than Life documentation, including the `C` decay-state
  parameter and `NN` von Neumann neighborhoods:
  https://golly.sourceforge.io/Help/Algorithms/Larger_than_Life.html
- Golly and MCell Life-like and Generations rule collections for the classic
  B/S and S/B/C rules.

## Scope

- Add `VonNeumannTable` and `Sandpile` family models on the existing
  direction-preserving von Neumann contract.
- Compile Golly `@TABLE` text (variables, comma or compact transitions, and
  `none`/`reflect_horizontal`/`rotate4`/`rotate4reflect`/`permute`
  symmetries) into a dense lookup of at most 12^5 entries.
- Extend cyclic rules with an optional range and square/circular/diamond
  shape; extend Larger than Life with the diamond shape and C-state decay.
- Generalize the extended-range kernel to count a rule-selected state and
  read each target through a halo window.
- Add an exact Golly-RLE starter strategy.
- Ship the rules, palettes, focused tests, and synchronized documentation.

## Non-goals

- Moore-neighborhood rule tables (a dense 9-cell table is infeasible), JvN29
  or other tables above twelve states, and Golly `@TREE` input.
- Margolus/block partitioning, probabilistic updates, or continuous (Lenia)
  states.
- Parallelizing the directional or extended-range kernels.

## Design

Table text is stored verbatim in `rule_table` with a separate `symmetries`
key; `n_states` and `neighborhood` come from the family. Compilation expands
each transition's distinct variables with an odometer (repeated variables bind
to one value), applies every symmetry permutation, and writes only unset
entries, so the first written transition wins. Remaining entries keep their
center. Golly state 0 swaps with Illumo background state 1 so the sparse
background invariant holds; a table whose quiescent neighborhood leaves
background is rejected. Expansion is capped at eight million entries.

Sandpile families hold heights 0..7 in the background-zero encoding followed
by `stateCount - 8` source phases. Heights never exceed seven because a
toppling cell keeps at most three grains before receiving four. The phase-0
source emits four grains; the shipped family pulses every other generation,
which lets the pile relax enough to show the fractal.

Cyclic rules with radius above one, or any non-square shape, report
`ExtendedRange`; `getExtendedCountedState` returns the successor so the shared
kernel counts successors. Radius-one square rules keep the Moore histogram
path, and their serialized form omits the new keys. A full cycle also advances
the empty background, so a prototype showed long-range soups invading the
infinite canvas at roughly one to three cells per generation (the existing
14-color rule spreads one cell per generation). The optional
`inert_background` flag removes state 1 from the cycle; the shipped Griffeath
families add a `Void` state and start from void-free species soups, so each
soup stays in a bounded dish. Larger-than-Life families
with more than two states send unsupported active cells into a decay trail
that neither counts nor accepts births.

`SparseCellGrid::fillHaloWindow` copies a canonical target chunk plus its
margin once, replacing a hash lookup per neighbor per cell in the extended and
directional kernels. Neighborhood membership is single-sourced in
`RuleSet::extendedNeighborhoodContains`.

## Ordered milestones

1. Contract, registry parsing/validation/serialization, and table compiler.
2. `DataRuleSet` transitions for tables, sandpiles, long-range cycles, and
   trails; halo-window kernels; dense reference evaluator.
3. RLE starters and automatic defaults; workshop labels and previews.
4. Catalog data generated from the published tables and prototyped rules.
5. Tests, documentation, formatting, full build, CTest, and tidy.

## Validation record

- The loop tables were first run in an independent Python implementation of
  Golly's RuleTable semantics; Langton's loop visibly produced four daughter
  loops by generation 360. Sandpile, Griffeath, and trailing Larger-than-Life
  parameters were chosen from rendered prototypes; Comet Rockets' spaceship
  was extracted from a soup and verified to travel at one cell per generation.
- New focused cases: `IllumoGame.Rules.VonNeumannTables`,
  `IllumoGame.Rules.SandpileFamily`,
  `IllumoGame.Rules.LongRangeCyclicAndTrails`, and
  `IllumoGame.CellGame.EveryShippedRuleStarts`. The loop cases match the
  Python reference's state histograms at generations 60 and 300 for all six
  loop rules. Sparse torus and infinite grids match the dense reference for
  Langton's Loops, the sandpile, range-three and diamond cyclic rules, and a
  trailing Larger-than-Life rule.
- `IllumoGame.Rules.ExperimentalFamilies` was updated because Caterpillars
  joins the four-phase Generations family by design.
- Full-build, CTest, tidy, and documentation results are recorded in the
  change's handoff.
