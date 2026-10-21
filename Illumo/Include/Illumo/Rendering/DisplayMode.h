#pragma once

#include <Illumo/Services/IEnvVars.h>
#include <string>
#include <string_view>

// How the host window occupies the screen. FullscreenWindow covers the
// monitor without an exclusive video-mode switch. Exclusive gives the
// monitor to the window.
enum class DisplayMode
{
  Window = 0,
  FullscreenWindow = 1,
  Exclusive = 2
};

inline const char*
displayModeName(DisplayMode mode)
{
  switch (mode) {
    case DisplayMode::FullscreenWindow:
      return "fullscreen";
    case DisplayMode::Exclusive:
      return "exclusive";
    case DisplayMode::Window:
      return "window";
  }
  return "window";
}

inline const char*
displayModeLabel(DisplayMode mode)
{
  switch (mode) {
    case DisplayMode::FullscreenWindow:
      return "Fullscreen window";
    case DisplayMode::Exclusive:
      return "Exclusive";
    case DisplayMode::Window:
      return "Window";
  }
  return "Window";
}

inline bool
parseDisplayMode(std::string_view text, DisplayMode* mode)
{
  if (mode == nullptr) {
    return false;
  }
  if (text == "window") {
    *mode = DisplayMode::Window;
    return true;
  }
  if (text == "fullscreen" || text == "borderless") {
    *mode = DisplayMode::FullscreenWindow;
    return true;
  }
  if (text == "exclusive") {
    *mode = DisplayMode::Exclusive;
    return true;
  }
  return false;
}

// `displayMode` wins. An older save that only has `fullscreen` means the
// borderless fullscreen window.
inline DisplayMode
displayModeFromEnvironment(IEnvVars* environment)
{
  if (environment == nullptr) {
    return DisplayMode::Window;
  }
  DisplayMode mode = DisplayMode::Window;
  const EnvVar& named = environment->getVar("displayMode");
  if (!named.value.empty() && parseDisplayMode(named.value, &mode)) {
    return mode;
  }
  return environment->getVar("fullscreen").valueAsBool
           ? DisplayMode::FullscreenWindow
           : DisplayMode::Window;
}

inline void
storeDisplayMode(IEnvVars* environment, DisplayMode mode)
{
  if (environment == nullptr) {
    return;
  }
  environment->setVar("displayMode", displayModeName(mode));
  environment->setVar("fullscreen", mode != DisplayMode::Window);
}
