# IllumoGame domain

The live game path is a sparse cellular-automata world with configurable
infinite or finite toroidal topology plus a bounded presentation view.

## Rule catalogs

`RuleSetRegistry` starts empty and compiles validated catalog text into
data-backed `DataRuleSet` instances. `IllumoGame/families.json` defines family
identity, model, state count, labels, and colors. `IllumoGame/rulesets.json`
defines each rule's stable identity, required `family_id`, and transition data.
The current rules schema is version 3; it supports Life-like B/S, Generations,
explicit Moore tables, and Wolfram elementary 1D. The reader retains
unversioned and schema-v1/v2 rule-catalog compatibility. Stable built-in IDs
remain unchanged for saved worlds.

`RuleCatalogLoader` owns file reads and selects the first valid base catalog
pair beside the executable, in the working directory, or in its `IllumoGame`
subdirectory. It then layers the working-directory `families.user.json` and
`rulesets.user.json` files as one validated pair, where matching IDs replace
shipped entries. Invalid or unreadable base pairs fall through, and malformed
overlays leave the published catalog unchanged.
The required-module factory loads the catalog once before constructing the menu
or direct game module. Rulesets does not call platform APIs or discover files.

F2 opens the separate in-game Ruleset Workshop; F1 remains the display and
simulation settings menu. Both menus carry a family/ruleset pair; the rule
selector is filtered to the chosen family, and each ruleset remains bound to
exactly one family. F2 family edits own the cell-state schema and palette while
rule edits own transition parameters. Changing Family in the staged editor
preserves the custom rule ID and display name while loading starter parameters
for that family. Life-like and Generations rules show
B/S count chips, Generations adds its state count, elementary rules show their
Wolfram number, and Moore tables explain that transitions are edited in JSON.
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
and consumes wheel input without changing selection or values. Raised cards,
eased focus lighting, value pulses, and a decorative cell colony match the
main menu; reduced motion freezes the colony and snaps focus feedback.

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
- Rulesets supply pure `nextState` and `evalCell` behavior. Data-defined rules
  compile to the same transition interface. Each ruleset's
  complete 256x9 transition table is cached once and shared by all serial and
  worker hot loops. Life-like and Generations definitions compile from neighbor
  masks. Rule 90 and
  Rule 184 are elementary 1D space-time rules: the source row is the maximum
  counted Y, the destination is Y+1, and older rows remain history (D-G2).
  Dense `calcGeneration` remains Moore/compatibility only.
  `CellGrid`/`Canvas` remain compatibility coverage.

## CanvasView (presentation)

- Separates the visible viewport from a globally aligned sampled cache padded
  by two 16-cell chunks on every side. Camera motion within the cache changes
  only the MVP. Aligned origin shifts copy retained CPU texels and resample
  only newly exposed strips. Near zoom uses one exact texel per cell; far zoom uses a stable
  integer density LOD bounded to roughly four screen pixels per texel. LOD
  coarsens immediately to fit and refines only when the next level fits within
  80% of the output budget.
- Owns one reusable RGB texture and one world-space, cell-aligned quad through
  `GameVisual`. Small capacity increases reserve 50% headroom so nearby zoom
  changes do not reallocate. Re-enrollment preserves the handle while the
  backend destroys its prior GL texture, PBOs, and fences; view destruction
  explicitly releases the texture.
- Uses nearest filtering so discrete cell colors stay sharp; the editor cursor
  uses the same centered cell bounds.
- CPU palette targets fade through `displayRgb` at exact-cell LOD; density
  overviews and newly revealed cells snap to their current color. A retained
  active-texel set makes each fade tick and
  zero-speed snap visit only colors still changing; repeated unchanged
  `setFadeSpeed(0)` calls are constant-time. Stable grid/camera/palette state
  skips resampling and texture upload. A one-revision grid change publishes
  current or removed chunks and resamples only affected cache texels at both
  exact and overview LODs unless those bins cover at least a quarter of the
  cache, in which case the complete bounded cache is resampled. Changed-bin
  marking stops once that quarter-cache threshold is reached. Revision gaps,
  non-aligned jumps, resize, palette changes, torus wrap, and whole-grid
  replacement fall back to a complete bounded refill. Dirty
  16x16-texel tiles merge into at most eight rectangles when that covers no
  more than half the enclosing AABB; otherwise one AABB is submitted.
- Overview sampling visits only sparse chunks intersecting the visible source
  region and accumulates occupied cells into density bins. Overview snaps
  convert the sampled RGB in one pass instead of per-texel fade enrollment.
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
changes, manual stepping, and shutdown drain first. Painting, Bresenham strokes, rectangular selection, copy/cut/paste, built-in
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
outside it remains blank even though generation neighbors wrap at its edges.

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
stages, cache refills, requested upload bytes, and upload rectangles.

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
brush. It starts closed. Click the centered pull tab to slide the drawer up;
click it again to slide it below the bottom edge. Swatches use active rule
metadata for every declared state and its actual catalog color; the drawer
scrolls through larger state sets. Right-click always erases. A ruleset change
resets the generic brush to state 0.
The panel uses shared rounded GuiKit surfaces and UiTheme colors, an eased
vertical slide and chevron, and animated hover/selection emphasis. Reduced UI
motion snaps transitions. Drawing and hit testing share the fitted UI scale. Palette
gestures capture input through mouse release, preventing paint-through and
camera zoom; settings, confirmation dialogs, and the console take precedence.
