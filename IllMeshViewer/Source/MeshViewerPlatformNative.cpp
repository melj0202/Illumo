#include "MeshViewerPlatform.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

// Synchronous native oracle used by the workspace tests: every completion
// runs before its request returns, through the replaceable SaveLoad
// functions. The label is the file's base name, as the viewer shows it.
class NativeMeshViewerPlatform final : public MeshViewerPlatform
{
public:
  void chooseMesh(const SaveLoadDialogSpec& specification,
                  LocationCallback done) override
  {
    const std::string path = SaveLoad::GetLoadLocation(specification);
    done({ path, label(path) });
  }
  void read(const std::string& location, ReadCallback done) override
  {
    try {
      std::ifstream file(
        std::filesystem::path(std::u8string(location.begin(), location.end())),
        std::ios::binary);
      if (!file.is_open()) {
        done(false, {}, "Failed to open mesh file: " + location);
        return;
      }
      const std::string bytes{ std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>() };
      if (file.bad()) {
        done(false, {}, "Failed while reading mesh file: " + location);
        return;
      }
      done(true, bytes, {});
    } catch (const std::system_error&) {
      done(false, {}, "Invalid or inaccessible UTF-8 file path");
    }
  }

private:
  static std::string label(const std::string& path)
  {
    const std::u8string name =
      std::filesystem::path(std::u8string(path.begin(), path.end()))
        .filename()
        .u8string();
    return std::string(reinterpret_cast<const char*>(name.data()), name.size());
  }
};

MeshViewerPlatform&
MeshViewerPlatform::current()
{
  static NativeMeshViewerPlatform platform;
  return platform;
}
