#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/DebugModule.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Foundation/ArrayQueue.h>
#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Foundation/MacroDefs.h>
#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Foundation/RollingMetric.h>
#include <Illumo/Gui/GridAtlas.h>
#include <Illumo/Gui/GuiDialog.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiTypes.h>
#include <Illumo/Platform/Clipboard.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/ITexture.h>
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Rendering/Primitives/ShapePrimitive.h>
#include <Illumo/Rendering/Primitives/SpriteAnimation.h>
#include <Illumo/Rendering/Primitives/SpritePrimitive.h>
#include <Illumo/Rendering/Primitives/TextPrimitive.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/RenderLayerId.h>
#include <Illumo/Rendering/RenderStyle.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/SplashText.h>
#include <Illumo/Rendering/WorldLook.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneNodeHandle.h>
#include <Illumo/Scene/Transform3D.h>
#include <Illumo/Services/ArenaAlloc.h>
#include <Illumo/Services/ChainedStackAlloc.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/DebugAlloc.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/IAllocator.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputContext.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/KeyCode.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Services/MallocAlloc.h>
#include <Illumo/Services/PoolAlloc.h>
#include <Illumo/Services/SysCmdLine.h>

#include <type_traits>

int
main()
{
  static_assert(!std::is_copy_constructible_v<Illumo>);
  static_assert(std::is_destructible_v<IllumoConfig>);
  static_assert(std::is_destructible_v<IllumoApplicationDefinition>);
  static_assert(std::has_virtual_destructor_v<IEnvVars>);
  static_assert(std::is_destructible_v<GameVisual>);
  static_assert(std::is_destructible_v<MeshVisual>);
  static_assert(std::is_base_of_v<ISceneRenderAttachment, MeshVisual>);
  static_assert(std::is_base_of_v<DrawableBase, MeshVisual>);
  static_assert(!std::is_copy_constructible_v<SceneGraph>);
  static_assert(std::has_virtual_destructor_v<ISceneRenderAttachment>);
  SceneGraph sceneGraph;
  const SceneNodeHandle sceneNode = sceneGraph.createNode();
  if (!sceneGraph.isNodeValid(sceneNode)) {
    return 1;
  }
  return 0;
}
