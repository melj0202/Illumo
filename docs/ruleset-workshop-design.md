# Data-driven rules and the F2 Ruleset Workshop

**Status: implemented 2026-09-15.** Ruleset families and transition rules are
separate definitions used throughout IllumoGame. The native Windows GUI was
not manually smoke-tested; automated evidence is recorded below.

## Data model

`IllumoGame/families.json` contains schema-version-1 family definitions. A
family owns its stable ID, display name, simulation model, state count, ordered
state names, and colors. Supported models are `life_like`, `generations`,
`moore_table`, and `elementary_1d`.

`IllumoGame/rulesets.json` contains schema-version-3 ruleset definitions. A
ruleset owns its stable ID, display name, one required `family_id`, and only the
transition parameters for that model: B/S counts, a Moore transition table, or
a Wolfram rule number. A rule cannot be paired with a different family at
runtime. `RuleFamilyDefinition` owns cell schema and appearance;
`RuleSetDefinition` owns transition behavior. The registry validates both
definitions together and compiles them into the immutable runtime `RuleSet`
interface used by the existing sparse transition table and elementary history
path.

The shipped IDs for Game of Life, Seeds, Brian's Brain, HighLife, Day & Night,
Life Without Death, Wireworld, Rule 90, and Rule 184 remain unchanged. Older
unversioned, schema-v1, and schema-v2 rule catalogs are normalized in memory
into family/ruleset pairs. Legacy records with the same complete state schema
share a deterministic compatible family; distinct schemas retain their own
family data. Loading old files does not rewrite them.

State 0 remains the counted state, state 1 remains the quiescent background,
and every family must preserve that background under an all-background
neighborhood. B0-like and odd elementary rules remain unsupported by the
sparse storage contract. The transition kernel, neighborhood geometries, and
simulation APIs are unchanged.

## Catalog loading and persistence

`RuleCatalogLoader` selects the first valid `families.json`/`rulesets.json`
base pair from the executable directory, working directory, or product
subdirectory. It then applies working-directory `families.user.json` and
`rulesets.user.json` overlays together. The registry itself remains
filesystem-free and transactionally validates text. A malformed base pair
falls through; a malformed overlay leaves the active catalog unchanged.

F2 Apply persists a user-defined family before the ruleset that references it.
Rules and families export together as a portable JSON package and imports are
validated as a pair. Editing a built-in family or rule produces a custom copy
instead of changing the meaning of a stable built-in ID. Custom family
appearance and schema edits update the family definition used by every linked
rule; the registry revalidates linked rules before replacement. A family
schema change that would invalidate live cells is rejected. Apply drains any
in-flight generation before replacing the active compiled pair and refreshing
the canvas palette and paint-state choices.

Sparse `.illumo` version 4 writes both family and ruleset IDs. Version 3,
version 2, and dense legacy saves continue to load by deriving the family from
their existing ruleset ID. Save/load validates the pair and all stored states
before replacing the active world. A custom world still requires its catalog
to be available at load time; rule code or definitions are not embedded in the
world file.

Startup configuration now stores `FamilyString` and `RuleSetString` separately.
Legacy `ModeString` remains accepted and derives the matching family. F1 and
New Simulation show a family selector followed by its filtered ruleset list;
the pair is also shown by runtime status and inspector surfaces. Console
ruleset/mode commands continue to identify behavior by the stable ruleset ID.

## F2 user interface

F2 opens a dedicated, primitive-composed `RulesetWorkshopMenu`. One scrollable
page is organized as Rule, Preview, Appearance, and Files. Rule controls and a
plain-language transition example appear first. Appearance exposes family
state names and colors; Files exposes JSON import/export. Save & Apply and
Discard remain in a fixed action area below the content viewport.

Controls depend on the chosen family's model. Life-like and Generations expose
selectable birth/survival counts; Generations also exposes state count;
elementary rules expose their Wolfram number; Moore-table rules direct users to
JSON editing. The preview labels its sample inputs and resulting state. State
labels, colors, and palette previews belong to the family draft, separate from
rule transition parameters.

Mouse focus, keyboard movement, text entry, and wheel scrolling have separate
roles. Arrows/WASD and Tab navigate applicable controls; Page Up/Down and
Home/End move through the scrollable content; wheel input moves the viewport
without stealing keyboard focus. Text fields retain their editing keys and
focus indicator. Small-window layout keeps actions visible and constrains
content above them. Reduced-motion preference snaps menu transitions and
selection effects.

F1 remains the display, performance, topology, and family/ruleset selection
menu. The selected family filters the rule list, so the UI cannot create an
unbound combination. New Simulation follows the same pair selection contract.

## Verification record

Coverage exercises separated registry definitions, legacy catalog
normalization, family/rule validation, catalog overlays and portable packages,
F1/F2 pair selection, family-owned appearance editing, F2
draft/preview/navigation/Apply behavior, state validation, and
v4/v3/v2/dense-save compatibility. The Release workspace build and separate
`IllumoWorkspace` CTest run both passed all 402 tests; `IllumoTidy` passed over
140 first-party translation units. The architecture PDF rebuilt. The native
Windows GUI was not manually smoke-tested. Full migration decisions and
compatibility details are in the
[family-separation design](ruleset-family-separation-design.md).
