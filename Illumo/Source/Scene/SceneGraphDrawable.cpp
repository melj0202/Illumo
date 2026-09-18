#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>

SceneGraphDrawable::SceneGraphDrawable(SceneGraph& graph)
  : m_graph(&graph)
{
}
bool
SceneGraphDrawable::AppendCommands(Renderer* renderer)
{
  emit(renderer, Pass::Color);
  return true;
}
void
SceneGraphDrawable::CollectShadowCasters(Renderer* renderer)
{
  emit(renderer, Pass::Collect);
}
void
SceneGraphDrawable::AppendShadowCommands(Renderer* renderer)
{
  emit(renderer, Pass::Shadow);
}

void
SceneGraphDrawable::emit(Renderer* renderer, Pass pass)
{
  if (!isVisible()) {
    return;
  }
  const bool active = renderer != nullptr && renderer->getFrameContext().active;
  if (active && !renderer->getFrameContext().sceneSnapshotExtraction) {
    m_graph->emitDirect(renderer, static_cast<unsigned>(pass));
    return;
  }
  const std::weak_ptr<const void> identity = renderer != nullptr
                                               ? renderer->getLifetimeIdentity()
                                               : std::weak_ptr<const void>{};
  const bool sameRenderer = !identity.owner_before(m_rendererIdentity) &&
                            !m_rendererIdentity.owner_before(identity);
  const uint64_t frame =
    renderer != nullptr ? renderer->getFrameContext().frameSerial : 0;
  if (!active || !m_frameStarted || !sameRenderer || frame != m_frameSerial) {
    m_snapshot = m_graph->extract(renderer);
    m_rendererIdentity = identity;
    m_frameSerial = frame;
    m_frameStarted = active;
  }
  size_t index = 0;
  for (;;) {
    const SceneSnapshot* snapshot = m_snapshot.get();
    if (snapshot == nullptr) {
      if (renderer != nullptr) {
        renderer->reportFrameError(
          "Scene snapshot invalidated before emission completed");
      }
      return;
    }
    if (index >= snapshot->items.size()) {
      return;
    }
    const SceneRenderItem item = snapshot->items[index++];
    if (pass == Pass::Color) {
      if (item.cameraVisible) {
        item.attachment->appendSceneCommands(renderer, item.worldTransform);
      }
    } else if (pass == Pass::Collect) {
      item.attachment->collectSceneShadowCasters(renderer, item.worldTransform);
    } else if (renderer == nullptr || !item.boundsValid ||
               renderer->isShadowCasterRelevant(item.worldBounds)) {
      item.attachment->appendSceneShadowCommands(renderer, item.worldTransform);
    }
    // A callback may detach another borrowed object or recycle the ring. Never
    // touch the next item's pointer until the snapshot is validated again.
  }
}
