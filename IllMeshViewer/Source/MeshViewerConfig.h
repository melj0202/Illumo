#pragma once

#include <string>

class IEnvVars;

class MeshViewerConfig
{
public:
  static const std::string& applicationName();
  static void ApplyDefaults(IEnvVars* environment);
};
