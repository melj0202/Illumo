#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Scene/SceneSnapshot.h>

class SceneGraph;

// Main-thread frame-list adapter. Graph and attachments outlive frame use.
class SceneGraphDrawable : public DrawableBase
{
public:
  explicit SceneGraphDrawable(SceneGraph& graph);
  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;
  void CollectShadowCasters(Renderer* renderer) override;
  void AppendShadowCommands(Renderer* renderer) override;

private:
  enum class Pass
  {
    Collect,
    Shadow,
    Color
  };
  SceneGraph* m_graph;
  SceneSnapshotView m_snapshot;
  std::weak_ptr<const void> m_rendererIdentity;
  uint64_t m_frameSerial = 0;
  bool m_frameStarted = false;
  void emit(Renderer* renderer, Pass pass);
};
