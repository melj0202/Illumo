#pragma once

#include "NewSimulationMenu.h"
#include <memory>
#include <string>

class CanvasScene;
class SceneDirector;

// CSim's scene flow (D-E31): the title screen and at most one canvas, held by
// the program's SceneDirector. Both are kept: returning to the title keeps the
// canvas, which the title offers to resume, and a new or loaded canvas
// replaces it. Scenes reach the flow through IllumoContext::scenes, so tests
// drive it with a real director. Only one switch is pending at a time.
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
  // Back to the title screen; the canvas is kept.
  static bool returnToTitle(SceneDirector& scenes);
  // A canvas is kept for the title to resume.
  static bool canResume(const SceneDirector& scenes);
  static bool resumeCanvas(SceneDirector& scenes);

private:
  static bool openCanvas(SceneDirector& scenes,
                         std::unique_ptr<CanvasScene> canvas);
};
