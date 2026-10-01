#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <string>
#include <string_view>
#include <vector>

// A list of strings stored as one member of a namespaced scene extension
// ("illed.view": {"locked": ["n1", "n2"]}), for editor and product data that
// rides with a scene without changing the format. Other members of the
// extension are kept as they are.
struct SceneExtensionList
{
  // The member's strings, in order; empty when the extension, the member or
  // a string value is missing (non-string entries are skipped).
  static std::vector<std::string> read(
    const std::vector<SceneExtension>& extensions,
    std::string_view key,
    std::string_view member);
  // Replaces the member (canonical compact JSON); an empty list removes it,
  // and the extension goes when nothing else is left in it.
  static void write(std::vector<SceneExtension>& extensions,
                    std::string_view key,
                    std::string_view member,
                    const std::vector<std::string>& values);
};
