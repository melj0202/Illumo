#include <IllumoGuest/VisualProxies.h>

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace {

std::uint32_t
pack(ColorRgba color)
{
  return static_cast<std::uint32_t>(color.r) |
         (static_cast<std::uint32_t>(color.g) << 8u) |
         (static_cast<std::uint32_t>(color.b) << 16u) |
         (static_cast<std::uint32_t>(color.a) << 24u);
}

bool
finite(std::initializer_list<float> values)
{
  return std::all_of(values.begin(), values.end(), [](float value) {
    return std::isfinite(value);
  });
}

GuestTransform2D
transformOf(const Transform2D& value)
{
  return { value.x,
           value.y,
           value.scaleX,
           value.scaleY,
           value.rotationRadians,
           value.pivotX,
           value.pivotY };
}

bool
finiteTransform(const GuestTransform2D& value)
{
  return finite({ value.x,
                  value.y,
                  value.scaleX,
                  value.scaleY,
                  value.rotation,
                  value.pivotX,
                  value.pivotY });
}

bool
sameId(const GuestResourceId& left, const GuestResourceId& right)
{
  return left.owner == right.owner && left.slot == right.slot &&
         left.generation == right.generation && left.kind == right.kind;
}

bool
sameTransform(const GuestTransform2D& left, const GuestTransform2D& right)
{
  return left.x == right.x && left.y == right.y &&
         left.scaleX == right.scaleX && left.scaleY == right.scaleY &&
         left.rotation == right.rotation && left.pivotX == right.pivotX &&
         left.pivotY == right.pivotY;
}

bool
sameItem(const GuestVisualItem& left, const GuestVisualItem& right)
{
  return left.kind == right.kind && left.drawOrder == right.drawOrder &&
         left.visible == right.visible &&
         sameTransform(left.transform, right.transform) &&
         left.shape == right.shape && left.rect == right.rect &&
         left.points == right.points && left.rgba == right.rgba &&
         left.vertexColors == right.vertexColors &&
         left.lineWidth == right.lineWidth &&
         sameId(left.texture, right.texture) && left.region == right.region &&
         left.flipX == right.flipX && left.flipY == right.flipY &&
         sameId(left.font, right.font) &&
         sameId(left.heavyFont, right.heavyFont) &&
         left.heavyBlend == right.heavyBlend && left.sizePt == right.sizePt &&
         left.stretchX == right.stretchX && left.stretchY == right.stretchY &&
         left.text == right.text;
}

bool
sameProperties(const GuestVisualProperties& left,
               const GuestVisualProperties& right)
{
  return left.worldSpace == right.worldSpace && left.layer == right.layer &&
         sameTransform(left.transform, right.transform) &&
         left.opacity == right.opacity && left.clipped == right.clipped &&
         left.clip == right.clip && left.visible == right.visible;
}

// Most of a visual's items changed: at least half, and more than a handful.
bool
churns(std::size_t changed, std::size_t count)
{
  return changed >= 32 && changed * 2 >= count;
}

} // namespace

GuestVisualProxies::GuestVisualProxies(TextureResolver resolver)
  : m_resolve(std::move(resolver))
{
}

void
GuestVisualProxies::beginFrame(std::vector<GuestVisualOperation>& operations)
{
  m_operations = 0;
  m_textBytes = 0;
  m_sentDestroys.clear();
  for (std::uint32_t id : m_pendingDestroys) {
    GuestVisualOperation destroy;
    destroy.op = GuestVisualOp::Destroy;
    destroy.id = id;
    operations.push_back(std::move(destroy));
    m_sentDestroys.push_back(id);
  }
  m_operations = m_pendingDestroys.size();
  m_pendingDestroys.clear();
}

// One item as the host decodes it. False when it cannot travel: a custom
// style, a texture or font without a ready host copy, or values the host
// would reject.
bool
GuestVisualProxies::convert(const GameVisual& visual,
                            Renderer& renderer,
                            std::size_t index,
                            GuestVisualItem& item,
                            std::size_t& textBytes) const
{
  const GameVisual::PrimitiveRef ref = visual.itemAt(index);
  // Reuses the item's text capacity across frames.
  std::string reused = std::move(item.text);
  item = GuestVisualItem{};
  reused.clear();
  item.text = std::move(reused);
  if (ref.kind == GameVisual::PrimitiveKind::Shape) {
    const ShapePrimitive* shape = visual.getShape(ref.index);
    if (shape == nullptr || shape->styleHandle.isValid()) {
      return false;
    }
    item.kind = GuestItemKind::Shape;
    item.drawOrder = shape->drawOrder;
    item.visible = shape->visible;
    item.transform = transformOf(shape->transform);
    item.shape = static_cast<std::uint32_t>(shape->kind);
    item.rect = { shape->rect.x, shape->rect.y, shape->rect.w, shape->rect.h };
    item.points = { shape->x0, shape->y0, shape->x1, shape->y1,
                    shape->x2, shape->y2, shape->x3, shape->y3 };
    item.rgba = pack(shape->color);
    for (std::size_t corner = 0; corner < 4; ++corner) {
      item.vertexColors[corner] = pack(shape->vertexColors[corner]);
    }
    item.lineWidth = shape->lineWidth;
    return item.shape <= 5 && finiteTransform(item.transform) &&
           finite({ item.rect[0],
                    item.rect[1],
                    item.rect[2],
                    item.rect[3],
                    item.points[0],
                    item.points[1],
                    item.points[2],
                    item.points[3],
                    item.points[4],
                    item.points[5],
                    item.points[6],
                    item.points[7],
                    item.lineWidth });
  }
  if (ref.kind == GameVisual::PrimitiveKind::Sprite) {
    const SpritePrimitive* sprite = visual.getSprite(ref.index);
    if (sprite == nullptr || sprite->styleHandle.isValid()) {
      return false;
    }
    item.kind = GuestItemKind::Sprite;
    item.drawOrder = sprite->drawOrder;
    item.visible = sprite->visible;
    item.transform = transformOf(sprite->transform);
    item.rect = {
      sprite->rect.x, sprite->rect.y, sprite->rect.w, sprite->rect.h
    };
    item.texture = m_resolve(sprite->textureHandle);
    item.region = {
      sprite->region.u0, sprite->region.v0, sprite->region.u1, sprite->region.v1
    };
    item.rgba = pack(sprite->tint);
    item.flipX = sprite->flipX;
    item.flipY = sprite->flipY;
    return item.texture.owner != 0 && finiteTransform(item.transform) &&
           finite({ item.rect[0],
                    item.rect[1],
                    item.rect[2],
                    item.rect[3],
                    item.region[0],
                    item.region[1],
                    item.region[2],
                    item.region[3] });
  }
  const TextPrimitive* text = visual.getText(ref.index);
  if (text == nullptr || text->styleHandle.isValid() ||
      text->content.size() > GuestFrame::MaximumItemText) {
    return false;
  }
  const std::shared_ptr<Font> font =
    text->font ? text->font : Font::getDefaultFont();
  if (!font || !font->isValid()) {
    return false;
  }
  item.kind = GuestItemKind::Text;
  item.drawOrder = text->drawOrder;
  item.visible = text->visible;
  item.rect = { text->x, text->y, 0.0f, 0.0f };
  item.sizePt = text->sizePt;
  item.rgba = pack(text->color);
  item.font = m_resolve(font->getTextureHandle(&renderer));
  // As GameVisual draws them: a stretch that isn't positive and finite is 1.
  item.stretchX = std::isfinite(text->stretchX) && text->stretchX > 0.0f
                    ? text->stretchX
                    : 1.0f;
  item.stretchY = std::isfinite(text->stretchY) && text->stretchY > 0.0f
                    ? text->stretchY
                    : 1.0f;
  if (text->heavyFont != nullptr && text->heavyFont != font &&
      text->heavyBlend > 0.0f) {
    if (!text->heavyFont->isValid()) {
      return false; // the heavy sample is still loading
    }
    item.heavyFont = m_resolve(text->heavyFont->getTextureHandle(&renderer));
    item.heavyBlend = std::clamp(text->heavyBlend, 0.0f, 1.0f);
    if (item.heavyFont.owner == 0) {
      return false;
    }
  }
  item.text.assign(text->content);
  textBytes = item.text.size();
  return item.font.owner != 0 && finite({ item.rect[0], item.rect[1] }) &&
         std::isfinite(item.sizePt) && item.sizePt > 0.0f &&
         item.sizePt <= 4096.0f && std::isfinite(item.heavyBlend);
}

std::uint32_t
GuestVisualProxies::sync(const GameVisual& visual,
                         Renderer& renderer,
                         GuestLayer layer,
                         std::vector<GuestVisualOperation>& operations,
                         std::uint64_t textureEpoch)
{
  // A visual's own quad cap does not travel: the host applies its store's.
  const std::size_t count = visual.itemCount();
  if (count > 65536) {
    return 0;
  }
  const auto [found, inserted] = m_proxies.try_emplace(&visual);
  Proxy& proxy = found->second;
  if (inserted) {
    proxy.id = m_nextId++;
  }
  if (proxy.touched) {
    return proxy.id; // appended again this frame: already current
  }
  // A visual whose items keep changing wholesale (per-item animation) is
  // cheaper as recorded batches. It is rechecked now and then, and returns
  // once it settles.
  if (proxy.churning) {
    if (proxy.recheck > 0) {
      proxy.recheck -= 1;
      return 0;
    }
    ILLUMO_PROFILE_ZONE("VisualProxies.recheckChurn");
    proxy.recheck = kChurnRecheck;
    proxy.proposed.resize(count);
    std::size_t changed = proxy.sample.size() != count ? count : 0;
    for (std::size_t index = 0; index < count; ++index) {
      std::size_t bytes = 0;
      if (!convert(visual, renderer, index, proxy.proposed[index], bytes)) {
        return 0;
      }
      if (changed != count &&
          !sameItem(proxy.sample[index], proxy.proposed[index])) {
        changed += 1;
      }
    }
    std::swap(proxy.sample, proxy.proposed);
    if (churns(changed, count)) {
      return 0;
    }
    proxy.churning = false;
    proxy.churnFrames = 0;
  }

  GuestVisualProperties& properties = proxy.proposedProperties;
  properties = GuestVisualProperties{};
  properties.worldSpace = visual.getSpace() == PrimitiveSpace::World;
  properties.layer = layer;
  properties.transform = transformOf(visual.getTransform());
  properties.opacity = visual.getOpacity();
  properties.clipped = visual.hasPixelClipRect();
  const Rect2& clip = visual.getPixelClipRect();
  properties.clip = { clip.x, clip.y, clip.w, clip.h };
  if (!finiteTransform(properties.transform) ||
      !finite({ properties.clip[0],
                properties.clip[1],
                properties.clip[2],
                properties.clip[3] }) ||
      properties.clip[2] < 0.0f || properties.clip[3] < 0.0f) {
    return 0;
  }

  // Nothing about the visual changed since the host confirmed it: no item
  // needs converting, and nothing travels.
  if (proxy.onHost && proxy.hasProperties &&
      proxy.editRevision == visual.editRevision() &&
      proxy.textureEpoch == textureEpoch &&
      sameProperties(proxy.properties, properties)) {
    return proxy.id;
  }

  // Only visuals that changed get here: conversion and diffing of items.
  ILLUMO_PROFILE_ZONE("VisualProxies.syncChanged");
  proxy.proposed.resize(count);
  for (std::size_t index = 0; index < count; ++index) {
    std::size_t bytes = 0;
    if (!convert(visual, renderer, index, proxy.proposed[index], bytes)) {
      return 0;
    }
  }
  // Items match the host's by position, but a list that grew or shrank in the
  // middle keeps its unchanged head and tail: only the part between them
  // travels, as replacements plus one insertion or removal.
  const std::vector<GuestVisualItem>& base = proxy.items;
  const std::size_t baseCount = proxy.onHost ? base.size() : 0;
  const std::size_t shorter = std::min(baseCount, count);
  std::size_t head = 0;
  while (head < shorter && sameItem(base[head], proxy.proposed[head])) {
    ++head;
  }
  std::size_t tail = 0;
  while (tail < shorter - head && sameItem(base[baseCount - 1 - tail],
                                           proxy.proposed[count - 1 - tail])) {
    ++tail;
  }
  const std::size_t baseMiddle = baseCount - head - tail;
  const std::size_t middle = count - head - tail;
  const std::size_t overlap = std::min(baseMiddle, middle);
  std::size_t replaced = 0;
  std::size_t textBytes = 0;
  for (std::size_t index = head; index < head + overlap; ++index) {
    if (!sameItem(base[index], proxy.proposed[index])) {
      replaced += 1;
      textBytes += proxy.proposed[index].text.size();
    }
  }
  const std::size_t added = middle > baseMiddle ? middle - baseMiddle : 0;
  const std::size_t removed = baseMiddle > middle ? baseMiddle - middle : 0;
  for (std::size_t index = head + overlap; index < head + middle; ++index) {
    textBytes += proxy.proposed[index].text.size();
  }
  // Added items before an unchanged tail need an insertion; at the end they
  // simply append.
  const bool insert = added > 0 && tail > 0;
  const std::size_t changedItems = replaced + added + removed;
  const bool sameProps = proxy.onHost && proxy.hasProperties &&
                         sameProperties(proxy.properties, properties);
  const std::size_t operationCount =
    (proxy.onHost ? 0u : 1u) + (sameProps ? 0u : 1u) + replaced + added +
    (insert ? 1u : 0u) + (removed > 0 ? 1u : 0u);
  if (proxy.onHost && churns(changedItems, count)) {
    proxy.churnFrames += 1;
    if (proxy.churnFrames >= kChurnFrames) {
      proxy.churning = true;
      proxy.recheck = kChurnRecheck;
      std::swap(proxy.sample, proxy.proposed);
      return 0;
    }
  } else {
    proxy.churnFrames = 0;
  }
  if (operationCount > kOperations - m_operations ||
      textBytes > kTextBytes - m_textBytes) {
    return 0;
  }

  if (!proxy.onHost) {
    GuestVisualOperation create;
    create.op = GuestVisualOp::Create;
    create.id = proxy.id;
    operations.push_back(std::move(create));
  }
  if (!sameProps) {
    GuestVisualOperation set;
    set.op = GuestVisualOp::Set;
    set.id = proxy.id;
    set.properties = properties;
    operations.push_back(std::move(set));
  }
  const auto setItem = [&](std::size_t index) {
    GuestVisualOperation& set = operations.emplace_back();
    set.op = GuestVisualOp::ItemSet;
    set.id = proxy.id;
    set.index = static_cast<std::uint32_t>(index);
    set.item = proxy.proposed[index];
  };
  for (std::size_t index = head; index < head + overlap; ++index) {
    if (!sameItem(base[index], proxy.proposed[index])) {
      setItem(index);
    }
  }
  if (insert) {
    GuestVisualOperation insertion;
    insertion.op = GuestVisualOp::ItemInsert;
    insertion.id = proxy.id;
    insertion.index = static_cast<std::uint32_t>(head + overlap);
    insertion.count = static_cast<std::uint32_t>(added);
    operations.push_back(std::move(insertion));
  }
  for (std::size_t index = head + overlap; index < head + middle; ++index) {
    setItem(index);
  }
  if (removed > 0) {
    GuestVisualOperation removal;
    removal.op = GuestVisualOp::ItemRemove;
    removal.id = proxy.id;
    removal.index = static_cast<std::uint32_t>(head + overlap);
    removal.count = static_cast<std::uint32_t>(removed);
    operations.push_back(std::move(removal));
  }
  proxy.proposedRevision = visual.editRevision();
  proxy.proposedEpoch = textureEpoch;
  m_operations += operationCount;
  m_textBytes += textBytes;
  proxy.touched = true;
  m_touched.push_back(&visual);
  return proxy.id;
}

void
GuestVisualProxies::forget(const GameVisual& visual)
{
  const auto found = m_proxies.find(&visual);
  if (found == m_proxies.end()) {
    return;
  }
  const Proxy& proxy = found->second;
  // A visual whose creation is still in flight is destroyed only if that
  // frame is delivered; one the host never saw needs nothing.
  if (proxy.onHost) {
    m_pendingDestroys.push_back(proxy.id);
  } else if (proxy.touched) {
    m_orphanedCreates.push_back(proxy.id);
  }
  m_touched.erase(std::remove(m_touched.begin(), m_touched.end(), &visual),
                  m_touched.end());
  m_proxies.erase(found);
}

void
GuestVisualProxies::commit()
{
  ILLUMO_PROFILE_ZONE("VisualProxies.commit");
  ILLUMO_PROFILE_PLOT("Guest visual proxies changed", m_touched.size());
  ILLUMO_PROFILE_PLOT("Guest visual proxy operations", m_operations);
  ILLUMO_PROFILE_PLOT("Guest visual text bytes", m_textBytes);
  for (const GameVisual* visual : m_touched) {
    Proxy& proxy = m_proxies.at(visual);
    std::swap(proxy.items, proxy.proposed);
    proxy.properties = proxy.proposedProperties;
    proxy.hasProperties = true;
    proxy.onHost = true;
    proxy.editRevision = proxy.proposedRevision;
    proxy.textureEpoch = proxy.proposedEpoch;
    proxy.touched = false;
  }
  m_touched.clear();
  m_sentDestroys.clear();
  m_pendingDestroys.insert(m_pendingDestroys.end(),
                           m_orphanedCreates.begin(),
                           m_orphanedCreates.end());
  m_orphanedCreates.clear();
}

void
GuestVisualProxies::drop()
{
  for (const GameVisual* visual : m_touched) {
    m_proxies.at(visual).touched = false;
  }
  m_touched.clear();
  m_orphanedCreates.clear();
  // Undelivered destroys go out with the next frame.
  m_pendingDestroys.insert(
    m_pendingDestroys.begin(), m_sentDestroys.begin(), m_sentDestroys.end());
  m_sentDestroys.clear();
}
