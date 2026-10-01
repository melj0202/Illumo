# Illumo application runtime

Illumo owns process-level system behavior:

- `Illumo/Source/Engine/Application.cpp` initializes logging, applies the
  consumer's defaults callback, invokes engine `SysCmdLine`, logs the startup
  report, fallibly initializes the host, calls the definition's `run`
  callback, performs shutdown, relaunches when the application asked to, and
  returns an explicit process code.
- `Illumo/Source/Platform/<port>/` supplies the selected entry point and native
  dialogs.
- `Illumo/Include/Illumo/Engine/Application.h` is the narrow reverse seam. A
  consumer defines `CreateIllumoApplication()` and returns only declarative
  identity, CLI metadata, defaults, and its `run` callback
  (`int (*)(Illumo&, std::chrono::steady_clock::time_point launched)`), which
  runs the frame loop on the initialized engine and returns the exit code.

`IllumoRuntime` (`Illumo/Source/Wasm/RuntimeApplication.cpp`) is the only
definition; there are no native product definitions (D-E31). Its `run` builds
a `RuntimeShell` (`Illumo/Include/Illumo/Wasm/RuntimeShell.h`) around one
`WasmProgram` and runs it: the shell starts the program and, in debug-tool
builds, the `DebugOverlay`, drives the paced `std::chrono::steady_clock` frame
loop, and stops both (see `engine.md`). The overlay stays active across the
program's scene switches, which happen inside the guest.

The runtime launches one `app` package described by `illumo.json`: `--app <name>` resolves `apps/<name>/` or
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
