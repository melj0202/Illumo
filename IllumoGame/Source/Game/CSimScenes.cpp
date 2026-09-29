#include "CSimScenes.h"
#include "CanvasScene.h"
#include "TitleScene.h"
#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>

bool
CSimScenes::openTitle(SceneDirector& scenes)
{
  ILLUMO_PROFILE_ZONE("CSimScenes.openTitle");
  if (scenes.hasPendingSwitch()) {
    return false;
  }
  if (!scenes.has(kTitle)) {
    scenes.emplace<TitleScene>(kTitle);
  }
  return scenes.switchTo(kTitle);
}

bool
CSimScenes::newSimulation(SceneDirector& scenes,
                          const NewSimulationConfiguration& configuration)
{
  return openCanvas(scenes, std::make_unique<CanvasScene>(configuration));
}

bool
CSimScenes::loadSimulation(SceneDirector& scenes, const std::string& location)
{
  return openCanvas(scenes, std::make_unique<CanvasScene>(location));
}

bool
CSimScenes::returnToTitle(SceneDirector& scenes)
{
  return openTitle(scenes);
}

bool
CSimScenes::canResume(const SceneDirector& scenes)
{
  return scenes.has(kCanvas) && scenes.activeName() != kCanvas;
}

bool
CSimScenes::resumeCanvas(SceneDirector& scenes)
{
  return !scenes.hasPendingSwitch() && canResume(scenes) &&
         scenes.switchTo(kCanvas);
}

bool
CSimScenes::openCanvas(SceneDirector& scenes,
                       std::unique_ptr<CanvasScene> canvas)
{
  ILLUMO_PROFILE_ZONE("CSimScenes.openCanvas");
  if (scenes.hasPendingSwitch()) {
    Logger::LogWarning("A canvas was requested while a switch is pending");
    return false;
  }
  if (scenes.has(kCanvas) && !scenes.release(kCanvas)) {
    return false;
  }
  return scenes.add(kCanvas, std::move(canvas)) && scenes.switchTo(kCanvas);
}
