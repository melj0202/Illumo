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

Illumo owns `SysCmdLine` parser mechanics, window flags, help/version dispatch,
and exit results. Its public parser configuration accepts CA option/help data
without introducing Game types. Illumo also owns the public `SaveLoad` dialog
contract; concrete native implementations live in Platform.

IllumoGame owns CA defaults and `envvars.json`, TPS, speed, fade, ruleset,
canvas, simulation, camera, persistence commands, canvas CLI descriptors, and
dialog labels/default filenames. `CellGameModule` registers domain commands
through `CommandRegistry` and calls SaveLoad without containing native code.
