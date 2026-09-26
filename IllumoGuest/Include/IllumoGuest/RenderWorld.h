#pragma once

#include <Illumo/Rendering/IRenderWorld.h>
#include <IllumoGuest/Frame.h>
#include <IllumoGuest/RecordingBackend.h>
#include <deque>
#include <unordered_map>
#include <vector>

// IRenderWorld for a guest granted HostRender: every call is validated with
// the host's rules and queued as a frame schema v6 world operation, so the
// host never has to refuse (and retire) this guest. An instance whose mesh is
// still uploading waits here and is created once the host copy is ready;
// later calls on it change the waiting description instead of travelling.
// Meshes must be static lit meshes (Pos3Norm3Color4U8Uv2).
class GuestRenderWorld final : public IRenderWorld
{
public:
  explicit GuestRenderWorld(const GuestRecordingBackend& backend);

  bool createMaterial(RenderMaterialId id,
                      const RenderMaterialDesc& desc) override;
  bool updateMaterial(RenderMaterialId id,
                      const RenderMaterialDesc& desc) override;
  bool destroyMaterial(RenderMaterialId id) override;
  bool createInstance(RenderInstanceId id,
                      const RenderInstanceDesc& desc) override;
  bool setInstanceTransform(RenderInstanceId id,
                            const std::array<float, 16>& world) override;
  bool setInstanceTint(RenderInstanceId id,
                       const std::array<float, 4>& tint) override;
  bool setInstanceVisible(RenderInstanceId id, bool shown) override;
  bool destroyInstance(RenderInstanceId id) override;
  void setEnvironment(const RenderEnvironment& environment) override;
  // Unlike the rest of the world, the guest's sky follows the frame: it
  // shows only in frames that set it (a SkyboxVisual does, through the
  // recorder, each frame it is drawn). Only changes travel, as a frame v7
  // Skybox operation. A cubemap without a ready host copy shows no sky.
  bool setSkybox(const RenderSkyboxDesc& skybox) override;
  // Starts a frame with no sky.
  void beginFrame() { m_skyShown = false; }

  // Moves at most `limit` queued operations into `output`, in order. Waiting
  // instances whose meshes became ready are created only after the queue has
  // drained, so their materials always reach the host first.
  void takeOperations(std::vector<GuestWorldOperation>& output,
                      std::size_t limit);
  std::size_t queuedOperations() const { return m_queue.size(); }
  std::size_t waitingInstances() const { return m_waiting.size(); }

private:
  struct Instance
  {
    RenderInstanceDesc desc;
    bool waiting = false;
  };
  GuestWorldOperation createOperation(RenderInstanceId id,
                                      const RenderInstanceDesc& desc,
                                      const GuestResourceId& mesh) const;

  const GuestRecordingBackend& m_backend;
  // Material id to the number of instances using it, waiting ones included.
  std::unordered_map<RenderMaterialId, std::size_t> m_materials;
  std::unordered_map<RenderInstanceId, Instance> m_instances;
  std::deque<GuestWorldOperation> m_queue;
  std::vector<RenderInstanceId> m_waiting;
  // This frame's sky, and the one the host last received.
  bool m_skyShown = false;
  GuestResourceId m_sky;
  std::array<float, 4> m_skyTint{ 1.0f, 1.0f, 1.0f, 1.0f };
  GuestResourceId m_sentSky;
  std::array<float, 4> m_sentSkyTint{ 1.0f, 1.0f, 1.0f, 1.0f };
};
