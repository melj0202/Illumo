# IllumoGame tests guidance

This file specializes the repository `AGENTS.md` for `IllumoGame/Tests/`.

IllumoGame tests own rulesets, simulation, CA configuration, persistence,
product commands, and game/render boundaries. Register every logical behavior
as an exact `IllumoGame.<area>.<case>` and keep `--list`, exact `--run`, and
CTest discovery synchronized.

Tests must be headless, deterministic, process-isolated under
`build/Testing/IllumoGame/`, and independent of ambient user configuration.
Use `Illumo::TestSupport` for MockBackend and generic fixtures. Do not compile
library test sources into this runner or reach through broad Illumo private
include roots; a narrowly named private implementation header is acceptable
only for a focused implementation-policy test.

Use deterministic SaveLoad replacements in automation. Native dialogs, real
OpenGL pixels, window lifecycle, and unsupported platforms require separate
manual smoke evidence.

```powershell
cmake --build build --config Release --target IllumoGameTests
ctest --test-dir build -C Release -L IllumoGame --output-on-failure
build/Release/IllumoGameTests.exe --list
build/Release/IllumoGameTests.exe --run <exact-test-name>
```

The aggregate workspace label is `IllumoWorkspace`; combined coverage retains
the repository's 85% production-line gate.
