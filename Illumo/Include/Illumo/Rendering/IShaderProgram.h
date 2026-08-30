#pragma once
#include <string>
#include <vector>

struct ShaderPaths
{
  std::string vertexPath;
  std::string fragmentPath;
  std::vector<std::string> defines;
  // Expandable later for geometry/compute paths
};

struct ShaderSources
{
  std::string vertexSource;
  std::string fragmentSource;
  std::vector<std::string> defines;
  // Expandable later for geometry/compute paths
};

class IShaderProgram
{
public:
  IShaderProgram() = default;
  virtual ~IShaderProgram() = default;
  virtual void CompileAndLink(const std::string& vertexSource,
                              const std::string& fragmentSource) = 0;
  virtual void CompileAndLink(const ShaderSources& sources) = 0;
  virtual void CompileAndLink(const ShaderPaths& paths) = 0;
  virtual void Destroy() = 0;
  virtual unsigned long GetID() const = 0;
  virtual bool isValid() const = 0;

protected:
  unsigned int _programID;
  unsigned int _vertexShadeID;
  unsigned int _fragmentShaderID;
};
