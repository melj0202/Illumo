#pragma once

#include <Illumo/Rendering/Primitives/VisualStore.h>
#include <IllumoGuest/Frame.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Font;
class Renderer;

// Frame schema v7 (HostRender): one guest's host-retained visuals and the
// compositions that place them. Operations are planned against the live
// store plus their own effects before anything changes, like world
// operations, and a composition applies only to the frame that carries it
// (or repeats it with `same`). Main-thread only.
class WasmVisuals
{
public:
  // A guest texture resolved for an item. The lease keeps the host texture
  // alive while an item draws it; fonts are LoadFont atlases.
  struct Texture
  {
    TextureHandle handle{};
    std::shared_ptr<Font> font;
    std::shared_ptr<const void> lease;
  };
  struct Host
  {
    virtual ~Host() = default;
    // False for unknown ids and cubemaps.
    virtual bool resolveTexture(const GuestResourceId& id, Texture& out) = 0;
    // The batches a composition target draws this frame, or nullptr when the
    // target is neither the main frame (0) nor one of the frame's surfaces.
    virtual const std::vector<GuestBatch>* targetBatches(
      std::uint32_t target) = 0;
  };
  // Receives the non-visual entries of a composition while it is drawn.
  struct Painter
  {
    virtual ~Painter() = default;
    // Batches [first, first + count) that belong to the layer being drawn.
    virtual void batches(std::uint32_t first, std::uint32_t count) = 0;
    virtual void world() = 0;
  };

  static constexpr std::size_t kVisuals = 4096;
  static constexpr std::size_t kItemsPerVisual = 65536;

  explicit WasmVisuals(Renderer& renderer);
  ~WasmVisuals();
  WasmVisuals(const WasmVisuals&) = delete;
  WasmVisuals& operator=(const WasmVisuals&) = delete;
  WasmVisuals(WasmVisuals&&) = delete;
  WasmVisuals& operator=(WasmVisuals&&) = delete;

  // Validates `frame`'s visual operations and compositions. False sets
  // `error` and changes nothing; apply() must follow a successful plan.
  bool plan(const GuestFrame& frame, Host& host, std::string& error);
  void apply(const GuestFrame& frame);

  bool hasComposition(std::uint32_t target) const;
  bool compositionHasWorld() const;
  // Whether a target's composition, or a visual it lists, changed in the
  // last applied frame.
  bool targetChanged(std::uint32_t target) const;
  // Whether a visual the target lists draws `texture` (a resolved lease).
  bool targetUsesTexture(std::uint32_t target, const void* texture) const;
  // Draws target's entries on `layer`, in order. Pixel-space visuals lay
  // out in the composition's logical size; world-space ones use
  // `worldMvp` (the guest camera) when given.
  void draw(Renderer& renderer,
            std::uint32_t target,
            GuestLayer layer,
            const std::array<float, 16>* worldMvp,
            Painter& painter);

  // Drops every visual and composition, for retirement.
  void clear();
  std::size_t visualCount() const { return m_visuals.size(); }
  const VisualStore& store() const { return m_store; }

private:
  struct ItemLease
  {
    std::shared_ptr<const void> texture;
    std::shared_ptr<const void> heavy;
  };
  struct Visual
  {
    GuestLayer layer = GuestLayer::Ui;
    std::vector<ItemLease> leases;
  };
  struct PlannedVisual
  {
    bool exists = false;
    GuestLayer layer = GuestLayer::Ui;
    std::size_t items = 0;
  };
  struct Composition
  {
    std::uint32_t target = 0;
    float width = 0.0f;
    float height = 0.0f;
    std::vector<GuestCompositionEntry> entries;
    bool changed = false;
  };

  PlannedVisual plannedState(std::uint32_t id) const;
  void stage(const GuestVisualItem& item,
             const Texture& first,
             const Texture& second);
  const Composition* find(std::uint32_t target) const;

  VisualStore m_store;
  std::unordered_map<std::uint32_t, Visual> m_visuals;
  std::vector<Composition> m_compositions;
  // Plan scratch, retained so frames with operations reuse capacity.
  // Sorted by id; a vector so steady frames with a few operations reuse
  // its capacity instead of allocating map nodes.
  std::vector<std::pair<std::uint32_t, PlannedVisual>> m_planned;
  std::vector<Texture> m_resolved; // two per operation
  std::vector<Composition> m_nextCompositions;
  // Visuals the last applied frame changed, sorted.
  std::vector<std::uint32_t> m_touched;
  // Staged items (stage), retained for their capacity.
  ShapePrimitive m_shape;
  SpritePrimitive m_sprite;
  TextPrimitive m_text;
};
