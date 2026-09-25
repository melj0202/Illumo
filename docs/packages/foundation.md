# Illumo Foundation

Dependency-light supported pieces include:

- compiler/platform macros (`MacroDefs`)
- engine build/version metadata (`BuildInfo`)
- math and compact containers (`MathTypes`, `ArrayQueue`, `RollingMetric`)

The math header deliberately avoids the name `Math.h`, which shadows the CRT on
case-insensitive Windows. `BuildInfo` is an Illumo Foundation contract used by
the engine-owned system command-line parser, the log header and the startup
report.

`BuildInfo` carries the build version `vYY.MM_B` (D-F2). Its values live in a
source that `cmake/IllumoVersion.cmake` regenerates on every build, so only
that file recompiles when the version changes:

- `Release` (`26.09`) is `VERSION.txt` at the repository root, changed only
  when a release is cut with `python build.py version --set YY.MM`;
- `BuildNumber` counts first-parent commits since `VERSION.txt` last changed
  (0 without Git history or in a shallow clone);
- `Commit` and `Dirty` identify the exact source;
- `VersionNumber` (`v26.09_12`) is what products show and `FullVersion`
  (`v26.09_12 (1f709073, dirty)`) is what logs and `--version` show.

The same step writes the version (`26.09_12`) into each staged app's
`illumo.json`; source manifests do not set it.
