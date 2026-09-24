# IllEd guidance

This file specializes the repository `AGENTS.md` for `IllEd/Source/`.

## Product identity

IllEd is an Illumo application. Its job is to author SceneGraph documents that
later Illumo applications load. Treat it as the in-tree editor bootstrap
(Unreal Editor to Unreal), not as a second cellular-automata product.

IllEd ships only as the `IllEd.wasm` package (`apps/illed/`, manifest
`IllEd/illumo.json`, `launchAccess: "edit"`, private storage `storage/illed/`)
run by `IllumoRuntime --app illed [--open scene.ilsc]`, like IllumoGame.
There is no native `IllEd.exe`. `Wasm/EditorApplication.cpp` is the guest
entry (`GuestModuleApplication`); it preloads the editor UI atlas from the
package (`packageAssets()`) and hands the `--open` launch file to the
platform seam. `IllEdCore` stays a native library for the `IllEdTests`
oracle suite.

## Scope and boundaries

- Depend only on `Illumo::Illumo` and `Illumo::Content`. Do not link Game,
  Rulesets, or `IllumoGameCore`.
- `EditorDocument` owns one `SceneInstance` (Content) plus `EditorHistory`
  and is the only mutation gateway. Every edit is recorded as a patch
  command (node before/after states plus optional scene settings); dirty
  means the history cursor differs from the saved one, so camera and other
  `SceneEditorState` changes never dirty. Drags share a merge key so one drag
  is one undo step. Never keep a second cycle validator, world-transform
  composer or scene model beside the instance.
- `SceneInstance` draws every node; the editor adds no per-node attachments.
  Stable ids are graph names. Pick through `SceneInstance::pickRay` (pick
  proxies make empty, light and camera nodes selectable); its exact local-box
  test respects rotation, visibility and 2D preorder stacking.
- `EditorSelection` is an ordered set with a primary node; bulk edits use
  `topLevel()` roots. New nodes are created at the root. Box select (a drag
  from empty viewport space) tests projected bounds centers and skips hidden
  nodes, like picking.
- `SceneInstance::findNode` records carry an empty `parentId`; the parent
  lives in the graph, so ask `SceneInstance::parentOf`.
- Copy/paste goes through `EditorClipboard`: an `.ilsc` format 2 fragment
  tagged `illed.fragment`, at most 4 MiB, roots stored at their world pose.
  `EditorDocument::paste` remaps every id and merges assets (reuse identical,
  rename conflicting) as one command. Never paste untagged text.
- The hierarchy panel draws and hits only rows inside its row window; fold,
  eye, drop-before/into/after and the context menu live in
  `EditorSceneGraphView`, which hands menu choices to the module through
  `takeCommand()` rather than editing beyond drops and visibility itself.
- The asset browser (`EditorAssetBrowser`, over `GuiFileTree`) lists the
  virtual file tree through `IllEdPlatform::listDirectory`; dropping a mesh or
  texture into the viewport calls `EditorDocument::placeAsset` (asset entry
  plus node as one pasted command, package-relative reference, identical
  entries reused). Scenes load collect-fetch-instantiate: references are
  fetched with `IllEdPlatform::fetchAssets` (the guest asset cache; an OBJ's
  MTL libraries in a second stage) before `loadFromText`.
- Tree documents use `vfs:<virtual path>` locations (`IllEdPlatform::
  kTreePrefix`). With `--project`, new documents resolve against `/project`,
  Save to Project writes `/project/scenes/<name>.ilsc`, Import copies a pick
  into `/project/<meshes|textures|scenes|assets>` after the texture-cap check
  (`EditorAssets::validateImport`), and Pack writes an `.ilpk`. The native
  oracle serves the tree from `IllEdNativeTree::install`; tests install one.
- `EditorShortcuts` is the only key map; menus show `labelFor()` and the
  toolbar dispatches `match()`. Camera navigation uses arrows, PageUp/PageDown
  and the mouse so letters stay free for commands.
- `EditorModule` is split by concern: `EditorModule.cpp` (lifetime, frame,
  drawables), `EditorModulePanels.cpp` (dock, placements, saved layout),
  `EditorModuleCommands.cpp` (dispatch, files, node commands),
  `EditorModuleViewport.cpp` (camera, grid, picking, gizmo, selection input).
- Keep UI primitive-composed through `GameVisual` in the plain tool look
  (`GuiToolStyle`, D-UI7): flat, no animation or glow. No retained widget
  tree.
- The Hierarchy, Assets, Tools (`EditorToolsPanel`) and Inspector panels live
  in one `GuiPanelDock` owned by `EditorModule` (left: Hierarchy over Assets;
  right: Tools over Inspector). Each panel is a content renderer: it draws
  into the `GuiPanelPlacement` it is given (content rectangle plus surface)
  and reads its pointer through `GuiPanelPointer`, docked or detached
  alike. Panels never lay themselves out against the window, never own
  title bars or collapse strips, and never know whether they are detached.
  `EditorToolbar` is only the menu bar, dropdowns, mode label, toasts and
  status bar; its chrome keeps the tool metrics while `fontSize` scales
  panel content.
- Detached panels render into `IllumoContext::panelSurfaces` scenes and work
  without it (everything stays docked). A press the menu bar, an open menu or
  dock chrome takes is marked `inputBlocked` for the panels in that surface.
  An asset dropped from a detached Assets window maps to main-window pixels
  through the surfaces' screen origins. The layout is saved in the
  `panelLayout` setting (the dock's text with `;` for newlines) and detached
  panels reopen on the next start. `EditorModule` keeps its own
  world-picking and gizmo drag state; that is world input, not panel chrome.
- Main-thread affine. Iterative hierarchy walks only.
- Files and dialogs go through `IllEdPlatform` (`IllEdPlatform.h`), the
  counterpart of the game's `CSimPlatform`: `IllEdPlatformNative.cpp` for the
  native oracle, `Wasm/EditorApplication.cpp` over `GuestDocuments` for the
  package. Save, open and close confirmation are asynchronous. The document
  keeps an opaque location plus a display label (the guest never sees host
  paths), and editing input is held while a transfer is in flight.

## Persistence

`.ilsc` format 2 is shared by every Illumo program and owned by
`Illumo::Content` (`IlscCodec`, `SceneDocument`); IllEd has no private codec.
Format 1 files are refused with an explicit message. Validate a complete
document before replacing live state; history clears on load. See
`docs/content-packages-and-scenes-design.md`.

`IllEd.Wasm.Package` (`IllEd/Tests/Wasm/TestEditorPackage.cpp`) drives the
real package through the generic host: launch scene, package-preloaded atlas,
arrow-key pan, and Ctrl+S save in place.