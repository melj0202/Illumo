# Tracy profiling

Tracy shows where a frame's time goes across the host, the engine, and the
WASM guests the applications run in, including IllumoGame's simulation lanes.
The in-game pie ([frame-profiler.md](frame-profiler.md)) answers "which phase
is slow"; Tracy answers "which function, on which thread, in which frame".

## Build and run

The `tracy` build profile is Release with Tracy compiled into `IllumoRuntime`
and into every guest package:

```powershell
python build.py play --profile tracy --app game
```

It is equivalent to configuring with `-DILLUMO_ENABLE_TRACY=ON` (build
directory `build-workspace-tracy`, no documentation or clang-tidy). Then start
the Tracy profiler GUI, version **0.14.1** to match the vendored client, and
connect to `IllumoRuntime` (it is listed under localhost). The client records
from launch and holds data until the GUI connects, so startup and package
compilation are included.

Tracy's tools are not vendored; put the 0.14.1 Windows release in
`tools/tracy/` (its executables are Git-ignored). A mismatched version refuses
the connection (`incompatible protocol version`).

Debug builds also define `TRACY_ENABLE`, in on-demand mode (they record only
while a GUI is connected), but only for host code: guests are always compiled
Release and get profiling markers only from `ILLUMO_ENABLE_TRACY`. Debug is
the AddressSanitizer profile, so its timings are not representative.

Package benchmarks and the `--bench-frames` mode run the same zones; combine
them for reproducible captures:

```powershell
build-workspace-tracy\Release\IllumoRuntime.exe --app game --bench-frames 600
```

## What the capture contains

- **Main thread**: `Frame.Update`, `Frame.Render` and `Frame.Pacing` under
  each frame mark, then the WASM program's exchange (`Wasm.GuestUpdate`,
  `Wasm.GuestFrame`, `Wasm.FrameAccept`, services) and host rendering down to
  `GLBackend.SubmitCommandQueue` and `GLBackend.swapBuffers`.
- **Guest zones**, drawn in one green shade so they stand apart from host
  zones, nest inside the host call that ran them: the guest program's update
  and frame, scene update and draw, recording and serializing the frame, and
  IllumoGame's canvas, menus, runner and sparse-grid phases.
- **Wasm worker N** threads: one per simulation lane (`CSimWorkerGuest.wasm`),
  each `WasmWorker.job` containing the lane's own guest zones.
- **Plots** for per-frame and per-generation counts (frame bytes, uploads,
  chunk counts and similar), and named frame marks `Sim.inFlightDeferred` and
  `Sim.debtDropped` for simulation steps that were deferred or dropped.

The simulation runs beside the frame: a generation on lanes appears on the
worker threads while the main thread keeps drawing, and the control store's
merge appears inside the game's update.

## Adding markers

Include `<Illumo/Foundation/Profile.h>`; it works unchanged in host, engine,
guest and product code:

| Macro | Effect |
|---|---|
| `ILLUMO_PROFILE_ZONE("Class.method");` | Zone for the enclosing block |
| `ILLUMO_PROFILE_PLOT("Name", value);` | One plot sample |
| `ILLUMO_PROFILE_FRAME_MARK("Name");` | Named discontinuous frame mark |
| `ILLUMO_PROFILE_THREAD(name);` | Names the current host thread |

Names are string literals. Natively the macros are Tracy's `ZoneScopedN`,
`TracyPlot`, `FrameMarkNamed` and `SetThreadName`; without `TRACY_ENABLE` (or,
in a guest, without `ILLUMO_GUEST_PROFILE`) they compile to nothing. Use them
instead of including `<tracy/Tracy.hpp>` so a marker also works inside
guests.

Keep zones around meaningful work. A guest zone costs two host calls (on the
order of 100 ns in total), so zone a loop, not each iteration of a per-cell,
per-vertex or per-glyph loop. A zone declares variables: inside a `case`
label, wrap it in braces.

## Guest bridge

A Tracy client cannot run inside a WASM sandbox, so guests report markers to
the host through five imports in the `illumo_profile` module, defined by
`WasmProfile.cpp` for every `WasmInstance`:

| Import | Signature | Effect |
|---|---|---|
| `register` | `(kind, name, nameLength, function, functionLength, file, fileLength, line) -> site` | Registers one static site; kind 0 zone, 1 plot, 2 frame mark |
| `zone_begin` | `(site)` | Begins a zone |
| `zone_end` | `()` | Ends the innermost zone |
| `plot` | `(site, f64 value)` | Plots a value |
| `frame_mark` | `(site)` | Emits a named frame mark |

Each marker site registers once, through a function-local static, and later
calls pass only its index. The host copies the site's text out of guest memory
during `register` and interns it in a process-wide table, because Tracy keeps
source-location and name pointers for the rest of the process; identical sites
from reloaded guests or parallel workers share one entry. Events are emitted
on the thread running the guest, so lane workers get their own rows.

Bounds and failure handling:

- Text spans are checked against linear memory and limited to 512 bytes; a bad
  span, an unknown kind or an empty name returns -1 and records nothing.
- An instance registers at most 4096 sites, and the process interns at most
  16,384 sites and 4 MiB of text; past either bound `register` returns -1.
- Zones nest at most 256 deep; deeper begins are counted so their ends still
  pair. Unknown or wrong-kind site ids and unmatched ends are ignored.
- Zones a guest call leaves open, after a trap, a deadline or an unbalanced
  guest, are closed when the call returns, so the thread's zone stack stays
  balanced.
- A host built without Tracy still defines the imports; `register` returns -1
  there, so a profiling guest makes no further calls.

`Illumo.Wasm.ProfileImports` covers linking, range and kind validation, the
registration bound, unbalanced and over-deep zones, and a trap inside a zone.

## Limitations

- Guest zones measure elapsed host time around guest code, including the
  import calls themselves; very small zones are dominated by that overhead.
- Plots and frame marks from guests use interned names shared by every guest
  that registers the same site.
- GPU execution is not profiled; `GLBackend.swapBuffers` includes driver
  waits. No GPU zones or forced synchronization are added.
