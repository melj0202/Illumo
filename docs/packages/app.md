# Illumo application runtime

Illumo owns process-level system behavior:

- `Illumo/Source/Engine/Application.cpp` initializes logging, applies the
  consumer's defaults callback, invokes engine `SysCmdLine`, fallibly
  initializes the host, registers the consumer's required module and optional
  Debug `DebugModule` overlay, drives the `std::chrono::steady_clock` frame loop,
  performs shutdown, and returns an explicit process code. The overlay remains
  active across main-menu and canvas transitions.
- `Illumo/Source/Platform/<port>/` supplies the selected entry point and native
  dialogs.
- `Illumo/Include/Illumo/Engine/Application.h` is the narrow reverse seam. A
  consumer defines `CreateIllumoApplication()` and returns only declarative
  identity, CLI metadata, defaults, and its required module factory.

Each in-tree product implements that definition: IllumoGame in
`IllumoGame/Source/Game/IllumoGameApplication.cpp`, IllEd in
`IllEd/Source/IllEdApplication.cpp`, and IllMeshViewer in
`IllMeshViewer/Source/IllMeshViewerApplication.cpp`. These contain product policy only — no
process loop, logger lifetime, platform SDK code, or system parser.

`IllumoRuntime` (`Illumo/Source/Wasm/RuntimeApplication.cpp`) is the generic
application definition for WASM packages. It launches one `app` package
described by `illumo.json`: `--app <name>` resolves `apps/<name>/` or
`apps/<name>.ilpk` beside the runtime (`game` by default), and `--package`
takes a package directory or `.ilpk` instead (`--app` cannot be combined with
`--package` or `--game`). Every package in `packages/` beside the runtime is
discovered and mounted at `/packages/<id>`; `--mount <dir|.ilpk>` (a
repeatable `paths` SysCmdLine option) adds more, and `--project <dir>` mounts
a writable `/project`. A missing mount, project or package refuses to start,
as do `--mount` and `--project` with `--game`. Module and worker bytes are
read from `/app` through the virtual file tree (`PackageMounts`, see
`content.md`), and the host registers the `vfs` console command when a tree
exists. Guests reach the tree through file protocol v2 (`GuestFileRequest`
version 2 in `IllumoGuest/FileProtocol.h`): the `Mounted` area takes absolute
virtual paths (`Package` stays an alias of `/app`), and `List`, `Stat`,
`Import` and `Pack` join the transfer actions. Reading and listing mounted
paths need `Assets`; project writes, `Import` and `Pack` need the
`ProjectFiles` capability, which the host offers only when `/project` is
mounted. `../wasm-game-runtime-design.md` records the protocol limits.
