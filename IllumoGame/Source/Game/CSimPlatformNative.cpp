#include "CSimPlatform.h"
#include "RuleCatalogLoader.h"
#include <Illumo/Platform/AtomicFile.h>
#include <Illumo/Platform/Clipboard.h>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

static std::filesystem::path
utf8Path(const std::string& location)
{
  return std::filesystem::path(std::u8string(location.begin(), location.end()));
}

// "*.EXT" names one extension; a chosen path without it gains ".ext".
static std::string
completeExtension(const std::string& location, const std::string& pattern)
{
  if (location.empty() || pattern.size() < 3 || pattern[0] != '*' ||
      pattern[1] != '.' ||
      pattern.find_first_of(";*", 1) != std::string::npos) {
    return location;
  }
  std::string extension = pattern.substr(1);
  for (char& character : extension) {
    character =
      static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  if (location.size() >= extension.size()) {
    std::string ending = location.substr(location.size() - extension.size());
    for (char& character : ending) {
      character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    if (ending == extension) {
      return location;
    }
  }
  return location + extension;
}

// Synchronous native oracle used by the workspace tests: every completion runs
// before its request returns, through the replaceable platform functions.
class NativeCSimPlatform final : public CSimPlatform
{
public:
  void chooseLoadLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override
  {
    done(SaveLoad::GetLoadLocation(specification));
  }
  void chooseSaveLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override
  {
    done(completeExtension(SaveLoad::GetSaveLocation(specification),
                           specification.extensionPattern));
  }
  void readFirst(std::vector<std::string> locations, ReadCallback done) override
  {
    CSimReadResult result;
    result.missing = true;
    for (const std::string& location : locations) {
      try {
        std::ifstream file(utf8Path(location), std::ios::binary);
        if (!file.is_open()) {
          continue;
        }
        result.missing = false;
        result.location = location;
        result.bytes.assign(std::istreambuf_iterator<char>(file),
                            std::istreambuf_iterator<char>());
        if (file.bad()) {
          result.bytes.clear();
          result.error = "Failed to read: " + location;
        } else {
          result.success = true;
        }
      } catch (const std::system_error&) {
        result.missing = false;
        result.error = "Invalid or inaccessible UTF-8 file path";
      }
      break;
    }
    if (result.missing) {
      result.error = "Failed to open for loading: " +
                     (locations.empty() ? std::string() : locations.back());
    }
    done(result);
  }
  void writeFile(const std::string& location,
                 std::string bytes,
                 WriteCallback done) override
  {
    if (location.empty()) {
      done(false, "Save path is empty");
      return;
    }
    std::string error;
    bool written = false;
    try {
      written = AtomicFile::write(
        utf8Path(location),
        [&bytes](std::ostream& stream, std::string*) {
          stream.write(bytes.data(),
                       static_cast<std::streamsize>(bytes.size()));
          return static_cast<bool>(stream);
        },
        &error);
    } catch (const std::system_error&) {
      error = "Invalid or inaccessible UTF-8 file path";
    }
    done(written, error);
  }
  void readClipboard(TextCallback done) override
  {
    done(true, Clipboard::GetText());
  }
  void writeClipboard(const std::string& text) override
  {
    Clipboard::SetText(text);
  }
  void saveUserCatalog(std::vector<RuleFamilyDefinition> families,
                       std::vector<RuleSetDefinition> rules,
                       WriteCallback done) override
  {
    std::error_code errorCode;
    const std::filesystem::path workingDirectory =
      std::filesystem::current_path(errorCode);
    std::string error;
    if (errorCode) {
      done(false, "The working directory is unavailable.");
      return;
    }
    if ((!families.empty() && !RuleCatalogLoader::saveUserFamilies(
                                workingDirectory, families, &error)) ||
        !RuleCatalogLoader::saveUserRules(workingDirectory, rules, &error)) {
      done(false, error);
      return;
    }
    done(true, {});
  }
};

CSimPlatform&
CSimPlatform::current()
{
  static NativeCSimPlatform platform;
  return platform;
}
