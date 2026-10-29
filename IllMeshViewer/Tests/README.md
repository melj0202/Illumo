# IllMeshViewer Tests

Automated headless test runners for IllMeshViewer, which ships only as the
`IllMeshViewer.wasm` package run by `IllumoRuntime --app meshviewer`.

- `IllMeshViewerTests` runs the native `IllMeshViewerCore` oracle cases.
- `IllMeshViewerWasmPackageTests` runs `IllMeshViewer.Wasm.Package`, which
  drives the real package through the generic host (launch mesh as one
  retained host mesh, skybox cubemap preloaded from `/engine`). Built only
  with the WASM runtime (default on Windows x64, opt-in on Linux x86_64).

Run focused tests:
```powershell
build/Release/IllMeshViewerTests.exe --list
build/Release/IllMeshViewerTests.exe --run <IllMeshViewer.exact-name>
ctest --test-dir build -C Release -R IllMeshViewer.Wasm.Package --output-on-failure
```
