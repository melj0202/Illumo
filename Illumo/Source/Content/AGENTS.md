# Content subsystem guidance

This file specializes the repository `AGENTS.md` for `Illumo/Source/Content/`
(`Illumo::Content`, public headers under `Illumo/Include/Illumo/Content/`).
The design is `docs/content-packages-and-scenes-design.md`; decisions D-E18 to
D-E24 and D-E26.

## Scope and boundaries

- Content sits above the engine: it depends on `Illumo::Illumo` only. Core
  Illumo never includes `<Illumo/Content/...>`, and Content never includes
  `Illumo/Wasm`, the guest SDK, or a product.
- nlohmann stays private to `.cpp` files; public headers carry opaque JSON as
  canonical text.
- The guest mirror `IllumoGuestContent` (`IllumoGuest/CMakeLists.txt`) builds
  the serial-safe subset: `VirtualPath`, `PackageManifest`, `SceneDocument`,
  `IlscCodec`, `SceneAssetRefs`, `SceneInstance`, `ScenePrimitiveMeshes`.
  `PackageArchive`, `VirtualFileSystem`, `VfsAssetSource`, `VfsConsole`,
  `VfsTreeSource` and `PackageMounts` are host-only. Keep a new source in the
  right list.

## Invariants

- Every path is validated by `VirtualPath` (one grammar, case-sensitive
  components, no `..`, drive letters, backslashes, device names or
  `.illumo-*`). Never build a host path from guest input without it.
- Archives are untrusted: keep every `PackageArchiveLimits` bound, the CRC
  check on full reads, and the name/collision rejections. Inflate is bounded
  by the declared size.
- Mount tables are immutable snapshots swapped under the mutex; a `VfsFile`
  keeps its backend alive. Only a writable directory mount accepts writes, and
  it refuses `*.wasm` and `illumo.json`. `stat` may name a package id, never a
  host path.
- `.ilsc` is format 2 only. Core components are strict (unknown keys fail);
  namespaced components and `extensions` are preserved verbatim. Output is
  canonical (fixed key order, shortest round-trip floats), so a load/save
  round trip is byte-stable.
- `SceneInstance` builds graphs only through the public `SceneGraph` API,
  calls `invalidateSnapshots()` before reconfiguring attachments, and shares
  one immutable unit mesh per primitive kind. Missing assets become
  placeholders with a warning, never a failed load.
- Loads collect references (`collectSceneFetches`), make them readable, then
  instantiate: `IAssetSource` reads stay synchronous.
- No recursion: tree walks, dependency order (Kahn) and flattening are
  iterative.

## Verification

`Illumo.Content.*` in `Illumo/Tests/Content/`, `Illumo.Wasm.*` for the host
file services, `Illumo.Pack.StagedApp` for the staged packages, and the
package tests of each app. Benchmarks: `Illumo.Content.Bench.Instantiate2000`
and `Illumo.Wasm.Bench.FileThroughput`.
