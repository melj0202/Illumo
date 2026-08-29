#include "EditorUiAtlas.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

static void
testAtlasRegions()
{
  testSection("EditorUiAtlas: world-editor tool cells");
  unsigned int column = 99;
  unsigned int row = 99;
  testTrue(g,
           EditorUiAtlas::cellFor(EditorCommand::SelectTool, &column, &row) &&
             column == 0 && row == 0,
           "select is top-left cell");
  testTrue(g,
           EditorUiAtlas::cellFor(EditorCommand::CreateCube, &column, &row) &&
             column == 0 && row == 1,
           "cube cell");
  testTrue(
    g,
    EditorUiAtlas::cellFor(EditorCommand::CreatePyramid, &column, &row) &&
      column == 1 && row == 1,
    "pyramid cell");
  testTrue(g,
           EditorUiAtlas::cellFor(EditorCommand::SetMode2D, &column, &row) &&
             column == 0 && row == 3,
           "2D mode cell");
  testTrue(g,
           !EditorUiAtlas::cellFor(EditorCommand::None, &column, &row),
           "none has no cell");
  const TextureRegion cube =
    EditorUiAtlas::regionFor(EditorCommand::CreateCube);
  const TextureRegion expected = TextureRegion::gridCell(
    EditorUiAtlas::kColumns, EditorUiAtlas::kRows, 0, 1);
  testTrue(g,
           cube.u0 == expected.u0 && cube.v0 == expected.v0 &&
             cube.u1 == expected.u1 && cube.v1 == expected.v1,
           "cube UVs match the 6x6 atlas grid");
}

void
registerEditorUiAtlasTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.UiAtlas.Regions", []() {
    g = {};
    testAtlasRegions();
    return g.failures;
  });
}
