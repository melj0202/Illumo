#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <string>
#include <vector>

// The "illumo.play" scene extension: the game (an installed application id)
// that plays the scene and, in a copy an editor launches for Play, the
// virtual package root its relative references resolve against. Both are
// optional. See docs/scene-behaviours-design.md.
struct ScenePlay
{
  static constexpr const char* kExtension = "illumo.play";

  std::string application;
  std::string root;

  // Reads the extension; false when the document has none. A member that is
  // not a valid package id or normalized absolute virtual path reads empty.
  static bool read(const std::vector<SceneExtension>& extensions,
                   ScenePlay* play);
  // Replaces the extension with `play` (canonical compact JSON), keeping any
  // members it does not know; with neither member set and nothing else kept,
  // removes it.
  static void write(std::vector<SceneExtension>& extensions,
                    const ScenePlay& play);
};
