#pragma once

class IEnvVars;

class IllEdConfig
{
public:
  static void ApplyDefaults(IEnvVars* environment);
  static const char* applicationName() { return "IllEd"; }
};
