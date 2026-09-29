#pragma once
#include <filesystem>
#include <string>
#include <string_view>

// UTF-8 text for file-system paths without exceptions. The standard
// conversions (path::string(), path(std::string), u8string()) report
// unrepresentable characters by throwing, which this workspace never does.

// The path as UTF-8. Characters that cannot be encoded (a lone UTF-16
// surrogate on Windows) become U+FFFD, so the result is for display and
// logging, not for reopening the file.
std::string
pathToUtf8(const std::filesystem::path& path);

// The generic (forward-slash) form of pathToUtf8.
std::string
pathToGenericUtf8(const std::filesystem::path& path);

// Builds a path from UTF-8 text. Returns false, leaving *path unchanged, when
// the text is not valid UTF-8.
bool
pathFromUtf8(std::string_view text, std::filesystem::path* path);
