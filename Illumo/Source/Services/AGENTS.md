# Services subsystem guidance

This file specializes the repository `AGENTS.md` for
`Illumo/Source/Services/`.

## Scope and boundaries

Services provides input collection, console editing and command dispatch,
logging, environment persistence, command-line parsing, allocator utilities,
and the platform save/load declarations. Services must remain reusable by
Engine and modules without depending on Game, Rulesets, or concrete OpenGL.

- General console commands belong in `CommandLine`; domain commands are
  registered by their owning module through `CommandRegistry`.
- Native dialogs and filesystem UI belong in `Platform/`; Services owns only
  the cross-platform declaration and orchestration contract.
- Use backend-neutral Rendering types for console drawables.

## Ownership, lifetime, and concurrency

- Service objects are Engine-owned. Logger sinks, command callbacks, input
  contexts, and non-owning pointers must be detached before their targets die.
- Input callbacks enqueue events; consumers drain them on the main thread.
  Bound or deliberately coalesce queues so a build without a consumer cannot
  grow memory indefinitely.
- Validate context identifiers before indexing. Registration failure must be
  observable and must not become an out-of-bounds active context.
- Command queuing and execution are main-thread operations. If callbacks may
  queue more commands, execution must not invalidate the container currently
  being iterated.
- Owning allocators and queues must explicitly prohibit or implement copying
  and moving. Preserve capacity, alignment, destruction, and overflow
  semantics.

## Parsing, persistence, and errors

- Keep public argument syntax, console quoting/escaping, completion, and help
  mutually consistent. Windows paths must survive the same parser used for
  command execution.
- Reject empty or out-of-range numeric input before conversion. Do not let
  invalid dimensions or indices reach window, input, or allocation APIs.
- `envvars.json` lives beside the executable. A malformed or unreadable file
  must not be silently replaced during teardown unless recovery is an explicit
  user action.
- Logging and diagnostics must not throw through shutdown paths. Report I/O,
  parse, queue overflow, and registration failures at the boundary that can
  act on them.
- Console token payload storage must remain valid through renderer submission.
  Keep the existing token-rendered console; do not introduce an independent
  retained widget hierarchy.
- Cached console wrap results must stay equivalent to direct wrapping of the
  same history at the same width. Settled composition cache must not skip a
  frame that changed history, scroll, input, or settled layout.

## Documentation and verification

- `docs/packages/services.md`
- command/input/config sections in `docs/architecture-consensus.md`
- `TestServices.cpp`, `TestAllocators.cpp`, and console token tests

Use exact tests for quoting, backslashes, empty values, overflow, callback
reentrancy, registration exhaustion, malformed configuration, and teardown.
Console interaction and native dialogs still need Debug GUI smoke tests.
Update this file only for durable Services rules.
