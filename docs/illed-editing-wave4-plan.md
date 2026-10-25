# IllEd editing wave 4: workflow robustness and a scene-driven Saturn (plan)

Status: approved 2026-10-21 with the recommended answers to the open
questions in section 10 (relaunch, recent list in the runtime's settings,
SDK hooks on `GuestPlayScene`, session restore on); every milestone done
2026-10-22 (section 7). The owner
chose on 2026-10-21, from four offered lanes, lane B ("workflow robustness") plus the
prerequisite of making the nested `Saturn/` program load an `.ilsc` scene
through `SceneInstance`, so that IllEd authors Saturn's world and every later
editor wave is driven by a real consumer. Lanes A (prefabs and nested scene
instances), C (lights, materials, glTF) and D (game-facing authoring) stay
open owner decisions.

## 1. Objective and end state

| # | Condition |
|---|---|
| E1 | Saturn is a `GuestPlayProgram`: it plays `/app/Scenes/world.ilsc` (an orange slab, a capsule mesh, an isometric primary camera) and moves the capsule through a registered `saturn.player` behaviour described in `/app/behaviours.json`; IllEd lists Saturn under Play with and Ctrl+P plays the edited scene in Saturn |
| E2 | IllEd autosaves every dirty document into its private storage on a cadence; after a crash or kill the next start offers to recover each autosaved document, and a clean exit leaves nothing to recover |
| E3 | Saving into `/project` keeps the previous version as a backup; Revert to Saved and Save All exist; the OS window title shows the active document and its dirty mark |
| E4 | Several scenes are open at once in a tab strip (Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+W); each tab keeps its own history, selection, camera, hierarchy and isolation state; closing a tab or the window asks per dirty tab |
| E5 | Without `--project`, File offers Open Project, New Project and a Recent Projects list; choosing one relaunches the editor on that project; host paths never cross the ABI |
| E6 | A real-GPU CTest case drives the IllEd package through a window, saves a scene and checks the capture, under the existing `IllumoGpu` label and skip convention |

## 2. Current-state evidence

- **Saturn** (its own git repository, one commit `b327e44`, uncommitted
  capsule work in `Source/SpinningCubeScene.*` and its test) is a plain
  `GuestProgram` whose one `ProgramScene` hand-builds a grid, a slab and a
  capsule with `MeshVisual` and never touches `content()`. No assets folder,
  no `.ilsc`, no `Tests/Wasm`, no `launchAccess` in `illumo.json`
  (`Saturn/Source/Wasm/SpinningCubeProgram.cpp`, `Saturn/PackageTargets.cmake`).
- **The scene-playing pattern** is `GuestPlayProgram` plus `GuestPlayScene`
  (`IllumoGuest/Include/IllumoGuest/PlayProgram.h`): collect references,
  fetch through `GuestSceneFetches`, `content().load`, attach
  `SceneBehaviours`, view through the primary camera. `GuestPlayScene` is
  `final`, so a game cannot add anything to it (a grid overlay, its own
  console commands). Playground (`Playground/Source/*`) is the only user;
  its `WalkerBehaviour` is the movement template. Format 2 has no capsule
  primitive (`SceneDocument.h:80-90`).
- **Saving and storage.** Every platform operation completes through
  callbacks (`IllEd/Source/IllEdPlatform.h`). A location without the `vfs:`
  prefix or a `selected:` grant is a private storage name
  (`IllumoGuest/Source/Documents.cpp:74-76`) under
  `storage/illed/` (`RuntimeApplication.cpp:497-505`). The file protocol has
  no List, Stat or Delete for the Storage area and the host creates no
  directories for storage names (`WasmFileServices.cpp:124-151,602-619`), so a
  guest can write and re-read flat storage names but cannot enumerate or
  remove them. Project writes create their parent folders
  (`VirtualFileSystem.cpp:257`) and refuse only `illumo.json` and `*.wasm`.
- **Dialogs.** Only file open and save exist (`Platform/SaveLoad.h`,
  `WinSaveLoad.cpp:83`, `LinuxSaveLoad.cpp:115`); no folder picker. The guest
  `Dialog` service returns a `sel-N` grant and a label, never a path.
- **Projects.** `--project` is mounted once at launch and the `ProjectFiles`
  capability is decided once at guest start (`WasmProgram.cpp:229-232`). No
  guest service requests a mount. Launch options are cleared before and
  after parsing so a stale package never replays (`RuntimeApplication.cpp:
  243-255`). There is no recent list anywhere. The content-packages design
  declared a folder chooser, recent lists and persisted mounts non-goals
  (`docs/content-packages-and-scenes-design.md:65-66`) and lists the folder
  chooser as a follow-up (section 15).
- **Safeguards today.** `EditorConfirmDialog` (Save / Discard / Cancel)
  guards New, Open and Exit (`EditorSceneCommands.cpp:1084-1137`) and the
  host asks the guest before closing the window (`RuntimeShell.cpp:252-266`,
  `WasmProgram.cpp:554-573`). No autosave, backup, Revert or Save All; no OS
  window title is set although the guest `Window` service has `SetTitle`
  (`IllumoGuest/Include/IllumoGuest/Windows.h:15`).
- **One document.** `EditorScene` holds `EditorDocument m_document`, attached
  to the program scene's content (`EditorScene.cpp:98-106`) but drawn by the
  editor itself (`EditorScene.cpp:646-674`, never `dispatchContent`). Per
  document state outside the document: selection, hover, isolation, drag,
  context point, busy flag and pending action (`EditorScene.h:69-161`),
  hierarchy scroll/fold/anchor/filter (`EditorSceneGraphView.h:158-207`),
  inspector edit and scrub (`EditorInspector.h:194-277`). Async completions
  capture `this` and act on `m_document` (`EditorSceneCommands.cpp:357-386,
  459-505, 653-683`). `loadDocument` calls `releaseAssets()` for the whole
  guest cache (`EditorSceneCommands.cpp:446`). The engine allows several
  `SceneInstance`s: file assets are reference counted, unit meshes are per
  instance, but a shared `RenderWorld` draws every bound instance and the
  last `setEnvironment` wins (`SceneInstance.cpp:294-466`).
- **Visual tests.** `tools/verify_capture.py` is a desktop script outside
  CTest; CTest's runtime cases are option checks only (`cmake/IllumoWasm.cmake:
  261-289`). Real-GPU cases exist only for the engine (`Illumo.Gpu.*`, label
  `IllumoGpu`, `SKIP_RETURN_CODE 77`, `Illumo/CMakeLists.txt:411-437`).
  Capture scripts take console commands, `@wait`, `@key` letters and
  `@begin`; under `--capture`, `--open` did not load in IllEd, the working
  recipe is `LaunchScene` in a scratch `--storage` (`.claude/skills/
  illumo-visual-capture-check/SKILL.md`).

## 3. Scope

In scope: the six end-state rows. The engine-side pieces are a folder dialog
on the platform seam, a host project service with relaunch, two SDK hooks on
`GuestPlayScene`, and the GPU smoke driver.

Non-goals (unchanged from earlier designs unless listed): prefabs, nested
scene instances, materials, new light kinds, glTF, Play inside the editor's
process, a persisted mount table or replay of launch options, host-side
backups of files chosen through the OS dialog (the host already replaces
them atomically; the guest cannot name a sibling), a capsule primitive in
format 2, Linux dialog smoke, and changing Saturn's gameplay beyond moving
the capsule.

## 4. Constraints and invariants

- `EditorDocument` stays the only mutation gateway; one user action is one
  history command. Autosave and backups never change what the document
  considers saved (`markSaved` is only called by a user save).
- Host paths never cross the ABI. The guest sees project entries as an
  opaque id plus a label (the folder name). Storage stays flat names.
- `IllumoRuntime` and `Illumo/Source/Wasm` stay product-agnostic: the
  project service is generic ("a writable folder for this app"), gated by the
  manifest's `launchApps` flag like Play, with decoder and deny tests
  (`illumo-add-guest-capability` skill).
- No `.ilsc` format change. Saturn's scene uses format 2.0 as it is.
- Plain tool look, primitive-composed UI, `EditorShortcuts` the only key map,
  no recursion, no exceptions, no `auto`, main-thread affine.
- Automated tests never touch the user's settings: fresh `EnvVars` files,
  scratch `--storage`, and the GPU driver backs up and restores the
  runtime's `envvars.json` (it rewrites `WinX`/`WinY`).
- Saturn is the owner's repository: this plan changes its files but the
  owner commits there. The uncommitted capsule work is folded into the mesh
  asset rather than discarded.

## 5. Design and alternatives

### 5.1 Saturn as a scene-driven game (S)

- **SDK hooks.** `GuestPlayScene` loses `final`; `update` and `dispatch`
  stay overridable and two protected hooks are added: `onSceneLoaded()`
  (after `content().load` and behaviour attach) and `dispatchOverlay(DrawList&)`
  (called after the content). `GuestPlayProgram::createScenes` becomes the
  place a game emplaces its subclass. Alternative, rejected: Saturn copies the
  ~100-line load flow into its own `ProgramScene`, which is the duplication
  the SDK exists to prevent.
- **Saturn** becomes `SaturnProgram : GuestPlayProgram("Saturn",
  "/app/Scenes/world.ilsc")` with `describe()` adding `Assets` and
  `SelectedFiles`, `launchAccess: "read"` in `illumo.json`, and
  `SaturnScene : GuestPlayScene` that keeps the G-key grid overlay and the
  console line. `SaturnCore` keeps only the behaviours
  (`SaturnBehaviours.*`): `saturn.player` (speed, WASD and arrows on XZ, the
  Walker pattern) registered through `registerBehaviours`, described in
  `Saturn/behaviours.json`.
- **Content.** `Saturn/Scenes/world.ilsc`: `slab` (cube primitive, extent
  0.9/0.03/0.9, orange), `player` (mesh `capsule` with the `saturn.player`
  behaviour, standing on the slab), `camera` (perspective, fov 50, placed at
  (1,1,1)·4.5 looking at the origin, `primary: true`), one directional light.
  `Saturn/Assets/Meshes/capsule.obj` is generated once by
  `Saturn/tools/make_capsule.py` from the current `buildCapsule` parameters
  (radius 0.5, height 2, 8 cap rings, 24 sectors) and committed; the C++
  capsule builder is retired. `PackageTargets.cmake` stages `behaviours.json`
  and the `Scenes/` and `Assets/` trees.
- **Tests.** `SaturnTests` becomes a native behaviour suite: load the
  shipped `world.ilsc` into a `SceneInstance` on `MockBackend`, attach
  `SceneBehaviours` with the Saturn registry, feed key state, assert the
  player moves on XZ and the camera is the primary. `Saturn.Wasm.Package`
  (new `Tests/Wasm/TestSaturnPackage.cpp`, the Playground harness) checks
  `play_status` reports the node and behaviour counts, a held key moves
  `player` (`play_node player`), and no "registered but" or "Frame dropped"
  lines. IllEd's `IllEd.Wasm.PlayPackage` gains nothing new; a native
  `IllEd.Module.PlayWith` case checks Saturn's `behaviours.json` is read from
  `/apps/saturn` and the `saturn.player` fields are typed.

### 5.2 Autosave and recovery (B1)

- **Cadence.** A dirty document is autosaved 30 s after its last edit or at
  the latest 120 s after the previous autosave while it stays dirty, and
  immediately before Play. Nothing is written for a clean document. Autosave
  never sets `m_busy` and never blocks input; it queues a storage write and
  ignores a failure except for one status toast.
- **Storage layout** (flat names, Storage area, no protocol change):
  `autosave-<slot>.ilsc` holds the encoded document; `autosave-index.json`
  lists `{slot, label, location (vfs paths only, empty for dialog grants and
  new documents), packageRoot, savedAt, dirty}`. Slots are reused and the
  index is the only truth: a recovered or discarded slot is marked free in
  the index and its content overwritten with an empty file on the next use.
  A clean exit (approved close) frees every slot. Alternative, rejected: a
  Delete action in the file protocol, an ABI change for a cosmetic gain.
- **Recovery.** On start, after settings load, IllEd reads the index; for
  every slot marked dirty it opens a recovery dialog (a `GuiDialog` modal
  listing label and time) with Recover all / Discard all / Decide later.
  Recover opens each as a tab: a `vfs:` location is rebound to its path
  (dirty, so Save writes it back); a grant or new document becomes
  "<label> (recovered)" without a location, so Save becomes Save As. Decide
  later keeps the slots for the next start.
- **Session restore.** The same index records the open tabs' `vfs:`
  locations and the active tab on every autosave tick and on clean exit, so
  the next start reopens the last session's project scenes (an owner-visible
  setting, on by default). Dialog-granted files cannot be reopened (grants do
  not survive the process); they are listed in a status toast instead.

### 5.3 Safeguards (B2)

- **Backups.** Saving to `vfs:/project/scenes/<name>.ilsc` first copies the
  existing file to `vfs:/project/scenes/.backup/<name>.ilsc` (one generation;
  the project writes create the folder). `EditorAssets::validateImport`
  and `packProject` skip `.backup/`. Files chosen through the OS dialog get no
  backup (non-goal above; the host's atomic replace already protects against
  partial writes).
- **Revert to Saved** reloads the document's location, through the existing
  confirm dialog when dirty. **Save All** saves every dirty tab in sequence
  (one busy span). **Window title**: `<name>* - IllEd` through the guest
  `Window` `SetTitle` action, updated with the status bar; the native oracle
  records the last title.

### 5.4 Tabs (B3)

- **Ownership.** `EditorScene` owns `std::vector<std::unique_ptr<EditorTab>>`
  and an active index. `EditorTab` bundles the per-document state listed in
  section 2: `EditorDocument` (owning its `SceneInstance`, `makeScene` with
  pick proxies), `EditorSelection`, hover, isolation, drag and context state,
  busy flag and pending action, camera `SceneEditorState` (already in the
  document; `ic->camera` is stored on deactivate and restored on activate,
  as `SceneDirector` does per program scene), hierarchy view state, and the
  per-document fetch set. Gizmo mode, space, pivot, tool, grid and snap
  settings stay session-wide. The program scene's `content()` is no longer
  attached; `EditorDocument::attach` is removed and
  `IllEd.Module.DocumentIsContent` becomes `ActiveDocumentIsDrawn`. This ends
  the "document borrows the scene's content" wording of D-E31 for IllEd; the
  consensus doc and `IllEd/Source/AGENTS.md` say so.
- **Render world.** Only the active tab's instance is bound to the world
  (`setRenderWorld`); activation binds, deactivation unbinds. Inactive tabs
  keep their instance, history and asset references alive (GPU memory for N
  open scenes is accepted; closing a tab releases them).
- **Asset cache.** `GuestSceneFetches` gains per-set release: `fetch` returns
  a set id, `release(set)` frees one set and `release()` keeps its meaning.
  `IllEdPlatform::fetchAssets` returns the id and `releaseAssets(id)` replaces
  the whole-cache release in `loadDocument`. Each tab owns its set.
- **Completions.** Every async completion captures the tab's serial and
  resolves it through `EditorScene::tabBySerial`; a closed tab's completion
  is dropped, an inactive tab's completion applies to that tab (a save marks
  it saved; a load replaces its content). Switching tabs is refused while
  the active tab is busy. An inspector edit or scrub in progress is committed
  (Enter semantics) before a switch; hierarchy and inspector get a
  `resetForDocument()` call.
- **UI and commands.** A tab strip row under the menu bar in `EditorToolbar`
  (name, dirty mark, close box, middle-click closes, overflow scrolls).
  New commands `NextTab` (Ctrl+Tab), `PreviousTab` (Ctrl+Shift+Tab),
  `CloseTab` (Ctrl+W), `SaveAll` (Ctrl+Shift+S moves nothing: Save As keeps
  Ctrl+Shift+S, Save All is Ctrl+Alt+S), `RevertDocument`, and `OpenProject`,
  `NewProject`, `RecentProject<n>` for 5.5. `NewDocument` and `OpenDocument`
  open a new tab; `--open` and `LaunchScene` open the first tab. Window close
  walks dirty tabs through the existing confirm flow one at a time, with
  Save All offered when more than one is dirty. Console commands bind to the
  active tab through an accessor.

### 5.5 Projects without the command line (B4)

- **Platform.** `SaveLoad::GetFolderLocation(spec)`: `IFileDialog` with
  `FOS_PICKFOLDERS` on Windows, the gtkmm select-folder action on Linux
  (compiled, not smoke tested).
- **Host service.** A new `GuestService::Project` (next free id, version 1)
  with actions `List` (recent entries as `{id, label}`), `Choose` (folder
  dialog, adds the entry), `Create` (folder dialog, writes `illumo.json`
  `{format: ilpk, id: <folder>, kind: "package"}` and the `scenes/`,
  `meshes/`, `textures/`, `assets/` folders, adds the entry), and `Open(id)`
  (starts a new `IllumoRuntime --app <id> --project <path>` through
  `RuntimeAppLauncher` and reports success; the guest then requests its own
  close). Gated by the manifest's `launchApps`, like Play, since it spawns a
  process. Decoder and deny tests as for every service.
- **Recent list.** The runtime keeps `recentProjects.<appId>` (at most 10
  host paths, newest first) in its own `envvars.json`, separate from
  `kLaunchOptions` so a stale package still never replays; an entry whose
  folder is gone is dropped on `List`. Its id is the entry's position hash,
  valid for one listing.
- **Relaunch, not remount.** Alternative, rejected: unmounting and
  remounting `/project` in the running guest. `ProjectFiles` is granted once
  at start, the guest cache and every open tab would hold stale paths, and
  the rename of `/project` under open documents has no good answer. Relaunch
  reuses the Play launcher, costs one window flash, and the unsaved-tab flow
  already runs before close.
- **Editor.** File > Open Project…, New Project…, Recent Projects (list,
  "Clear list"). With no project mounted the status bar shows "No project"
  and Save to Project, Import and Pack stay disabled as today.

### 5.6 Live GPU smoke (B5)

- `IllEdGpuSmoke` (C++, `IllEd/tools/GpuSmoke.cpp`, links
  `Illumo::TestSupport`, exit 77 when `IllumoRuntime` reports no GPU) makes
  a scratch project and storage, writes the storage `envvars.json` with
  `LaunchScene=vfs:/project/scenes/smoke.ilsc` and a two-column
  `panelLayout`, backs up the runtime's `envvars.json`, runs
  `IllumoRuntime --app illed --project … --storage … --capture out.png
  --capture-script smoke.txt` (`scene_create cube`, `scene_save_project`,
  `@wait`), restores the runtime settings, then asserts the capture is a
  non-uniform image of the expected size (stb_image) and the saved scene has
  one more node. Registered as `IllEd.Gpu.Smoke` with label `IllumoGpu`,
  `SKIP_RETURN_CODE 77`, timeout 120 s. The capture skill's recipe is the
  source of the script and settings.

## 6. Contracts and compatibility

- Guest ABI: `GuestService::Project` (new, version 1) with its capability
  gate; `GuestSceneFetches::fetch` returns a set id (source-compatible for
  callers ignoring it). `GuestPlayScene` becomes a base class with two hooks;
  Playground is unchanged. No frame schema, file protocol or manifest change
  except Saturn's own `launchAccess`.
- Platform: `SaveLoad::GetFolderLocation` added.
- IllEd: `EditorDocument::attach` removed; `IllEdPlatform` gains `setTitle`,
  `fetchAssets` returning a set id, `releaseAssets(id)`, and the project
  operations (`listProjects`, `chooseProject`, `createProject`,
  `openProject`); the native oracle records them synchronously.
- Settings: `storage/illed/` gains `autosave-*.ilsc`, `autosave-index.json`;
  the runtime's `envvars.json` gains `recentProjects.<appId>`. No existing
  key changes meaning.
- Documentation: `docs/content-packages-and-scenes-design.md` section 3
  non-goals are amended (folder chooser and recent list now exist; launch
  options are still not persisted); `docs/architecture-consensus.md`,
  `IllEd/Source/AGENTS.md`, `docs/scene-behaviours-design.md` (the hooks)
  and a new decision entry for the project service and relaunch (D-E42) and
  the editor's owned documents (recorded with the `illumo-record-decision`
  skill at the end).

## 7. Milestones

| # | Milestone | Status |
|---|---|---|
| S1 | SDK hooks on `GuestPlayScene`; Saturn as a `GuestPlayProgram` with `world.ilsc`, the capsule mesh, `saturn.player`, native and package tests; IllEd Play with Saturn | done (2026-10-21, uncommitted) |
| W1 | Autosave, recovery dialog, session index; clean-exit cleanup | done (2026-10-21, uncommitted) |
| W2 | Project backups, Revert to Saved, window title | done (2026-10-21, uncommitted) |
| W3 | Tabs: `EditorTab`, per-set fetch release, completion routing, tab strip, commands, Save All, close-all flow | done (2026-10-21, uncommitted) |
| W4 | Folder dialog, `Project` service with decoder/deny tests, recent list, relaunch, File menu entries | done (2026-10-22, uncommitted) |
| W5 | `IllEd.Gpu.Smoke` driver and CTest registration | done (2026-10-22, uncommitted) |
| W6 | Docs, decision record (D-E42), full Release build and labelled CTest | done (2026-10-22, uncommitted) |

S1 first: it is independent of W1-W5 and produces the consumer every later
wave is judged against. W1 before W3 so recovery exists while the single
document still holds; W3 before W4 so relaunch walks tabs, not one document.

## 8. Verification strategy

- Native cases per milestone in `IllEdTests` (`IllEd.Module.Autosave`,
  `.Recovery`, `.Backup`, `.Revert`, `.Tabs`, `.TabCompletions`,
  `.CloseAllTabs`, `.Projects`) over the synchronous oracle with fresh
  `EnvVars` files and an installed native tree; `Illumo.Wasm.*` decoder and
  deny cases for the `Project` service; `IllumoGuest` cases for per-set
  fetch release.
- Package cases: `IllEd.Wasm.Package` extended with an autosave tick and a
  recovery on the next launch (two `WasmProgram` runs over one storage
  folder); `IllEd.Wasm.ProjectPackage` extended with the backup file; a new
  `IllEd.Wasm.Tabs` case driven by console commands (`scene_tab_open`,
  `scene_tab_next`, `scene_tab_close`); `Saturn.Wasm.Package`.
- `IllEd.Gpu.Smoke` on this machine (OpenGL; Vulkan and D3D12 through
  `--graphics-api` by hand once), plus a manual check of the tab strip,
  recovery dialog, folder dialog and relaunch in windowed and fullscreen
  mode, reported as such.
- `clang-tidy -p build-workspace-tidy` on the changed files (the batch run
  already fails on untouched code), changed-line formatting, and the full
  Release `IllumoWorkspace` suite with `CSim/` cloned before the push.

## 9. Risks and containment

- **Autosave write cost on large scenes**: encoding runs on the main thread.
  Contained by the 30 s debounce and a size log; a chunked encode is a
  follow-up if a scene exceeds a few MiB.
- **Storage slot limits**: `storageFits` refuses past 4096 entries or 1 GiB;
  slots are capped at 16 and the index reports refusals as a toast.
- **Tabs touch most of `EditorScene`**: W3 lands as one commit behind the
  existing single-tab tests, which must pass unchanged except the renamed
  `DocumentIsContent`. Rollback is reverting that commit; W1, W2 and W4 do
  not depend on it structurally (W4's close-all walks whatever tabs exist).
- **Relaunch leaves two windows for a moment**: the new process starts
  before the old one closes so a failed start keeps the editor open; the
  owner sees the flash. Contained by the `Open` result gating the close.
- **Folder dialog on Linux** is compiled, not smoke tested; the platform
  note stays "not supported".
- **`--open` under `--capture`** is unexplained (wave 2 trap); the smoke uses
  `LaunchScene`, and the cause is logged as a follow-up, not fixed here.
- **Saturn's uncommitted work** is in the owner's repository; the plan folds
  the capsule into an asset and the owner reviews the diff before committing.

## 10. Open questions (owner decisions)

1. **Project switching by relaunch** (recommended, 5.5) or by remounting in
   the running editor (rejected above for the stale-state reasons).
2. **Recent projects live in the runtime's own settings file**, which
   amends the content-packages non-goal "recent-project lists" while keeping
   launch options unpersisted. Confirm the amendment.
3. **Saturn extends the SDK's play scene** (recommended, two hooks) rather
   than copying the load flow. Confirm the SDK change.
4. **Session restore on by default** (reopen last session's project scenes;
   5.2) or off until asked.

## 11. Validation log

- **S1 (2026-10-21).** Engine: `GuestPlayScene` is no longer final and
  gains `program()`, `sceneLoaded()` and `dispatchOverlay()`
  (`IllumoGuest/Include/IllumoGuest/PlayProgram.h`); `clang-tidy -p
  build-workspace-tidy` on `PlayProgram.cpp` is clean. Saturn (the owner's
  repository, left uncommitted for their review): `SaturnProgram` over
  `GuestPlayProgram`, `SaturnScene` over `GuestPlayScene` (grid overlay,
  `SaturnKeyToggle` for the G key), `SaturnBehaviours` (`saturn.player`,
  camera-relative WASD on the ground plane), `behaviours.json`,
  `Scenes/world.ilsc`, `Assets/Meshes/capsule.obj` from
  `tools/make_capsule.py`, `launchAccess: "read"`. The `SpinningCube*`
  sources and their test were removed; the capsule parameters live in the
  generator. Cases: `Saturn.Behaviours.SchemaMatchesRegistry`,
  `Saturn.World.Loads` (mesh through a `VfsAssetSource`, camera node on the
  isometric diagonal looking at the origin, capsule bounds),
  `Saturn.World.PlayerMoves` (screen-right, away, diagonal speed, rest),
  `Saturn.KeyToggle.ConsoleCapture`, `Saturn.Wasm.Package` (3 nodes, 1
  behaviour, a held D moves the player to +X/-Z, no missing asset or dropped
  frame); `Playground.Wasm.DefaultScene`, `Playground.Wasm.LaunchedScene`,
  `IllEd.Wasm.PlayPackage`, `IllEd.Behaviours.*` and `IllEd.Module.PlayScene`
  still pass. A Release OpenGL capture of `--app saturn` (scratch
  `--storage`, runtime `envvars.json` restored) shows the lit slab, the
  shaded capsule with its shadow and the grid.
  - Deviation: the planned native `IllEd.Module.PlayWith` case would tie
    `IllEdTests` to the optional Saturn checkout, so the contract IllEd
    relies on is checked from Saturn's side instead
    (`SchemaMatchesRegistry`: `behaviours.json` and the registry agree);
    app discovery under `/apps/<id>` stays covered by
    `IllEd.Behaviours.Discovery`.
  - Not checked by an automated case: IllEd's Play with menu listing Saturn
    in a live window (Play is not offered under `--capture`). The W3 capture
    later showed Saturn's `Player` behaviour offered by the inspector, so
    the `/apps/saturn` discovery works in the real package.
  - Trap: a scene's `sun.direction` is the direction the light travels
    (`SceneSun`), the opposite of `MeshVisual::setLightDirection`; the first
    capture was ambient-only until the sign was flipped. Windows headers
    define `near` as an empty macro, which silently hollows out a helper of
    that name.
- **W1 (2026-10-21).** Content: `SceneExtensionText` (one string member of
  a namespaced extension) beside `SceneExtensionList`. IllEd:
  `EditorAutosaveRecord` (`illed.autosave`), `EditorRecoveryDialog`,
  `EditorSceneAutosave.cpp` (slot scan at start, cadence, free on clean,
  close waits for the free write, recovery, `lastScene` record and
  `restoreSession`), `EditorDocument::loadParsed` and `markRecovered`, the
  `restoreSession` default, and the console commands `scene_create`,
  `scene_autosave`, `scene_recover`, `scene_status`. Cases:
  `IllEd.Module.Autosave` (idle and 120 s rules, record contents, save
  frees the slot, discard-on-exit frees it before the close),
  `IllEd.Module.Recovery` (dialog text, first free slot, Recover as dirty
  "(recovered)" work with the record stripped and the slot adopted,
  Discard, Later, own slot beside a kept one), `IllEd.Module.SessionRestore`
  (record on open, cleared on New, reopened at start, off switch, a gone
  scene starts empty), `IllEd.Wasm.Recovery` (two guest runs over one
  storage folder: autosave lands in storage, the next run offers and
  recovers it). The IllEd label (109 cases) and `Illumo.Content.
  SceneExtensionList` pass; `clang-tidy` is clean on the new files. A
  Release OpenGL capture of `--app illed` over a scratch `--storage` holding
  a live slot shows the "Unsaved Work Found" dialog over the real package.
  - Deviation: the plan's separate `autosave-index.json` is not needed; the
    record rides inside each slot file as an extension, and the session's
    open scenes are a settings key (`lastScene`), which tabs (W3) extend to
    a list. Slots are 8, not 16; one document is recovered per start until
    tabs exist, the rest wait on disk (the dialog says so).
  - Design note: on the synchronous native oracle the slot scan issues every
    read in one update, in the guest one read per completion, so no
    recursion either way.
  - Trap: a package test must pump until a storage write lands before it
    stops the guest; the status line printed in the same frame still says
    "dirty" without "autosaved". A native case that runs concurrently with
    the package tests in the same Release folder saw one spurious miss
    (not reproduced in three solo runs; keep IllEd native and package runs
    sequential).
- **W2 (2026-10-21).** Backups: `EditorScene::writeDocument` reads a
  project scene's current file and writes it to `scenes/.backup/<name>`
  before `writeDocumentNow` overwrites it; `PackageMounts::packMounted`
  skips dot entries. Revert: `EditorCommand::RevertDocument` (File menu),
  `EditorConfirmDialog::openDiscard` (a two-button mode; the three-button
  set is rebuilt per `open`), pending action `RevertDocument`. Title: the
  wire now accepts `SetTitle` for surface 0 (Open and Close still refuse
  it), `WasmPanelWindows::handle` routes it to the main window,
  `GuestPanelSurfaces::setTitle(kMainSurface)` sends it when granted, and
  `EditorScene::applyWindowTitle` runs from `updateStatus`. Cases:
  `IllEd.Module.Backup` (previous version kept, one generation, the packed
  project omits `.backup`, a host file gets none), `IllEd.Module.Revert`
  (never saved, clean, Cancel keeps, Revert reloads clean),
  `IllEd.Module.WindowTitle` (Untitled, dirty mark, saved name),
  `Illumo.Wasm.WindowServiceDecoder` (surface 0 retitle decodes, open and
  close of it refuse) and `Illumo.Wasm.PanelWindowsLifecycle` (a surface 0
  retitle reaches the main window only); the IllEd label passes;
  `clang-tidy` is clean on the changed host, content and dialog sources. A
  real `IllumoRuntime --app illed` window reports the title
  "Untitled - IllEd" to the OS (read through the process's main window
  title), so the guest-to-host path works outside tests.
  - Deviation: Save All moves to W3 with tabs, as section 5.4 lists it;
    with one document it is Save. Backups stay visible in the asset
    browser on purpose, since the browser is the only way to open a tree
    scene and so the only way to open a backup.
  - Trap: service request ids in the window tests must keep increasing
    ("Replayed service request"); a new request goes at the end of a case
    with a higher id.
- **W3 (2026-10-21).** SDK: `GuestSceneFetches::fetch` returns a ticket and
  `release(ticket)` frees that document's held assets; `IllEdPlatform::
  fetchAssets` returns it and `releaseAssets(ticket)` mirrors it. Editor:
  `EditorTab` (document owning its instance, selection, hover, isolation,
  camera height, fetch ticket, `EditorHierarchyState`, autosave state);
  `EditorScene` owns the tabs, `document()`/`selection()` are the active
  tab's, `EditorDocument::attach` is gone and the program scene's content
  stays empty. `EditorSceneTabs.cpp`: `openTab`, `activateTab` (commits a
  focused inspector field, moves the camera, hierarchy state and the render
  world binding, refused while busy), `closeTab` (the last becomes empty),
  `tabForOpening` (a pristine active tab is reused), the open queue
  (session restore and recovery, one document per update), `saveAll`
  (activates each dirty tab in turn) and the exit walk (`firstDirtyTab`,
  `m_exitDiscarded`, Save All offered by `EditorConfirmDialog::open` with
  more than one dirty tab). Autosave is per tab with lazy slots; recovery
  opens every live slot into its own tab; `sessionTabs`/`sessionActive`
  replace `lastScene`. Toolbar: the tab strip under the menu bar
  (`chromeTop()`, `setTabs`, `takeTabClick`); commands `SaveAll`
  (Ctrl+Alt+S), `CloseTab` (Ctrl+W), `NextTab`/`PreviousTab`
  (Ctrl+Tab/Ctrl+Shift+Tab); console `scene_tab`. Cases:
  `IllEd.Module.Tabs` (reuse of a pristine tab, per-tab document, selection,
  camera and isolation, cycling, the strip's rectangles, close clean/dirty/
  last), `IllEd.Module.CloseAllTabs` (the walk, Cancel resets it, Save All,
  discard all), `IllEd.Module.SaveAll`, the extended `IllEd.Module.Recovery`
  (two slots recover into two tabs adopting their slots) and
  `IllEd.Module.SessionRestore` (two tree documents in tab order with the
  active one), `IllEd.Module.ActiveDocumentIsDrawn` (replaces
  `DocumentIsContent`), and `IllEd.Wasm.Tabs` (console and key-driven tabs
  through the generic host).
  The IllEd label passes (116 cases); `clang-tidy` is clean on the new and
  changed editor sources except one pre-existing finding in
  `EditorInspector.cpp:3625` (a float loop counter from wave 3, left as is).
  A Release OpenGL capture of `--app illed` driven by a capture script
  (`scene_create cube`, `scene_tab open`, `scene_create sphere`) shows two
  dirty tabs in the strip under the menu bar, the second raised and active
  with its sphere, and the dock below the strip; the same capture shows
  Saturn's `Player` behaviour in the inspector's Add behaviour row, which
  closes the live check S1 had left open.
  - Deviation: completions are not routed by tab serial for busy
    operations, since switching is refused while one is in flight; only
    autosave and recovery, which never hold input, resolve tabs by serial.
    Middle-click close is not implemented (the panel pointer has no middle
    button); the close box and Ctrl+W close. The hierarchy filter stays
    session-wide.
- **W4 (2026-10-22).** Platform: `SaveLoad::GetFolderLocation` (the common
  item dialog in folder mode on Windows, `Ole32`/`Uuid` linked; gtkmm
  select-folder on Linux, compiled only). Restart: `SetRelaunchArguments`
  replaces the command line `RelaunchCurrentProcess` replays (quoted for
  `CommandLineToArgvW` on Windows, `posix_spawn` argv on Linux). ABI:
  `GuestCapability::Project` (bit 14), `GuestService::Project` (20),
  `GuestProjectRequest`/`GuestProjectResult` version 1 and the `GuestProjects`
  helper (`IllumoGuest/Projects.h`), `IProjectHost`, the host's
  `completeProjects`, and `RuntimeProjectHost` (recent list in the runtime's
  settings, scaffolding, restart arguments). The runtime offers the
  capability beside Launch for `launchApps` apps in interactive runs. IllEd:
  the platform seam's project operations (guest adapter and native oracle
  `IllEdNativeProjects`), File > Open Project, New Project, the recent list
  and Clear Recent Projects, `switchProject` (the exit walk, then the host's
  open and `requestRestart`), "No project" in the status bar, console
  `scene_project`. Cases: `Illumo.Wasm.ProjectServiceDecoder`,
  `Illumo.Wasm.ProjectDeny` (no grant, no host, unavailable host, malformed
  request fails the exchange, the granted flows), `Illumo.Wasm.GuestProjects`,
  `Illumo.Wasm.RuntimeProjectHost` (dialog cancel, choose, create with the
  scaffold and manifest, gone folders dropped, open's restart arguments,
  dedupe, clear, read-back), the updated newest-service and
  newest-capability assertions in `Illumo.Wasm.LaunchServiceDecoder`,
  `IllEd.Module.Projects` (hidden items and the toast without a host, the
  menu entries, a cancelled dialog, a chosen folder switching at once on a
  clean document, Cancel dropping a switch, a discard restarting, Clear),
  `IllEd.Wasm.Projects` (the guest lists the fake host's projects and an
  open makes it ask to restart).
  - Design change from section 5.5: switching does not spawn a child
    runtime, since a launched child ends with its parent on Windows (the
    kill-on-close job object) and the existing restart replays the command
    line verbatim. Instead the host records replacement arguments and the
    guest uses the restart flag it already has, so the switch is the normal
    close plus a relaunch, with no second window.
  - Entries are gated by a new capability bit rather than only the Launch
    grant, so the deny test can refuse a guest without it while Launch stays
    granted.
  - Not verified live: the whole switch in a real window (the OS folder
    dialog, then the runtime restarting itself with `--project`). It needs
    the in-app console or the File menu of a live window, which this session
    could not drive (desktop control of the runtime was declined), and
    neither `--capture` nor `--bench-*` runs offer the capability or honour
    a restart. Each link is covered on its own: the host records the restart
    arguments (`Illumo.Wasm.RuntimeProjectHost`), the guest asks to restart
    (`IllEd.Wasm.Projects`, through the host's existing restart handling),
    and `RelaunchCurrentProcess` builds the command line. The owner's next
    `python build.py play --app illed` run with File > Open Project covers
    it; the IllEd label (118 cases) and `clang-tidy` on the new sources pass.
- **W5 (2026-10-22).** `IllEd/tools/GpuSmoke.cpp` (`IllEdGpuSmoke`,
  registered in `IllEd/PackageTargets.cmake` as `IllEd.Gpu.Smoke`, label
  `IllumoGpu`, `SKIP_RETURN_CODE 77`, timeout 120 s, run from the runtime's
  folder with the runtime's path as its argument). It makes a scratch
  project and storage, starts `IllumoRuntime --app illed --project ...
  --storage ... -ww 1280 -wh 720 --capture ... --capture-frame 150
  --capture-script ...` directly (no command processor; `CreateProcessW`
  with a pipe on Windows, `posix_spawn` on Linux, compiled only) with the
  script `scene_create cube`, `scene_save_project`, reads the capture
  result line, restores the runtime's `envvars.json`, decodes the PNG with
  the engine's `stb_image`, and requires an image at least the window's
  size with at least 16 distinct colours and a saved `scenes/Untitled.ilsc`
  holding one primitive node. No capture result at all (the runtime stopped
  before its shell, as without a render window) is a skip. On this machine
  (OpenGL) it passes in about 5 s with a 2560x1417 capture; a stand-in
  program that prints no result exits 77.
  - Deviation: the plan's `LaunchScene` and `panelLayout` settings are not
    needed; the scratch project with an untitled document and
    `scene_save_project` writing `Untitled.ilsc` avoids the `--open` under
    `--capture` trap entirely.
- **W6 (2026-10-22).** Decision D-E42 (the `Project` capability and
  service, `RuntimeProjectHost`, the relaunch with replacement arguments,
  and the editor's tab-owned documents; refines D-E31 and D-E25, extends
  D-E22, D-E36 and D-UI13) in `docs/latex/sections/09-design-decision-log.
  tex` and the consensus catalog; the "Folder dialog" open question is
  resolved; `08-services-platform.tex` describes the service, `04-runtime-
  loop.tex`, `05-rendering-current.tex`, the consensus program section,
  `docs/packages/illed.md` and `IllEd/Source/AGENTS.md` no longer say the
  document borrows the scene's content; `B-file-map.tex` lists the new
  files. `EditorDocument::attach` and the owned-or-borrowed branch of
  `setAssetManager`, which W3 had left declared without a caller, are
  removed. `docs/build.ps1` rebuilt `docs/output/illumo.pdf` without LaTeX
  errors and with no overfull box in the new entry. Full Release
  `IllumoWorkspace` suite on this tree with `CSim/` at `b77cc44`: 784 of
  785 pass in 337 s, no case skipped.
  - The one failure, `IllumoGame.CellGame.ReleaseConfiguration`
    ("fullscreen applies immediately once (got 0, expected 1)"), is in the
    game repository and predates this wave: its `CellGameFixture` uses a
    default `EnvVars` (file `envvars.json` in the working directory) and
    resets `fullscreen` but not the `displayMode` key that engine commit
    `3b4bb478` and CSim commit `b77cc44` (both 2026-10-21) introduced;
    `displayModeFromEnvironment` prefers `displayMode`, so after the first
    run in a build tree the case's own CTest working directory holds
    `displayMode: fullscreen`, the applied mode equals the stored one and
    the null window counts no toggle. It passes once in a fresh directory
    and fails every run after. Fixed afterwards in the game repository
    (uncommitted): `CellGameFixture` now resets `displayMode` to `window`,
    and the case passes three CTest reruns in its previously poisoned
    working directory and two direct runs.
  - The `IllumoGpu` label (not part of the workspace label) was run
    separately on the same build: `IllEd.Gpu.Smoke` and the seven
    OpenGL/Vulkan/D3D12 parity cases pass, so the real package still opens,
    draws and saves through a window after the `attach` removal.
