# IllEd editing wave 2: format gaps and ergonomics (plan)

Tier 2 execution plan (`.agent/PLANS.md`). Started 2026-10-01 on
`release/v26.10`. The owner chose two feature groups from a survey of IllEd
against `.ilsc` format 2: **close the format gaps** and **editing
ergonomics**. Preview/play mode and format extensions (point/spot lights,
prefabs, materials) were not chosen and stay out of scope.

## 1. Objective and end state

Every value `.ilsc` format 2.0 stores for nodes, assets and the environment
that IllEd wave 1 could not author becomes editable in the inspector or the
Tools panel, and a set of everyday editing conveniences lands. Each item has
a headless `IllEd.*` test, the Release workspace build and the labelled
`IllumoWorkspace` CTest suite pass, and the canonical documents describe the
new behaviour.

## 2. Current-state evidence

- Inspector (`EditorInspector.cpp`): node name/flags/transform; primitive,
  mesh (asset as free text), sprite (texture as free text, size, facing,
  tint, flip), light, camera fields; opaque components read-only; Add
  component offers only Shape, Light and Camera. Scene view: mode, title,
  author, skybox (free text), ambient, sun.
- Not editable although stored: sprite region `u0..v1` and atlas cell
  (`hasCell`, column, row); node `tags`; `metadata.description`;
  `environment.skyboxTint`; the asset table's texture options, atlas grid,
  mesh import options and image asset type; editor `gridSpacing`,
  `gridVisible`, `snapTranslate`, `snapRotateDegrees`, `snapScale` (design
  §11.3 says they are "configured in the toolbar").
- Gizmo pivot is the primary node's origin only (`EditorScene::gizmoFrame`);
  design §11.3 promises "pivot at the primary node or the selection center".
- Hierarchy (`EditorSceneGraphView.cpp`): Ctrl or Shift click toggles; no
  range selection and no filter.
- Asset drags show nothing over the viewport until release
  (`docs/content-packages-and-scenes-plan.md` follow-up "IllEd drag preview").
- Frame Selection and Reset Camera centre on the whole window, so with the
  right column docked the target sits left of the visible centre
  (`docs/detachable-panels-plan.md` follow-up).
- `SceneInstance::setAssets` validates the whole document and rebuilds every
  attachment; `EditorHistory` settings patches already carry the asset table.

## 3. Scope

In scope:

1. **Inspector, node:** sprite source (region or atlas cell) with its
   fields; tags as one comma-separated text field; Mesh and Sprite in Add
   component; mesh asset and sprite texture as choices among compatible
   asset-table entries (the asset picker).
2. **Inspector, scene:** description; skybox as a choice among cubemap
   assets plus none; skybox tint; an Assets section listing every asset-table
   entry with its editable options (image type Texture/Atlas/Cubemap cross,
   filter, wrap, mipmaps, atlas columns and rows, mesh centre-and-normalize,
   target radius, flip V, generate normals) and Remove for unreferenced
   entries.
3. **Tools panel:** Grid (show toggle, spacing stepper) and Snap step
   steppers (move, rotate, scale) over preset ladders; a pivot toggle
   (primary node or selection centre); Align and Distribute buttons.
4. **Viewport:** selection-centre pivot for rotate and scale; a drop preview
   while an asset is dragged over the viewport; framing and reset centred on
   the dock's centre rectangle.
5. **Hierarchy:** Shift+click range selection (Ctrl+click toggles); a
   filter field (Ctrl+F) that shows matching nodes with their ancestors.
6. **Commands:** Align min/centre/max per axis and Distribute per axis as
   `EditorCommand`s in a new Arrange menu, each one undo step.

Non-goals: new component kinds or any `.ilsc` format change; editing opaque
component or extension JSON; creating assets from the inspector (assets
still arrive by dropping or pasting); editing cubemap face paths (shown
read-only); fly-through or play mode; persisting the pivot choice or the
filter text.

## 4. Constraints and invariants

- `EditorDocument` stays the only mutation gateway; every edit is a history
  patch. Asset-table edits go through a new `EditorDocument::setAssets`
  recorded as a settings command; drags and scrubs share merge keys.
- No `.ilsc` change: `SceneDocument::kFormatMinor` stays 0, and the editor
  view state keeps its existing fields and ranges.
- Validation stays in Content: the inspector applies an edit and marks the
  field invalid (or the click does nothing) when `SceneInstance` rejects it;
  it never restates rules such as "a cell needs an Atlas asset".
- Primitive-composed UI in the plain tool look; panels draw into their
  placement and work docked or detached. Text entry in the hierarchy filter
  consumes keys before shortcuts, like the inspector.
- `EditorShortcuts` stays the only key map (new: Ctrl+F filter, P pivot).
- Main-thread affine; iterative hierarchy walks; no exceptions; no `auto`.

## 5. Design and alternatives

- **Grid and snap settings:** steppers (`<` value `>`) over preset ladders,
  issuing commands, keep the Tools panel command-only. A typed numeric field
  there was rejected: it would duplicate the inspector's text-edit and focus
  machinery in a second panel. Values outside a ladder step to the nearest
  ladder value in the chosen direction. Editor view state never dirties the
  document.
- **Asset picker:** a Choice field cycling through compatible asset ids is
  the smallest primitive-composed picker; Add Mesh/Sprite is offered only
  when a compatible asset exists and starts on the first one.
- **Asset editor in the scene inspector** rather than inline under each
  component: one asset may serve many nodes, so its options belong to the
  scene, and one list also covers assets no node uses yet.
- **Pivot:** the selection-centre pivot is the centre of the union of the
  top-level nodes' world bounds (origins when a node has none). Local axes
  still follow the primary node. Translate is unaffected.
- **Range select:** rows between the anchor (the last row clicked without
  Shift) and the clicked row, in visible tree order; the clicked row becomes
  primary.
- **Filter:** case-insensitive substring of name or id; ancestors of matches
  stay as dimmed context rows; folding is ignored while filtering.
- **Framing:** offset the camera target so the framed point lands on the
  centre rectangle's centre. 2D: exact (pixels per unit is the zoom). 3D:
  the target moves along the camera's right and up vectors by the pixel
  offset times world units per pixel at the target distance (exact at the
  target point, which is what framing places). An off-centre projection
  was rejected: it would change `Camera` for every program.
- **Align/Distribute:** align each top-level node's world bounds to the
  selection bounds' min, centre or max on one axis; distribute moves bounds
  centres to equal spacing between the two outermost (three or more nodes).
  Both move nodes by world deltas through the existing `translate` path as
  one command.
- **Drop preview:** a ghost on the edit plane under the cursor (wire box for
  a mesh, outline square for a texture) while the Assets panel drags a
  placeable file over the viewport centre, docked or detached.

## 6. Contracts and compatibility

No public engine API, ABI, frame schema or file format change. New
`EditorCommand` values, `EditorDocument::setAssets` and
`EditorDocument::alignNodes`/`distributeNodes`, inspector field keys, and
`EditorAssetBrowser::dragPoint` are IllEd-internal. Older scenes load
unchanged.

## 7. Milestones

| # | Milestone | Status |
|---|---|---|
| W1 | Inspector node gaps: sprite source, tags, asset choices, Add Mesh/Sprite | done |
| W2 | Inspector scene gaps: description, skybox choice and tint, Assets section | done |
| W3 | Tools panel: grid and snap steppers, pivot toggle; selection-centre pivot | done |
| W4 | Arrange: align and distribute commands, menu and Tools buttons | done |
| W5 | Hierarchy: range select and filter (Ctrl+F) | done |
| W6 | Viewport: drop preview and framing on the centre rectangle | done |
| W7 | Docs sync, full Release build and labelled CTest | done |

## 8. Verification strategy

Each milestone adds exact `IllEd.<area>.<case>` tests (registered in its
area's file) and runs them with `IllEdTests.exe --run`. W7 runs `python
build.py build` (Release workspace with tidy) and `ctest -L IllumoWorkspace`
for the Release workspace, plus clang-format on every touched file. A live
GUI smoke of the new panels is reported separately when it was or was not
run.

## 9. Risks and containment

- `setAssets` rebuilds every attachment; an asset edit on a large scene is a
  full rebuild. Acceptable for an explicit click; scrubbing a mesh radius
  merges into one command but rebuilds per frame — the radius field edits
  by typing only (no scrub) to avoid that.
- Changing an image asset to a cubemap cross is rejected by validation while
  a sprite uses it; the choice simply does not advance.
- Each milestone is self-contained; reverting one leaves the others working.

## 10. Open questions

None blocking. Ladder values and the P/Ctrl+F keys are editor-local choices
that the owner can change cheaply.

## 11. Validation log

2026-10-01, all milestones on `release/v26.10`:

- New cases: `IllEd.Inspector.SpriteSourceTagsAndAssets`,
  `IllEd.Inspector.SceneAssetsSection`, `IllEd.Document.AlignAndDistribute`,
  `IllEd.Tools.SteppersAndArrange`, `IllEd.Module.SettingStepsPivotAndArrange`,
  `IllEd.SceneGraphView.RangeSelectAndFilter`,
  `IllEd.Module.FindFocusesHierarchyFilter`, `IllEd.Module.DropPreview`;
  `IllEd.Gizmo.FrameSelectionFitsBounds` now also checks Reset Camera and 3D
  framing against the dock's centre rectangle.
- Changed expectations (behaviour change, not regressions):
  `IllEd.SceneGraphView.Hierarchy` and `.Placement` (rows start below the
  filter field); `IllEd.Module.NewDocumentCommand` (Reset Camera centres the
  origin in the viewport instead of zeroing the camera position).
- `python build.py build` (Release workspace, in-build `IllumoRunTests`):
  716/716. `ctest --test-dir build-workspace -C Release -L IllumoWorkspace`:
  716/716, including 92 `IllEd.*` cases and `IllEd.Wasm.Package` /
  `IllEd.Wasm.ProjectPackage` against the rebuilt `IllEd.wasm`.
- `clang-tidy -p build-workspace-tidy` on the 15 changed IllEd translation
  units: clean. `python build.py tidy` fails on pre-existing clang-tidy 22
  `bugprone-*` findings in untouched Vulkan, D3D12, `SimulationLanes.cpp`
  and two Illumo tests; none in IllEd. Recorded, not fixed (out of scope).
- `git clang-format --diff HEAD`: clean on changed lines; whole-file churn in
  older files was reverted.
- The first build attempts hit transient file locks (`C1083` / `LNK1104` on
  `IllEdTests` outputs, no process holding them); a rerun succeeded.

Deviations: none in scope. Panel item drawing was not checked in a live
window (headless tests only).

Follow-ups: dropping an asset-browser file onto the inspector's asset
choices (assets still enter the table by viewport drop or paste); editing
cubemap face paths; framing on the centre rectangle in IllMeshViewer; a
live GUI smoke of the new Tools rows and the hierarchy filter.
