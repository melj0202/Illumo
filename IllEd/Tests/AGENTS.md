# IllEd tests guidance

This file specializes the repository `AGENTS.md` for `IllEd/Tests/`.

IllEd tests own application identity, the editor document model, menu bar,
Tools panel and dock panel behaviour (docked and, through
`FakePanelSurfaces`, detached), and module graph wiring. Register every
logical behavior as an exact `IllEd.<area>.<case>` and keep `--list`, exact
`--run`, and CTest discovery synchronized.

Tests must be headless, deterministic, process-isolated under
`build/Testing/IllEd/`, and independent of ambient user configuration.
`EnvVars` loads and saves `envvars.json` in the working directory, so module
fixtures give it a fresh file in the temp directory: saved toggles and panel
layouts must never leak between cases. Use
`Illumo::TestSupport` for MockBackend fixtures. Do not compile IllumoGame
sources into this runner.

```powershell
cmake --build build --config Release --target IllEdTests
ctest --test-dir build -C Release -L IllEd --output-on-failure
build/Release/IllEdTests.exe --list
build/Release/IllEdTests.exe --run <exact-test-name>
```

The aggregate workspace label is `IllumoWorkspace`. Combined workspace coverage
builds this runner, refreshes discovery, and includes its production objects.
