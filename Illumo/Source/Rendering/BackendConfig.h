#pragma once

#include <Illumo/Services/IEnvVars.h>

#include <string>

enum class BackendDef
{
  OPENGL,
  OPENGL_ES,
  VULKAN,
  DIRECTX12,
  DIRECTX11
};

inline BackendDef
StringToToken(IEnvVars* vars)
{
  std::string token = vars->getVar("GraphicsAPI").value;
  if (token == "OPENGL") {
    return BackendDef::OPENGL;
  } else if (token == "OPENGL_ES") {
    return BackendDef::OPENGL_ES;
  } else if (token == "VULKAN") {
    return BackendDef::VULKAN;
  } else if (token == "DIRECTX12") {
    return BackendDef::DIRECTX12;
  } else if (token == "DIRECTX11") {
    return BackendDef::DIRECTX11;
  } else {
    return BackendDef::OPENGL;
  }
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
