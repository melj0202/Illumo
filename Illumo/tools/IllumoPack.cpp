// IllumoPack: packs a package directory into an .ilpk archive, or verifies
// one. Packing requires a valid illumo.json at the directory root; verifying
// reads every entry back with its CRC checked.
//
//   IllumoPack <package-dir> <out.ilpk>
//   IllumoPack --verify <file.ilpk>

#include <Illumo/Content/PackageArchive.h>
#include <Illumo/Content/PackageMounts.h>
#include <cstdio>
#include <filesystem>
#include <string>

static std::filesystem::path
pathOf(const char* argument)
{
  const std::string text(argument);
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

static int
verify(const std::filesystem::path& path)
{
  std::string error;
  LoadedPackage package;
  if (!PackageMounts::open(path, PackageCeilings{}, package, error)) {
    std::fprintf(stderr, "IllumoPack: %s\n", error.c_str());
    return 1;
  }
  std::unique_ptr<PackageArchive> archive =
    PackageArchive::openFile(path, error);
  if (!archive) {
    std::fprintf(stderr, "IllumoPack: %s\n", error.c_str());
    return 1;
  }
  std::vector<uint8_t> bytes;
  for (const PackageArchiveEntry& entry : archive->entries()) {
    if (!archive->read(entry, bytes, error)) {
      std::fprintf(stderr, "IllumoPack: %s\n", error.c_str());
      return 1;
    }
  }
  std::printf("%s %s: %zu files, %llu bytes\n",
              package.manifest.id.c_str(),
              package.manifest.version.c_str(),
              archive->entries().size(),
              static_cast<unsigned long long>(archive->totalBytes()));
  return 0;
}

static int
pack(const std::filesystem::path& directory,
     const std::filesystem::path& output)
{
  std::string error;
  std::error_code code;
  LoadedPackage package;
  if (!std::filesystem::is_directory(directory, code) ||
      !PackageMounts::open(directory, PackageCeilings{}, package, error)) {
    std::fprintf(stderr,
                 "IllumoPack: %s\n",
                 error.empty() ? "not a package directory" : error.c_str());
    return 1;
  }
  PackageArchiveWriter writer;
  if (!writer.addDirectory(directory, error) || !writer.write(output, error)) {
    std::fprintf(stderr, "IllumoPack: %s\n", error.c_str());
    return 1;
  }
  std::printf("Packed %s (%zu files)\n",
              package.manifest.id.c_str(),
              writer.entryCount());
  return 0;
}

int
main(int argc, char** argv)
{
  if (argc == 3 && std::string(argv[1]) == "--verify") {
    return verify(pathOf(argv[2]));
  }
  if (argc == 3 && argv[1][0] != '-') {
    return pack(pathOf(argv[1]), pathOf(argv[2]));
  }
  std::fprintf(stderr,
               "usage: IllumoPack <package-dir> <out.ilpk>\n"
               "       IllumoPack --verify <file.ilpk>\n");
  return 2;
}
