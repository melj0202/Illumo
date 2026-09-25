// This target consumes only Illumo::Illumo, with no private vendor include
// paths.
#if __has_include(<stb/stb_image.h>) || \
  __has_include(<tinyobjloader/tiny_obj_loader.h>) || \
  __has_include(<json/single_include/nlohmann/json.hpp>) || \
  __has_include(<glfw-3.4/include/GLFW/glfw3.h>) || \
  __has_include(<glew-2.1.0/include/GL/glew.h>) || \
  __has_include(<tracy-0.13.1/public/tracy/Tracy.hpp>)
#error "Illumo exports unrelated vendor headers to public consumers"
#endif

#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/DebugModule.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Engine/IModuleHost.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Foundation/ArrayQueue.h>
#include <Illumo/Foundation/AxisAlignedBounds3.h>
#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Foundation/MacroDefs.h>
#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Foundation/RollingMetric.h>
#include <Illumo/Gui/GridAtlas.h>
#include <Illumo/Gui/GuiDialog.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Gui/GuiTypes.h>
#include <Illumo/Platform/AtomicFile.h>
#include <Illumo/Platform/Clipboard.h>
#include <Illumo/Platform/PlatformTimer.h>
#include <Illumo/Platform/ProcessMemoryStats.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Platform/SystemInfo.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/ITexture.h>
#include <Illumo/Rendering/MeshData.h>
#include <Illumo/Rendering/MeshLoader.h>
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
#include <Illumo/Rendering/RenderPass.h>
#include <Illumo/Rendering/RenderStyle.h>
#include <Illumo/Rendering/RenderTargetPool.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Rendering/SplashText.h>
#include <Illumo/Rendering/UiScale.h>
#include <Illumo/Rendering/WorldLook.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>
#include <Illumo/Scene/SceneNodeHandle.h>
#include <Illumo/Scene/Transform3D.h>
#include <Illumo/Services/ArenaAlloc.h>
#include <Illumo/Services/ChainedStackAlloc.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandLineCore.h>
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
#include <Illumo/Services/WorkerPool.h>

#include <type_traits>

int
main()
{
  static_assert(!std::is_copy_constructible_v<Illumo>);
  constexpr RenderCommand legacyPipelineCommand{ CommandType::SetPipelineState,
                                                 PipelineState{},
                                                 {} };
  static_assert(legacyPipelineCommand.clearDepthValue == 1.0f);
  static_assert(std::is_trivially_copyable_v<RenderCommand>);
  static_assert(sizeof(RenderCommand) <= 72);
  static_assert(!std::is_copy_constructible_v<InputManager>);
  static_assert(!std::is_copy_assignable_v<InputManager>);
  static_assert(!std::is_move_constructible_v<InputManager>);
  static_assert(!std::is_move_assignable_v<InputManager>);
  static_assert(!std::is_copy_constructible_v<CommandRegistry>);
  static_assert(!std::is_move_constructible_v<CommandRegistry>);
  static_assert(std::is_destructible_v<IllumoConfig>);
  static_assert(std::is_destructible_v<IllumoApplicationDefinition>);
  static_assert(std::has_virtual_destructor_v<IModuleHost>);
  static_assert(std::is_destructible_v<FramePacer>);
  static_assert(std::is_destructible_v<PlatformTimerScope>);
  static_assert(std::has_virtual_destructor_v<IEnvVars>);
  static_assert(std::is_destructible_v<GameVisual>);
  static_assert(std::is_destructible_v<MeshVisual>);
  static_assert(std::is_destructible_v<MeshData>);
  static_assert(std::is_destructible_v<MeshLoadOptions>);
  static_assert(std::is_destructible_v<MeshLoadResult>);
  static_assert(std::has_virtual_destructor_v<IMeshLoaderBackend>);
  static_assert(std::is_base_of_v<ISceneRenderAttachment, MeshVisual>);
  static_assert(std::is_base_of_v<DrawableBase, MeshVisual>);
  static_assert(!std::is_copy_constructible_v<SceneGraph>);
  static_assert(std::has_virtual_destructor_v<ISceneRenderAttachment>);
  static_assert(std::is_destructible_v<RenderPassDesc>);
  static_assert(std::is_destructible_v<RenderTargetPool>);
  static_assert(std::is_destructible_v<PreprocessOptions>);
  SceneGraph sceneGraph;
  const SceneNodeHandle sceneNode = sceneGraph.createNode();
  static_assert(!std::is_base_of_v<DrawableBase, SceneGraph>);
  static_assert(std::is_base_of_v<DrawableBase, SceneGraphDrawable>);
  SceneGraphDrawable graphDrawable(sceneGraph);
  SceneNodeDesc description;
  description.name = "public";
  description.parent = sceneNode;
  const SceneNodeHandle child = sceneGraph.createNode(description);
  Transform3D transform;
  sceneGraph.getLocalTransform(child, &transform);
  sceneGraph.setLocalTransforms(&child, &transform, 1);
  sceneGraph.setName(child, "renamed");
  sceneGraph.findByName("renamed");
  sceneGraph.getName(child);
  sceneGraph.setUserData(child, 5);
  uint64_t payload = 0;
  sceneGraph.getUserData(child, &payload);
  sceneGraph.getNextSibling(child);
  sceneGraph.firstNode();
  sceneGraph.nextNode(sceneNode);
  sceneGraph.getAttachmentCount(child);
  sceneGraph.getAttachment(child, 0);
  sceneGraph.addAttachment(child, nullptr);
  sceneGraph.removeAttachment(child, nullptr);
  sceneGraph.notifyAttachmentChanged(child);
  sceneGraph.canSetParent(child, sceneNode);
  sceneGraph.isEffectivelyVisible(child);
  SceneRayHit hit;
  sceneGraph.raycast(Vector3(0), Vector3(0, 0, 1), &hit);
  std::vector<SceneRayHit> candidates;
  sceneGraph.raycastCandidates(Vector3(0), Vector3(0, 0, 1), &candidates);
  std::vector<SceneNodeHandle> overlaps;
  sceneGraph.queryBounds(AxisAlignedBounds3{}, &overlaps);
  std::vector<SceneChange> changes;
  sceneGraph.readChanges(sceneGraph.getChangeSequence(), &changes);
  sceneGraph.getStructuralRevision();
  sceneGraph.getStatistics();
  const SceneSnapshotView snapshot = sceneGraph.extract(nullptr);
  snapshot.get();
  sceneGraph.invalidateSnapshots();
  WorkerPool workers;
  workers.start(0);
  workers.getWorkerCount();
  workers.submitRange(0, 1, [](void*, size_t, size_t) noexcept {}, nullptr);
  workers.join();
  workers.stop();
  const AxisAlignedBounds3 bounds{ Vector3(-1.0f), Vector3(1.0f) };
  if (!sceneGraph.isNodeValid(sceneNode) || !bounds.isValid()) {
    return 1;
  }
  return 0;
}
