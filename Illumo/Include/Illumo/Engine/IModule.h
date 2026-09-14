#pragma once
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/Scene.h>

class IModule
{
public:
  IModule() = default;
  virtual ~IModule() = default;
  IModule(const IModule&) = delete;
  IModule& operator=(const IModule&) = delete;
  IModule(IModule&&) = delete;
  IModule& operator=(IModule&&) = delete;

protected:
  IllumoContext* ic{ nullptr };

public:
  virtual bool Start(IllumoContext* context) = 0;
  virtual void Update(double dt) = 0;
  virtual void DispatchDrawables(Scene* scene) = 0;
  virtual void Exit() = 0;
  // Main-thread close negotiation. Return false to keep updating, then request
  // close again after any product confirmation completes.
  virtual bool OnCloseRequested() { return true; }
};
