#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class IAssetSource;

struct PreprocessOptions
{
  std::string sourcePath;
  std::vector<std::string> defines;
  std::vector<std::string> searchDirectories;
  bool emitLineDirectives = true;
};

struct PreprocessResult
{
  std::string source;
  bool success = true;
  std::string errorMessage;
  std::vector<std::string> fileDependencies;
};

class ShaderPreprocessor
{
public:
  static PreprocessResult Process(const std::string& source,
                                  const PreprocessOptions& options = {});

  // Reads the file through `files` (DefaultAssetSource() when null), so an
  // AssetManager's own source, or a distribution's engine package, serves
  // it. File includes still resolve on the native file system.
  static PreprocessResult ProcessFile(const std::string& filePath,
                                      const PreprocessOptions& options = {},
                                      const IAssetSource* files = nullptr);

  static void RegisterVirtualInclude(const std::string& name,
                                     const std::string& source);
  static void UnregisterVirtualInclude(const std::string& name);
  static bool HasVirtualInclude(const std::string& name);
  static std::string GetVirtualInclude(const std::string& name);
  static void ClearVirtualIncludes();
  static void RegisterDefaultModules();

private:
  static std::string normalizeModuleName(const std::string& name);
};
