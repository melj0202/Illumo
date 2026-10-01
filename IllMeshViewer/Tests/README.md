# IllMeshViewer Tests

Automated headless test runners for IllMeshViewer, which ships only as the
`IllMeshViewer.wasm` package run by `IllumoRuntime --app meshviewer`.

- `IllMeshViewerTests` runs the native `IllMeshViewerCore` oracle cases.
- `IllMeshViewerWasmPackageTests` runs `IllMeshViewer.Wasm.Package`, which
  drives the real package through the generic host (launch mesh as one
  retained host mesh, package-preloaded skybox cubemap). Windows x64 only.

Run focused tests:
```powershell
build/Release/IllMeshViewerTests.exe --list
build/Release/IllMeshViewerTests.exe --run <IllMeshViewer.exact-name>
ctest --test-dir build -C Release -R IllMeshViewer.Wasm.Package --output-on-failure
```
