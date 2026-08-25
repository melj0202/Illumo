#pragma once

#include <memory>

class IModule;

class IModuleHost
{
public:
  virtual ~IModuleHost() = default;

  virtual void RequestTransition(std::unique_ptr<IModule> nextModule) = 0;
  virtual bool HasPendingTransition() const = 0;
};
