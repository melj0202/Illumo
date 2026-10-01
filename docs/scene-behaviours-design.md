# Scene behaviours and Play in a window (design)

Tier 3 design (`.agent/PLANS.md`), drafted 2026-10-01 on `release/v26.10`.
The owner approved the direction on 2026-10-01: game code stays in each
game's own WebAssembly module and attaches to scene nodes, and IllEd gets a
Play button that runs the scene in the game. This document fixes the shape
and asks for sign-off before implementation.

## 1. Problem

Scenes (`.ilsc` format 2) carry content only. Behaviour lives in C++
`ProgramScene` code that knows its scene by node id, so a designer cannot put
"this crate spins" or "this door opens" on a node in IllEd. The editor also
cannot run what it edits; the owner wants a Unity-style Play.

Two earlier decisions stand in the way and are reopened by this design:

- `docs/scene-programs-design.md` §4 and §8: "No scripting or data-driven
  behaviour… Scenes as data (`.ilsc` plus behaviour bindings)… needs a
  behaviour-binding or scripting design first. Rejected for now." This is
  that design.
- `docs/illed-editing-wave2-plan.md`: play mode was out of scope for wave 2.

The charter already asks for the pieces (`docs/charter-direction.md`:
"registered game definitions with validated values/actions, authoring
context" and "editor save/load of registered objects into a separately
launched application").

## 2. End state

1. A game registers behaviour types in its own WASM module
   (`"mygame.spinner"` → a C++ class). Loading a scene attaches one behaviour
   instance to every node carrying a component of that type, and the
   behaviour gets start, per-frame update and stop calls with typed access to
   its component's values and its node.
2. The game package ships `behaviours.json`, describing each behaviour's
   fields. IllEd reads it and shows typed inspector fields and an Add
   Behaviour list instead of read-only JSON.
3. IllEd's Play (Ctrl+P) starts the game in its own window on a copy of the
   scene being edited; Stop (Ctrl+P again, or closing that window) ends it.
   The edited document never changes.
4. In-viewport play (pause, edit while paused, reset on Stop) is a later
   phase with its own design; nothing here blocks it.

## 3. Non-goals

- No interpreted language (Lua, JavaScript, C#) and no visual scripting.
  WebAssembly already gives sandboxing, reload and language choice.
- No WASM module per object. One module per game; behaviours are classes in
  it. (Per-object modules would mean one sandbox, memory and boundary
  crossing per object per frame.)
- No `.ilsc` format change: behaviours use the existing namespaced
  ("opaque") components, so every program keeps reading every scene.
- No hot reload inside a running game; rebuild and press Play again.
- No in-viewport play, pause or paused-edit sync in this design.

## 4. Design

### 4.1 Binding: components name behaviours

A behaviour is a namespaced component type, `<namespace>.<name>`, exactly as
`.ilsc` already stores it (`SceneOpaqueComponent{type, data}`):

```json
{ "type": "playground.spinner", "speed": 2, "axis": [0, 1, 0] }
```

A node may carry several behaviours (one per type, as today). Programs that
do not know a type keep it verbatim, as now.

### 4.2 Engine side (`Illumo::Content`, mirrored in `IllumoGuestContent`)

- `SceneBehaviour`: base class with `start`, `update(double elapsed)`,
  `stop` and `valuesChanged`. Each instance gets a `BehaviourContext`: its
  node id, the `SceneInstance` (read transforms and records, set transforms
  and flags through the normal edit calls), its typed values, and a deferred
  queue for structural edits (create, destroy, reparent), applied after the
  update pass so the walk never sees a half-edited graph.
- `BehaviourRegistry`: `add("playground.spinner", factory)`; mirrors the
  `SceneDirector::emplace` factory pattern. Unknown types are skipped with
  one warning per type.
- `BehaviourSchema`: the decoded `behaviours.json` (§4.3). The registry
  checks code against it at startup: a registered type missing from the
  file, or a file type with no factory, logs a warning. Values are decoded
  from component JSON through the schema, with defaults for missing fields;
  invalid values fall back to defaults with a warning, never a failure.
- `SceneBehaviours`: the driver. A `ProgramScene` opts in by owning one over
  its `content()`. It attaches and detaches behaviours as nodes appear,
  disappear or change their components, runs `update` in scene preorder
  once per frame, and stops everything on scene stop.
- `SceneInstance` gains a narrow observer (`ISceneContentObserver`:
  `nodeAdded`, `nodeRemoving`, `componentsChanged`, `replaced`) called from
  `load`, `clear`, `insertNode`, `removeSubtree`, `replaceNode` and
  `setComponents`. Today there is no hook besides `revision()`. Transform
  and flag edits do not notify, so behaviours moving nodes cause no churn.
  Main-thread affine, like the rest of `SceneInstance`.

### 4.3 Field descriptions: `behaviours.json`

A file in the game package root, beside `illumo.json` (the strict manifest
decoder is left alone):

```json
{
  "format": "illumo-behaviours",
  "format_version": 1,
  "behaviours": [
    { "type": "playground.spinner", "title": "Spinner",
      "fields": [
        { "name": "speed", "kind": "number", "default": 1, "min": -50, "max": 50 },
        { "name": "axis", "kind": "vector3", "default": [0, 1, 0] } ] }
  ]
}
```

Field kinds: `number`, `integer`, `bool`, `text`, `color`, `vector3`,
`choice` (with `options`), `asset` (with asset types) and `node` (a node id
in the same scene). Strict decoding with clear errors, like `IlscCodec`;
owned by `Illumo::Content`.

### 4.4 IllEd

- Discovers schemas from `/apps/*/behaviours.json` (installed applications,
  mounted read-only for an app whose manifest sets `launchApps`, §4.5),
  `/packages/*/behaviours.json` and `/project/behaviours.json` through
  `IllEdPlatform::listDirectory`/`read`.
- The inspector shows a known behaviour component as a section of typed
  fields (the existing Number, Toggle, Choice, Text kinds plus the asset
  choice from wave 2); edits stay one history command each through
  `editNodes`. Unknown types stay read-only JSON.
- Add component gains a Behaviour list from the discovered schemas,
  inserting each field's default.
- `node` fields use the asset-choice pattern over node names; pasting
  remaps ids inside behaviour data for `node` fields (closing part of the
  "ids inside opaque data" follow-up for typed fields).

### 4.5 Play in a window

- The scene names its game with a scene extension
  `"illumo.play": { "app": "<package id>" }`, set from the scene inspector
  (a choice among discovered games). Extensions are already preserved.
- **Host:** a new capability `Launch` (bit 13) and guest service
  `LaunchApp` (id 19), generic and product-agnostic: "start installed app
  `<id>` with this document". Only an application whose manifest sets
  `"app": { "launchApps": true }` gets it (IllEd); the runtime then also
  mounts every installed application read-only at `/apps/<id>`, which is
  how the editor reads their `behaviours.json`. (Every offered capability is
  granted, so the manifest flag, not the guest's request, is the gate.) The
  host (`RuntimeAppLauncher`) writes the bytes to a private temporary play
  directory, then starts a second `IllumoRuntime` process
  (`--app <id> --open <file>`, plus the parent's `--mount` and `--project`
  options so asset references resolve the same way). It keeps the child
  (`ChildProcess`, `Illumo/Platform/ChildProcess.h`; on Windows a
  kill-on-close job object, so the child never outlives the parent): one
  child at a time, `Stop` terminates it, and the guest polls its state
  (`GuestLauncher`, `IllumoGuest/Launcher.h`). Not offered in capture or
  benchmark runs (like Windows and Audio). Decoder and deny tests ship with
  it, per the repository rules.
- **Game:** `GuestPlayProgram` and its `GuestPlayScene`
  (`IllumoGuest/PlayProgram.h`) load the launch document, or the app's
  default scene, into `content()` (collect, fetch, instantiate, as
  IllMeshViewer does), own a `SceneBehaviours`, and use the scene's primary
  camera. A game supports Play by deriving from `GuestPlayProgram` and
  registering its behaviours.
- **IllEd:** Play/Stop in the View menu, Ctrl+P (Unity's binding; the host
  already takes F5 for asset reloads), a status-bar state,
  and a toast when the scene names no game or the host refuses.

### 4.6 First consumer

A small sample app, `playground` (`apps/playground`, `Playground.wasm`):
`PlayScene` plus three behaviours (spinner, bob, orbit-around-node) and its
`behaviours.json`. It proves the path end to end and is the template for
real games. CSim is unchanged.

## 5. Alternatives considered

- **Embedded interpreter (Lua or JavaScript).** A second language, a new
  third-party dependency and a second binding of the engine API. Rejected.
- **A WASM module per object.** Rejected (§3).
- **Schemas in `illumo.json`.** Needs the strict manifest decoder and its
  tests to change for app-specific data; a separate file keeps the manifest
  stable. Rejected.
- **Schemas exported by running the game's code.** Would need the editor to
  instantiate the game module at authoring time. Rejected for now; the file
  is the single source and code is checked against it.
- **Play inside IllEd's own process.** The host runs exactly one program;
  hosting two needs a separate design (phase 2).

## 6. Compatibility, ownership and risk

- No `.ilsc`, frame schema or existing ABI change. New: one capability bit,
  one service id, one package file kind, public Content classes.
- `SceneInstance` stays the only scene model; behaviours edit through it.
- Behaviours run in the game's own store, so a faulting behaviour stops the
  game window only, never the editor.
- Child processes: one at a time, terminated on Stop and when the parent
  exits; the play file lives in a temporary directory removed with the
  launcher.
- Risk found and fixed in B2: `--open` did nothing for package launches
  (`WasmProgram` granted the launch document only without a virtual file
  tree), which is why it did not load in the wave 2 screenshot runs.

## 7. Milestones

| # | Milestone |
|---|---|
| B1 | Content: observer, `SceneBehaviour`, registry, schema decoder, `SceneBehaviours` driver; native tests |
| B2 | `PlayScene` and the `playground` sample app with three behaviours; package test |
| B3 | IllEd: schema discovery, typed behaviour fields, Add Behaviour, `node` id remap on paste |
| B4 | Host: `Launch` capability, `LaunchApp` service, process spawn, decoder and deny tests |
| B5 | IllEd Play/Stop (Ctrl+P), scene "Play with" choice; end-to-end package test |
| B6 | Docs: decision log (D-E35 behaviours, D-E36 launch), architecture-consensus, LaTeX, package maps |

Each milestone builds and passes the Release workspace suite before the next.

## 8. Decisions

The owner authorized implementation as written on 2026-10-01 and chose:

1. `behaviours.json` as a separate package file.
2. The play child gets the parent's `--project` (same access as launching
   it by hand).
3. A new `playground` sample app as the first consumer; CSim unchanged.

## 9. Validation

- B1 (1e3df684): `Illumo.Content.BehaviourSchemaParse`, `BehaviourValues`
  and `SceneBehavioursLifecycle`; Release workspace suite green.
- B2 (aa31e199): `Playground.Wasm.DefaultScene` (seven nodes, four
  behaviours, the moon orbits the beacon at its radius) and
  `Playground.Wasm.LaunchedScene` (a launched scene's bob moves its node; an
  unknown component is ignored); 722 of 722 workspace tests pass.
- B4 (596b871f): `Illumo.Wasm.LaunchServiceDecoder`, `LaunchDeny`,
  `GuestLauncher`, `ChildProcess` (this test program relaunched as a
  sleeping child, stopped through its job object) and `RuntimeAppLauncher`;
  manifest `launchApps` and the `/apps` mount in
  `Illumo.Content.PackageManifestDecode` and `PackageDiscovery`; 727 of 727.
- B3 (3d48aebe): `IllEd.Behaviours.InspectorFields`, `CopiesRemap` and
  `Discovery`; 730 of 730.
- B5: `IllEd.Module.PlayScene` (game choice and inference, launch document,
  stop, a closed game window), `IllEd.Wasm.PlayPackage` (the real IllEd.wasm
  reads `/apps/playground/behaviours.json` and hands a recording launcher the
  scene with its game and root) and `Illumo.Content.ScenePlayExtension`; 733
  of 733. Real runtime: `--app playground` renders the demo and
  `--app playground --open <file>` (the child's exact command) plays it; IllEd
  shows the demo moon's Orbit behaviour as typed fields (a node choice, radius,
  degrees) with Spinner, Bob and Walker under Add behaviour. Play inside the
  editor's own process remains out of scope (§5).
