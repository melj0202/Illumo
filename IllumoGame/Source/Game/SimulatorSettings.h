#pragma once

#include "ConfigurationMenu.h"
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/IEnvVars.h>
#include <cctype>
#include <cmath>
#include <string>

// CSim's persisted settings, shared by every scene that opens the Settings
// menu (D-E31): their environment names, product defaults and ranges, so the
// title and the canvas read, check and write them identically. Values outside
// their range read as the default and are rejected when applied. The canvas
// overlays its live world on what is read and applies world changes itself.
class SimulatorSettings final
{
public:
  static constexpr long kDefaultTps = 30;
  static constexpr long kMaximumTps = 1000;
  static constexpr double kMaximumSpeedFactor = 100.0;
  static constexpr double kDefaultFadeSpeed = 8.0;
  static constexpr double kMaximumFadeSpeed = 100.0;
  // An explicit interface scale; 0 is automatic.
  static constexpr double kMinimumUiScale = 1.0;
  static constexpr double kMaximumUiScale = 8.0;
  static constexpr long kMaximumFpsCap = 1000;
  static constexpr double kMaximumCellGlow = 2.0;
  static constexpr double kMinimumZoomStep = 0.01;
  static constexpr double kMaximumZoomStep = 0.5;
  static constexpr long kMaximumPanSpeed = 5000;
  static constexpr long kMaximumAutosaveMinutes = 240;
  // Where autosave writes, in the game's private storage.
  static constexpr const char* kAutosaveFile = "autosave.csim";

  // Every stored setting, with the world new canvases open with.
  static void read(IEnvVars* environment, SimulatorConfiguration* output);
  // Every setting is in range and the ruleset belongs to the family.
  static bool valid(const SimulatorConfiguration& configuration);
  // Stores every setting; the caller saves the environment.
  static void write(IEnvVars* environment,
                    const SimulatorConfiguration& configuration);

  // Whether an applied configuration holds a setting that only a restart
  // applies (MSAA and the rendering backend, chosen when the window is
  // created): true when it differs from what the running window uses.
  // Unknown windows never ask.
  static bool restartNeeded(const SimulatorConfiguration& configuration,
                            const IRenderWindow* window)
  {
    if (window == nullptr) {
      return false;
    }
    const int running = window->getMsaaSamples();
    const std::string runningApi = window->graphicsApi();
    return (running >= 0 && running != configuration.msaa) ||
           (!runningApi.empty() && runningApi != configuration.graphicsApi);
  }

  // The rendering backend the environment asks for, "OPENGL" unless it
  // names Vulkan or Direct3D 12 (in any letter case; "D3D12" reads as
  // "DIRECTX12").
  static std::string graphicsApi(IEnvVars* environment)
  {
    std::string name = environment->getVar("GraphicsAPI").value;
    for (char& character : name) {
      character =
        static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    if (name == "DIRECTX12" || name == "D3D12") {
      return "DIRECTX12";
    }
    return name == "VULKAN" ? name : std::string("OPENGL");
  }

  // The MSAA sample count the environment asks for.
  static long msaa(IEnvVars* environment)
  {
    const EnvVar& value = environment->getVar("msaa");
    return value.value.empty() ? 4 : value.valueAsLong;
  }

  // An unset preference reads as its default.
  static bool flag(IEnvVars* environment, const char* name, bool fallback)
  {
    const EnvVar& value = environment->getVar(name);
    return value.value.empty() ? fallback : value.valueAsBool;
  }

  static double number(IEnvVars* environment,
                       const char* name,
                       double fallback,
                       double minimum,
                       double maximum)
  {
    const EnvVar& value = environment->getVar(name);
    if (value.value.empty() || !std::isfinite(value.valueAsDouble) ||
        value.valueAsDouble < minimum || value.valueAsDouble > maximum) {
      return fallback;
    }
    return value.valueAsDouble;
  }
};
