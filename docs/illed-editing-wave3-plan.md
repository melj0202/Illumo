# IllEd editing wave 3: interaction, tweaking, color and grouping (plan)

Status: in progress. The owner asked on 2026-10-01 for "more ways to edit,
tweak and interact" and chose all four offered groups: viewport interaction,
inspector tweaking, color picking, and hierarchy and grouping.

## 1. End state

- **Viewport:** the node under the cursor is outlined (hover) and named in
  the status bar; a right-click that does not drag opens a context menu on
  the node under it (or the empty scene); Alt held when a move starts
  duplicates the selection and moves the copies; Drop to Floor (End) lowers
  each selected subtree onto the surface below it or the ground plane;
  Alt+arrows (and Alt+PageUp/PageDown in 3D) nudge the selection by the move
  snap step.
- **Inspector:** Ctrl+wheel over a number steps it (Shift finer); Shift drag
  scrubs ten times finer and Ctrl drag ten times coarser; number fields
  accept arithmetic (`2*3`, `(1+2)/4`) and relative edits (`+=1`, `-=2`,
  `*=2`, `/=2`); right-clicking a field or section opens a menu: Reset row,
  Reset component, Copy and Paste component values, Remove; section headers
  fold and unfold.
- **Color:** every color row starts with a swatch; clicking it opens a picker
  (saturation/value square, hue strip, alpha strip, hex field) that edits the
  row's values live as one merged command.
- **Hierarchy:** Group (Ctrl+G) puts the selection's top-level nodes under a
  new empty parent at their centre; Ungroup (Ctrl+Shift+G) lifts children out
  and removes the empty group; Select Parent and Select Children; Lock
  (Ctrl+L) makes a node and its subtree unpickable in the viewport (a lock
  icon in the hierarchy, saved with the scene); Isolate (Shift+H) hides
  everything except the selection's subtrees and their ancestors for viewing
  only.

## 2. Constraints

- `EditorDocument` stays the only mutation gateway; each user action is one
  history command (picker drags and nudges merge per gesture).
- No `.ilsc` format change. Locks persist in the IllEd-owned scene extension
  `illed.view` (`{"locked": [ids]}`); isolation is view state and never saved.
- Content gains two editor hooks with no format effect:
  `SceneInstance::setViewHidden` (nodes draw and pick as hidden without their
  document `visible` changing) and a skip set for `pickRay`.
- Plain tool look, primitive-composed drawing, `EditorShortcuts` the only key
  map, no recursion (the expression parser is a shunting-yard loop), no
  exceptions, no `auto`.

## 3. Milestones

| # | Milestone | Status |
|---|---|---|
| X1 | Content hooks: view-hidden nodes, pick skip set, extension string lists; tests | done |
| X2 | Hierarchy and grouping: group/ungroup, select parent/children, lock, isolate | done |
| X3 | Viewport: hover, context menu, Alt-drag duplicate, Drop to Floor, nudge | done |
| X4 | Inspector: wheel and modifier steps, expressions, field menu, folding | |
| X5 | Color swatches and picker | |
| X6 | Docs, full Release build and labelled CTest | |

## 4. Validation log

- X1-X3: `Illumo.Content.InstancePicking` (skip set, view hiding),
  `Illumo.Content.SceneExtensionList`, `IllEd.Module.GroupLockIsolate` and
  `IllEd.Module.ViewportInteraction` (real pointer input: hover, the right-click
  menu, Alt-drag copies, the empty-space menu's Create, Drop to Floor, nudges);
  736 of 736 workspace tests.
