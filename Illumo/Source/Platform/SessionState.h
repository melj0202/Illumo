#pragma once

// True while this login session is locked (the OS shows its lock screen).
// Presenting to a window can then stall inside the graphics driver, so the
// Vulkan backend skips presents while it lasts. False where the platform
// cannot tell.
bool
PlatformSessionLocked();
