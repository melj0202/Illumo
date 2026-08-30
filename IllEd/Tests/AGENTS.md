# IllEd tests guidance

This file specializes the repository `AGENTS.md` for `IllEd/Tests/`.

IllEd tests own application identity, `.ilsc` codec behavior, the editor
document model, toolbar hit testing, and module graph wiring. Register every
logical behavior as an exact `IllEd.<area>.<case>` and keep `--list`, exact
`--run`, and CTest discovery synchronized.

Tests must be headless, deterministic, process-isolated under
`build/Testing/IllEd/`, and independent of ambient user configuration. Use
`Illumo::TestSupport` for MockBackend fixtures. Do not compile IllumoGame
sources into this runner.

```powershell
cmake --build build --config Release --target IllEdTests
ctest --test-dir build -C Release -L IllEd --output-on-failure
build/Release/IllEdTests.exe --list
build/Release/IllEdTests.exe --run <exact-test-name>
```

The aggregate workspace label is `IllumoWorkspace`. Combined coverage does not
include IllEd yet.
