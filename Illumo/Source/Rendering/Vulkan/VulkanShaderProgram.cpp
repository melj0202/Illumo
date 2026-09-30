#include "VulkanShaderProgram.h"

VulkanShaderProgram::VulkanShaderProgram(const ShaderPaths& paths)
{
  CompileAndLink(paths);
}

VulkanShaderProgram::VulkanShaderProgram(const ShaderSources& sources)
{
  CompileAndLink(sources);
}

VulkanShaderProgram::~VulkanShaderProgram()
{
  Destroy();
}

void
VulkanShaderProgram::Destroy()
{
}

void
VulkanShaderProgram::CompileAndLink(const std::string& vertexSource,
                                    const std::string& fragmentSource)
{
  (void)vertexSource;
  (void)fragmentSource;
}

void
VulkanShaderProgram::CompileAndLink(const ShaderSources& sources)
{
  (void)sources;
}

void
VulkanShaderProgram::CompileAndLink(const ShaderPaths& paths)
{
  (void)paths;
}
