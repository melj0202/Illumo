#pragma once

#include <Illumo/Audio/Audio.h>
#include <Illumo/Foundation/MacroDefs.h>
#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/IRenderWorld.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/FileTreeSource.h>
#include <Illumo/Services/InputManager.h>

class FrameProfiler;
class SceneDirector;

// Non-owning service bag passed to a program, its scenes and the debug overlay
// when they start.
struct IllumoContext
{
  Scene* scene{ nullptr };
  IRenderWindow* window{ nullptr };
  CommandLine* commandLine{ nullptr };
  InputManager* inputManager{ nullptr };
  Renderer* renderer{ nullptr };
  AssetManager* assetManager{ nullptr };
  IEnvVars* envVars{ nullptr };
  Camera* camera{ nullptr };
  CommandRegistry* commandRegistry{ nullptr };
  // The program's scenes (D-E31), when the product runs as a scene program.
  // camera and renderWorld follow the active scene.
  SceneDirector* scenes{ nullptr };
  // The host's mounted file tree, when it has one (IllumoRuntime). Published
  // by the program that owns the tree when it starts and withdrawn when it
  // stops; tools read it at use time, never cache it.
  const IFileTreeSource* fileTree{ nullptr };
  // Extra windows for detached tool panels, when the host can show them
  // (IllumoRuntime guests granted the Windows capability). Composed by the
  // guest application; products must also work without it.
  IPanelSurfaces* panelSurfaces{ nullptr };
  // Sound effects, when the host can play them (IllumoRuntime guests
  // granted the Audio capability). Composed by the guest application;
  // products must also work silently without it.
  IAudio* audio{ nullptr };
  // Host-owned world render objects (D-E30), when the host keeps them
  // (IllumoRuntime guests granted HostRender). Composed by the guest
  // application; products must also work without it.
  IRenderWorld* renderWorld{ nullptr };
  // The host loop's phase profiler (main-thread timings), for diagnostics
  // such as the runtime benchmark; disabled unless someone enables it.
  FrameProfiler* frameProfiler{ nullptr };
};

// Required wiring for DebugOverlay (console, FPS overlay, env flags).
inline bool
IllumoContextHasDebugCore(const IllumoContext* c)
{
  return c != nullptr && c->envVars != nullptr && c->window != nullptr &&
         c->camera != nullptr && c->renderer != nullptr &&
         c->inputManager != nullptr && c->commandLine != nullptr &&
         c->commandRegistry != nullptr && c->assetManager != nullptr;
}
