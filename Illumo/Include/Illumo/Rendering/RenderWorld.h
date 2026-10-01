#pragma once
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWorld.h>
#include <Illumo/Rendering/RecordedCommandList.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class Renderer;

// Persistent, flat world render objects drawn as one World-layer drawable.
// Instances that share a mesh range and material form a bucket; each bucket
// draws with one instanced call per pass from tokens recorded once, and
// re-recorded only when its buffers, material or the environment change.
// Every frame it culls, streams the visible instances from host memory,
// patches instance counts and executes the recorded lists in place.
// Main-thread only. Meshes stay owned by the caller and must outlive the
// instances that draw them.
class RenderWorld final
  : public DrawableBase
  , public IRenderWorld
{
public:
  struct Stats
  {
    size_t instances = 0;
    size_t buckets = 0;
    // Lifetime count of recorded lists, color and shadow.
    size_t recordings = 0;
    // Instances drawn in the last frame's color and shadow passes.
    size_t drawnInstances = 0;
    size_t shadowInstances = 0;
  };

  RenderWorld();
  ~RenderWorld() override;
  RenderWorld(const RenderWorld&) = delete;
  RenderWorld& operator=(const RenderWorld&) = delete;
  RenderWorld(RenderWorld&&) = delete;
  RenderWorld& operator=(RenderWorld&&) = delete;

  // Destroys the instance buffers. Call before the renderer goes away.
  void releaseResources();

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
  // Drawn first each frame, from the frame's world view projection with its
  // translation removed, so no per-frame camera data is needed.
  bool setSkybox(const RenderSkyboxDesc& skybox) override;
  bool hasSkybox() const { return m_skybox.cubemap.isValid(); }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;
  void CollectShadowCasters(Renderer* renderer) override;
  void AppendShadowCommands(Renderer* renderer) override;

  const Stats& stats() const { return m_stats; }

private:
  // Mirrors InstanceLayout::LitModelTint.
  struct GpuInstance
  {
    std::array<float, 16> model;
    std::array<float, 16> previousModel;
    std::array<float, 4> tint;
  };
  struct Material
  {
    RenderMaterialDesc desc;
    size_t users = 0;
  };
  struct Instance
  {
    RenderInstanceId id = 0;
    bool alive = false;
    RenderInstanceDesc desc;
    std::array<float, 16> previousWorld{};
    // The frame whose previous transform is previousWorld.
    uint64_t movedFrame = 0;
    bool hasWorldBounds = false;
    AxisAlignedBounds3 worldBounds{};
    size_t bucket = 0;
    size_t bucketPosition = 0;
  };
  struct BucketKey
  {
    uint32_t meshSlot = 0;
    uint32_t meshGeneration = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    RenderMaterialId material = 0;
    // Blended instances never share: the key carries their own id.
    RenderInstanceId blendOwner = 0;
    bool operator==(const BucketKey& other) const;
  };
  struct BucketKeyHash
  {
    size_t operator()(const BucketKey& key) const;
  };
  struct Bucket
  {
    BucketKey key;
    bool inUse = false;
    std::vector<size_t> members;
    size_t capacity = 0;
    BufferHandle colorBuffer{};
    BufferHandle shadowBuffer{};
    std::vector<GpuInstance> colorStaging;
    std::vector<GpuInstance> shadowStaging;
    RecordedCommandList color;
    RecordedCommandList shadow;
    size_t colorDraw = 0;
    size_t shadowDraw = 0;
    bool dirty = true;
  };

  Instance* findInstance(RenderInstanceId id);
  void addToBucket(size_t slot);
  void removeFromBucket(size_t slot);
  void markMaterialDirty(RenderMaterialId id);
  static bool computeWorldBounds(Instance& instance);
  // Once per frame, before any tokens: grows buffers and re-records dirty
  // lists. False when the renderer cannot draw instanced.
  bool prepareFrame(Renderer* renderer);
  bool ensureBuffers(Renderer* renderer, Bucket& bucket);
  void recordColor(Renderer* renderer, Bucket& bucket);
  void recordShadow(Renderer* renderer, Bucket& bucket);
  void fillRecord(const Instance& instance, GpuInstance& record) const;
  // Culls, streams and draws one bucket's color pass. `bound` tracks whether
  // this frame's shared state is already bound.
  void drawColor(Renderer* renderer, Bucket& bucket, bool& bound);
  void drawSkybox(Renderer* renderer);

  std::unordered_map<RenderMaterialId, Material> m_materials;
  std::vector<Instance> m_instances;
  std::vector<size_t> m_freeInstances;
  std::unordered_map<RenderInstanceId, size_t> m_instanceSlots;
  std::vector<std::unique_ptr<Bucket>> m_buckets;
  std::vector<size_t> m_freeBuckets;
  std::unordered_map<BucketKey, size_t, BucketKeyHash> m_bucketIndex;
  // Blended buckets in creation order, drawn after the opaque ones.
  std::vector<size_t> m_blendOrder;
  RenderEnvironment m_environment;
  RenderSkyboxDesc m_skybox;
  MeshHandle m_skyboxMesh{};
  Renderer* m_renderer = nullptr;
  uint64_t m_frame = 0;
  uint64_t m_preparedSerial = 0;
  bool m_ready = false;
  bool m_unsupportedReported = false;
  Stats m_stats;
};
