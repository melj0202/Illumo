#include "SimulatorSettings.h"
#include "CSimSounds.h"
#include "CellContext.h"
#include "Rulesets/RuleSetRegistry.h"
#include "SparseCellGrid.h"
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Rendering/UiScale.h>
#include <algorithm>
#include <cctype>
#include <string>

static std::string
lower(std::string text)
{
  std::transform(
    text.begin(), text.end(), text.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
  return text;
}

void
SimulatorSettings::read(IEnvVars* environment, SimulatorConfiguration* output)
{
  if (environment == nullptr || output == nullptr) {
    return;
  }
  // An unknown ruleset reads as the default; the family follows the ruleset.
  std::string ruleSet = environment->getVar("RuleSetString").value;
  if (ruleSet.empty()) {
    ruleSet = environment->getVar("ModeString").value;
  }
  const RuleSetRegistry& rules = RuleSetRegistry::instance();
  const RuleSetDefinition* rule = rules.getRuleSetDefinition(ruleSet);
  if (rule == nullptr) {
    rule = rules.getRuleSetDefinition(SimulatorConfiguration{}.ruleSet);
  }
  if (rule != nullptr) {
    output->ruleSet = rule->id;
    output->family = rule->familyId;
  }
  output->worldChunkWidth = environment->getVar("WorldChunksX").valueAsLong;
  output->worldChunkHeight = environment->getVar("WorldChunksY").valueAsLong;
  if (!SparseCellGrid::isValidTopology(output->worldChunkWidth,
                                       output->worldChunkHeight)) {
    output->worldChunkWidth = 0;
    output->worldChunkHeight = 0;
  }

  output->tps = std::lround(number(environment,
                                   "tps",
                                   static_cast<double>(kDefaultTps),
                                   1.0,
                                   static_cast<double>(kMaximumTps)));
  output->speedFactor =
    number(environment, "speedFactor", 1.0, 0.0, kMaximumSpeedFactor);
  if (output->speedFactor <= 0.0) {
    output->speedFactor = 1.0;
  }
  output->fadeSpeed = number(
    environment, "cellFadeSpeed", kDefaultFadeSpeed, 0.0, kMaximumFadeSpeed);
  output->fpsCap = std::min(getTargetFps(environment), kMaximumFpsCap);

  output->editHints = flag(environment, "editHints", true);
  output->softwareCursor = flag(environment, "softwareCursor", true);
  output->vsync = environment->getVar("vsync").valueAsBool;
  output->fullscreen = environment->getVar("fullscreen").valueAsBool;
  // 0 is automatic; explicit factors outside the menu's range read as 1x.
  output->uiScale = UiScale::stored(environment->getVar("uiScale"));
  if (output->uiScale != 0.0 && (output->uiScale < kMinimumUiScale ||
                                 output->uiScale > kMaximumUiScale)) {
    output->uiScale = 1.0;
  }
  output->msaa = msaa(environment);
  output->graphicsApi = graphicsApi(environment);
  output->showInspector = environment->getVar("showInspector").valueAsBool;
  output->reducedUiMotion = environment->getVar("reducedUiMotion").valueAsBool;
  output->soundVolume = CSimSounds::volumeSetting(environment);
  output->musicVolume = CSimSounds::musicVolumeSetting(environment);

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
  output->panSpeed = std::lround(number(environment,
                                        "panSpeed",
                                        600.0,
                                        0.0,
                                        static_cast<double>(kMaximumPanSpeed)));
  output->autosaveMinutes =
    std::lround(number(environment,
                       "autosaveMinutes",
                       0.0,
                       0.0,
                       static_cast<double>(kMaximumAutosaveMinutes)));
  output->confirmClear = flag(environment, "confirmClear", true);
}

bool
SimulatorSettings::valid(const SimulatorConfiguration& configuration)
{
  const RuleSetDefinition* rule =
    RuleSetRegistry::instance().getRuleSetDefinition(configuration.ruleSet);
  return CellContext::IsKnownFamilyString(configuration.family) &&
         CellContext::IsKnownModeString(configuration.ruleSet) &&
         rule != nullptr && rule->familyId == configuration.family &&
         SparseCellGrid::isValidTopology(configuration.worldChunkWidth,
                                         configuration.worldChunkHeight) &&
         configuration.tps >= 1 && configuration.tps <= kMaximumTps &&
         std::isfinite(configuration.speedFactor) &&
         configuration.speedFactor > 0.0 &&
         configuration.speedFactor <= kMaximumSpeedFactor &&
         std::isfinite(configuration.fadeSpeed) &&
         configuration.fadeSpeed >= 0.0 &&
         configuration.fadeSpeed <= kMaximumFadeSpeed &&
         std::isfinite(configuration.uiScale) &&
         (configuration.uiScale == 0.0 ||
          (configuration.uiScale >= kMinimumUiScale &&
           configuration.uiScale <= kMaximumUiScale)) &&
         configuration.fpsCap >= 0 && configuration.fpsCap <= kMaximumFpsCap &&
         std::isfinite(configuration.cellGlow) &&
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

void
SimulatorSettings::write(IEnvVars* environment,
                         const SimulatorConfiguration& configuration)
{
  if (environment == nullptr) {
    return;
  }
  environment->setVar("FamilyString", configuration.family);
  environment->setVar("RuleSetString", configuration.ruleSet);
  environment->setVar("ModeString", configuration.ruleSet);
  environment->setVar("WorldChunksX",
                      static_cast<long>(configuration.worldChunkWidth));
  environment->setVar("WorldChunksY",
                      static_cast<long>(configuration.worldChunkHeight));
  environment->setVar("tps", configuration.tps);
  environment->setVar("speedFactor", configuration.speedFactor);
  environment->setVar("cellFadeSpeed", configuration.fadeSpeed);
  environment->setVar("fps", configuration.fpsCap);
  environment->setVar("showInspector", configuration.showInspector);
  environment->setVar("reducedUiMotion", configuration.reducedUiMotion);
  environment->setVar("editHints", configuration.editHints);
  environment->setVar("softwareCursor", configuration.softwareCursor);
  environment->setVar("vsync", configuration.vsync);
  environment->setVar("fullscreen", configuration.fullscreen);
  environment->setVar("uiScale",
                      UiScale::text(static_cast<float>(configuration.uiScale)));
  environment->setVar("msaa", configuration.msaa);
  environment->setVar("GraphicsAPI", configuration.graphicsApi);
  environment->setVar("soundVolume", configuration.soundVolume);
  environment->setVar("musicVolume", configuration.musicVolume);
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
