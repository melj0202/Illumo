#pragma once

#include <string>

class IEnvVars;

class SpinningCubeConfig
{
public:
  static const std::string& applicationName();
  static void ApplyDefaults(IEnvVars* environment);
};
