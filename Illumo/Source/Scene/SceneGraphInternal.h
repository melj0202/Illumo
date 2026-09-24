#pragma once

#include "SceneQueryIndex.h"
#include <Illumo/Scene/SceneGraph.h>
#include <array>
#include <deque>
#include <limits>
#include <string>
#include <unordered_map>

struct SceneSnapshotLifetime
{
  uint64_t epoch = 1;
  bool alive = true;
};

struct SceneGraph::Impl
{
  static constexpr uint32_t kAlive = 1;
  static constexpr uint32_t kEnabled = 2;
  static constexpr uint32_t kVisible = 4;
  static constexpr uint32_t kDirty = 8;
  static constexpr uint32_t kBoundsDirty = 16;
  static constexpr uint32_t kNoIndex = std::numeric_limits<uint32_t>::max();
  static constexpr size_t kJournalCapacity = 4096;
  static constexpr uint32_t kAttachmentChunkSize = 64;

  struct Attachment
  {
    ISceneRenderAttachment* pointer = nullptr;
    uint32_t next = 0;
    uint32_t previous = 0;
    uint32_t owner = 0;
    uint64_t revision = 0;
    AxisAlignedBounds3 localBounds;
    AxisAlignedBounds3 worldBounds;
    bool worldValid = false;
    bool cached = false;
    bool valid = false;
  };

  uint64_t graphId;
  std::vector<uint32_t> generation{ 0 }, flags{ 0 };
  std::vector<uint32_t> parent{ 0 }, firstChild{ 0 }, lastChild{ 0 };
  // previousSibling is required for O(1) detach; first/next/last alone cannot
  // find a predecessor without scanning the parent's children.
  std::vector<uint32_t> nextSibling{ 0 }, previousSibling{ 0 }, childCount{ 0 };
  std::vector<Transform3D> local{ Transform3D{} };
  std::vector<uint32_t> nameId{ 0 };
  std::vector<uint32_t> nameNext{ 0 }, namePrevious{ 0 };
  std::vector<uint32_t> nameFirst{ 0 };
  std::vector<uint64_t> userData{ 0 };
  std::vector<uint32_t> attachmentFirst{ 0 }, attachmentLast{ 0 },
    attachmentCount{ 0 };
  std::vector<uint32_t> freeSlots;
  std::deque<std::string> names{ std::string{} };
  struct NameHash
  {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept
    {
      return std::hash<std::string_view>{}(value);
    }
  };
  using NameMap =
    std::unordered_map<std::string, uint32_t, NameHash, std::equal_to<>>;
  NameMap nameIds;
  std::deque<std::array<Attachment, kAttachmentChunkSize>> attachmentChunks;
  uint32_t attachmentSlots = 1;
  uint32_t freeAttachment = 0;
  size_t nodeCount = 0;
  uint64_t structuralRevision = 1;
  uint64_t contentRevision = 1;
  std::array<SceneChange, kJournalCapacity> journal{};
  uint64_t sequence = 0;
  bool extractionActive = false;

  std::vector<uint32_t> preorder, slotToIndex, parentIndex, subtreeSize, depth;
  std::vector<Matrix4> world;
  std::vector<AxisAlignedBounds3> worldBounds, subtreeBounds;
  std::vector<unsigned char> effective, recomputed, boundsValid, subtreeValid,
    subtreePresent;
  uint64_t compiledRevision = 0;
  size_t lowestDirtyIndex = 0;
  bool stateDirty = true;
  bool boundsDirty = true;
  std::vector<uint32_t> pathScratch, destroyScratch;
  SceneQueryIndex queryIndex;
  std::vector<uint32_t> queryScratch;
  uint64_t queryRevision = 0;
  SceneGraphStatistics statistics;

  std::shared_ptr<SceneSnapshotLifetime> lifetime =
    std::make_shared<SceneSnapshotLifetime>();
  std::array<std::shared_ptr<SceneSnapshot>, 2> snapshots{
    std::make_shared<SceneSnapshot>(),
    std::make_shared<SceneSnapshot>()
  };
  uint64_t publication = 0;

  explicit Impl(uint64_t id)
    : graphId(id)
  {
  }
  bool current(SceneNodeHandle node) const;
  SceneNodeHandle handle(uint32_t slot) const;
  uint32_t nextSlot(uint32_t slot) const;
  void appendChild(uint32_t slot, uint32_t parentSlot);
  void insertChildBefore(uint32_t slot, uint32_t parentSlot, uint32_t before);
  void detach(uint32_t slot);
  void record(SceneChangeKind kind, uint32_t slot);
  void structuralChange();
  void dirty(uint32_t slot);
  uint32_t intern(std::string_view name);
  void assignName(uint32_t slot, uint32_t name);
  uint32_t allocateSlot();
  void releaseSlot(uint32_t slot);
  Attachment& attachment(uint32_t index);
  const Attachment& attachment(uint32_t index) const;
  uint32_t allocateAttachment(ISceneRenderAttachment* pointer);
  void releaseAttachments(uint32_t slot);
  bool refreshBounds(uint32_t index);
  bool localBounds(uint32_t slot, AxisAlignedBounds3* bounds);
  bool boundsAtWorld(uint32_t slot,
                     const Matrix4& world,
                     AxisAlignedBounds3* bounds,
                     bool refresh);
  bool worldTransform(uint32_t slot, Matrix4* transform);
  bool compile();
  bool resolve(bool withBounds);
  bool ensureQueryIndex();
};
