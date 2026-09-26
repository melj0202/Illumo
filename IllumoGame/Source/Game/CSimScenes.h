#pragma once

#include "NewSimulationMenu.h"
#include <memory>
#include <string>

class CanvasScene;
class SceneDirector;

// CSim's scene flow (D-E31): the title screen and at most one canvas, held by
// the program's SceneDirector. Scenes reach it through IllumoContext::scenes,
// so tests drive the same flow with a real director. Like the module
// transitions it replaces, only one switch is pending at a time.
class CSimScenes final
{
public:
  static constexpr const char* kTitle = "title";
  static constexpr const char* kCanvas = "canvas";

  // The program's first scene.
  static bool openTitle(SceneDirector& scenes);
  // A fresh canvas from the setup screen, replacing any kept canvas.
  static bool newSimulation(SceneDirector& scenes,
                            const NewSimulationConfiguration& configuration);
  // A canvas loaded from a save, replacing any kept canvas.
  static bool loadSimulation(SceneDirector& scenes,
                             const std::string& location);
  static bool returnToTitle(SceneDirector& scenes);
  // Called by the program each frame, after the director applied a switch:
  // releases the screen that was left.
  static void settle(SceneDirector& scenes);

private:
  static bool openCanvas(SceneDirector& scenes,
                         std::unique_ptr<CanvasScene> canvas);
};
