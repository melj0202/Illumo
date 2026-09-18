#pragma once

// One-shot GTK initialization for Linux dialogs and clipboard. GLFW still owns
// the application window; GTK is used only for those native OS surfaces.
bool
EnsureLinuxGtkInitialized();
