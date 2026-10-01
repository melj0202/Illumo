#pragma once

#include <Illumo/Content/VirtualFileSystem.h>
#include <cstddef>
#include <string>
#include <vector>

// Pure formatters for the host "vfs" console command. Each returns the lines
// to print; nothing here registers commands or reveals host paths.
class VfsConsole
{
public:
  static constexpr std::size_t kMaximumTreeLines = 400;
  static constexpr std::size_t kDefaultCatBytes = 4096;
  static constexpr std::size_t kMaximumCatBytes = 65536;

  static std::vector<std::string> mounts(const VirtualFileSystem& vfs);
  static std::vector<std::string> ls(const VirtualFileSystem& vfs,
                                     const std::string& path);
  // An iterative walk to depth levels, capped at kMaximumTreeLines.
  static std::vector<std::string> tree(const VirtualFileSystem& vfs,
                                       const std::string& path,
                                       int depth = 3);
  static std::vector<std::string> stat(const VirtualFileSystem& vfs,
                                       const std::string& path);
  // A text preview, or a hex dump when the bytes are not printable UTF-8.
  static std::vector<std::string> cat(const VirtualFileSystem& vfs,
                                      const std::string& path,
                                      std::size_t bytes = kDefaultCatBytes);

  // "mounts", "ls <path>", "tree <path> [depth]", "stat <path>",
  // "cat <path> [bytes]"; anything else prints usage.
  static std::vector<std::string> run(const VirtualFileSystem& vfs,
                                      const std::vector<std::string>& args);
  static std::vector<std::string> usage();
};
