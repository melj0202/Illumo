#pragma once

#include <Illumo/Rendering/ResourceHandle.h>
#include <IllumoGuest/Frame.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

class GameVisual;
class Renderer;

// Frame schema v7: the guest side of host-retained visuals. Each GameVisual a
// frame appends becomes a host visual; only items and properties that differ
// from what the host last confirmed travel, as visual operations. Nothing is
// confirmed until the frame is delivered (commit); a dropped frame's changes
// are sent again by the next one.
class GuestVisualProxies
{
public:
  // A texture's host id, or an empty id while its host copy is not ready.
  using TextureResolver = std::function<GuestResourceId(TextureHandle)>;
  // Per-frame shares of the host quotas; the rest stays for world
  // operations and batch-path text.
  static constexpr std::size_t kOperations = 32768;
  static constexpr std::size_t kTextBytes = 512u * 1024u;
  static constexpr std::uint32_t kChurnFrames = 3;
  static constexpr std::uint32_t kChurnRecheck = 30;

  explicit GuestVisualProxies(TextureResolver resolver);

  // Queues destroys of visuals forgotten since the last delivered frame and
  // resets this frame's budgets.
  void beginFrame(std::vector<GuestVisualOperation>& operations);
  // Appends the operations that bring `visual`'s host copy up to date and
  // returns its host id, or 0 when it must record its own tokens this frame
  // (custom styles, textures or fonts not on the host yet, or no budget left).
  std::uint32_t sync(const GameVisual& visual,
                     Renderer& renderer,
                     GuestLayer layer,
                     std::vector<GuestVisualOperation>& operations,
                     std::uint64_t textureEpoch);
  void forget(const GameVisual& visual);
  void commit();
  void drop();

  std::size_t proxyCount() const { return m_proxies.size(); }

private:
  struct Proxy
  {
    std::uint32_t id = 0;
    bool onHost = false;
    bool hasProperties = false;
    GuestVisualProperties properties;
    std::vector<GuestVisualItem> items;
    // This frame's state, confirmed by commit.
    bool touched = false;
    GuestVisualProperties proposedProperties;
    std::vector<GuestVisualItem> proposed;
    // The visual's edit revision and the texture epoch the host's copy
    // matches; an unchanged pair skips the visual (GameVisual::editRevision).
    std::uint64_t editRevision = 0;
    std::uint64_t textureEpoch = 0;
    std::uint64_t proposedRevision = 0;
    std::uint64_t proposedEpoch = 0;
    // Churn: consecutive frames that changed most items; while churning the
    // visual records batches, and every kChurnRecheck frames its items are
    // compared with the last sample to see whether it has settled.
    std::uint32_t churnFrames = 0;
    bool churning = false;
    std::uint32_t recheck = 0;
    std::vector<GuestVisualItem> sample;
  };

  bool convert(const GameVisual& visual,
               Renderer& renderer,
               std::size_t index,
               GuestVisualItem& item,
               std::size_t& textBytes) const;

  TextureResolver m_resolve;
  std::unordered_map<const GameVisual*, Proxy> m_proxies;
  std::vector<const GameVisual*> m_touched;
  // Destroys not yet delivered, and those carried by the current frame.
  std::vector<std::uint32_t> m_pendingDestroys;
  std::vector<std::uint32_t> m_sentDestroys;
  // Visuals forgotten while their creation was in the undelivered frame.
  std::vector<std::uint32_t> m_orphanedCreates;
  std::uint32_t m_nextId = 1;
  std::size_t m_operations = 0;
  std::size_t m_textBytes = 0;
};
