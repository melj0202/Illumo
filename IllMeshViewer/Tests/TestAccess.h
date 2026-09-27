#pragma once

#include "MeshViewerScene.h"

class MeshViewerSceneTestAccess
{
public:
  static MeshViewerUi* ui(MeshViewerScene& module) { return module.ui(); }
  static MeshVisual* meshVisual(MeshViewerScene& module)
  {
    return module.meshVisual();
  }
  static MeshVisual* gridVisual(MeshViewerScene& module)
  {
    return module.gridVisual();
  }
  static MeshVisual* wireframeVisual(MeshViewerScene& module)
  {
    return module.wireframeVisual();
  }
};
