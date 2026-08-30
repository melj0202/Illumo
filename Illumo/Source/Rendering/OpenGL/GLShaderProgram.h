#pragma once
#include <GL/glew.h> // Or your preferred OpenGL loader header
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

class GLShaderProgram : public IShaderProgram
{
public:
  // Constructor keeps it simple and safe
  GLShaderProgram(const ShaderPaths& paths)
  {
    _programID = 0;
    CompileAndLink(paths);
  }
  GLShaderProgram(const ShaderSources& sources)
  {
    _programID = 0;
    CompileAndLink(sources);
  }

  // RAII Destructor prevents GPU memory leaks
  ~GLShaderProgram()
  {
    // Do nothing. asset destruction should be explicit
  }

  unsigned long GetID() const override { return _programID; }
  bool isValid() const override { return _valid && _programID != 0; }

  void Destroy() override
  {
    if (_programID != 0) {
      glDeleteProgram(_programID);
      _programID = 0;
    }
    _valid = false;
  }

private:
  bool _valid = false;

  void CompileAndLink(const ShaderSources& sources) override
  {
    PreprocessOptions vsOptions;
    vsOptions.defines = sources.defines;
    PreprocessResult vsResult =
      ShaderPreprocessor::Process(sources.vertexSource, vsOptions);
    if (!vsResult.success) {
      Logger::LogError("GLShaderProgram: Vertex preprocessor failed: " +
                       vsResult.errorMessage);
      _valid = false;
      return;
    }

    PreprocessOptions fsOptions;
    fsOptions.defines = sources.defines;
    PreprocessResult fsResult =
      ShaderPreprocessor::Process(sources.fragmentSource, fsOptions);
    if (!fsResult.success) {
      Logger::LogError("GLShaderProgram: Fragment preprocessor failed: " +
                       fsResult.errorMessage);
      _valid = false;
      return;
    }

    CompileAndLinkRaw(vsResult.source, fsResult.source);
  }

  void CompileAndLink(const ShaderPaths& paths) override
  {
    PreprocessOptions vsOptions;
    vsOptions.defines = paths.defines;
    vsOptions.sourcePath = paths.vertexPath;
    PreprocessResult vsResult =
      ShaderPreprocessor::ProcessFile(paths.vertexPath, vsOptions);
    if (!vsResult.success) {
      Logger::LogError("GLShaderProgram: Vertex preprocessor failed for " +
                       paths.vertexPath + ": " + vsResult.errorMessage);
      _valid = false;
      return;
    }

    PreprocessOptions fsOptions;
    fsOptions.defines = paths.defines;
    fsOptions.sourcePath = paths.fragmentPath;
    PreprocessResult fsResult =
      ShaderPreprocessor::ProcessFile(paths.fragmentPath, fsOptions);
    if (!fsResult.success) {
      Logger::LogError("GLShaderProgram: Fragment preprocessor failed for " +
                       paths.fragmentPath + ": " + fsResult.errorMessage);
      _valid = false;
      return;
    }

    CompileAndLinkRaw(vsResult.source, fsResult.source);
  }

  void CompileAndLink(const std::string& vertexSource,
                      const std::string& fragmentSource) override
  {
    ShaderSources sources;
    sources.vertexSource = vertexSource;
    sources.fragmentSource = fragmentSource;
    CompileAndLink(sources);
  }

  void CompileAndLinkRaw(const std::string& vertexSource,
                         const std::string& fragmentSource)
  {
    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertexSource);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs == 0 || fs == 0) {
      if (vs != 0) {
        glDeleteShader(vs);
      }
      if (fs != 0) {
        glDeleteShader(fs);
      }
      _valid = false;
      return;
    }

    _programID = glCreateProgram();
    glAttachShader(_programID, vs);
    glAttachShader(_programID, fs);
    glLinkProgram(_programID);

    // Check Link Status
    int linked;
    glGetProgramiv(_programID, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      int length = 0;
      glGetProgramiv(_programID, GL_INFO_LOG_LENGTH, &length);
      std::vector<char> message(length);
      glGetProgramInfoLog(_programID, length, &length, message.data());
      std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
                << message.data() << std::endl;
      glDeleteProgram(_programID);
      _programID = 0;
      _valid = false;
    } else {
      _valid = true;
    }

    // Clean up intermediate shader objects
    glDeleteShader(vs);
    glDeleteShader(fs);
  }

  unsigned int CompileShader(unsigned int type, const std::string& source)
  {
    unsigned int id = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    int result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
      int length;
      glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
      std::vector<char> message(length);
      glGetShaderInfoLog(id, length, &length, message.data());
      std::cerr << "ERROR::SHADER::COMPILATION_FAILED_FOR_"
                << (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT") << "\n"
                << message.data() << std::endl;
      glDeleteShader(id);
      return 0;
    }
    return id;
  }
};
