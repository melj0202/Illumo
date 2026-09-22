# IllEd guidance

This file specializes the repository `AGENTS.md` for `IllEd/Source/`.

## Product identity

IllEd is an Illumo application. Its job is to author SceneGraph documents that
later Illumo applications load. Treat it as the in-tree editor bootstrap
(Unreal Editor to Unreal), not as a second cellular-automata product.

IllEd ships only as the `IllEd.wasm` package (`apps/illed/`, manifest
`IllEd/app.json`, `launchAccess: "edit"`, private storage `storage/illed/`)
run by `IllumoRuntime --app illed [--open scene.ilsc]`, like IllumoGame.
There is no native `IllEd.exe`. `Wasm/EditorApplication.cpp` is the guest
entry (`GuestModuleApplication`); it preloads the editor UI atlas from the
package (`packageAssets()`) and hands the `--open` launch file to the
platform seam. `IllEdCore` stays a native library for the `IllEdTests`
oracle suite.

## Scope and boundaries

- Depend only on `Illumo::Illumo`. Do not link Game, Rulesets, or
  `IllumoGameCore`.
- `EditorDocument` owns the runtime graph and is the mutation gateway. Graph
  hierarchy/TRS/state are authoritative; document recipes retain stable file
  IDs, display names, and geometry for `.ilsc`. Never maintain a second cycle
  validator or world-transform composer. Serialize in graph order.
- Bind stable document IDs to graph names and recipe indices to opaque user
  data. Product identity policy and file I/O stay in IllEd.
- EditorModule consumes the change journal to update persistent render bindings.
  Overflow resynchronizes bindings without rebuilding nodes. Detach/invalidate
  snapshots before reconfiguring or deleting a borrowed visual.
- Graph AABB queries supply picking candidates; editor-owned exact local-box
  tests retain rotation, singular-transform, and selection-order policy.
- Write UTF-8 JSON `.ilsc` version 1 with 2D/3D primitive kinds and
  `world_mode`. Do not write `.illumo` or serialize `SceneNodeHandle` values.
- Keep UI primitive-composed through `GameVisual`. No retained widget tree.
- Toolbar, sidebar, and scene-graph panels take shared behavior from
  `Illumo/Gui/GuiMenuShell`: `GuiPanelLayout::viewport` for the UI-scale
  viewport docked chrome anchors to, `GuiPointerTracker` for virtual-space
  pointer position and press/release edges, and `GuiEasing` for reveal curves
  and panel slides. Do not restate those in a panel. `EditorModule` keeps its
  own world-picking and gizmo drag state; that is world input, not panel
  chrome.
- Main-thread affine. Iterative hierarchy walks only.
- Files and dialogs go through `IllEdPlatform` (`IllEdPlatform.h`), the
  counterpart of the game's `CSimPlatform`: `IllEdPlatformNative.cpp` for the
  native oracle, `Wasm/EditorApplication.cpp` over `GuestDocuments` for the
  package. Save, open and close confirmation are asynchronous. The document
  keeps an opaque location plus a display label (the guest never sees host
  paths), and editing input is held while a transfer is in flight.

## Persistence

`.ilsc` is the interchange contract between IllEd and future scene consumers.
Validate a complete document before replacing live state. Extra JSON keys may
be ignored; unknown kinds and cycles fail closed. Promote the codec into
Illumo only when a second loader exists. The WASM port did not change the
`.ilsc` format.

`IllEd.Wasm.Package` (`IllEd/Tests/Wasm/TestEditorPackage.cpp`) drives the
real package through the generic host: launch scene, package-preloaded atlas,
keyboard pan, and Ctrl+S save in place.
