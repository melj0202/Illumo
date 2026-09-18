# Linux platform guidance

This file specializes `Illumo/Source/Platform/AGENTS.md` for Linux.

Linux sources target Ubuntu 24.04 x86_64 with X11 or XWayland, GLFW/OpenGL
3.3, and gtkmm-3 native dialogs/clipboard. They must use the same generic
application runner and SaveLoad contract as Windows and must not include game
types. GTK stays limited to dialogs and clipboard; GLFW owns the window,
including the hidden capture window.

Cancel returns an empty string. Dialogs must honor `SaveLoadDialogSpec`,
restore the process working directory, and never parse product files. POSIX
`AtomicFile` publish is same-filesystem `rename` only.

Do not describe Linux as supported until native CMake, compiler, GLFW,
OpenGL, GTK dialog, runtime, and shutdown checks pass on a named host. Build
and smoke steps: `docs/packages/platform-linux.md`.
