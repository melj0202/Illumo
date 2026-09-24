#pragma once

#include <Illumo/Audio/Audio.h>
#include <Illumo/Foundation/MacroDefs.h>
#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/FileTreeSource.h>
#include <Illumo/Services/InputManager.h>

class IModuleHost;

// Non-owning service bag passed to IModule::Start.
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
  IModuleHost* moduleHost{ nullptr };
  // The host's mounted file tree, when it has one (IllumoRuntime). Published
  // by the module that owns the tree during Start and withdrawn on Exit;
  // tools read it at use time, never cache it.
  const IFileTreeSource* fileTree{ nullptr };
  // Extra windows for detached tool panels, when the host can show them
  // (IllumoRuntime guests granted the Windows capability). Composed by the
  // guest application; products must also work without it.
  IPanelSurfaces* panelSurfaces{ nullptr };
  // Sound effects, when the host can play them (IllumoRuntime guests
  // granted the Audio capability). Composed by the guest application;
  // products must also work silently without it.
  IAudio* audio{ nullptr };
};

// Required wiring for DebugModule (console, FPS overlay, env flags).
inline bool
IllumoContextHasDebugCore(const IllumoContext* c)
{
  return c != nullptr && c->envVars != nullptr && c->window != nullptr &&
         c->camera != nullptr && c->renderer != nullptr &&
         c->inputManager != nullptr && c->commandLine != nullptr &&
         c->commandRegistry != nullptr && c->assetManager != nullptr;
}
