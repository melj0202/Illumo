#pragma once

// Every user-facing editor action. Menus, the panels and keyboard shortcuts
// all produce these; EditorScene::handleCommand is the single dispatcher.
enum class EditorCommand
{
  None,
  NewDocument,
  OpenDocument,
  SaveDocument,
  SaveDocumentAs,
  // Project commands (--project): save into /project/scenes, import a file,
  // pack the project into an .ilpk.
  SaveToProject,
  ImportAsset,
  PackProject,
  ExitEditor,
  Undo,
  Redo,
  Cut,
  Copy,
  Paste,
  Duplicate,
  SelectAll,
  DeselectAll,
  Rename,
  DeleteNode,
  UnparentNode,
  // Hierarchy context actions on the selection.
  ToggleVisible,
  ToggleEnabled,
  CreateChild,
  CreateEmpty,
  CreateRect,
  CreateEllipse,
  CreateTriangle,
  CreateCube,
  CreatePyramid,
  CreateSphere,
  CreateWireCube,
  CreateWireSphere,
  // Placed immediately at the view center.
  CreateLight,
  CreateCamera,
  SelectTool,
  TranslateMode,
  RotateMode,
  ScaleMode,
  ToggleGizmoSpace,
  ToggleSnap,
  // Rotate and scale about the selection's bounds centre instead of the
  // primary node's origin.
  TogglePivot,
  // Grid and snap view settings (never dirty the document); the steps walk
  // fixed ladders of common values.
  ToggleGrid,
  GridSpacingDown,
  GridSpacingUp,
  SnapMoveDown,
  SnapMoveUp,
  SnapRotateDown,
  SnapRotateUp,
  SnapScaleDown,
  SnapScaleUp,
  // Arrange: line the selection's top-level nodes up on one side or the
  // centre of their combined bounds, or space their centres evenly.
  AlignMinX,
  AlignCenterX,
  AlignMaxX,
  AlignMinY,
  AlignCenterY,
  AlignMaxY,
  AlignMinZ,
  AlignCenterZ,
  AlignMaxZ,
  DistributeX,
  DistributeY,
  DistributeZ,
  // Focuses the hierarchy's filter field.
  FindInHierarchy,
  // Hierarchy: put the top-level selection under a new parent or lift a
  // group's children out, walk up or down, lock nodes against viewport
  // picking, and isolate the selection in the view.
  GroupSelection,
  UngroupSelection,
  SelectParent,
  SelectChildren,
  ToggleLock,
  ToggleIsolate,
  // Lowers each selected subtree onto the surface or ground below it.
  DropToFloor,
  SetMode2D,
  SetMode3D,
  CycleColor,
  NudgeExtent,
  ResetCamera,
  FrameSelection,
  // Plays the scene in its game's own window, or stops it (Ctrl+P; the host
  // keeps F5 for asset reloads).
  PlayScene,
  // View > panels: show or hide, pop out or dock, and reset the layout.
  ToggleHierarchyPanel,
  ToggleAssetsPanel,
  ToggleToolsPanel,
  ToggleInspectorPanel,
  PopOutHierarchyPanel,
  PopOutAssetsPanel,
  PopOutToolsPanel,
  PopOutInspectorPanel,
  ResetLayout
};
