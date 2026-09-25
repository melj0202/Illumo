#pragma once

#include "ConfigurationMenu.h"
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/IEnvVars.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

// Persisted canvas, control and behaviour preferences shared by the canvas
// and the main menu: their environment names, defaults and ranges, so both
// read and write them identically. Values outside their range fall back to
// the default when read and are rejected when applied.
class SimulatorSettings final
{
public:
  static constexpr double kMaximumCellGlow = 2.0;
  static constexpr double kMinimumZoomStep = 0.01;
  static constexpr double kMaximumZoomStep = 0.5;
  static constexpr long kMaximumPanSpeed = 5000;
  static constexpr long kMaximumAutosaveMinutes = 240;
  // Where autosave writes, in the game's private storage.
  static constexpr const char* kAutosaveFile = "autosave.csim";

  static void read(IEnvVars* environment, SimulatorConfiguration* output)
  {
    if (environment == nullptr || output == nullptr) {
      return;
    }
    output->startPaused = flag(environment, "startPaused", true);
    output->ledCells =
      !lower(environment->getVar("cellStyle").value).starts_with("flat");
    output->cellGlow =
      number(environment, "cellGlow", 1.0, 0.0, kMaximumCellGlow);
    output->gridLines = flag(environment, "gridLines", false);
    output->showFps = flag(environment, "showFPS", false);
    output->showMemory = flag(environment, "showMemory", false);
    output->zoomStep =
      number(environment, "zoomStep", 0.15, kMinimumZoomStep, kMaximumZoomStep);
    output->invertZoom = flag(environment, "invertZoom", false);
    output->panSpeed = static_cast<long>(
      std::lround(number(environment,
                         "panSpeed",
                         600.0,
                         0.0,
                         static_cast<double>(kMaximumPanSpeed))));
    output->autosaveMinutes = static_cast<long>(
      std::lround(number(environment,
                         "autosaveMinutes",
                         0.0,
                         0.0,
                         static_cast<double>(kMaximumAutosaveMinutes))));
    output->confirmClear = flag(environment, "confirmClear", true);
  }

  static bool valid(const SimulatorConfiguration& configuration)
  {
    return std::isfinite(configuration.cellGlow) &&
           configuration.cellGlow >= 0.0 &&
           configuration.cellGlow <= kMaximumCellGlow &&
           std::isfinite(configuration.zoomStep) &&
           configuration.zoomStep >= kMinimumZoomStep &&
           configuration.zoomStep <= kMaximumZoomStep &&
           configuration.panSpeed >= 0 &&
           configuration.panSpeed <= kMaximumPanSpeed &&
           configuration.autosaveMinutes >= 0 &&
           configuration.autosaveMinutes <= kMaximumAutosaveMinutes;
  }

  static void write(IEnvVars* environment,
                    const SimulatorConfiguration& configuration)
  {
    if (environment == nullptr) {
      return;
    }
    environment->setVar("startPaused", configuration.startPaused);
    environment->setVar("cellStyle",
                        std::string(configuration.ledCells ? "led" : "flat"));
    environment->setVar("cellGlow", configuration.cellGlow);
    environment->setVar("gridLines", configuration.gridLines);
    environment->setVar("showFPS", configuration.showFps);
    environment->setVar("showMemory", configuration.showMemory);
    environment->setVar("zoomStep", configuration.zoomStep);
    environment->setVar("invertZoom", configuration.invertZoom);
    environment->setVar("panSpeed", configuration.panSpeed);
    environment->setVar("autosaveMinutes", configuration.autosaveMinutes);
    environment->setVar("confirmClear", configuration.confirmClear);
  }

  // Whether an applied configuration holds a setting that only a restart
  // applies (today MSAA, read when the window is created): true when it
  // differs from what the running window uses. Unknown windows never ask.
  static bool restartNeeded(const SimulatorConfiguration& configuration,
                            const IRenderWindow* window)
  {
    if (window == nullptr) {
      return false;
    }
    const int running = window->getMsaaSamples();
    return running >= 0 && running != configuration.msaa;
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

private:
  static std::string lower(std::string text)
  {
    std::transform(
      text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
    return text;
  }
};
