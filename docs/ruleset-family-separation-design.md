# Separate rule families from rulesets

**Status: implemented and Release-verified 2026-09-15.** The Windows GUI was
not manually smoke-tested.

## Problem and previous implementation

Before the migration, one `RuleSetDefinition` owned the rule ID, family
selector, state count, state names, colors, and transition data. The typed
family value also selected the simulation path: Life-like and Generations
compiled B/S data, Moore-table rules used a 256-by-9 table, and elementary
rules used the separate one-dimensional history path. The F2 draft could
change family directly.

F1 and New Simulation selected only a ruleset ID. `CellContext` stored that
one ID, `ModeString` was the startup/configuration variable, and `.illumo`
version 3 stored a fixed ruleset-ID field. A valid version-3 load resolved the
ruleset from the external catalog; that catalog therefore determined both the
cell schema and transition behavior. Version-2 sparse and legacy dense-save
readers were already supported.

The canonical architecture decision D-GC3 distinguishes a named ruleset from
its typed transition family, but it does not establish reusable family data.
This proposal replaces that representation: a ruleset references exactly one
family, and a family owns the cell-state schema used by all of its rules.

## Intended result

Represent a **family** and a **ruleset** as separate stable data definitions.
Family data declares the available cell states, their labels and colors, and
the simulation model required to interpret transitions. A ruleset declares a
stable ID, display name, one required family ID, and transition parameters
valid for that family's model. Rulesets cannot be combined with arbitrary
families; the registry rejects a missing family or a transition form that is
invalid for the referenced family.

The built-in model kinds remain bounded to the existing contracts:

- Life-like: binary Moore neighborhood, B/S transition counts.
- Generations: Moore neighborhood, B/S counts and deterministic decay through
  the states declared by the family.
- Moore table: current-state and counted-neighbor transition rows.
- Elementary 1D: Wolfram neighborhood and the existing history-row evolution.

The model kind continues to select the evaluator. Family identity and state
metadata become data-owned and reusable. The sparse-grid invariants remain:
state 1 is quiescent background, state 0 is the counted state, and transitions
preserve a uniform background. Families must satisfy these constraints until
the sparse simulation API is separately extended.

## Proposed data and runtime contracts

### Catalogs

Ship a `families.json` catalog with `schema_version: 1` and a separate
`rulesets.json` catalog with `schema_version: 3`. A family record contains a
stable family ID, display name, model kind, and a complete ordered state list
(value, name, RGB color). A ruleset record contains a stable ruleset ID,
display name, `family_id`, and only the model-specific transition data. Family
and ruleset IDs remain distinct; a ruleset may not redefine state names,
colors, or state count. For example, a Life-like rule stores `family_id`,
`birth`, and `survive`; its referenced family supplies the two state records.

Use matching optional `families.user.json` and `rulesets.user.json` overlays.
Catalog discovery selects a valid base pair from one location before applying
the working-directory overlays. The registry publishes neither catalog until
both base files and all present overlays validate together. A ruleset can only
be persisted after its referenced family is available. Exporting a portable
rule package includes the referenced family and ruleset as separate objects;
import validates and stages the pair before publishing either.

The C++ registry owns `RuleFamilyDefinition` and `RuleSetDefinition` collections.
The latter contains a family ID rather than copied state metadata. Registry
compilation resolves the pair into the existing immutable `RuleSet` runtime
view, which continues to expose state validation, labels, palette evaluation,
neighborhood kind, and a cached transition table to Game and
`SparseCellGrid`. This keeps cell rendering and simulation call paths simple
while removing duplicated ownership from the data format. No new runtime
framework, scripting language, or simulation API is introduced.

### Menus and selection

F1 and New Simulation present a family selector followed by a ruleset selector
filtered to that family. The pair is always validated and applied together;
there is no state where a selected rule has a different family. The current
family and ruleset are both visible in status and relevant rule-selection
surfaces.

F2 remains the rules workbench entry point, but clearly separates **Family**
editing from **Ruleset** editing. Family editing owns state count, state names,
colors, and model kind. Ruleset editing owns its stable identity, family
reference, and transition controls. A rule is created against one selected
family; changing an existing rule's family is a deliberate duplicate/rebind
operation that must validate against the destination schema. Family changes
that reduce state count are rejected if live cells would become invalid. The
active simulation is drained before replacing an active compiled pair, as it
is today.

Console selection remains convenient by ruleset ID and reports the associated
family. Existing `ruleset` and `mode` commands remain aliases. Add explicit
family display/listing only where it helps discovery; no second independent
runtime-selection path is introduced.

Configuration moves from the ambiguous `ModeString` to explicit family and
ruleset settings. Existing configuration that supplies only `ModeString`
continues to select the same ruleset; its family is derived from that rule.
New writes persist the explicit pair. No Illumo engine or generic settings API
changes are required.

### World persistence and compatibility

`.illumo` version 4 stores both the family ID and ruleset ID while retaining
external catalog references rather than embedding definitions. On load, the
registry verifies that the selected ruleset belongs to the stored family and
that every cell state is valid for that family before replacing live state.
The load remains transactional.

Version-3 worlds derive family ID from their saved ruleset ID. Version-2
sparse and legacy dense saves retain their current migration behavior, then
resolve the ruleset and family through the loaded catalog. Existing built-in
and custom ruleset IDs remain stable. Missing IDs, mismatched pairs, or
unsupported cell states fail with a useful error and leave the current world
unchanged.

The catalog reader retains schema-v1 and schema-v2 compatibility. Legacy rule
records are normalized into a family-plus-ruleset pair. Records whose family
kind and full state schema match a known family reuse it. A legacy record with
a distinct state schema receives a deterministic compatibility family so
custom labels, colors, and state counts are not silently lost. Reading an old
catalog does not rewrite it; the new overlays use the separated schemas.

Built-in family and ruleset IDs are immutable compatibility points. Editing a
built-in starts a user-defined copy, following the existing built-in-ruleset
policy. Editing a custom family's appearance affects all rules that reference
it. Any edit that changes its state schema must validate the active world and
all rules referencing that family before Apply; it cannot silently reinterpret
stored cell values.

## Alternatives considered

1. **Keep state metadata embedded in each ruleset.** Rejected because it
   duplicates cell schemas and palettes across rules and cannot make a saved
   family a stable, explicit reference.
2. **Allow arbitrary family/ruleset combinations.** Rejected per the clarified
   requirement and because Life-like, Generations, Moore-table, and elementary
   transitions have different input and evolution contracts.
3. **Replace the current simulator with a generic transition scripting
   system.** Rejected as unrelated expansion. Existing model kinds and sparse
   transition-table execution remain the boundary.
4. **Keep `.illumo` version 3 and infer family forever.** Rejected for new
   writes because it leaves the family implicit and cannot detect a mismatched
   pair. The version-3 reader remains as a compatibility bridge.

## Scope and non-goals

In scope: family and ruleset schemas; registry validation and compilation;
base/overlay discovery, import/export, and persistence; F1 and New Simulation
selection; F2 family/ruleset editing; startup settings and console/status
integration; `.illumo` version 4 with older readers; focused tests and canonical
documentation.

Out of scope: new neighborhood geometries, non-quiescent backgrounds, multiple
independently counted states, stochastic or continuous automata, arbitrary
user code, new persistence of cell arrays, changes to sparse storage or
simulation threading, and generic Illumo engine settings or menu frameworks.

## Compatibility, ownership, and runtime consequences

- `RuleCatalogLoader` owns file discovery and I/O; `RuleSetRegistry` remains
  text-only and transactionally validates catalogs.
- The immutable compiled rule view owns resolved family metadata for its
  lifetime. `SimulationRunner` keeps borrowing it; rule/family replacement
  continues to drain outstanding work first.
- `SparseCellGrid` keeps its current transition table and elementary dispatch.
  Existing serial/parallel and candidate/halo/frontier paths must produce
  identical generations.
- The family/rule pair is validated before active state changes. File writes
  remain atomic per catalog; a newly authored family is saved before a rule
  that references it. Orphan custom families are valid catalog entries.
- Only the Windows product path is in scope. No platform API or OpenGL change
  is expected.
- F2's family editor may add definition rows but remains primitive-composed
  through `GameVisual`; it does not add a retained widget system.

## Implementation record

- Added family-owned and ruleset-owned data definitions, strict pair
  validation, schema-v1 family and schema-v3 rules catalogs, and compatibility
  normalization for unversioned/schema-v1/schema-v2 rule catalogs.
- Compiled family/rule pairs into the existing immutable `RuleSet` interface;
  moved state names, colors, counts, and model ownership to families without
  changing sparse evaluation or simulation APIs.
- Carried the explicit pair through `CellContext`, startup configuration, F1,
  New Simulation, F2, runtime status, inspector data, and save/load. Legacy
  `ModeString` remains a compatibility input.
- Added v4 saves containing both IDs. Version 3, version 2, and dense legacy
  readers derive the family from the saved rule and validate all cell states
  before committing the world.
- Split F2 family and rule drafts, added portable package import/export,
  built-in-copy behavior, and family-schema/live-state validation. Apply drains
  in-flight simulation work and persists family data before its linked rule.
- Updated architecture, package, workshop, decision, and subsystem guidance.

## Verification record

Focused registry, catalog, F1/F2, family/ruleset, simulation-parity, and
persistence tests passed. The Release workspace build completed and its
embedded run passed all 402 tests; a separate
`ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure` run
also passed all 402 tests. `python build.py tidy` passed across 140 first-party
translation units. `clang-format` was run on all changed C++ files, `git diff
--check` passed, and the architecture PDF rebuilt successfully.

The native Windows GUI was not manually smoke-tested in this environment, so
live rendering and native file dialogs are not covered by the headless results.

## Family editing policy

Custom families are edited in place, so appearance changes affect every
referencing rule. State-schema edits revalidate all referencing rules and the
active world before Apply. Built-in families are copied before editing. Saved
worlds continue to reference external catalog IDs; invalid cell values in a
saved world are rejected transactionally when loaded. This preserves the
external catalog-reference model without silently rewriting cell data.
