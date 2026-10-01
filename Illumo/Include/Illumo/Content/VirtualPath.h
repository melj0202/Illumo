#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// One path grammar for the virtual file tree, packages, scenes and host file
// services. A virtual path is absolute and '/'-separated ("/app/meshes/a.obj"),
// case-sensitive, and never contains "." or ".." components. Components use the
// portable rules the host file services have always enforced, so a loose
// package directory and its packed archive resolve identically on Windows.
class VirtualPath
{
public:
  static constexpr std::size_t kMaximumPathBytes = 1024;
  static constexpr std::size_t kMaximumComponentBytes = 255;

  // One name: non-empty UTF-8 of at most 255 bytes, no \ / : * ? " < > |,
  // control characters, "." or "..", trailing dot or space, ".illumo-" host
  // staging prefix, or DOS device stem (CON, NUL, COM1, ...), the last two
  // compared case-insensitively.
  static bool validComponent(std::string_view component);

  // A non-empty relative path of valid components separated by single '/'.
  // This is the rule host file services apply to package and storage names.
  static bool validRelative(std::string_view path);

  // Normalizes an absolute path: repeated '/' collapse and a trailing '/' is
  // dropped; "/" is the root. Returns false (leaving output untouched) for a
  // relative path, an invalid component or a path over kMaximumPathBytes.
  static bool normalize(std::string_view path, std::string& output);

  // Resolves a reference against a base directory. An absolute reference is
  // normalized on its own; a relative one is appended to the normalized base.
  static bool join(std::string_view baseDirectory,
                   std::string_view reference,
                   std::string& output);

  // The directory containing a normalized path ("/" for "/" and "/a").
  static std::string parent(std::string_view normalized);

  // The last component of a normalized path (empty for "/").
  static std::string_view fileName(std::string_view normalized);

  // The first component of a normalized path (empty for "/"): the mount name.
  static std::string_view mountName(std::string_view normalized);

  // True when a normalized path equals a normalized prefix or lies below it.
  static bool isWithin(std::string_view normalized, std::string_view prefix);

  // The part of a normalized path below a normalized prefix, without a leading
  // '/' (empty when they are equal). Requires isWithin(normalized, prefix).
  static std::string_view relativeTo(std::string_view normalized,
                                     std::string_view prefix);
};
