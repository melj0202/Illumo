# Illumo Services

The static library owns generic logging, environment variables, input, command
registry/console editing, and allocators. `CommandLine` receives its branding
from the application name and owns only generic help, editing, history, alias,
environment, window/presentation, and quit behavior. It renders through
`GameVisual` and the value-only `UiTheme`. History wrap metrics are cached until
contents or panel width change; settled console composition is replayed until a
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
`ClearQueue()` cancels pending work and the current batch remainder without
destroying the active callback. Exceptions propagate, discard the remaining
detached batch, and leave newly queued work available to the next dispatch.
`InputManager` is also non-copyable/non-movable because its active context and
scroll storage belong to that instance.

Illumo owns `SysCmdLine` parser mechanics, window flags, help/version dispatch,
and exit results. Its public parser configuration accepts CA option/help data
without introducing Game types. Illumo also owns the public `SaveLoad` dialog
contract; concrete native implementations live in Platform.

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
