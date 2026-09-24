# Split headless tests

| Target | Namespace and responsibility |
|---|---|
| `IllumoTests` | `Illumo.*`: application/host lifecycle, BuildInfo, SysCmdLine, services, allocators, persistent scene hierarchy, public/private rendering, assets, generic console/UI, the content layer (`Illumo.Content.*`: paths, manifests, archives, virtual file tree, `.ilsc`, `SceneInstance`) |
| `IllumoGameTests` | `IllumoGame.*`: CA CLI metadata/configuration, rulesets, topology, sparse simulation, presentation, editor/pattern I/O, commands, persistence |
| `IllEdTests` | `IllEd.*`: editor identity, document model over `SceneInstance`, history, selection, gizmos, inspector, clipboard, hierarchy, assets and project flow, toolbar and Tools panel hits, detachable panels through `FakePanelSurfaces` (`IllEd.Panels.*`), SceneGraph wiring (the `.ilsc` codec is tested in `Illumo.Content.*`) |
| `IllMeshViewerTests` | `IllMeshViewer.*`: viewer camera, configuration, module input, menus, the Info and Display panels docked and detached (`IllMeshViewer.Panels.*`), and `.ilsc` scenes from the file tree |
| `IllumoPublicHeaderSmoke` | Consumer-only compile/link smoke using no private source include paths |

All four runners support `--list` and exact `--run`. CMake discovers each logical
case into a per-configuration file and CTest runs it in an isolated working
directory. The aggregate `IllumoWorkspace` label covers the library, IllumoGame,
IllEd, IllMeshViewer, and the public-header smoke. Standalone Illumo tests also
carry both the `Illumo` and `IllumoWorkspace` labels.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure
```

Combined LLVM coverage uses a generated manifest of every registered workspace
runner, depends on their discovery targets, and preserves the 85% production-line
gate. This includes all four in-tree runners and follows the selected runners
in generated workspaces. Workspace `clang-tidy` runs on first-party C++ during the default build
(`ILLUMO_ENABLE_CLANG_TIDY` defaults to ON) and also as the batch
`IllumoTidy` target (`python build.py tidy`). Disable compile-time linting with
`-DILLUMO_ENABLE_CLANG_TIDY=OFF` or `python build.py build --no-tidy`. Headless
success does not replace live Windows OpenGL or native-dialog smoke tests.
