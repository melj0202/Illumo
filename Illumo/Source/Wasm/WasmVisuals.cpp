#include "WasmVisuals.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Renderer.h>
#include <algorithm>

namespace {

ColorRgba
unpack(std::uint32_t rgba)
{
  return ColorRgba{ static_cast<unsigned char>(rgba & 0xffu),
                    static_cast<unsigned char>((rgba >> 8u) & 0xffu),
                    static_cast<unsigned char>((rgba >> 16u) & 0xffu),
                    static_cast<unsigned char>((rgba >> 24u) & 0xffu) };
}

Transform2D
transformOf(const GuestTransform2D& value)
{
  Transform2D transform;
  transform.x = value.x;
  transform.y = value.y;
  transform.scaleX = value.scaleX;
  transform.scaleY = value.scaleY;
  transform.rotationRadians = value.rotation;
  transform.pivotX = value.pivotX;
  transform.pivotY = value.pivotY;
  return transform;
}

VisualProperties
propertiesOf(const GuestVisualProperties& value)
{
  VisualProperties properties;
  properties.space =
    value.worldSpace ? PrimitiveSpace::World : PrimitiveSpace::Pixels;
  properties.layer =
    value.layer == GuestLayer::World ? RenderLayerId::World : RenderLayerId::UI;
  properties.transform = transformOf(value.transform);
  properties.opacity = value.opacity;
  properties.clipEnabled = value.clipped;
  properties.clipRect = {
    value.clip[0], value.clip[1], value.clip[2], value.clip[3]
  };
  properties.visible = value.visible;
  return properties;
}

std::uint32_t
layerRank(GuestLayer layer)
{
  return static_cast<std::uint32_t>(layer);
}

bool
emptyId(const GuestResourceId& id)
{
  return id.owner == 0 && id.slot == 0 && id.generation == 0;
}

} // namespace

WasmVisuals::WasmVisuals(Renderer& renderer)
{
  m_store.setRenderer(&renderer);
  // Per-frame scratch sized for typical frames, so a frame that changes a
  // few more visuals than any before does not allocate.
  m_planned.reserve(64);
  m_resolved.reserve(256);
  m_touched.reserve(64);
  m_compositions.reserve(GuestFrame::MaximumSurfaces + 1u);
  m_nextCompositions.reserve(GuestFrame::MaximumSurfaces + 1u);
}

WasmVisuals::~WasmVisuals() = default;

// The state of a visual after this frame's operations planned so far.
WasmVisuals::PlannedVisual
WasmVisuals::plannedState(std::uint32_t id) const
{
  const auto planned =
    std::lower_bound(m_planned.begin(),
                     m_planned.end(),
                     id,
                     [](const std::pair<std::uint32_t, PlannedVisual>& entry,
                        std::uint32_t key) { return entry.first < key; });
  if (planned != m_planned.end() && planned->first == id) {
    return planned->second;
  }
  const auto live = m_visuals.find(id);
  if (live == m_visuals.end()) {
    return PlannedVisual{};
  }
  return PlannedVisual{ true, live->second.layer, live->second.leases.size() };
}

const WasmVisuals::Composition*
WasmVisuals::find(std::uint32_t target) const
{
  for (const Composition& composition : m_compositions) {
    if (composition.target == target) {
      return &composition;
    }
  }
  return nullptr;
}

bool
WasmVisuals::plan(const GuestFrame& frame, Host& host, std::string& error)
{
  ILLUMO_PROFILE_ZONE("WasmVisuals.plan");
  const std::vector<GuestVisualOperation>& operations = frame.visualOperations;
  m_resolved.clear();
  m_planned.clear();
  std::size_t visuals = m_visuals.size();
  if (!operations.empty()) {
    m_resolved.resize(operations.size() * 2);
  }
  for (std::size_t index = 0; index < operations.size(); ++index) {
    const GuestVisualOperation& operation = operations[index];
    PlannedVisual state = plannedState(operation.id);
    bool valid = true;
    switch (operation.op) {
      case GuestVisualOp::Create:
        valid = !state.exists && visuals < kVisuals;
        state = PlannedVisual{ true, GuestLayer::Ui, 0 };
        visuals += 1;
        break;
      case GuestVisualOp::Destroy:
        valid = state.exists;
        state = PlannedVisual{};
        visuals -= 1;
        break;
      case GuestVisualOp::Set:
        valid = state.exists;
        state.layer = operation.properties.layer;
        break;
      case GuestVisualOp::ItemSet: {
        const GuestVisualItem& item = operation.item;
        Texture& first = m_resolved[index * 2];
        Texture& second = m_resolved[index * 2 + 1];
        valid = state.exists && operation.index <= state.items &&
                operation.index < kItemsPerVisual;
        if (valid && item.kind == GuestItemKind::Sprite) {
          valid = host.resolveTexture(item.texture, first);
        } else if (valid && item.kind == GuestItemKind::Text) {
          valid =
            host.resolveTexture(item.font, first) && first.font &&
            (emptyId(item.heavyFont) ||
             (host.resolveTexture(item.heavyFont, second) && second.font));
        }
        if (operation.index == state.items) {
          state.items += 1;
        }
        break;
      }
      case GuestVisualOp::ItemRemove:
        valid = state.exists && operation.index <= state.items &&
                operation.count <= state.items - operation.index;
        if (valid) {
          state.items -= operation.count;
        }
        break;
      case GuestVisualOp::ItemsClear:
        valid = state.exists;
        state.items = 0;
        break;
      case GuestVisualOp::ItemInsert:
        valid = state.exists && operation.index <= state.items &&
                operation.count <= kItemsPerVisual - state.items;
        if (valid) {
          state.items += operation.count;
        }
        break;
    }
    if (!valid) {
      error = "Invalid guest visual operation " + std::to_string(index) +
              ": unknown or duplicate id, bad item index, over budget, or an "
              "unusable texture or font";
      m_resolved.clear();
      return false;
    }
    const auto slot =
      std::lower_bound(m_planned.begin(),
                       m_planned.end(),
                       operation.id,
                       [](const std::pair<std::uint32_t, PlannedVisual>& entry,
                          std::uint32_t key) { return entry.first < key; });
    if (slot != m_planned.end() && slot->first == operation.id) {
      slot->second = state;
    } else {
      m_planned.insert(slot, { operation.id, state });
    }
  }

  for (const GuestComposition& composition : frame.compositions) {
    const std::vector<GuestCompositionEntry>* entries = &composition.entries;
    if (composition.same) {
      const Composition* live = find(composition.target);
      entries = live != nullptr ? &live->entries : nullptr;
    }
    const std::vector<GuestBatch>* batches =
      host.targetBatches(composition.target);
    bool valid = entries != nullptr && batches != nullptr;
    std::uint32_t next = 0;
    GuestLayer last = GuestLayer::World;
    bool world = false;
    for (std::size_t index = 0; valid && index < entries->size(); ++index) {
      const GuestCompositionEntry& entry = (*entries)[index];
      if (entry.kind == GuestCompositionKind::Visual) {
        const PlannedVisual state = plannedState(entry.first);
        valid = state.exists && layerRank(state.layer) >= layerRank(last) &&
                (composition.target == 0 || state.layer == GuestLayer::Ui);
        last = state.layer;
      } else if (entry.kind == GuestCompositionKind::Batches) {
        valid = entry.first == next && entry.count <= batches->size() - next;
        for (std::uint32_t batch = entry.first;
             valid && batch < entry.first + entry.count;
             ++batch) {
          const GuestLayer layer = (*batches)[batch].layer;
          valid = layerRank(layer) >= layerRank(last);
          last = layer;
        }
        next += entry.count;
      } else {
        valid = !world && last == GuestLayer::World;
        world = true;
      }
    }
    if (!valid || next != batches->size()) {
      error = "Invalid guest composition for target " +
              std::to_string(composition.target) +
              ": unknown target or visual, layers out of order, or batches "
              "not covered once in order";
      m_resolved.clear();
      return false;
    }
  }
  return true;
}

// Stages an item in the retained scratch primitive of its kind, so text
// reuses its capacity from frame to frame.
void
WasmVisuals::stage(const GuestVisualItem& item,
                   const Texture& first,
                   const Texture& second)
{
  if (item.kind == GuestItemKind::Sprite) {
    SpritePrimitive& sprite = m_sprite;
    sprite = SpritePrimitive{};
    sprite.rect = { item.rect[0], item.rect[1], item.rect[2], item.rect[3] };
    sprite.textureHandle = first.handle;
    sprite.region = {
      item.region[0], item.region[1], item.region[2], item.region[3]
    };
    sprite.transform = transformOf(item.transform);
    sprite.tint = unpack(item.rgba);
    sprite.drawOrder = item.drawOrder;
    sprite.flipX = item.flipX;
    sprite.flipY = item.flipY;
    sprite.visible = item.visible;
    return;
  }
  if (item.kind == GuestItemKind::Text) {
    TextPrimitive& text = m_text;
    text.content.assign(item.text);
    text.x = item.rect[0];
    text.y = item.rect[1];
    text.sizePt = item.sizePt;
    text.color = unpack(item.rgba);
    text.font = first.font;
    text.heavyFont = second.font;
    text.heavyBlend = item.heavyBlend;
    text.stretchX = item.stretchX;
    text.stretchY = item.stretchY;
    text.drawOrder = item.drawOrder;
    text.visible = item.visible;
    return;
  }
  ShapePrimitive& shape = m_shape;
  shape = ShapePrimitive{};
  shape.kind = static_cast<ShapeKind>(item.shape);
  shape.rect = { item.rect[0], item.rect[1], item.rect[2], item.rect[3] };
  shape.x0 = item.points[0];
  shape.y0 = item.points[1];
  shape.x1 = item.points[2];
  shape.y1 = item.points[3];
  shape.x2 = item.points[4];
  shape.y2 = item.points[5];
  shape.x3 = item.points[6];
  shape.y3 = item.points[7];
  shape.lineWidth = item.lineWidth;
  shape.color = unpack(item.rgba);
  for (std::size_t corner = 0; corner < 4; ++corner) {
    shape.vertexColors[corner] = unpack(item.vertexColors[corner]);
  }
  shape.transform = transformOf(item.transform);
  shape.drawOrder = item.drawOrder;
  shape.visible = item.visible;
}

void
WasmVisuals::apply(const GuestFrame& frame)
{
  ILLUMO_PROFILE_ZONE("WasmVisuals.apply");
  const std::vector<GuestVisualOperation>& operations = frame.visualOperations;
  m_touched.clear();
  for (std::size_t index = 0; index < operations.size(); ++index) {
    const GuestVisualOperation& operation = operations[index];
    const std::uint32_t id = operation.id;
    m_touched.push_back(id);
    switch (operation.op) {
      case GuestVisualOp::Create: {
        m_store.create(id);
        m_store.setProperties(id, propertiesOf(GuestVisualProperties{}));
        m_visuals[id] = Visual{};
        break;
      }
      case GuestVisualOp::Destroy:
        m_store.destroy(id);
        m_visuals.erase(id);
        break;
      case GuestVisualOp::Set:
        m_store.setProperties(id, propertiesOf(operation.properties));
        m_visuals[id].layer = operation.properties.layer;
        break;
      case GuestVisualOp::ItemSet: {
        const Texture& first = m_resolved[index * 2];
        const Texture& second = m_resolved[index * 2 + 1];
        stage(operation.item, first, second);
        if (operation.item.kind == GuestItemKind::Sprite) {
          m_store.setItem(id, operation.index, m_sprite);
        } else if (operation.item.kind == GuestItemKind::Text) {
          m_store.setItem(id, operation.index, m_text);
        } else {
          m_store.setItem(id, operation.index, m_shape);
        }
        std::vector<ItemLease>& leases = m_visuals[id].leases;
        const ItemLease lease{ first.lease, second.lease };
        if (operation.index == leases.size()) {
          leases.push_back(lease);
        } else {
          leases[operation.index] = lease;
        }
        break;
      }
      case GuestVisualOp::ItemRemove: {
        m_store.removeItems(id, operation.index, operation.count);
        std::vector<ItemLease>& leases = m_visuals[id].leases;
        leases.erase(
          leases.begin() + static_cast<std::ptrdiff_t>(operation.index),
          leases.begin() +
            static_cast<std::ptrdiff_t>(operation.index + operation.count));
        break;
      }
      case GuestVisualOp::ItemsClear:
        m_store.clearItems(id);
        m_visuals[id].leases.clear();
        break;
      case GuestVisualOp::ItemInsert: {
        m_store.insertItems(id, operation.index, operation.count);
        std::vector<ItemLease>& leases = m_visuals[id].leases;
        leases.insert(leases.begin() +
                        static_cast<std::ptrdiff_t>(operation.index),
                      operation.count,
                      ItemLease{});
        break;
      }
    }
  }
  m_resolved.clear();
  std::sort(m_touched.begin(), m_touched.end());
  m_touched.erase(std::unique(m_touched.begin(), m_touched.end()),
                  m_touched.end());

  // Assign in place, so steady `same` frames reuse every entry vector.
  m_nextCompositions.resize(frame.compositions.size());
  for (std::size_t index = 0; index < frame.compositions.size(); ++index) {
    const GuestComposition& source = frame.compositions[index];
    Composition& next = m_nextCompositions[index];
    next.target = source.target;
    next.changed = !source.same;
    if (source.same) {
      const Composition& live = *find(source.target);
      next.width = live.width;
      next.height = live.height;
      next.entries.assign(live.entries.begin(), live.entries.end());
    } else {
      next.width = source.width;
      next.height = source.height;
      next.entries.assign(source.entries.begin(), source.entries.end());
    }
  }
  std::swap(m_compositions, m_nextCompositions);
}

bool
WasmVisuals::hasComposition(std::uint32_t target) const
{
  return find(target) != nullptr;
}

bool
WasmVisuals::compositionHasWorld() const
{
  const Composition* main = find(0);
  if (main == nullptr) {
    return false;
  }
  return std::any_of(main->entries.begin(),
                     main->entries.end(),
                     [](const GuestCompositionEntry& entry) {
                       return entry.kind == GuestCompositionKind::World;
                     });
}

bool
WasmVisuals::targetChanged(std::uint32_t target) const
{
  const Composition* composition = find(target);
  if (composition == nullptr) {
    return false;
  }
  if (composition->changed) {
    return true;
  }
  return std::any_of(composition->entries.begin(),
                     composition->entries.end(),
                     [this](const GuestCompositionEntry& entry) {
                       return entry.kind == GuestCompositionKind::Visual &&
                              std::binary_search(m_touched.begin(),
                                                 m_touched.end(),
                                                 entry.first);
                     });
}

bool
WasmVisuals::targetUsesTexture(std::uint32_t target, const void* texture) const
{
  const Composition* composition = find(target);
  if (composition == nullptr) {
    return false;
  }
  for (const GuestCompositionEntry& entry : composition->entries) {
    if (entry.kind != GuestCompositionKind::Visual) {
      continue;
    }
    const auto visual = m_visuals.find(entry.first);
    if (visual == m_visuals.end()) {
      continue;
    }
    for (const ItemLease& lease : visual->second.leases) {
      if (lease.texture.get() == texture || lease.heavy.get() == texture) {
        return true;
      }
    }
  }
  return false;
}

void
WasmVisuals::draw(Renderer& renderer,
                  std::uint32_t target,
                  GuestLayer layer,
                  const std::array<float, 16>* worldMvp,
                  Painter& painter)
{
  const Composition* composition = find(target);
  if (composition == nullptr) {
    return;
  }
  ILLUMO_PROFILE_ZONE("WasmVisuals.draw");
  GameVisual::FrameOverride frame;
  frame.width = composition->width;
  frame.height = composition->height;
  if (worldMvp != nullptr) {
    frame.hasWorldMvp = true;
    frame.worldMvp = *worldMvp;
  }
  for (const GuestCompositionEntry& entry : composition->entries) {
    if (entry.kind == GuestCompositionKind::Visual) {
      const auto visual = m_visuals.find(entry.first);
      if (visual != m_visuals.end() && visual->second.layer == layer) {
        m_store.append(&renderer, entry.first, &frame);
      }
    } else if (entry.kind == GuestCompositionKind::Batches) {
      painter.batches(entry.first, entry.count);
    } else if (layer == GuestLayer::World) {
      painter.world();
    }
  }
}

void
WasmVisuals::clear()
{
  m_store.releaseResources();
  m_visuals.clear();
  m_compositions.clear();
  m_touched.clear();
  m_text = TextPrimitive{};
}
