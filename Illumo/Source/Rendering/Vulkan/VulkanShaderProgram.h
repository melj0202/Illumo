#pragma once
#include <Illumo/Rendering/IShaderProgram.h>

// Stub shader program; VulkanBackend does not yet compile SPIR-V or build a
// pipeline, so every instance reports invalid.
class VulkanShaderProgram : public IShaderProgram
{
public:
  explicit VulkanShaderProgram(const ShaderPaths& paths);
  explicit VulkanShaderProgram(const ShaderSources& sources);
  ~VulkanShaderProgram() override;

  bool isValid() const override { return false; }
  void Destroy() override;

private:
  void CompileAndLink(const std::string& vertexSource,
                      const std::string& fragmentSource) override;
  void CompileAndLink(const ShaderSources& sources) override;
  void CompileAndLink(const ShaderPaths& paths) override;
};
