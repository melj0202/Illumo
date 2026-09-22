#include <Illumo/Rendering/AssetSource.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

// Native filesystem source: the behaviour AssetManager always had. Paths are
// canonicalized (case-folded on Windows) so aliases share one cache entry.
class FileAssetSource final : public IAssetSource
{
public:
  std::string canonical(const std::string& path) const override
  {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
      return std::filesystem::path(path).lexically_normal().string();
    }
    std::filesystem::path canonical =
      std::filesystem::weakly_canonical(absolute, error);
    std::string result =
      (error ? absolute.lexically_normal() : canonical).string();
#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(), lowercase);
#endif
    return result;
  }

  bool read(const std::string& canonical,
            std::vector<unsigned char>& bytes) const override
  {
    std::ifstream input(canonical, std::ios::binary | std::ios::ate);
    const std::streamoff size = input.tellg();
    if (!input || size < 0) {
      return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0);
    return size == 0 || static_cast<bool>(input.read(
                          reinterpret_cast<char*>(bytes.data()), size));
  }

  std::int64_t stamp(const std::string& canonical) const override
  {
    std::error_code error;
    const std::filesystem::file_time_type time =
      std::filesystem::last_write_time(canonical, error);
    return error ? 0
                 : static_cast<std::int64_t>(time.time_since_epoch().count());
  }

  bool hasFileSystem() const override { return true; }

private:
  static char lowercase(char value)
  {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  }
};

IAssetSource*
DefaultAssetSource()
{
  static FileAssetSource source;
  return &source;
}
