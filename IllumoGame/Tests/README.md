# IllumoGame headless tests

`IllumoGameTests` registers each `IllumoGame.*` behavior as an isolated CTest
process under `build/Testing/IllumoGame/`. The runner supports `--list`, exact
`--run`, and generated CTest discovery.

```powershell
cmake -S . -B build -DILLUMO_BUILD_DOCUMENTATION=OFF
cmake --build build --config Release --target IllumoGameTests
build/Release/IllumoGameTests.exe --list
build/Release/IllumoGameTests.exe --run IllumoGame.CellGame.SaveLoadRoundTrip
ctest --test-dir build -C Release -L IllumoGame --output-on-failure
```

Workspace coverage is configured from the repository root and merges this
runner with `IllumoTests` before applying the 85% production-line gate. Native
dialogs, real-window behavior, and live OpenGL still require Windows smoke
tests.
