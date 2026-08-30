#include "EditorUiAtlas.h"

bool
EditorUiAtlas::cellFor(EditorCommand command,
                       unsigned int* column,
                       unsigned int* row)
{
  if (column == nullptr || row == nullptr) {
    return false;
  }
  unsigned int cellColumn = 0;
  unsigned int cellRow = 0;
  switch (command) {
    case EditorCommand::SelectTool:
      cellColumn = 0;
      cellRow = 0;
      break;
    case EditorCommand::CreateEmpty:
      cellColumn = 4;
      cellRow = 0;
      break;
    case EditorCommand::CreateRect:
      cellColumn = 5;
      cellRow = 0;
      break;
    case EditorCommand::CreateEllipse:
      cellColumn = 4;
      cellRow = 1;
      break;
    case EditorCommand::CreateTriangle:
      cellColumn = 5;
      cellRow = 1;
      break;
    case EditorCommand::CreateCube:
      cellColumn = 0;
      cellRow = 1;
      break;
    case EditorCommand::CreatePyramid:
      cellColumn = 1;
      cellRow = 1;
      break;
    case EditorCommand::CreateSphere:
      cellColumn = 2;
      cellRow = 1;
      break;
    case EditorCommand::SetMode2D:
      cellColumn = 0;
      cellRow = 3;
      break;
    case EditorCommand::SetMode3D:
      cellColumn = 1;
      cellRow = 3;
      break;
    case EditorCommand::CycleColor:
      cellColumn = 2;
      cellRow = 3;
      break;
    case EditorCommand::DeleteNode:
      cellColumn = 3;
      cellRow = 4;
      break;
    case EditorCommand::UnparentNode:
      cellColumn = 5;
      cellRow = 2;
      break;
    case EditorCommand::ResetCamera:
      cellColumn = 5;
      cellRow = 5;
      break;
    case EditorCommand::NewDocument:
      cellColumn = 2;
      cellRow = 5;
      break;
    case EditorCommand::OpenDocument:
      cellColumn = 2;
      cellRow = 4;
      break;
    case EditorCommand::SaveDocument:
    case EditorCommand::SaveDocumentAs:
      cellColumn = 1;
      cellRow = 4;
      break;
    default:
      return false;
  }
  *column = cellColumn;
  *row = cellRow;
  return true;
}

TextureRegion
EditorUiAtlas::regionFor(EditorCommand command)
{
  unsigned int column = 0;
  unsigned int row = 0;
  if (!cellFor(command, &column, &row)) {
    return TextureRegion{};
  }
  return GridAtlas::computeCellRegion(column, row, kColumns, kRows);
}
