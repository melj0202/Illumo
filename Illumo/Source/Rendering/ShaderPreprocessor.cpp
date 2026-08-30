#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_set>

static std::mutex s_virtualModulesMutex;
static std::unordered_map<std::string, std::string> s_virtualModules;
static bool s_defaultsRegistered = false;

static std::string
trimString(const std::string& str)
{
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first + 1));
}

static bool
startsWith(const std::string& str, const std::string& prefix)
{
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

std::string
ShaderPreprocessor::normalizeModuleName(const std::string& name)
{
  std::string result = trimString(name);
  if (startsWith(result, "<") && result.size() >= 2 && result.back() == '>') {
    result = result.substr(1, result.size() - 2);
  } else if (startsWith(result, "\"") && result.size() >= 2 &&
             result.back() == '\"') {
    result = result.substr(1, result.size() - 2);
  }
  std::replace(result.begin(), result.end(), '\\', '/');
#ifdef _WIN32
  std::transform(
    result.begin(), result.end(), result.begin(), [](char c) -> char {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
#endif
  return result;
}

static std::string
readFileToString(const std::string& filePath)
{
  std::ifstream file(filePath, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return "";
  }
  std::stringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

void
ShaderPreprocessor::RegisterVirtualInclude(const std::string& name,
                                           const std::string& source)
{
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  s_virtualModules[normalizeModuleName(name)] = source;
}

void
ShaderPreprocessor::UnregisterVirtualInclude(const std::string& name)
{
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  s_virtualModules.erase(normalizeModuleName(name));
}

bool
ShaderPreprocessor::HasVirtualInclude(const std::string& name)
{
  RegisterDefaultModules();
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  return s_virtualModules.find(normalizeModuleName(name)) !=
         s_virtualModules.end();
}

std::string
ShaderPreprocessor::GetVirtualInclude(const std::string& name)
{
  RegisterDefaultModules();
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  std::unordered_map<std::string, std::string>::const_iterator it =
    s_virtualModules.find(normalizeModuleName(name));
  if (it != s_virtualModules.end()) {
    return it->second;
  }
  return "";
}

void
ShaderPreprocessor::ClearVirtualIncludes()
{
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  s_virtualModules.clear();
  s_defaultsRegistered = false;
}

void
ShaderPreprocessor::RegisterDefaultModules()
{
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  if (s_defaultsRegistered) {
    return;
  }

  // <illumo/common.glsl>
  s_virtualModules["illumo/common.glsl"] = R"(
#ifndef ILLUMO_COMMON_GLSL
#define ILLUMO_COMMON_GLSL

// Standard Illumo uniform contracts
// uMVP: 4x4 model-view-projection matrix for 2D/3D visual objects
// u_resolution: 2D target resolution in pixels (width, height)
// u_scale: 2D object scale factor
// u_position: 2D object position offset

#endif
)";

  // <illumo/screen_transform.glsl>
  s_virtualModules["illumo/screen_transform.glsl"] = R"(
#ifndef ILLUMO_SCREEN_TRANSFORM_GLSL
#define ILLUMO_SCREEN_TRANSFORM_GLSL

vec4 illumoScreenToClip(vec2 pixelPos, vec2 resolution, float z) {
    float x = (pixelPos.x / resolution.x) * 2.0 - 1.0;
    float y = 1.0 - (pixelPos.y / resolution.y) * 2.0;
    return vec4(x, y, z, 1.0);
}

vec4 illumoScaledScreenToClip(vec2 pos, vec2 scale, vec2 offset, vec2 resolution, float z) {
    vec2 pixelPos = pos * scale + offset;
    return illumoScreenToClip(pixelPos, resolution, z);
}

#endif
)";

  // <illumo/vertex_2d.glsl>
  s_virtualModules["illumo/vertex_2d.glsl"] = R"(
#ifndef ILLUMO_VERTEX_2D_GLSL
#define ILLUMO_VERTEX_2D_GLSL

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aUv;

#endif
)";

  // <illumo/vertex_3d.glsl>
  s_virtualModules["illumo/vertex_3d.glsl"] = R"(
#ifndef ILLUMO_VERTEX_3D_GLSL
#define ILLUMO_VERTEX_3D_GLSL

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aUv;
layout (location = 3) in vec3 aNormal;

#endif
)";

  // <illumo/sprite.glsl>
  s_virtualModules["illumo/sprite.glsl"] = R"(
#ifndef ILLUMO_SPRITE_GLSL
#define ILLUMO_SPRITE_GLSL

vec4 sampleSprite(sampler2D tex, vec2 uv, vec4 tint) {
    return texture(tex, uv) * tint;
}

#endif
)";

  s_defaultsRegistered = true;
}

namespace {

struct PreprocessContext
{
  const PreprocessOptions* options = nullptr;
  std::vector<std::string> includeStack;
  std::unordered_set<std::string> pragmaOnceSet;
  std::vector<std::string> dependencies;
  std::unordered_set<std::string> dependencySet;
  std::string versionLine;
  int recursionDepth = 0;
};

static bool
parseInclude(const std::string& line, std::string& outPath, bool& outIsAngled)
{
  std::string trimmed = trimString(line);
  if (!startsWith(trimmed, "#include")) {
    return false;
  }
  size_t restStart = 8; // length of "#include"
  while (restStart < trimmed.size() &&
         (trimmed[restStart] == ' ' || trimmed[restStart] == '\t')) {
    ++restStart;
  }
  if (restStart >= trimmed.size()) {
    return false;
  }

  char openChar = trimmed[restStart];
  char closeChar = '\0';
  if (openChar == '<') {
    closeChar = '>';
    outIsAngled = true;
  } else if (openChar == '\"') {
    closeChar = '\"';
    outIsAngled = false;
  } else {
    return false;
  }

  size_t pathStart = restStart + 1;
  size_t pathEnd = trimmed.find(closeChar, pathStart);
  if (pathEnd == std::string::npos) {
    return false;
  }

  outPath = trimmed.substr(pathStart, pathEnd - pathStart);
  return true;
}

static bool
isPragmaOnceDirective(const std::string& line)
{
  std::string trimmed = trimString(line);
  return trimmed == "#pragma once";
}

static bool
isVersionDirective(const std::string& line, std::string& outVersion)
{
  std::string trimmed = trimString(line);
  if (startsWith(trimmed, "#version")) {
    outVersion = trimmed;
    return true;
  }
  return false;
}

static std::string
canonicalPathString(const std::string& path)
{
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(path, ec);
  if (ec) {
    return std::filesystem::path(path).lexically_normal().string();
  }
  std::filesystem::path canon = std::filesystem::weakly_canonical(abs, ec);
  std::string result = (ec ? abs.lexically_normal() : canon).string();
  std::replace(result.begin(), result.end(), '\\', '/');
#ifdef _WIN32
  std::transform(
    result.begin(), result.end(), result.begin(), [](char c) -> char {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
#endif
  return result;
}

static bool
resolveDiskFile(const std::string& includePath,
                const std::string& currentSourcePath,
                const std::vector<std::string>& searchDirs,
                std::string& outResolvedPath)
{
  // 1. Relative to directory of current file
  if (!currentSourcePath.empty()) {
    std::filesystem::path parentDir =
      std::filesystem::path(currentSourcePath).parent_path();
    std::filesystem::path candidate = parentDir / includePath;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) &&
        !std::filesystem::is_directory(candidate, ec)) {
      outResolvedPath = canonicalPathString(candidate.string());
      return true;
    }
  }

  // 2. Relative to current working directory
  {
    std::filesystem::path candidate = std::filesystem::path(includePath);
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) &&
        !std::filesystem::is_directory(candidate, ec)) {
      outResolvedPath = canonicalPathString(candidate.string());
      return true;
    }
  }

  // 3. Search directories
  for (size_t i = 0; i < searchDirs.size(); ++i) {
    std::filesystem::path candidate =
      std::filesystem::path(searchDirs[i]) / includePath;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) &&
        !std::filesystem::is_directory(candidate, ec)) {
      outResolvedPath = canonicalPathString(candidate.string());
      return true;
    }
  }

  return false;
}

static bool
processSourceInternal(const std::string& source,
                      const std::string& currentIdentifier,
                      PreprocessContext& ctx,
                      std::stringstream& output,
                      std::string& outError)
{
  if (ctx.recursionDepth > 32) {
    outError = "Maximum include recursion depth (32) exceeded";
    return false;
  }

  std::stringstream ss(source);
  std::string line;
  int lineNumber = 0;

  while (std::getline(ss, line)) {
    ++lineNumber;

    // Check version directive
    std::string ver;
    if (isVersionDirective(line, ver)) {
      if (ctx.versionLine.empty()) {
        ctx.versionLine = ver;
      }
      // Omit #version from intermediate line emission; placed at the very top
      // later
      continue;
    }

    // Check pragma once
    if (isPragmaOnceDirective(line)) {
      ctx.pragmaOnceSet.insert(currentIdentifier);
      continue;
    }

    // Check include directive
    std::string incPath;
    bool isAngled = false;
    if (parseInclude(line, incPath, isAngled)) {
      std::string normalizedModule =
        ShaderPreprocessor::HasVirtualInclude(incPath) ? incPath : "";
      if (normalizedModule.empty() && isAngled) {
        // Try stripping angle brackets / lookup
        if (ShaderPreprocessor::HasVirtualInclude(incPath)) {
          normalizedModule = incPath;
        }
      }

      std::string resolvedDiskPath;
      bool isVirtual = !normalizedModule.empty();

      if (!isVirtual) {
        if (!resolveDiskFile(incPath,
                             currentIdentifier,
                             ctx.options->searchDirectories,
                             resolvedDiskPath)) {
          outError = "Could not resolve include: '" + incPath + "' from '" +
                     currentIdentifier + "'";
          return false;
        }
      }

      const std::string targetId =
        isVirtual ? ("virtual:" + incPath) : resolvedDiskPath;

      // Check circular inclusion
      for (size_t i = 0; i < ctx.includeStack.size(); ++i) {
        if (ctx.includeStack[i] == targetId) {
          outError = "Circular include detected: '" + targetId +
                     "' already in include stack";
          return false;
        }
      }

      // Check pragma once
      if (ctx.pragmaOnceSet.find(targetId) != ctx.pragmaOnceSet.end()) {
        continue;
      }

      std::string contentToInclude;
      if (isVirtual) {
        contentToInclude = ShaderPreprocessor::GetVirtualInclude(incPath);
      } else {
        contentToInclude = readFileToString(resolvedDiskPath);
        if (contentToInclude.empty()) {
          // Check if file truly empty vs read failure
          std::error_code ec;
          if (std::filesystem::file_size(resolvedDiskPath, ec) > 0 || ec) {
            outError =
              "Failed to read include file: '" + resolvedDiskPath + "'";
            return false;
          }
        }
        if (ctx.dependencySet.insert(resolvedDiskPath).second) {
          ctx.dependencies.push_back(resolvedDiskPath);
        }
      }

      // Preprocess the included content
      ctx.includeStack.push_back(targetId);
      ctx.recursionDepth += 1;

      if (ctx.options->emitLineDirectives) {
        output << "#line 1\n";
      }

      if (!processSourceInternal(
            contentToInclude, targetId, ctx, output, outError)) {
        return false;
      }

      if (ctx.options->emitLineDirectives) {
        output << "\n#line " << (lineNumber + 1) << "\n";
      }

      ctx.recursionDepth -= 1;
      ctx.includeStack.pop_back();
      continue;
    }

    output << line << "\n";
  }

  return true;
}

} // namespace

PreprocessResult
ShaderPreprocessor::Process(const std::string& source,
                            const PreprocessOptions& options)
{
  RegisterDefaultModules();

  PreprocessContext ctx;
  ctx.options = &options;
  if (!options.sourcePath.empty()) {
    std::string canon = canonicalPathString(options.sourcePath);
    ctx.includeStack.push_back(canon);
  }

  std::stringstream body;
  std::string error;
  if (!processSourceInternal(source, options.sourcePath, ctx, body, error)) {
    PreprocessResult result;
    result.success = false;
    result.errorMessage = error;
    result.fileDependencies = ctx.dependencies;
    return result;
  }

  std::stringstream finalSource;

  // 1. Version directive first
  if (!ctx.versionLine.empty()) {
    finalSource << ctx.versionLine << "\n";
  }

  // 2. Defines immediately after version
  for (size_t i = 0; i < options.defines.size(); ++i) {
    const std::string& def = options.defines[i];
    std::string trimmedDef = trimString(def);
    if (trimmedDef.empty()) {
      continue;
    }
    size_t eqPos = trimmedDef.find('=');
    if (eqPos != std::string::npos) {
      std::string key = trimString(trimmedDef.substr(0, eqPos));
      std::string val = trimString(trimmedDef.substr(eqPos + 1));
      finalSource << "#define " << key << " " << val << "\n";
    } else {
      finalSource << "#define " << trimmedDef << "\n";
    }
  }

  // 3. Body
  finalSource << body.str();

  PreprocessResult result;
  result.success = true;
  result.source = finalSource.str();
  result.fileDependencies = ctx.dependencies;
  return result;
}

PreprocessResult
ShaderPreprocessor::ProcessFile(const std::string& filePath,
                                const PreprocessOptions& options)
{
  std::string source = readFileToString(filePath);
  if (source.empty()) {
    std::error_code ec;
    if (std::filesystem::file_size(filePath, ec) > 0 || ec) {
      PreprocessResult result;
      result.success = false;
      result.errorMessage = "Failed to open shader file: " + filePath;
      return result;
    }
  }

  PreprocessOptions fileOptions = options;
  if (fileOptions.sourcePath.empty()) {
    fileOptions.sourcePath = filePath;
  }
  return Process(source, fileOptions);
}
