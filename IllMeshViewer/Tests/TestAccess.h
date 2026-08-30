#pragma once

#include "MeshViewerModule.h"

class MeshViewerModuleTestAccess
{
public:
  static MeshViewerUi* ui(MeshViewerModule& module) { return module.ui(); }
  static MeshVisual* meshVisual(MeshViewerModule& module)
  {
    return module.meshVisual();
  }
  static MeshVisual* gridVisual(MeshViewerModule& module)
  {
    return module.gridVisual();
  }
  static MeshVisual* wireframeVisual(MeshViewerModule& module)
  {
    return module.wireframeVisual();
  }
};
