#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <string>
#include <string_view>

// .ilsc format 2: UTF-8 JSON shared by every Illumo program.
//
// Reading is strict and transactional: core objects reject unknown keys,
// wrong types and out-of-range values; namespaced component types and
// extension entries are preserved verbatim; an unknown non-namespaced
// component type or a newer minor version fails with "requires format 2.N";
// format 1 files fail with an explicit message. A rejected parse leaves the
// output document untouched.
//
// Writing is canonical: keys in schema order, two-space indentation and the
// shortest decimal form of every stored float, so encoding the same document
// twice is byte-identical and diffs stay readable.
class IlscCodec
{
public:
  static constexpr const char* kExtension = ".ilsc";
  // Upper bound on accepted text, well above any practical scene.
  static constexpr std::size_t kMaximumBytes = 256u * 1024u * 1024u;

  static bool parse(std::string_view text,
                    SceneDocument& document,
                    std::string& error);

  // Encodes a document that satisfies validateSceneDocument. The editor
  // block is written only when includeEditor is true and document.hasEditor.
  static std::string encode(const SceneDocument& document,
                            bool includeEditor = true);

  // Appends ".ilsc" unless the name already ends with it (case-insensitive).
  static std::string withExtension(const std::string& name);
};
