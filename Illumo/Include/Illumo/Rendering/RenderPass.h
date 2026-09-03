#pragma once

#include <Illumo/Rendering/ITexture.h>
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/RenderStyle.h>
#include <Illumo/Rendering/RenderTargetPool.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

class Renderer;

enum class PassType : uint8_t
{
  Draw,
  PostProcess,
  Custom
};

struct PassClearConfig
{
  bool clearColor = false;
  std::array<float, 4> clearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f };
  bool clearDepth = false;
  float clearDepthValue = 1.0f;
};

struct PassTextureBinding
{
  TextureHandle texture{};
  unsigned int slot = 0;
};

struct PassInputTargetBinding
{
  std::string targetName;
  size_t attachmentIndex = 0;
  unsigned int slot = 0;
  std::string samplerUniformName;
};

struct PassUniformFloat
{
  std::string name;
  float value = 0.0f;
};

struct PassUniformInt
{
  std::string name;
  int value = 0;
};

struct PassUniformMat4
{
  std::string name;
  std::array<float, 16> matrix{};
};

struct RenderPassDesc
{
  std::string name = "DrawPass";
  PassType type = PassType::Draw;

  // Target configuration
  bool useScreenTarget = true; // true = FramebufferHandle{} (default screen)
  std::string pooledTargetName;
  PooledRenderTargetDesc targetDesc;

  // Clear configuration
  PassClearConfig clear;

  // Viewport configuration
  bool customViewport = false;
  int viewportX = 0;
  int viewportY = 0;
  int viewportWidth = 0;
  int viewportHeight = 0;

  // Pipeline state override
  bool overridePipelineState = false;
  PipelineState pipelineState;

  // DrawPass filter
  uint32_t passMask = 0xFFFFFFFF;

  // PostProcessPass configuration
  RenderStyleHandle styleHandle{};
  ShaderHandle shaderHandle{};
  std::vector<PassTextureBinding> inputTextures;
  std::vector<PassInputTargetBinding> inputTargetTextures;
  std::vector<PassUniformFloat> uniformFloats;
  std::vector<PassUniformInt> uniformInts;
  std::vector<PassUniformMat4> uniformMat4s;

  // CustomPass callback
  std::function<void(Renderer*)> customExecution;

  static RenderPassDesc CreateDefaultDrawPass(
    const std::string& passName = "DrawPass")
  {
    RenderPassDesc desc;
    desc.name = passName;
    desc.type = PassType::Draw;
    desc.useScreenTarget = true;
    return desc;
  }

  static RenderPassDesc CreatePostProcessPass(
    const std::string& passName,
    RenderStyleHandle style,
    const std::vector<PassTextureBinding>& inputs = {})
  {
    RenderPassDesc desc;
    desc.name = passName;
    desc.type = PassType::PostProcess;
    desc.useScreenTarget = true;
    desc.styleHandle = style;
    desc.inputTextures = inputs;
    desc.overridePipelineState = true;
    desc.pipelineState.depthTestEnabled = false;
    desc.pipelineState.blendEnabled = false;
    return desc;
  }
};
