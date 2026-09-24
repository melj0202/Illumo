#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class SceneInstance;

// Copy and paste of node subtrees through the system clipboard. The text is a
// format 2 .ilsc document holding the copied subtrees (each root with its
// world transform and no parent) and the assets they reference, tagged with
// the "illed.fragment" extension so arbitrary clipboard text never pastes.
// Pasting (EditorDocument::paste) gives every node a fresh id.
class EditorClipboard
{
public:
  static constexpr std::size_t kMaximumBytes = 4u * 1024u * 1024u;
  static constexpr const char* kExtensionKey = "illed.fragment";

  // Encodes the subtrees under roots (top-level ids, in order). Returns empty
  // when nothing exists to copy or the text would exceed kMaximumBytes.
  static std::string copy(const SceneInstance& scene,
                          const std::vector<std::string>& roots);

  // Parses clipboard text into a fragment. Fails for oversize text, text
  // that is not a scene, and scenes without the fragment tag or nodes.
  static bool read(std::string_view text,
                   SceneDocument& fragment,
                   std::string& error);
};
