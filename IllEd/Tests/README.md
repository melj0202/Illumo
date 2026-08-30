# IllEd headless tests

`IllEdTests` registers each `IllEd.*` behavior as an isolated CTest process
under `build/Testing/IllEd/`. The runner supports `--list`, exact `--run`, and
generated CTest discovery.

```powershell
cmake --build build --config Release --target IllEdTests
build/Release/IllEdTests.exe --list
build/Release/IllEdTests.exe --run IllEd.Ilsc.RoundTrip
ctest --test-dir build -C Release -L IllEd --output-on-failure
```

Native dialogs, the live OpenGL window, and toolbar pixels still require a
Windows smoke test.
