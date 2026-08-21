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
