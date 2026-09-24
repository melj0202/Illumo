# Illumo Services

The static library owns generic logging, environment variables, input, command
registry/console editing, and allocators. `CommandLine` receives its branding
from the application name and owns only generic help, editing, history, alias,
environment, window/presentation, and quit behavior, plus generic debugging
tools: output view filters (`filter`, `loglevel`), `timestamps`, `copy`,
`savelog`, `exec` scripts, F-key `bind`/`unbind`, `!!`/`!n`/`!prefix` recall,
Ctrl+R reverse search, `help <word>` search, `add`/`cycle` setting steps,
`watch`/`unwatch` live variables, `writeconfig`, and the closed-console
`alerts` badge (D-UI4). Output entries record a `ConsoleLevel`, a
timestamp, and a repeat count; identical consecutive lines collapse. The buffer
keeps `MAX_CONSOLE_LINES` (2,048) entries and command recall `MAX_CMD_HISTORY`
(256). Clipboard and file access go through `CommandLineCore` virtual hooks and
are compiled out under `ILLUMO_SERIAL_GUEST`.

`CommandLine` renders through `GameVisual` with its own flat palette rather than
`UiTheme`, so the tool never reads as product UI. Composition is separate from
token submission: attached, primitives go to the Renderer; detached (D-UI5),
the host's `SoftwareCanvas` rasterizes them for a separate window.
`CommandLine` never creates windows; it raises `Detach`/`Dock`/`Close`
requests (`takeWindowRequest`) that `DebugModule` fulfils. History wrap metrics are
cached until contents, view filters, timestamps, or panel width change; hidden
entries wrap to zero lines. Settled console composition is replayed until a
dirty reason fires.

Illumo generic defaults cover window dimensions, fullscreen, VSync, FPS display,
and log level. An explicit configuration path keeps runtime and file-backed tests
independent of the launch directory.

`EnvVars` loads transactionally. Malformed, non-object, or unreadable input
disables saving, including teardown saving, while retaining live values and the
original file. Repair or remove the file and call `load()` successfully to
restore persistence. Missing files allow first-run creation. Diagnostics use
stderr without relying on logger startup/shutdown lifetime.

`CommandRegistry` is a non-copyable, non-movable main-thread owner. Dispatch
detaches one batch; callback-enqueued work waits for the next top-level
`ExecuteQueue()`, and nested dispatch is ignored. Unregistering or replacing a
registration cancels its unstarted callbacks, including detached ones.
`CommandLine` registers `console_mode` and `console_size` with callbacks that
capture the console instance and unregisters both names on destruction so those
callbacks cannot outlive it. Engine shutdown destroys `CommandLine` before
`CommandRegistry`.
`ClearQueue()` cancels pending work and the current batch remainder without
destroying the active callback. Exceptions propagate, discard the remaining
detached batch, and leave newly queued work available to the next dispatch.
`InputManager` is also non-copyable/non-movable because its active context and
scroll storage belong to that instance.

Illumo owns `SysCmdLine` parser mechanics, window flags, help/version dispatch,
and exit results. Its public parser configuration accepts CA option/help data
without introducing Game types. Option value names `path`, `file`, `name` and
`string` take one string; `paths` may repeat and joins its values with
newlines (`IllumoRuntime --mount`); anything else is a positive integer.
Platform owns the public `<Illumo/Platform/SaveLoad.h>` dialog
contract and its native implementations.

`<Illumo/Services/FileTreeSource.h>` defines `IFileTreeSource`, a read-only,
synchronous, main-thread view of a file tree for engine tools: `list` returns
one directory's `FileTreeEntry` children sorted by name, and `stat` returns a
`FileTreeStatus` (directory flag, size, supplying package id). Paths are
normalized absolute `/` paths; core code never learns how the tree is mounted
or where its files live on the host. `IllumoRuntime` publishes its virtual
file tree through `IllumoContext::fileTree` using `Illumo::Content`'s
`VfsTreeSource` (see `engine.md` and `content.md`).

IllumoGame owns CA defaults and `envvars.json`, TPS, speed, fade, ruleset,
canvas, simulation, camera, persistence commands, canvas CLI descriptors, and
dialog labels/default filenames. `CellGameModule` registers domain commands
through `CommandRegistry` and calls SaveLoad without containing native code.

## Allocator alignment

Allocator utilities preserve their four-chunk cap. Arena and stack byte requests
align the actual address; stronger alignment can replace empty backing storage
or allocate a new aligned chunk without moving live allocations. Alignment zero
retains its byte-alignment meaning; other non-power-of-two values are rejected.
Typed pools use storage aligned for their element type, including over-aligned
types. Clear and stack LIFO behavior remain unchanged. Invalid size arithmetic
is rejected before allocation; underlying allocation failures may throw.

## Input registration lifetime

InputManager supports 32 live contexts with reusable storage and non-reused
manager-local long IDs. Registration returns -1 when full or ID space is
exhausted. Invalid activation preserves selection; unregistering the active ID
selects neutral input. Unknown actions are inactive. Modules must unregister
on Exit; rejected startup must not retain a registration.

`WorkerPool` provides a generic owner-thread range dispatcher with explicit
start/stop, one outstanding submission, caller participation in join, and
allocation-free dispatch. Callbacks are noexcept and operate on disjoint
caller-owned ranges; context outlives join, and stop drains and joins workers.
The CA-specific SparseWorkerPool remains separate pending measured migration.

Input callbacks retain at most 256 characters and 256 key events, preserving the
oldest pending events and counting dropped new events. Release queries use the
current frame edge, including mouse buttons; idle keys remain None. Public and
execution argument parsing share one grammar: ordinary backslashes are literal,
Windows drive/UNC paths preserve separators, unquoted non-path tokens can escape
spaces or punctuation, and doubled matching quotes encode quotes inside a quoted
argument. Quoted paths may end with a separator. Empty quoted arguments survive.

Logger retains its process-wide facade with unique ownership and explicit runner
shutdown. Its default file is `log.txt` beside the executable; tests may supply an
explicit filesystem path. File-open failure is reported rather than returning
successful initialization. Messages logged before the first console attaches
are replayed into it once (bounded at `Logger::kStartupBacklogLimit`, with an
omitted-count warning); background threads may log, reaching only the file and
terminal (`Illumo.Logger.StartupBacklog`). `EnvVars` load and save problems go
through Logger rather than raw stderr. CLI default usage appends `.exe` only on Windows;
unknown options retain the existing compatibility behavior.
