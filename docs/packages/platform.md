# Illumo Platform

OS entry points and native persistence dialogs are engine-owned under
`Illumo/Source/Platform`. Public dialog data/contracts live under
`Illumo/Include/Illumo/Platform`.

| Port | Entry | Status |
|---|---|---|
| Windows | `Windows/WinMain.cpp` | Supported; native dialogs in `WinSaveLoad.cpp` |
| Linux | `Linux/_main.cpp` | Unsupported stale scaffold |
| macOS | `macOS/Main.cpp` | Unsupported stale scaffold |

Entry code obtains the consumer's `IllumoApplicationDefinition` and calls the
generic Illumo runner. Dialog implementations accept game-owned labels and
defaults as data; they do not include Game types or parse save files. Clipboard
text (`Clipboard::GetText` / `SetText`) follows the same platform split:
Windows is implemented, Linux/macOS return empty/false scaffolds and are not
supported clipboard ports. Source
presence does not establish support: each port requires native build, tests,
live rendering/input, dialogs, and clean shutdown.

`AtomicFile::write` synchronously creates an exclusive sibling staging file,
streams the caller's format, checks write/flush/close, and publishes by replacing
the destination. A reported failure preserves the old destination and removes
only the staging file owned by that call. IllEd and IllumoGame codecs use this
operation and clear dirty state/report success only after it succeeds. Windows
uses same-directory `MoveFileExW` with replacement, without cross-volume copy
fallback ([API contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)).
This is not a power-loss durability or destination-security-metadata preservation
guarantee. POSIX rename code is unverified scaffolding.
