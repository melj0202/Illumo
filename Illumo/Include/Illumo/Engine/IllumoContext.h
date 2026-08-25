#pragma once

#include <Illumo/Foundation/MacroDefs.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
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
  EnvVars* envVars{ nullptr };
  Camera* camera{ nullptr };
  CommandRegistry* commandRegistry{ nullptr };
  IModuleHost* moduleHost{ nullptr };
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
