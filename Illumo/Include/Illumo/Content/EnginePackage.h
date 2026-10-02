#pragma once

#include <Illumo/Content/PackageManifest.h>
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Rendering/AssetSource.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

// engine.ilpk as a file tree that follows the file on disk (D-E38). The
// archive is read into memory, so the file is never held open and a rebuilt
// package can replace it while the runtime runs; refresh() re-reads it once
// its size or write time changes. A replacement that does not open as the
// illumo-engine content package is ignored and the current content kept.
// Thread-safe: readers see the archive current when they call.
class EngineArchiveBackend final : public IVfsBackend
{
public:
  static std::shared_ptr<EngineArchiveBackend> open(
    const std::filesystem::path& path,
    const PackageCeilings& ceilings,
    std::string& error);

  // Re-reads the file when it changed on disk; true when new content was
  // taken. A failed re-read leaves the current content and reports why.
  bool refresh(std::string* error = nullptr);
  // How many times content has been taken (1 after open).
  std::uint64_t generation() const;
  // A member's content stamp (its CRC-32, never 0), or 0 when it is absent.
  std::int64_t memberStamp(std::string_view member) const;

  bool stat(std::string_view relative, VfsStat& output) const override;
  bool list(std::string_view relative,
            std::vector<VfsEntry>& entries) const override;
  bool read(std::string_view relative,
            std::vector<uint8_t>& bytes,
            std::string& error) const override;
  bool readRange(std::string_view relative,
                 uint64_t offset,
                 std::size_t bytes,
                 std::vector<uint8_t>& output,
                 std::string& error) const override;
  const char* kindName() const override { return "archive"; }

private:
  struct FileState
  {
    std::filesystem::file_time_type writeTime{};
    std::uintmax_t size = 0;
    bool operator==(const FileState&) const = default;
  };

  EngineArchiveBackend(std::filesystem::path path, PackageCeilings ceilings);
  // The archive at m_path, checked to be the engine package.
  std::shared_ptr<ArchiveVfsBackend> load(FileState* state,
                                          std::string& error) const;
  std::shared_ptr<const ArchiveVfsBackend> current() const;

  std::filesystem::path m_path;
  PackageCeilings m_ceilings;
  mutable std::mutex m_mutex;
  std::shared_ptr<const ArchiveVfsBackend> m_archive;
  std::uint64_t m_generation = 0;
  // Serializes refreshes; the file state last taken, or last rejected so a
  // bad file is reported once rather than on every poll.
  std::mutex m_refreshMutex;
  FileState m_taken;
  FileState m_rejected;
};

// The engine's own files for a distribution (D-E38): engine.ilpk beside the
// runtime, whose root holds what a development build stages loose as Assets/
// (Branding/, Fonts/, Skybox/, ...) plus Shader/. Native only.
class EnginePackage
{
public:
  static constexpr const char* kFileName = "engine.ilpk";
  static constexpr const char* kPackageId = "illumo-engine";

  // The archive beside the runtime when there is no loose Assets/ directory
  // there (a development build); null with an empty error when there is
  // neither or the loose directory wins. A present but invalid archive (bad
  // archive, missing manifest, wrong id or kind) is null with an error.
  static std::shared_ptr<EngineArchiveBackend> open(
    const std::filesystem::path& runtimeDirectory,
    const PackageCeilings& ceilings,
    std::string& error);
};

// Serves engine files from the package under the names engine code already
// uses: "Assets/<x>" is the member <x> and "Shader/<x>" the member
// Shader/<x>, given relative to the runtime directory (the working directory)
// or absolute below it. Every other name, and a member the package lacks,
// falls through to the native file system, so products and user files are
// unaffected. Hot reload sees a rebuilt package: stamps are each member's
// CRC, so AssetManager's polling reloads just the files whose content
// changed, and every read takes the package's current content. Thread-safe.
class EnginePackageSource final : public IAssetSource
{
public:
  EnginePackageSource(std::shared_ptr<EngineArchiveBackend> package,
                      std::filesystem::path runtimeDirectory,
                      IAssetSource* fallback);

  std::string canonical(const std::string& path) const override;
  bool read(const std::string& canonical,
            std::vector<unsigned char>& bytes) const override;
  std::int64_t stamp(const std::string& canonical) const override;
  bool hasFileSystem() const override { return true; }

  // The package member a name maps to, or false when it names none.
  bool memberFor(const std::string& path, std::string* member) const;

private:
  std::shared_ptr<EngineArchiveBackend> m_package;
  std::filesystem::path m_runtimeDirectory;
  IAssetSource* m_fallback;
};
