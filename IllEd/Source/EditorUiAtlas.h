#pragma once

#include "EditorToolbar.h"
#include <Illumo/Gui/GridAtlas.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>

// Implicit 6x6 cells over Assets/IllEd/editor-ui-atlas.jpg (row 0 is top).
class EditorUiAtlas
{
public:
  static constexpr unsigned int kColumns = 6;
  static constexpr unsigned int kRows = 6;
  static constexpr float kIconSize = 16.0f;

  static const char* relativePath()
  {
    return "Assets/IllEd/editor-ui-atlas.jpg";
  }

  static bool cellFor(EditorCommand command,
                      unsigned int* column,
                      unsigned int* row);
  static TextureRegion regionFor(EditorCommand command);
};
