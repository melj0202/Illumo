# IllumoGame domain

The live game path is a sparse cellular-automata world with configurable
infinite or finite toroidal topology plus a bounded presentation view.

## Rule catalogs

`RuleSetRegistry` starts empty and compiles validated catalog text into
data-backed `DataRuleSet` instances. `IllumoGame/families.json` defines family
identity, model, state count, labels, and colors. `IllumoGame/rulesets.json`
defines each rule's stable identity, required `family_id`, and transition data.
The current rules schema is version 3; it supports Life-like B/S, Generations,
explicit Moore tables, cyclic interaction, colorized Life, Larger-than-Life,
Hodgepodge chemistry, directional Turmites, HPP lattice gas, five-species
dominance, Golly von Neumann rule tables, Abelian sandpiles, quantized Lenia,
and Wolfram elementary 1D. Rules can also select a deterministic starter
strategy and soup radius/density, an exact Golly-RLE starter, or a Lenia-RLE
starter. The reader retains
unversioned and schema-v1/v2 rule-catalog compatibility. Stable built-in IDs
remain unchanged for saved worlds.

The shipped catalog also demonstrates the family/rule split beyond the original
set. `GENERATIONS_4_PHASE` supplies a neon four-state decay palette shared by
Star Wars (B2/S345/C4) and Nova Trails (B23/S34/C4). The five-state
`EXCITABLE_MEDIA_5_PHASE` family supplies an excited/resting/refractory schema
for two Greenberg-Hastings-style Moore tables: threshold one launches broad
wave fronts, while threshold two requires two excited neighbors and favors
colliding waves. State `0` remains the counted excited state and state `1` the
quiescent background, so both families use the existing sparse simulation path.

`PRISMATIC_ECOLOGY_12` and `ELEMENTAL_COURT_9` make every declared state an
actor. In a cyclic rule, a cell advances by the rule's cycle step only when its
successor state reaches the configured threshold among the eight neighbors.
Steps must be coprime with the family state count, so one cycle visits every
kind. Prism Rush, Chromatic Storm, Crystal Domains, Elemental Surge, and Aurora
Conflict combine thresholds one through three with short and long successor
jumps. `CLASSIC_CCA_14_T1` implements the one-step 14-color experiment from
Fisch, Gravner, and Griffeath's 1991 paper and starts from a deterministic mixed
phase soup, matching the paper's random-initial-condition regime.

`IMMIGRATION_LIFE_2_SPECIES` and `QUADLIFE_4_SPECIES` add colorized Life. All
non-background states count as live under B3/S23. Surviving cells retain their
species; births inherit the majority parent species. QuadLife's three-distinct-
parent case produces the missing fourth species. The rule was described in the
January 1979 BYTE article, and both dense compatibility and sparse production
evaluate the full Moore state histogram.

`LARGER_THAN_LIFE_BINARY` adds range 1..16 square or circular neighborhoods,
optional center counting, and inclusive birth/survival intervals. The shipped
Bosco/Bugs, Bugsmovie, and Globe parameters follow Golly's official
Larger-than-Life documentation. These rules use an isolated sparse evaluator
that expands occupied source chunks by the required chunk radius and evaluates
target chunks on the worker pool (D-GC10); the optimized radius-one
candidate/halo paths remain unchanged.

The Generations catalog also includes the documented Banners and Transers
five-state rules and Fireworks with twenty-one visible phases. Model-appropriate
starters replace the old one-glider-for-most-rules heuristic: Generations and
Larger-than-Life use active soups, cyclic rules use phase soups, colorized Life
uses all live species, and excitable tables use mixed activation breaks. Seeds
are coordinate-hashed and therefore reproducible.

Research references: [Golly Generations](https://golly.sourceforge.io/Help/Algorithms/Generations.html),
[Golly Larger than Life](https://golly.sourceforge.io/Help/Algorithms/Larger_than_Life.html),
[Cyclic Cellular Automata in Two Dimensions](https://www.math.ucdavis.edu/~gravner/papers/cca.pdf),
and [BYTE, January 1979](https://www.worldradiohistory.com/Archive-Byte/70s/Byte-1979-01.pdf).

The second researched expansion adds four distinct interaction contracts.
`HODGEPODGE_101_LEVEL` implements healthy, infected, and ill transitions over
one hundred visible infection levels; two rules vary the infection increment.
Three Turmite families implement the researched RL, RRL, and RRLL turn words
with two, three, and four tape colors. Each tape color has four visible agent
directions. `HPP_LATTICE_GAS_16` treats each state as a combination of four
directional particles, rotates head-on pairs, and streams them without changing
particle count. `RPSLS_5_SPECIES` gives every species two prey and two predators;
threshold-one invasion creates fast fronts and threshold two creates reinforced
domains.

Turmites and HPP use a direction-preserving north/east/south/west von Neumann
contract in both dense compatibility and the serial sparse evaluator.
Hodgepodge and dominance reuse the complete Moore state histogram. All four
paths retain state 1 as sparse background and publish new chunks
transactionally. Deterministic Turmite swarms, particle clouds, chemical phase
soups, and five-species soups make the shipped rules active immediately.

Further research references: [Dewdney's Hodgepodge Machine description](https://people.sc.fsu.edu/~jburkardt/classes/ysp_2003/dewdney_hodgepodge.pdf),
[Langton's ant and Turmites](https://www.ci2ma.udec.cl/pdf/pre-publicaciones2/2016/pp16-19.pdf),
[Hardy--Pomeau--de Pazzis lattice gas](https://doi.org/10.1103/PhysRevA.13.1949),
[Cellular Automata Machines](https://people.csail.mit.edu/nhm/cam-book.pdf), and
[five-species cyclic dominance](https://arxiv.org/abs/1308.0964).

The third expansion (D-GC8) adds self-replication, sandpiles, long-range
cycles, and trails. `SELF_REPLICATING_LOOPS_8`, `BYL_LOOP_6`, and
`SAYAMA_LOOPS_9` are `von_neumann_table` families: Langton's Loops, Byl's
Loop, Chou-Reggia Loops I and II, SDSR Loops, and Evoloop store their Golly
rule tables verbatim and start from their published loops.
`SANDPILE_PULSE_2` topples four-grain cells to their von Neumann neighbors and
feeds a source that drops four grains every other generation; Mandala, Binary
Star, and Critical Avalanche starters grow the sandpile fractal. Griffeath's
313, Lava Lamp, Stripes, Squarish Spirals, Cyclic Spirals, and Turbulent Phase
use cyclic rules with range 1..3 and square or diamond neighborhoods; their
`inert_background` keeps a void that is not a phase, so each soup stays in its
dish instead of invading the infinite canvas. The
`LTL_TRAILS_7` and `LTL_TRAILS_8` families add Golly's decay trail to Larger
than Life: Comet Rockets starts four light-speed spaceships, and Rainbow Tides,
Neon Coral, and Bubble Swarm grow from soups. Forty-five more classic
Life-like and Generations rules (Maze, Coral, Anneal, Vote, Diamoeba,
Replicator, Frogs, Brian 6, Lava, Swirl, BelZhab, Bombers, Xtasy, Thrill
Grill, and others) complete the catalog with new neon Generations palettes.

Third-expansion references: [Golly's RuleTable format and rule files](https://golly.sourceforge.io/Help/formats.html#table),
C. G. Langton, *Self-reproduction in cellular automata*, Physica D 10 (1984);
J. Byl, Physica D 34 (1989); Reggia et al., Science 259 (1993); H. Sayama's
SDSR (1998) and Evoloop (1999) papers; Bak--Tang--Wiesenfeld,
[Self-organized criticality](https://doi.org/10.1103/PhysRevLett.59.381); and
Fisch--Gravner--Griffeath's cyclic automata paper above.

`LENIA_256_LEVEL` (D-GC9) adds Bert Chan's continuous Lenia as 256 intensity
levels: state 1 is empty, states 2..255 are levels 1..254, and state 0 is full,
drawn on a navy-to-cyan-to-amber ramp. `lenia` rules carry radius (1..16),
`time_steps`, `mu`, `sigma`, ring `peaks`, and `kernel_core`/`growth` shapes
(`polynomial`, `exponential`, `step`); they compile to integer kernel taps and a
fixed-point growth table and run on the chunk-parallel `WeightedKernel` sparse
path.
Orbium, Orbium bicaudatus, Gyrorbium, Scutium, Discutium, and Paraptera start
from their `lenia_rle` species cells; the paint brush's first state paints full
intensity, and custom Lenia rules default to a random-intensity soup. The F2
workshop shows the compiled `LENIA/...` contract and previews the next level at
a chosen potential in eighths. Parameters and cells are from Chan's
MIT-licensed [Lenia repository](https://github.com/Chakazul/Lenia)
(`Python/animals.json`); see also [Lenia](https://en.wikipedia.org/wiki/Lenia)
and B. W.-C. Chan, *Lenia: Biology of Artificial Life*, Complex Systems 28
(2019).
`RuleCatalogLoader` owns file reads and selects the first valid base catalog
pair beside the executable, in the working directory, or in its `IllumoGame`
subdirectory. It then layers the working-directory `families.user.json` and
`rulesets.user.json` files as one validated pair, where matching IDs replace
shipped entries. Invalid or unreadable base pairs fall through, and malformed
overlays leave the published catalog unchanged.
In the WASM package, `CSimCatalogBootstrap` also merges content packages:
after the packaged pair and before the storage overlays it lists `/packages`,
then reads every `/packages/<id>/csim/*.json` (packages in id order, files in
name order, at most 64). Files named `families*.json` add families; any other
name is a rule package. Each merges into a candidate copy, so a package
catalog that does not validate is skipped with a logged warning instead of
stopping the game. `IllumoGame.Wasm.CatalogMerge` covers the order and the
skip.
The required-module factory loads the catalog once before constructing the menu
or direct game module. Rulesets does not call platform APIs or discover files.

F2 opens the separate in-game Ruleset Workshop; F1 remains the display and
simulation settings menu. Both menus carry a family/ruleset pair; the rule
selector is filtered to the chosen family, and each ruleset remains bound to
exactly one family. F2 family edits own the cell-state schema and palette while
rule edits own transition parameters. Changing Family in the staged editor
preserves the custom rule ID and display name while loading starter parameters
for that family. Life-like, Generations, and colorized-Life rules show
B/S count chips, Generations adds its state count, elementary rules show their
Wolfram number, cyclic rules expose successor threshold and cycle step, and
Moore tables explain that transitions are edited in JSON. Larger-than-Life
shows its canonical range/threshold summary and uses JSON import for parameter
editing. Hodgepodge, Turmite, lattice-gas, dominance, rule-table, sandpile, and
Lenia definitions show their compiled interaction contract and retain their
parameters through JSON import/export.
Rule settings and a configurable transition example come first; state labels
and colors plus JSON import/export are lower sections in the same scrollable
page. Save & Apply and Discard stay in a pinned action area below the scrolling
content. Values show their step controls, and B/S counts can be toggled directly
with the mouse or by focusing a count and pressing Enter.
Save & Apply validates the draft, drains the simulation, rejects family schema
changes that would invalidate live cells, writes family data before the
referencing rule, then activates the pair. Import also rejects a replacement for
the active family if its smaller state range would invalidate the active rule,
even when current cells do not use the removed states. Moore tables can be
authored in JSON and imported. The
workshop uses the F1 menu's eased reveal, focus glide, theme, and reduced-motion
preference. Wheel scrolling changes the visible row window while keyboard focus
stays put. Pointer hover moves focus; arrows/WASD and Tab/Shift+Tab navigate
editable controls and pinned actions, Page Up/Down move by a page, and Home/End
jump to the first control or last action. Left/right adjust focused values;
Enter toggles focused B/S counts or activates buttons. W/S and Space remain
available for text entry while a name or state-label field is focused.

New simulation and the menu console command `play` open a dedicated canvas
setup screen, independent of F1 configuration. It offers the rules catalog,
infinite or wrapping boundaries, width and height in 16-cell increments, and
empty or starter contents. Infinite mode disables dimensions while retaining
the finite draft. Create passes validated canvas values to CellGameModule;
Back or Escape discards the draft. Display and performance preferences are not
part of this payload. The screen fits all rows, respects reduced menu motion,
and consumes wheel input without changing selection or values. Glass cards,
spring-driven focus lighting, the liquid selection drop, springy directional
value nudges (the value and the arrow on its side spring toward the change and
bounce back), a spring crossfade on the boundary
badge, and a live glider walking a 6x6 torus (`CellMotif`) match the main
menu; reduced motion freezes the motif and snaps focus feedback.

The title screen (`MainMenuModule`) runs Immigration Life behind its glass
panel at half zoom: a Gosper gun streams cyan gliders, coral acorns churn, and
a deterministic generator launches gliders and spaceships in from the edges
every 4.5 seconds (not under reduced motion). The world is reseeded past 1,400
chunks or after 15 minutes. Its `CellContext` restores the saved family,
ruleset and mode preferences it would otherwise overwrite, and the menu
refreshes the canvas targets after each advance. Above it, a radial vignette,
drifting lava-lamp glows with lazy pointer parallax, and a spotlight that
smears along the pointer's motion frame a glass panel that drops in on a
spring and swivels toward the pointer (layers shift by depth, the shadow
slides away and a glare follows the pointer; rows are hit where they are
drawn); its title letters fall in like drops and bounce; the 7x7 motif runs a
real glider whose newborn cells pop in as bouncing beads; rows drop into place
and lean in with jelly emphasis and animated icons; the selection is a liquid
drop that pours between rows, necks, tapers and sloshes back together, sweeps
a sheen, and wobbles on press while a splash of teardrop droplets leaps from
the press point; keycaps form the footer. The panel dims while settings or
canvas setup is open.

Type is Kikuta, a variable-weight face (`CSimTypeface`, D-UI9). Package
bootstrap installs weight 400 as the default font and a 400/600/800/1000 UI
weight ramp, and waits for the rest weight before the first menu. Row labels on
the title screen, settings, canvas setup and Ruleset Workshop, the workshop's
action buttons and the glass dialog buttons thicken with their emphasis springs
(`GuiKit::drawEmphasizedText`). The CSIM title draws from its own four-glyph
ramp (200..1000) and squashes and stretches each letter
(`TextPrimitive::stretchX`/`stretchY`): letters fall thin and tall and land
heavy and squat, a weight swell rolls through the word with the bob, and
letters near the pointer pool heavier. After the entrance, one letter at a time
strikes a random pose every 0.55--1.55 s (flex, slim, hop, or now and then a
wave of hops rippling out from it; `TitleLetterPose`
springs, the deterministic menu generator picks the letter and pose) and
jostles its neighbours; clicking the word sends a staggered hop through it.
Poses pause behind overlays and stop under reduced motion.
Characters Kikuta lacks come from Space Mono. The native test oracle installs
no typeface and keeps the engine default font.

## SparseCellGrid (simulation domain)

- Authoritative signed 64-bit cell coordinates.
- Hash-map storage of non-background 16x16 chunks. Infinite mode has no fixed
  chunk count or allocator-pool cap; finite mode canonicalizes cells into a
  configured chunk rectangle and wraps both axes.
- Negative coordinates use centralized floor division/modulo. All byte states
  are preserved, including Brian's Brain and Wireworld values.
- Each chunk maintains compact masks for stored non-background cells and cells
  that contribute to neighbor counts, plus cached counts for both masks. The
  authoritative and retained inactive maps maintain aggregate stored-cell,
  counted-cell, and candidate-preferred-chunk totals transactionally across
  edits, assignment, clear, local frontier patches, and complete map swaps.
  Settled stepping and complete-path selection therefore do not scan all
  allocated chunks. Sparse
  generations evaluate only every non-background cell plus the eight-neighbor
  birth candidates of counted cells. Exact affected chunk addresses are created
  directly through a retained generation-stamped open-addressed index; its slots
  point into contiguous scratch records carrying a 256-bit candidate mask and
  256 neighbor counts. Edge and corner participation is derived directly from
  counting-mask words rather than by rescanning counted cells, and each
  neighbor counter is initialized only when its candidate bit is first set.
  Index and scratch capacity survive generation resets,
  avoiding per-world-cell hash nodes, sorting, binary searches, and repeated
  candidate allocation. Serial preparation remains source-centric for cache
  locality. Large or explicitly parallel preparation is target-centric:
  discovery stores direct 3x3 source references in the not-yet-used neighbor
  count bytes, and workers copy those references locally before count
  initialization reuses the storage. Retained coarse ranges, capped at 256
  targets and sized to about eight ranges per worker, eliminate repeated map
  lookups and per-target atomic claims without shared writes. Flat-index lookup
  checks for an existing target before testing capacity, so duplicate enrollment
  never performs a growth calculation. Candidate sets
  with at least 16,384 cells are divided
  into retained ranges of roughly 2,048 candidate cells and evaluated through
  the reusable pool with up to four automatic workers. Each range writes
  independent result slots; small sets retain the direct serial path. Complete
  mixed worlds choose candidates or a deterministic 18x18 halo independently
  for each target. Counting-dense centers skip neighbor-count scratch
  preparation and go straight to halo evaluation; dense-majority frontiers
  skip scratch construction. Frontiers with at least 2,048 targets also skip
  scratch and evaluate as halo, because candidate fan-in exceeds halo
  evaluation. Only counting-sparse centers pay source-centric
  candidate fan-in. This lets dense
  Wireworld conductors use candidates because only heads contribute neighbor
  counts. Worlds whose source chunks are all densely counted bypass candidate
  scratch construction. At 32 or more halo targets, a grid-owned reusable pool
  uses up to eight workers. Complete-halo target addresses use another retained
  generation-stamped flat index, and their target/result vectors retain their
  high-water capacity instead of rebuilding a hash set, sorting, and allocating
  disposable buffers. Dense evaluation extracts 18-bit counted rows directly
  from the nine chunks' counting masks and reduces them through a rolling
  three-row stencil; it does not materialize an 18x18 byte halo or rescan cell
  states for counts. Empty results are not stored.
- Both paths write into a retained inactive chunk map. Old inactive nodes are
  extracted into a retained handle vector after aggregate statistics reset once.
  Sparse candidate results acquire and rekey a node before constructing cells
  directly in its mapped storage; dense halo results retain their complete-array
  bulk copy. Direct dual-grid generations instead update the spare grid's
  authoritative nodes in place. Each grid retains an exact topology epoch,
  candidate target/source references, and index-aligned output pointers; a
  returning source grid with unchanged chunk presence and counted edge/corner
  participation skips target discovery and output-map lookup. Topology changes
  invalidate the reuse before any retained pointer is read. Target-sized bucket
  headroom is retained because exact output reservation regressed measured
  insertion cost. Transactional map comparison/swap and zero steady-state
  allocation remain intact for non-direct generations.
- The inactive map also remains the prior-generation baseline. Retained flat
  address sets track exact state-change and counting-change masks per chunk.
  Each changed chunk enrolls itself; only counting changes on a shared edge or
  corner enroll the corresponding neighbor. Up to 16,384 changed addresses are
  retained for tracking; evaluation still uses the frontier versus complete work
  comparison. One-revision journals of at least 2,048 presentation chunks
  capture a lightweight replacement marker instead of per-chunk payloads.
  Sparse local sources
  build candidate masks only for frontier targets and choose candidate or halo
  evaluation independently. Exact local target/source bookkeeping, candidate,
  neighbor-contribution, and evaluation work is compared with a complete-path
  estimate derived from cached population totals. Cheaper frontiers patch the
  retained map; broad changes fall back to complete candidates or halos. Empty
  frontiers return immediately. Editing and ruleset-type changes repopulate or
  invalidate the frontier explicitly.
- Halo evaluation has a bounded on-demand memo keyed by the exact 18x18 cell
  state. Main-thread and worker shards require no locks; hashes select a
  four-way set and full keys prove hits. Adaptive sampling activates only for
  repeated neighborhoods, cools down after low hit rates, and is bypassed for
  candidate-only or small halo workloads. Ruleset transition changes clear it.
- Its revision changes only when a generation or edit changes the stored cell
  contents, allowing dependent views to skip idle resampling.
- Rulesets supply pure `nextState`, `nextStateFromNeighborhood`,
  `getExtendedCountedState`, `nextStateFromExtendedCount`, and `evalCell`
  behavior. Data-defined rules compile to the same transition interface. Each ruleset's
  complete 256x9 transition table is cached once and shared by all serial and
  worker hot loops. Life-like and Generations definitions compile from neighbor
  masks. Rule 90 and
  Rule 184 are elementary 1D space-time rules: the source row is the maximum
  counted Y, the destination is Y+1, and older rows remain history (D-G2).
  Cyclic rules request a full 256-state Moore histogram. Their correctness-first
  sparse evaluator expands each occupied source chunk by one, reuses the
  transactional output and change-journal machinery, and leaves optimized
  count-table kernels unchanged. Dense `calcGeneration` retains a compatible
  histogram path for tests and legacy consumers.
  Larger-than-Life rules use a separate extended-range evaluator. It
  builds the exact affected chunk band, counts square or circular neighborhoods
  through authoritative lookup (including torus wrapping), and reuses the
  transactional result/change-journal machinery. Range is capped at 16 and B0
  is rejected so infinite sparse backgrounds remain quiescent.
  Histogram, directional, extended-range and Lenia targets, and elementary 1D
  rows, evaluate on the grid worker pool with per-worker halo windows and serial
  in-order publication (D-GC10); `IllumoGame.Rules.WorkerPoolParity` checks every
  shipped rule against one worker.
  `CellGrid`/`Canvas` remain compatibility coverage.

## CanvasView (presentation)

- Separates the visible viewport from a globally aligned sampled cache padded
  by two 16-cell chunks on every side. Camera motion within the cache changes
  only the MVP. Aligned origin shifts copy retained CPU texels and resample
  only newly exposed strips. Far-zoom strips initialize background bins and
  accumulate occupied cells from intersecting sparse chunks, avoiding work
  proportional to the empty world area behind each overview texel. Near zoom
  uses one exact texel per cell; far zoom uses a stable
  integer density LOD bounded to roughly four screen pixels per texel. LOD
  coarsens immediately to fit and refines only when the next level fits within
  80% of the output budget.
- Owns one reusable RGB texture and one world-space, cell-aligned quad through
  `GameVisual`. Small capacity increases reserve 50% headroom so nearby zoom
  changes do not reallocate. Re-enrollment preserves the handle while the
  backend destroys its prior GL texture, PBOs, and fences; view destruction
  explicitly releases the texture.
- Uses nearest filtering so discrete cell colors stay sharp; the editor cursor
  uses the same centered cell bounds. It hugs the hovered LED key with a soft
  glow, a thin rounded rim, breathing viewfinder corner brackets and a small
  centre cross, and glides between cells on a `GuiMotion::kTrack` spring
  (stretching along its travel, landing with a small bounce while its
  brackets pop out); a cursor that was hidden snaps into place, and reduced
  motion snaps every move.
- Finite worlds add a screen-thickness-stable, theme-accented outline around
  the centered canonical rectangle so all four wrap edges remain visible while
  panning and zooming. Infinite worlds do not emit this boundary.
- CPU palette targets fade through `displayRgb` at exact-cell LOD; density
  overviews and newly revealed cells snap to their current color. A retained
  active-texel set makes each fade tick and
  zero-speed snap visit only colors still changing; repeated unchanged
  `setFadeSpeed(0)` calls are constant-time. Stable grid/camera/palette state
  skips resampling and texture upload. A one-revision grid change publishes
  current or removed chunks. Published generations map exact changed-cell
  masks to affected bins; overview bins reset together and recompute in one
  occupied-chunk traversal. Dense exact-cell bins covering at least a quarter
  of the cache select a complete bounded sample. Revision gaps,
  non-aligned jumps, resize, palette changes, torus wrap, and whole-grid
  replacement fall back to a complete bounded refill. Dirty
  16x16-texel tiles merge into at most eight rectangles when that covers no
  more than half the enclosing AABB; otherwise one AABB is submitted.
- Overview sampling visits only sparse chunks intersecting the visible source
  region and accumulates occupied cells into density bins. Incremental overview
  updates batch all marked bins through the same traversal, avoiding per-bin
  hash probes. Overview snaps convert the sampled RGB in one pass instead of
  per-texel fade enrollment.
  The visual texel budget does not limit stored chunks or world cells.
- `CellGameModule` dispatches the view on the World layer and the cursor,
  selection outline, inspector, splash, and configuration overlay on UI.

## CellGameModule

EDIT / NORMAL; simulation uses `tps` x `speedFactor` and keeps at most one
generation in flight on a persistent runner. The published grid is immutable
while the worker advances its mirror; completion and its changed-chunk delta
publish only at a frame boundary. Journals of at least 2,048 presentation
chunks capture a lightweight replacement marker instead of per-chunk
payloads. There is no backlog, and overdue whole steps
are dropped while fractional time is retained. Pause, edit, save/load, ruleset
changes, manual stepping, and shutdown drain first.

In the package (D-E17) the guest runner measures its serial generations and,
once they exceed 1 ms, runs generations on up to eight simulation lanes
(`IllumoGame/Source/Wasm/SimulationLanes.*`): isolated
`CSimWorkerGuest.wasm` stores that own interleaved bands of eight chunk rows
plus a one-row halo, advance them with the same kernels, and return their
owned changes. Elementary 1D rules, whose single active row spans columns,
partition by chunk column instead: the coordinator finds the global source
row (lane protocol version 2 carries it), each lane writes only the row
cells in its own columns, and the lanes' owned-column rows combine into the
next generation's source row. The control store merges them into the spare grid as one exact
delta (explicit changed chunks, never the replacement marker), publishes it as
above, and launches the next generation from the halos before merging. Lanes
resynchronize after any change to the published world, rule or topology.
Their generations finish on a later frame, so the runner reports
`canBlock()` false and a drain retires the outstanding generation: at most
one generation is discarded, and pause, save, load, edits and exit act on the
displayed world. Lanes stop for small or settled worlds (all lanes together
under 0.5 ms), and never run radii above 16 or after a lane failure. The
1 ms entry comes from `IllumoGame.Wasm.PackageBench`: at about 1 ms a lane
generation costs the store only its 0.3-0.4 ms merge, at the price of some
uncapped peak TPS. `status` reports the execution mode, round trip, slowest lane,
merge time, resynchronizations and retirements. Painting, Bresenham strokes, rectangular selection, copy/cut/paste, built-in
stamps, RLE/plaintext import, `setcell`,
randomization, and clearing operate directly on signed world coordinates.
The bottom edit-control legend defaults on and follows the persisted
`editHints` option in F1 settings. A flat, opaque menu-themed footer with muted
text reserves a bottom band. Canvas tokens scissor out the band and restore
scissor state before UI submission; picking, paint, paste, pan, and zoom ignore
that area. The camera mapping stays fixed when the band changes. Selection
actions appear only with a selection. It wraps key/action hints and includes
Wireworld brush keys. Selection ends on mouse release even when Shift is
released first. Painting, erasing, or leaving Edit clears the selection while
preserving the copied pattern; clipboard hotkeys act only in Edit mode.
Modal overlays and the console hide hints and selection outlines and stop
active strokes. Explicit console pattern commands remain mode-independent.
Pattern text is a clipboard/console side path and does not bump the sparse v3
save format (D-G1). `Ctrl+C`/`Ctrl+X`/`Ctrl+V` are editor clipboard keys; full clear remains
`clear_canvas`. An optional inspector HUD reports generation, hover address,
state, chunk, and census (`inspect` or `I`).
Startup patterns are centered around `(0, 0)`. Infinite mode is non-toroidal;
positive chunk width and height select a finite torus. `0 x 0` selects the
infinite canvas, while mixed zero/positive dimensions are rejected. Finite
presentation is clipped to the centered canonical rectangle; camera space
outside it remains blank even though generation neighbors wrap at its edges. A
contrasting accent outline marks those wrap edges without repeating the world.

F1 opens a primitive-composed settings overlay in both Release and Debug. It
edits family and its ruleset, world chunk dimensions, TPS, simulation speed, fade speed,
VSync, and fullscreen. Applying a topology change drains the worker and starts
a fresh centered world; other valid settings update the live runtime and the
persisted environment. The overlay uses larger high-contrast setting text,
human-readable ruleset names, split control help, and a selected-row
description. Q and the settings Exit action open a primitive-composed
confirmation overlay; confirming requests window closure through
`IRenderWindow` so the Illumo application runner can perform normal shutdown.
Frame-delta-driven scalar state provides an eased reveal, staggered rows, a
gliding selection highlight, and a short pulse after values change; input
remains live during every transition.

Sound cues (D-E28): `CSimSounds` plays the package's `Sounds/*.wav` through
`IllumoContext::audio` when the host granted `Audio`. The start cue plays with
the first screen; menus play hover when the selection or pointed button
changes, select when a value changes or an action succeeds, back on cancel
or discard, and error for rejected keystrokes, values at their limit,
disabled rows and reported errors; the canvas plays enter at the end of its
startup and exit when the return to the main menu begins. Each cue has a mix
level (hover is quietest) scaled by `soundVolume` (0-100, default 80), the F1
"Sound volume" row, which previews each step at the new level. The bank is
installed for the store's lifetime so menus without a context can fire cues;
without it cues are silent but counted for tests. The WAV sources in
`IllumoGame/Assets` are not tracked in git; a package staged without them is
silent.

That behavior is not restated per screen. `MainMenuModule`,
`ConfigurationMenu`, `NewSimulationMenu`, and `RulesetWorkshopMenu` share
`Illumo/Gui/GuiMenuShell`: `GuiEasing` curves and spring shapes,
`GuiSpring`/`GuiSpringArray` damped springs tuned from the `GuiMotion` presets
(row emphasis, toggle knobs, count chips, footer buttons, scroll thumbs,
parallax), `GuiMenuAnimator`
reveal/row-drop/liquid-selection/sheen/press-wobble/value-nudge/ambient/caret
clocks including `reducedUiMotion`, `GuiPanelTilt` (every glass panel swivels
toward the pointer; the layout origin carries the body shift so hit testing
follows the drawing), `GuiPanelLayout` virtual-resolution fitting plus visible-row
window and wheel scrolling, and `GuiPointerTracker` virtual-space pointer
sampling with hover and press edges. Each screen supplies only its rows, layout
constants, and `GameVisual` composition, so a new interface inherits the
existing theming and animation. The title screen keeps its own slower entrance
clock and per-item stagger.

CSim draws its own mouse pointer (`SoftwareCursor`, owned by the guest
`GameApplication` overlay so it survives module transitions) and hides the
system cursor through Display wire version 2. The arrow is built like the menu
glass: a thin dark halo, a lit rim that runs cyan at the tip to violet at the
tail, a deep glass body, and a soft glow around the whole edge. It leans against
sideways motion on a spring, leaves a faint holographic afterimage (ghost
arrows split into cyan and magenta-violet, shimmering) while moving, and
squishes with a splash on press. The persisted `softwareCursor` option
(Software cursor in F1 settings) defaults on; turning it off returns the system
cursor. The system cursor also returns while the host console is open or the
pointer leaves the window.
The inactive mirror does not retain a second copy of the outgoing delta.
Incremental catch-up uses its existing changed-address journal and skips prior
states replaced by an incoming record. Broad full replacements carry no chunk
snapshot: the spare grid advances directly from the immutable published grid
and updates its own retained nodes in place.

`status` reports output chunk nodes allocated, reused, and retained alongside
the simulation path, stored/counting cell counts, candidate-preferred chunk and
candidate/halo target counts, changed/frontier source and target counts,
exact changed/counting cell counts, frontier/complete work estimates, memo
hits/probes/entries/memory, candidate preparation/evaluation ranges and workers,
candidate enrollments/index growth/produced chunks, candidate discovery/
preparation/evaluation/change/recycle/output/merge stage timings, fading texels,
last sampled/faded texel work, and frame-step debt.
It also separates requested and achieved published TPS and reports rolling
256-sample p50/p95/max values for worker generations, mirror/advance/capture
stages, cache refills, cache scrolls, requested upload bytes, and upload
rectangles.

Save always writes version 4 sparse files containing the family and ruleset,
camera, topology, and deterministically sorted canonical chunks. Load validates
temporary state first, reads versions 4, 3, and 2 plus the prior dense format,
treats older formats as infinite, imports legacy cells around the world origin,
derives the family for pre-v4 saves, and restores the validated pair and camera.

Camera navigation reserves a `2^32`-cell margin from signed 64-bit endpoints:
each world-space axis must be finite and at most `16 * (2^63 - 2^32)` in
absolute value. Commands and sparse metadata reject unsupported positions
before changing live state; saves reject invalid camera metadata before
replacing the destination. Pan and cursor-centered zoom validate pending
targets. Checked cursor conversion rejects unsupported samples. A directly
supplied invalid camera leaves the view's previous safe cache intact;
`CanvasView::worldToCell` returns zero for invalid input, while editing uses
the checked conversion. The margin protects cache padding and alignment;
it does not restrict sparse storage or guarantee cell-level rendering precision
at extreme coordinates.

Clipboard capture/fill use bounded local offsets, so small selections at signed
coordinate endpoints remain valid. Elementary rules treat neighbors beyond the
representable X domain as background and omit unrepresentable children. A source
row at maximum signed Y fails to advance without changing cells or revision.

Pattern autodetection honors leading `!` plaintext comments and skips `#` RLE
comments. Unambiguous RLE syntax selects RLE; ambiguous multiline cell-only
text retains plaintext rows. The `rle` and `plaintext` commands select their
named parser explicitly. Ctrl+V clipboard paste rejects malformed text or text with
no occupied cells and leaves the world unchanged, without reusing a previous
internal pattern. Console `paste` and explicit pattern APIs use the retained
buffer, preserving the rotate/flip workflow.

RLE export uses `o` for state 0, `b` for background 1, and the project extension
`p{N}` for states 2..255. The parser accepts braced values 0..255 and preserves
legacy single-digit `pD`/`PD` tokens: `p23o` means state 2 followed by three live
cells. Braces separate state numbers from the next run count. Earlier exports
with unbraced multi-digit state numbers are ambiguous and require correction
to braced tokens; they cannot be recovered reliably by guessing. Binary RLE
syntax is unchanged. Sparse world-save encoding is unaffected.

Ruleset registration returns success/failure and rejects life-like B0 rules and
elementary rules with a birth from an empty neighborhood (odd rule numbers).
The current sparse representation requires a stable background for both infinite
and finite worlds. Catalog JSON loads atomically: all entries and the entire
JSON document must be valid before replacements become visible. Neighbor arrays
contain integers 0..8; elementary numbers and palette channels are bounded to
0..255. Life-like strings use labeled B/S or S/B, or legacy numeric S/B grammar.
Invalid direct transition neighbor counts above eight return background.

Manual stepping stops on the first failed generation and reports completed versus
requested counts. Async failure leaves the published grid intact, pauses in edit
mode and reports an explicit pending retry. `run` retries that work without
waiting for another time step; a successful manual step also satisfies it.
Failures do not automatically spin or count as completed generations.
Elementary evolution copies history into the retained inactive chunk map, stages
all destination writes there, and publishes once after success. Allocation or
evaluation failure preserves live cells, revision and the previous delta.
External-source advances clear their borrowed source on every exit and replace
stale spare state even when the source has no counted cells. Full-history staging
adds history-proportional work; the headless ElementaryHistoryBench reports that
cost without making a comparative speed claim.

Wireworld retains the sticky head/empty/tail/conductor brush (`1`/`H`, `2`,
`3`/`T`, `4`).

In Edit mode, the **Cell paint** drawer at the bottom selects the left-button
brush. It starts closed as a round bubble peeking over the footer, with an up
chevron and a paintbrush tipped in the current paint. Click the bubble and it
morphs into the drawer (it stretches wide, rises, and bounces into place);
click the drawer's header to morph it back into the bubble. Swatches use active rule
metadata for every declared state and its actual catalog color; the drawer
scrolls through larger state sets. Right-click always erases. A ruleset change
resets the generic brush to state 0.
The panel uses shared rounded GuiKit surfaces and UiTheme colors, a springy
bubble-to-drawer morph, and animated hover/selection emphasis. Reduced UI
motion snaps transitions. Drawing and hit testing share the fitted UI scale. Palette
gestures capture input through mouse release, preventing paint-through and
camera zoom; settings, confirmation dialogs, and the console take precedence.

### Dense compatibility boundary

RuleSet carries transitions and palette metadata only; it neither stores a
CellGrid pointer nor schedules dense workers. RuleSetRegistry takes a rule ID.
Tests/DenseRuleEvaluator borrows a grid and const rule, with an instance-local
worker override. CellGrid, Canvas, and legacy C++ rule implementations compile
only into IllumoGameTests. Elementary dense advancement preserves history, uses
the greatest active row and its X extent plus one, and wraps on both axes; it
preserves destination cells outside that extent, matching finite sparse behavior.
