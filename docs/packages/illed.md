# IllEd world editor

IllEd is the second in-tree Illumo application. It shares the host, renderer,
and `CreateIllumoApplication` seam with IllumoGame, but it does not simulate
cellular automata. It authors `.ilsc` format 2 scenes, the one scene format
every Illumo program loads (D-E19), so later Illumo applications can load the
same worlds.

Think of Illumo as the engine, IllEd as the editor that bootstraps products,
and IllumoGame as the first runtime product.

IllEd ships only as the `IllEd.wasm` package (`apps/illed/`, manifest
`IllEd/illumo.json`, `launchAccess: "edit"`) run by
`IllumoRuntime --app illed [--open scene.ilsc] [--project <dir>]`.
`IllEdCore` stays a native library for the `IllEdTests` oracle suite. The
guest requires the Render, Assets, Storage, SelectedFiles, Clipboard, Console
and Display capabilities, and enables its project commands only when the host
also grants `ProjectFiles` (offered with `--project`).

## Ownership

| Piece | Owner |
|---|---|
| Window, tokens, SceneGraph, dialogs, console host, `GuiTextEdit`, `GuiFileTree` | Illumo |
| `.ilsc` format 2 (`IlscCodec`, `SceneDocument`), `SceneInstance`, virtual file tree, `.ilpk` | Illumo::Content |
| Editor document, history, selection, shortcuts, gizmo, inspector, clipboard, asset browser, toolbar, module factory | IllEd |
| CA grid, rulesets, `.csim` | IllumoGame (untouched) |

`SceneGraph` remains a runtime hierarchy and never serializes itself.
`EditorDocument` wraps one `SceneInstance` (with pick proxies, so empty, light
and camera nodes are selectable) plus `EditorHistory`, and is the only
mutation gateway. `SceneInstance` draws every node; the editor adds no
per-node attachments. Stable ids are graph node names; handles are never
persisted. IllEd has no private codec: its format 1 `IlscCodec` and
`EditorAttachment` were deleted (D-E25, superseding D-E10).

## `.ilsc` format 2

UTF-8 JSON, extension `.ilsc`, read and written by `IlscCodec` in
`Illumo::Content`. The authoritative description is
`docs/content-packages-and-scenes-design.md` section 10; in short:

```json
{
  "format": "ilsc",
  "format_version": [2, 0],
  "metadata": { "title": "Forest", "author": "", "description": "" },
  "settings": {
    "world_mode": "3d",
    "environment": {
      "skybox": "sky",
      "ambient": [0.25, 0.27, 0.3],
      "sun": { "direction": [-0.4, -1, -0.3], "color": [1, 0.96, 0.9],
               "intensity": 1, "shadows": true }
    }
  },
  "assets": [
    { "id": "tree", "type": "mesh", "path": "meshes/tree.obj" },
    { "id": "sky", "type": "cubemap_cross", "path": "/engine/Skybox/skybox-daylight.png" }
  ],
  "nodes": [
    { "id": "n1", "parent": null, "name": "Tree", "enabled": true, "visible": true,
      "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
      "tags": ["static"],
      "components": [ { "type": "mesh", "asset": "tree", "tint": [255, 255, 255, 255] } ] }
  ],
  "editor": { "camera": { "x": 0, "y": 0, "zoom": 32, "yaw": 0.7, "pitch": 0.45 },
              "grid": { "spacing": 1, "visible": true } }
}
```

- Core components: `primitive` (`rect`, `ellipse`, `triangle`, `cube`,
  `pyramid`, `sphere`, `wire_cube`, `wire_sphere`), `mesh`, `sprite`, `light`
  (directional) and `camera`. Asset types: `mesh`, `texture`, `atlas`,
  `cubemap_cross`, `cubemap_faces`.
- Core objects are strict: unknown keys, wrong types and out-of-range values
  fail the whole load, which leaves the current document untouched. A
  component type containing a dot and every `extensions` entry are kept
  verbatim; IllEd shows them read-only and copies them unchanged.
- Asset references are package-relative (resolved against the scene's package
  root) or absolute virtual paths (`/engine/...`, `/packages/<id>/...`).
  Scenes opened through a dialog resolve relative references under `/local`.
- Node order is graph preorder, so sibling order round-trips. Output is
  canonical (fixed key order, shortest round-trip floats).
- The `editor` block holds IllEd's camera, grid and snap settings. It is
  written on a save and never makes the document dirty.
- Format 1 files (integer `version: 1`) are refused with an explicit message.

Native dialogs use description `Illumo Scene` and pattern `*.ilsc`. History is
cleared on load.

## Editing model

- **History** (`EditorHistory`) stores patches, not snapshots: each command
  lists the before and after state of every node it touched (record or
  absent, parent, next sibling) plus optional scene settings (world mode,
  environment, asset table, metadata, extensions). Undo and redo bring nodes
  to their target state (removals deepest first, creations parents first,
  then updates, then a reverse pass that restores sibling positions), so
  undoing a delete puts the node back in its exact place. Commands with the
  same merge key (a gizmo drag, an inspector scrub) merge into one, keeping
  the first before and the last after. History is capped at 512 commands and
  64 MiB of estimated records; the oldest fall off first.
- **Dirty** means the history cursor's command uid differs from the uid
  recorded at the last save. A merge barrier at the save keeps later edits
  from merging into the saved command. Camera, grid and other editor view
  state never dirty the document.
- **Selection** (`EditorSelection`) is an ordered set with a primary node (the
  last one chosen). The gizmo and inspector follow the primary; bulk edits,
  deletes and copies use `topLevel()` roots.
- **Shortcuts** (`EditorShortcuts`) are one table: menus show `labelFor()` and
  key handling uses `match()`, so every shortcut shown in a menu works
  (`IllEd.Module.ShortcutsMatchMenus`). Every action is an `EditorCommand`
  dispatched by `EditorModule::handleCommand`.
- **Creation** places new nodes at the root. The hierarchy's Add Child creates
  a child of the primary node.
- **Picking** uses `SceneInstance::pickRay` in 2D and 3D, which tests
  attachment local bounds from `SceneGraph::raycastCandidates` with rotation,
  scale and effective visibility, preferring the later node in preorder on
  ties.
- Reparenting keeps each node's world pose.

## Keys

| Action | Key |
|---|---|
| New / Open / Save / Save As | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Undo / Redo | Ctrl+Z / Ctrl+Y or Ctrl+Shift+Z |
| Cut / Copy / Paste / Duplicate | Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+D |
| Select All / Deselect | Ctrl+A / Escape |
| Rename / Delete / Unparent / Show-Hide | F2 / Del / U / H |
| Select, Move, Rotate, Scale tools | Q, W, E, R |
| World or local space / snapping | X / G |
| 2D or 3D world | 2 / 3 |
| Frame selection / reset camera | F / Home |

Camera navigation uses the arrows, PageUp/PageDown and the mouse, so letters
stay free for commands. While an inspector text field has focus it consumes
keys and characters, so no shortcut fires.

## Transform tools

`EditorGizmo` holds gizmo geometry, hit testing and drag math; the module turns
pointer positions into rays and deltas into transforms.

- Translate (axes, planes, center), rotate (three rings; only the Z ring in
  2D) and scale (axes and a uniform center), drawn at constant screen size.
- World space, or the primary node's local axes (X toggles).
- Rotation pivots on the primary node; scaling keeps each node's own axes and
  spreads positions from the pivot. Grabbing a node's body still moves the
  selection on the edit plane.
- Deltas are cumulative from the press and applied to the transforms captured
  at the press, so drags never drift. Every drag is one merged history command
  (`EditorDocument::setTransforms`).
- Snapping (G toggles, Ctrl inverts while dragging) quantizes translation per
  gizmo axis, angles and scale factors. The increments (default 0.5 units,
  15 degrees, 0.1) live in the scene's `editor` block.
- Frame selection (F) fits the selection's world bounds. Selection boxes are
  oriented per selected node.
- The Tools menu mirrors the tool keys, and the status bar shows mode, space
  and snap.

## Selection, clipboard and hierarchy

- **Box select:** a drag that starts on empty viewport space draws a marquee
  and selects every visible node whose world-bounds center projects inside it
  (Ctrl or Shift adds). A click without a drag clears the selection.
- **Clipboard** (`EditorClipboard`): Copy, Cut and Paste write the selection's
  top-level subtrees as a format 2 `.ilsc` fragment tagged with the
  `illed.fragment` extension, roots at their world pose, plus the assets they
  reference; at most 4 MiB. Untagged or foreign text is refused.
  `EditorDocument::paste` inserts after the primary selection as one command,
  gives every node a fresh id, reuses identical assets and renames
  conflicting asset ids (`tex_2`). Ids inside namespaced data are not
  remapped, and asset paths are not rewritten between packages. Duplicate
  (Ctrl+D) copies each subtree next to its original.
- **Hierarchy** (`EditorSceneGraphView`): only rows inside the row window are
  drawn or hit; the wheel scrolls it (with a thumb), a drag near its edges
  auto-scrolls, and a new primary selection unfolds its ancestors and scrolls
  into view. Rows have fold arrows and a visibility eye (struck through and
  dimmed when hidden). Drops land before, into or after a row by thirds,
  drawn as an insertion line, and reorder through
  `SceneGraph::setParent(node, parent, insertBefore)` (D-E26). Double-click
  renames. The right-click menu offers Rename, Duplicate, Copy, Cut, Paste,
  Add Child, Show/Hide, Enable/Disable, Unparent and Delete; the panel hands
  its choice to the module through `takeCommand()`.

## Inspector

`EditorInspector` is a typed property editor built on `GuiTextEdit` (UTF-8
caret, selection and clipboard, drawn by `GuiKit::drawTextField`; D-UI6).
Fields are rebuilt from the document every frame.

- With a selection: name (F2 focuses it), enabled and visible, position,
  rotation as Euler degrees (stored as a quaternion), scale, and each
  component: primitive shape, extent and color; mesh asset, tint and shadow
  casting; sprite texture, size, facing, tint and flip; light color,
  intensity and shadows; camera projection, field of view, clip planes, zoom
  and primary. Each core component has a Remove button, and an Add component
  section adds a Shape, Light or Camera the node does not have yet;
  namespaced components are read-only.
- With nothing selected: the scene's world mode, metadata and environment
  (skybox asset, ambient color, and the sun's direction, color, intensity and
  shadows).
- Enter commits one history command, Escape cancels, invalid text is rejected
  in place. Number fields scrub by dragging their label, one merged command
  per scrub. Multi-selection shows shared values, marks mixed ones with a
  dash, and applies one command to every selected node
  (`EditorDocument::editNodes`, which restores every node if any edit is
  rejected).

## Content and assets

- **Node kinds:** Create adds an empty node, every primitive shape (Rect,
  Ellipse, Triangle, Cube, Pyramid, Sphere, Wire Cube, Wire Sphere), Light and
  Camera (placed at the view center). Mesh and sprite nodes come from
  dropping a file from the asset browser (or `scene_place`).
- **Asset browser** (`EditorAssetBrowser`, over `GuiFileTree`): docked below
  the hierarchy (38% of the left column), it lists the virtual file tree
  (`/app`, `/engine`, `/packages/<id>`, `/project`) through
  `IllEdPlatform::listDirectory`. Directories expand in place; the panel
  scrolls; a double-clicked scene opens (with the usual unsaved-changes
  confirmation). `EditorAssets::kindFor` treats `.obj` as a mesh, PNG, JPEG,
  TGA and BMP as textures, and `.ilsc` as a scene.
- **Drag to place:** dragging a mesh or texture into the viewport calls
  `EditorDocument::placeAsset`, which adds an asset entry (fresh id, reference
  package-relative to the document's package root, an identical entry
  reused) plus a mesh-renderer or world-sprite node at the release point on
  the edit plane, as one command. There is no drag preview yet.
- **Loading** collects the scene's references (`collectSceneFetches`),
  fetches them with `IllEdPlatform::fetchAssets` (in the guest,
  `GuestSceneFetches` over the `GuestVfsAssets` cache, then an OBJ's MTL
  libraries in a second stage, missing ones tolerated), and only then
  instantiates. Missing assets draw as magenta placeholders.
- **Project** (`--project <dir>`): new documents resolve against `/project`.
  File gains Save to Project (writes `/project/scenes/<name>.ilsc` and rebases
  the document onto `/project`), Import to Project (copies a picked file into
  `/project/meshes`, `textures`, `scenes` or `assets` after
  `EditorAssets::validateImport` checks an image against the 16 MiB texture
  upload cap) and Pack Project (packs `/project` into an `.ilpk` at a chosen
  location). Without a project these commands explain that `--project` is
  needed. Tree documents use `vfs:<virtual path>` locations
  (`IllEdPlatform::kTreePrefix`).

## Console commands

IllEd registers `scene_select [id...]`, `scene_place <virtual path> [x] [y]`,
`scene_save_project`, `scene_undo`, `scene_redo` and `scene_frame`. Capture
scripts and package tests use them to reach the same flows as the UI.

## UI

The editor is drawn in the plain tool look (`GuiToolStyle`, D-UI7): a File /
Edit / Create / Tools / View menu bar and a status bar (`EditorToolbar`), the
viewport, and one `GuiPanelDock` holding four panels. The left column has the
Hierarchy (`EditorSceneGraphView`) above the Assets browser
(`EditorAssetBrowser`); the right column has Tools (`EditorToolsPanel`: 2D/3D
mode, Move/Rotate/Scale, local axes, snap and the Create tools) above the
Inspector (`EditorInspector`). Splitters resize the columns and the panels
within them. Each panel's title bar can hide it or pop it out into its own
window (where the host offers `IllumoContext::panelSurfaces`, D-E27), and
dragging a title past the window's edge tears it off. A detached panel docks
back from its title bar, by closing its window, or through View > Reset
Layout; View also shows, hides, pops out and docks each panel. The layout is
saved in the `panelLayout` setting and detached panels reopen on the next
start.

Panels are content renderers: each draws into the `GuiPanelPlacement` it is
given (content rectangle plus surface) and reads its pointer through
`GuiPanelPointer`, docked or detached alike. A press the menu bar, an open
menu or dock chrome took never reaches a panel. Text entry works in a
detached Inspector; keys come from the one input queue, so shortcuts work from
any editor window unless a field has focus. An asset dragged out of a
detached Assets window places where it is released over the viewport: the
release point is mapped into the main window through the windows' screen
origins. The mode label and toasts sit in the viewport between the columns.
`fontSize` scales panel content; the chrome keeps the tool metrics and follows
the renderer's UI scale. Tool rows sample `Assets/IllEd/editor-ui-atlas.jpg`
as a 6x6 sprite atlas through the token path; the guest preloads it from the
package, so icons draw identically in detached windows. The document's world
mode (2D or 3D) sets presentation and picking. The editor grid, gizmo and
selection wireframes are not saved.

`EditorModule` is split by concern: `EditorModule.cpp` (lifetime, frame,
drawables), `EditorModulePanels.cpp` (dock, placements, saved layout),
`EditorModuleCommands.cpp` (dispatch, files, node commands) and
`EditorModuleViewport.cpp` (camera, grid, picking, gizmo, selection input).
Files, dialogs, the clipboard and the file tree go through `IllEdPlatform`:
`IllEdPlatformNative.cpp` for the native oracle (with `IllEdNativeTree`
serving a test's virtual file tree) and `Wasm/EditorApplication.cpp` for the
package. Completions may arrive synchronously or on a later update.

## Tests

`IllEdTests` runs the native oracle cases: `IllEd.History.*` (undo/redo of
every command, sibling order after delete, drag merging, byte cap, dirty
cursor), `IllEd.Document.*`, `IllEd.Module.*` (including `CameraDoesNotDirty`,
`NewNodeParentsToRoot`, `ShortcutsMatchMenus`, `KeyboardUndoRedo`),
`IllEd.Inspector.*`, `IllEd.Gizmo.*` (axis projection, ring angles, scale,
local space, real pointer drags, snapping under a rotated parent, frame
selection), `IllEd.Selection.BoxSelect2D`/`BoxSelect3D`, `IllEd.Clipboard.*`
(id remap round trip, foreign or oversize text, paste position),
`IllEd.Hierarchy.*` (row window, reorder undo, fold and visibility),
`IllEd.Assets.*` (kinds and ids, the texture-cap import check, browsing the
tree, the project flow), `IllEd.Tools.*`, `IllEd.Toolbar.*` (menu hits,
chrome metrics, View panel items), `IllEd.Panels.*` (pop-out and dock,
detached text entry, cross-window asset drop, layout persistence, all through
`FakePanelSurfaces`), and the scene-graph view, UI atlas and config cases. `IllEd.Wasm.Package` drives the real package through the
generic host (launch scene, package-preloaded atlas, arrow-key pan, Ctrl+S
save in place); `IllEd.Wasm.ProjectPackage` runs it with a writable
`/project`, places a project mesh with its MTL and a texture through the guest
cache, and saves into the project. `IllEdCloseWindowTests` checks close
confirmation in a real hidden GLFW window. Import and Pack dialogs cannot be
scripted natively; their host halves are covered by `Illumo.Wasm.MountedFiles`
and `Illumo.Wasm.MountedDeny`.
