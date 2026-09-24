# Illumo Platform

OS entry points and native persistence dialogs are engine-owned under
`Illumo/Source/Platform`. Public dialog data/contracts live under
`Illumo/Include/Illumo/Platform`.

| Port | Entry | Status |
|---|---|---|
| Windows | `Windows/WinMain.cpp` | Supported; native dialogs in `WinSaveLoad.cpp` |
| Linux | `Linux/_main.cpp` | Source-repaired for Ubuntu 24.04 x86_64 X11/XWayland; not a support claim until native GUI smoke. See [platform-linux.md](platform-linux.md). |

Entry code obtains the consumer's `IllumoApplicationDefinition` and calls the
generic Illumo runner. Dialog implementations accept game-owned labels and
defaults as data; they do not include Game types or parse save files. Clipboard
text (`Clipboard::GetText` / `SetText`) follows the same platform split:
Windows uses Win32; Linux uses the GTK clipboard after one-shot gtkmm init.
`PixelWindow` (`Source/Platform/PixelWindow.*`, engine-internal) is a secondary
GLFW window created with `GLFW_NO_API` for CPU-drawn tools such as the detached
developer console (D-UI5). GLFW supplies creation, input callbacks, and native
move/resize/close; a per-OS presenter blits the RGBA image — Windows uses GDI
`StretchDIBits` (`Windows/WinPixelWindowPresent.cpp`), Linux reports
presentation unsupported (`Linux/LinuxPixelWindowPresent.cpp`). It never
creates or touches an OpenGL context. `ISurfaceWindow` and
`ISurfaceWindowFactory` (`Illumo/Platform/SurfaceWindow.h`,
`PlatformSurfaceWindows()`) wrap it for detached tool panels (D-E27): client
origin, title, focus, cursor, minimum size and events, with new windows
clamped into the work area of the monitor holding their centre.
`WasmPanelWindows` presents GPU-rendered panel surfaces through them.
macOS is not targeted; its scaffold has been removed and CMake rejects Apple
targets. Source presence does not establish
support: each port requires native build, tests, live rendering/input, dialogs,
and clean shutdown.

`AtomicFile::write` synchronously creates an exclusive sibling staging file,
streams the caller's format, checks write/flush/close, and publishes by replacing
the destination. A reported failure preserves the old destination and removes
only the staging file owned by that call. IllEd and IllumoGame codecs use this
operation and clear dirty state/report success only after it succeeds. Windows
uses same-directory `MoveFileExW` with replacement, without cross-volume copy
fallback ([API contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)).
This is not a power-loss durability or destination-security-metadata preservation
guarantee. POSIX `rename` replaces on the same filesystem and fails with `EXDEV`
across devices; there is no copy fallback. Treat that as the Linux contract,
proven by `Illumo.Platform.AtomicFile` on the native host.

SaveLoad specification strings and returned paths are UTF-8. Windows uses wide
common-dialog APIs and a 32,768-character filename buffer; invalid specification
encoding is rejected and cancellation remains an empty result. Product codecs
construct filesystem paths from UTF-8 and contain conversion failures. A wide
API and large buffer do not certify every native-dialog long-path scenario.
