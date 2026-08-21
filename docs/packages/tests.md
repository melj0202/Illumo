# Split headless tests

| Target | Namespace and responsibility |
|---|---|
| `IllumoTests` | `Illumo.*`: application/host lifecycle, BuildInfo, SysCmdLine, services, allocators, public/private rendering, assets, generic console/UI |
| `IllumoGameTests` | `IllumoGame.*`: CA CLI metadata/configuration, rulesets, topology, sparse simulation, presentation, editor/pattern I/O, commands, persistence |
| `IllumoPublicHeaderSmoke` | Consumer-only compile/link smoke using no private source include paths |

Both runners support `--list` and exact `--run`. CMake discovers each logical
case into a per-configuration file and CTest runs it in an isolated working
directory. The aggregate `IllumoWorkspace` label covers both inventories and
the public-header smoke; the standalone Illumo configuration exposes only the
`Illumo` label.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure
```

Combined LLVM coverage runs both runners and preserves the 85% production-line
gate. Headless success does not replace live Windows OpenGL or native-dialog
smoke tests.
