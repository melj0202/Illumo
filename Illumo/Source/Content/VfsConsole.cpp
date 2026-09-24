#include <Illumo/Content/VfsConsole.h>

#include <Illumo/Content/VirtualPath.h>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <system_error>
#include <vector>

static std::string
sizeText(uint64_t bytes)
{
  char buffer[32];
  if (bytes < 1024u) {
    std::snprintf(
      buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
  } else if (bytes < 1024u * 1024u) {
    std::snprintf(buffer, sizeof(buffer), "%.1f KiB", bytes / 1024.0);
  } else {
    std::snprintf(
      buffer, sizeof(buffer), "%.1f MiB", bytes / (1024.0 * 1024.0));
  }
  return buffer;
}

// A whole-string decimal count, or fallback when the text is not one.
static long long
parseCount(const std::string& text, long long fallback)
{
  long long value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed =
    std::from_chars(text.data(), end, value);
  return parsed.ec == std::errc() && parsed.ptr == end ? value : fallback;
}

static std::string
childPath(const std::string& directory, const std::string& name)
{
  return directory == "/" ? "/" + name : directory + "/" + name;
}

std::vector<std::string>
VfsConsole::usage()
{
  return { "usage: vfs mounts | ls <path> | tree <path> [depth] | stat <path> "
           "| cat <path> [bytes]" };
}

std::vector<std::string>
VfsConsole::mounts(const VirtualFileSystem& vfs)
{
  std::vector<std::string> lines;
  const std::shared_ptr<const VfsMountTable> table = vfs.table();
  if (table->mounts().empty()) {
    lines.push_back("(no mounts)");
    return lines;
  }
  for (const VfsMount& mount : table->mounts()) {
    std::string line = mount.point + "  ";
    for (std::size_t index = 0; index < mount.layers.size(); ++index) {
      const VfsLayer& layer = mount.layers[index];
      if (index > 0) {
        line += " over ";
      }
      line +=
        (layer.packageId.empty() ? std::string("engine") : layer.packageId) +
        " (" + layer.backend->kindName() +
        (layer.backend->writable() ? ", writable" : "") + ")";
    }
    lines.push_back(line);
    for (const std::string& conflict : mount.conflicts) {
      lines.push_back("  conflict: " + conflict);
    }
  }
  return lines;
}

std::vector<std::string>
VfsConsole::ls(const VirtualFileSystem& vfs, const std::string& path)
{
  std::vector<std::string> lines;
  std::vector<VfsEntry> entries;
  std::string error;
  if (!vfs.list(path, entries, error)) {
    lines.push_back(error);
    return lines;
  }
  if (entries.empty()) {
    lines.push_back("(empty)");
  }
  for (const VfsEntry& entry : entries) {
    lines.push_back(entry.kind == VfsKind::Directory
                      ? entry.name + "/"
                      : entry.name + "  " + sizeText(entry.size));
  }
  return lines;
}

std::vector<std::string>
VfsConsole::tree(const VirtualFileSystem& vfs,
                 const std::string& path,
                 int depth)
{
  struct Pending
  {
    std::string path;
    std::string name;
    int level;
  };
  std::vector<std::string> lines;
  std::string normalized;
  VfsStat root;
  std::string error;
  if (!VirtualPath::normalize(path, normalized) ||
      !vfs.stat(normalized, root, error)) {
    lines.push_back(error.empty() ? "Invalid virtual path: " + path : error);
    return lines;
  }
  lines.push_back(normalized);
  // Depth-first with an explicit stack, children pushed in reverse so they
  // print in name order.
  std::vector<Pending> stack;
  const int limit = std::clamp(depth, 1, 16);
  std::vector<VfsEntry> children;
  if (root.kind == VfsKind::Directory &&
      vfs.list(normalized, children, error)) {
    for (std::size_t index = children.size(); index-- > 0;) {
      stack.push_back(
        { childPath(normalized, children[index].name),
          children[index].name +
            (children[index].kind == VfsKind::Directory ? "/" : ""),
          1 });
    }
  }
  while (!stack.empty()) {
    if (lines.size() >= kMaximumTreeLines) {
      lines.push_back("... (output truncated)");
      break;
    }
    const Pending item = stack.back();
    stack.pop_back();
    lines.push_back(std::string(static_cast<std::size_t>(item.level) * 2, ' ') +
                    item.name);
    if (item.name.empty() || item.name.back() != '/' || item.level >= limit) {
      continue;
    }
    if (!vfs.list(item.path, children, error)) {
      continue;
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      stack.push_back(
        { childPath(item.path, children[index].name),
          children[index].name +
            (children[index].kind == VfsKind::Directory ? "/" : ""),
          item.level + 1 });
    }
  }
  return lines;
}

std::vector<std::string>
VfsConsole::stat(const VirtualFileSystem& vfs, const std::string& path)
{
  std::vector<std::string> lines;
  VfsStat stat;
  std::string error;
  if (!vfs.stat(path, stat, error)) {
    lines.push_back(error);
    return lines;
  }
  lines.push_back(std::string("kind: ") +
                  (stat.kind == VfsKind::Directory ? "directory" : "file"));
  if (stat.kind == VfsKind::File) {
    lines.push_back("size: " + std::to_string(stat.size) + " bytes");
  }
  lines.push_back("package: " +
                  (stat.packageId.empty() ? std::string("-") : stat.packageId));
  return lines;
}

static bool
printable(const std::vector<uint8_t>& bytes)
{
  for (uint8_t value : bytes) {
    if ((value < 0x20 && value != '\n' && value != '\r' && value != '\t') ||
        value == 0x7F) {
      return false;
    }
  }
  return true;
}

std::vector<std::string>
VfsConsole::cat(const VirtualFileSystem& vfs,
                const std::string& path,
                std::size_t bytes)
{
  std::vector<std::string> lines;
  std::shared_ptr<VfsFile> file;
  std::string error;
  file = vfs.open(path, error);
  std::vector<uint8_t> data;
  const std::size_t count = std::min(bytes, kMaximumCatBytes);
  if (!file || !file->read(0, count, data, error)) {
    lines.push_back(error);
    return lines;
  }
  if (printable(data)) {
    std::string current;
    for (uint8_t value : data) {
      if (value == '\n') {
        lines.push_back(current);
        current.clear();
      } else if (value != '\r') {
        current.push_back(static_cast<char>(value));
      }
    }
    if (!current.empty()) {
      lines.push_back(current);
    }
  } else {
    for (std::size_t row = 0; row < data.size(); row += 16) {
      char line[96];
      int written = std::snprintf(
        line, sizeof(line), "%08zx ", static_cast<std::size_t>(row));
      std::string text(line, static_cast<std::size_t>(written));
      std::string ascii;
      for (std::size_t column = 0; column < 16; ++column) {
        if (row + column < data.size()) {
          const uint8_t value = data[row + column];
          written = std::snprintf(line, sizeof(line), " %02x", value);
          text.append(line, static_cast<std::size_t>(written));
          ascii.push_back(
            value >= 0x20 && value < 0x7F ? static_cast<char>(value) : '.');
        } else {
          text += "   ";
        }
      }
      lines.push_back(text + "  |" + ascii + "|");
    }
  }
  if (file->size() > data.size()) {
    lines.push_back("... (" + std::to_string(file->size() - data.size()) +
                    " more bytes)");
  }
  return lines;
}

std::vector<std::string>
VfsConsole::run(const VirtualFileSystem& vfs,
                const std::vector<std::string>& args)
{
  if (args.empty()) {
    return usage();
  }
  const std::string& verb = args[0];
  if (verb == "mounts" && args.size() == 1) {
    return mounts(vfs);
  }
  if (verb == "ls" && args.size() <= 2) {
    return ls(vfs, args.size() == 2 ? args[1] : std::string("/"));
  }
  if (verb == "tree" && args.size() <= 3) {
    const int depth =
      static_cast<int>(args.size() == 3 ? parseCount(args[2], 3) : 3);
    return tree(vfs, args.size() >= 2 ? args[1] : std::string("/"), depth);
  }
  if (verb == "stat" && args.size() == 2) {
    return stat(vfs, args[1]);
  }
  if (verb == "cat" && (args.size() == 2 || args.size() == 3)) {
    const long long requested =
      args.size() == 3 ? parseCount(args[2], 0) : kDefaultCatBytes;
    return cat(vfs,
               args[1],
               requested > 0 ? static_cast<std::size_t>(requested)
                             : kDefaultCatBytes);
  }
  return usage();
}
