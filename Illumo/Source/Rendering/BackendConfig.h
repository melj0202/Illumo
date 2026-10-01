#pragma once

#include <Illumo/Services/IEnvVars.h>

#include <cctype>
#include <string>

enum class BackendDef
{
  OPENGL,
  OPENGL_ES,
  VULKAN,
  DIRECTX12,
  DIRECTX11
};

// The "GraphicsAPI" setting names the backend a launch starts with, in any
// letter case. False for a name that is not a backend; *definition is then
// OPENGL, the default.
inline bool
parseBackendDef(const std::string& token, BackendDef* definition)
{
  std::string upper = token;
  for (char& character : upper) {
    character =
      static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  }
  BackendDef parsed = BackendDef::OPENGL;
  bool recognized = true;
  if (upper == "OPENGL") {
    parsed = BackendDef::OPENGL;
  } else if (upper == "OPENGL_ES") {
    parsed = BackendDef::OPENGL_ES;
  } else if (upper == "VULKAN") {
    parsed = BackendDef::VULKAN;
  } else if (upper == "DIRECTX12" || upper == "D3D12") {
    parsed = BackendDef::DIRECTX12;
  } else if (upper == "DIRECTX11") {
    parsed = BackendDef::DIRECTX11;
  } else {
    recognized = false;
  }
  if (definition != nullptr) {
    *definition = parsed;
  }
  return recognized;
}

inline BackendDef
StringToToken(IEnvVars* vars)
{
  BackendDef definition = BackendDef::OPENGL;
  parseBackendDef(vars->getVar("GraphicsAPI").value, &definition);
  return definition;
}

inline std::string
TokenToString(BackendDef def)
{
  switch (def) {
    case BackendDef::OPENGL:
      return "OPENGL";
    case BackendDef::OPENGL_ES:
      return "OPENGL_ES";
    case BackendDef::VULKAN:
      return "VULKAN";
    case BackendDef::DIRECTX12:
      return "DIRECTX12";
    case BackendDef::DIRECTX11:
      return "DIRECTX11";
    default:
      return "OPENGL";
  }
}

// The backends this build can start; the others parse, then fall back.
// Direct3D 12 exists on Windows only.
inline bool
isBackendImplemented(BackendDef def)
{
#ifdef _WIN32
  if (def == BackendDef::DIRECTX12) {
    return true;
  }
#endif
  return def == BackendDef::OPENGL || def == BackendDef::VULKAN;
}
