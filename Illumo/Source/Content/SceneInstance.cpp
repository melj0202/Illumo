#include <Illumo/Content/SceneAssetRefs.h>
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <Illumo/Services/Logger.h>

#include "ScenePrimitiveMeshes.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <unordered_set>
#include <utility>

// Draws nothing; gives otherwise invisible nodes a small pickable box.
class SceneInstance::PickProxy : public ISceneRenderAttachment
{
public:
  uint64_t getSceneBoundsRevision() const override { return 1; }
  bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const override
  {
    if (bounds == nullptr) {
      return false;
    }
    *bounds = AxisAlignedBounds3{ Vector3(-0.2f), Vector3(0.2f) };
    return true;
  }
  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override
  {
    (void)renderer;
    (void)worldTransform;
  }
};

struct SceneInstance::Record
{
  SceneNode node;
  SceneNodeHandle handle;
  std::vector<std::unique_ptr<MeshVisual>> visuals;
  std::vector<bool> castShadows;
  std::unique_ptr<PickProxy> proxy;
  bool hasLight = false;
};

struct SceneInstance::AssetSlot
{
  SceneAsset asset;
  bool attempted = false;
  bool failed = false;
  TextureHandle texture{};
  MeshHandle mesh{};
  MeshAssetInfo meshInfo;
};

static const ColorRgba kPlaceholderColor{ 255, 0, 255, 255 };

// True while load() builds a whole scene: asset warnings are then reported
// as one summary instead of one line each (guest log lines share a bounded
// service queue). Scene instances are main-thread affine.
static bool batchingAssetWarnings = false;

static void
reportAssetWarning(const std::string& warning)
{
  if (!batchingAssetWarnings) {
    Logger::LogWarning(warning);
  }
}

SceneInstance::SceneInstance(AssetManager* assets, SceneInstanceOptions options)
  : m_assets(assets)
  , m_options(options)
  , m_drawable(m_graph)
{
}

SceneInstance::~SceneInstance()
{
  clear();
  if (m_assets != nullptr) {
    for (MeshHandle& handle : m_primitiveMeshes) {
      if (handle.isValid()) {
        m_assets->releaseMesh(handle);
        handle = MeshHandle{};
      }
    }
  }
}

void
SceneInstance::touch()
{
  ++m_revision;
  m_cacheDirty = true;
}

SceneInstance::Record*
SceneInstance::record(std::string_view id) const
{
  const std::unordered_map<std::string, std::unique_ptr<Record>>::const_iterator
    found = m_records.find(std::string(id));
  return found == m_records.end() ? nullptr : found->second.get();
}

void
SceneInstance::releaseAssets()
{
  if (m_skybox) {
    m_graph.invalidateSnapshots();
    m_skybox.reset();
  }
  for (std::pair<const std::string, std::unique_ptr<AssetSlot>>& entry :
       m_assetSlots) {
    AssetSlot& slot = *entry.second;
    if (m_assets != nullptr) {
      if (slot.texture.isValid()) {
        m_assets->releaseTexture(slot.texture);
      }
      if (slot.mesh.isValid()) {
        m_assets->releaseMesh(slot.mesh);
      }
    }
  }
  m_assetSlots.clear();
}

void
SceneInstance::clear()
{
  m_graph.invalidateSnapshots();
  for (std::pair<const std::string, std::unique_ptr<Record>>& entry :
       m_records) {
    destroyAttachments(*entry.second);
  }
  m_graph.clear();
  m_records.clear();
  releaseAssets();
  m_state = SceneDocument{};
  m_packageRoot.clear();
  m_warnings.clear();
  m_lighting = SceneLighting{};
  m_lightingDirty = true;
  touch();
}

bool
SceneInstance::load(const SceneDocument& document,
                    std::string_view packageRoot,
                    std::string& error)
{
  if (!validateSceneDocument(document, error)) {
    return false;
  }
  clear();
  m_packageRoot = std::string(packageRoot);
  m_state = document;
  m_state.nodes.clear();
  for (const SceneAsset& asset : document.assets) {
    std::unique_ptr<AssetSlot> slot = std::make_unique<AssetSlot>();
    slot->asset = asset;
    m_assetSlots.emplace(asset.id, std::move(slot));
  }
  m_records.reserve(document.nodes.size());
  // Asset problems found while building are summarized once below rather
  // than logged per asset.
  batchingAssetWarnings = true;
  std::size_t dropped = 0;
  std::string firstDrop;
  for (const SceneNode& node : document.nodes) {
    const SceneNodeHandle parent =
      node.parentId.empty() ? SceneNodeHandle{} : handleOf(node.parentId);
    std::string ignored;
    if (!buildNode(node, parent, SceneNodeHandle{}, ignored)) {
      if (dropped == 0) {
        firstDrop = node.id + ": " + ignored;
      }
      ++dropped;
    }
  }
  m_lighting = resolveLighting();
  m_lightingDirty = false;
  rebuildEnvironment();
  batchingAssetWarnings = false;
  for (std::pair<const std::string, std::unique_ptr<Record>>& entry :
       m_records) {
    for (size_t index = 0; index < entry.second->visuals.size(); ++index) {
      applyLightingTo(*entry.second->visuals[index],
                      entry.second->castShadows[index]);
    }
  }
  touch();
  error.clear();
  if (dropped > 0) {
    Logger::LogWarning("Scene instance dropped " + std::to_string(dropped) +
                       " node(s); first " + firstDrop);
  }
  if (!m_warnings.empty()) {
    Logger::LogWarning("Scene under " + m_packageRoot + " draws " +
                       std::to_string(m_warnings.size()) +
                       " asset placeholder(s); first: " + m_warnings.front());
  }
  if (!document.nodes.empty()) {
    Logger::LogTrace("Scene instantiated under " + m_packageRoot + ": " +
                     std::to_string(m_records.size()) + " nodes, " +
                     std::to_string(m_assetSlots.size()) + " assets");
  }
  return true;
}

void
SceneInstance::setRenderer(Renderer* renderer)
{
  if (renderer == m_renderer) {
    return;
  }
  const bool rebind = m_renderer != nullptr;
  m_renderer = renderer;
  if (rebind) {
    rebuildAllAttachments();
    rebuildEnvironment();
    return;
  }
  for (std::pair<const std::string, std::unique_ptr<Record>>& entry :
       m_records) {
    for (std::unique_ptr<MeshVisual>& visual : entry.second->visuals) {
      visual->prepare(renderer);
    }
  }
  if (m_skybox) {
    m_skybox->prepare(renderer);
  }
}

static bool
sameLighting(const SceneLighting& a, const SceneLighting& b)
{
  return a.lit == b.lit && a.towardLight == b.towardLight &&
         a.color == b.color && a.ambient == b.ambient &&
         a.shadows == b.shadows && a.sourceId == b.sourceId;
}

void
SceneInstance::update()
{
  bool anyLight = false;
  for (std::pair<const std::string, std::unique_ptr<Record>>& entry :
       m_records) {
    anyLight = anyLight || entry.second->hasLight;
  }
  if (!m_lightingDirty && !anyLight) {
    return;
  }
  const SceneLighting next = resolveLighting();
  m_lightingDirty = false;
  if (sameLighting(next, m_lighting)) {
    return;
  }
  m_lighting = next;
  m_graph.invalidateSnapshots();
  for (std::pair<const std::string, std::unique_ptr<Record>>& entry :
       m_records) {
    for (size_t index = 0; index < entry.second->visuals.size(); ++index) {
      applyLightingTo(*entry.second->visuals[index],
                      entry.second->castShadows[index]);
    }
  }
}

SceneLighting
SceneInstance::resolveLighting() const
{
  SceneLighting lighting;
  if (m_state.worldMode == SceneWorldMode::World2D) {
    return lighting;
  }
  for (SceneNodeHandle handle = m_graph.firstNode(); !handle.isNull();
       handle = m_graph.nextNode(handle)) {
    const Record* entry = record(m_graph.getName(handle));
    if (entry == nullptr || !entry->hasLight || !entry->node.enabled ||
        !m_graph.isEffectivelyVisible(handle)) {
      continue;
    }
    const SceneComponent* component =
      entry->node.find(SceneComponentType::Light);
    const SceneLight& light = std::get<SceneLight>(component->value);
    Matrix4 world(1.0f);
    m_graph.getWorldTransform(handle, &world);
    Vector3 up = Vector3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    const float length = glm::length(up);
    if (!std::isfinite(length) || length < 1.0e-6f) {
      continue;
    }
    lighting.lit = true;
    lighting.towardLight = up / length;
    lighting.color = light.color * light.intensity;
    lighting.ambient = m_state.environment.ambient;
    lighting.shadows = light.shadows;
    lighting.sourceId = entry->node.id;
    return lighting;
  }
  if (m_state.environment.hasSun) {
    const SceneSun& sun = m_state.environment.sun;
    lighting.lit = true;
    lighting.towardLight = -glm::normalize(sun.direction);
    lighting.color = sun.color * sun.intensity;
    lighting.ambient = m_state.environment.ambient;
    lighting.shadows = sun.shadows;
  }
  return lighting;
}

void
SceneInstance::applyLightingTo(MeshVisual& visual, bool castShadows) const
{
  visual.setLightingEnabled(m_lighting.lit);
  visual.setLightDirection(m_lighting.towardLight);
  visual.setLightColor(m_lighting.color);
  visual.setAmbientColor(m_lighting.ambient);
  visual.setShadowsEnabled(m_lighting.lit && m_lighting.shadows && castShadows);
}

const SceneDocument&
SceneInstance::document() const
{
  if (!m_cacheDirty) {
    return m_cache;
  }
  m_cache = m_state;
  m_cache.nodes.clear();
  m_cache.nodes.reserve(m_records.size());
  for (SceneNodeHandle handle = m_graph.firstNode(); !handle.isNull();
       handle = m_graph.nextNode(handle)) {
    const Record* entry = record(m_graph.getName(handle));
    if (entry == nullptr) {
      continue;
    }
    m_cache.nodes.push_back(entry->node);
    const SceneNodeHandle parent = m_graph.getParent(handle);
    m_cache.nodes.back().parentId =
      parent.isNull() ? std::string() : std::string(m_graph.getName(parent));
  }
  m_cacheDirty = false;
  return m_cache;
}

const SceneNode*
SceneInstance::findNode(std::string_view id) const
{
  const Record* entry = record(id);
  return entry == nullptr ? nullptr : &entry->node;
}

std::string
SceneInstance::parentOf(std::string_view id) const
{
  const SceneNodeHandle node = handleOf(id);
  return node.isNull() ? std::string() : idOf(m_graph.getParent(node));
}

SceneNodeHandle
SceneInstance::handleOf(std::string_view id) const
{
  const Record* entry = record(id);
  return entry == nullptr ? SceneNodeHandle{} : entry->handle;
}

std::string
SceneInstance::idOf(SceneNodeHandle node) const
{
  return m_graph.isNodeValid(node) ? std::string(m_graph.getName(node))
                                   : std::string();
}

std::vector<std::string>
SceneInstance::childIds(std::string_view parentId) const
{
  std::vector<std::string> ids;
  SceneNodeHandle child;
  if (parentId.empty()) {
    child = m_graph.getRoot(0);
  } else {
    const SceneNodeHandle parent = handleOf(parentId);
    if (parent.isNull()) {
      return ids;
    }
    child = m_graph.getChild(parent, 0);
  }
  while (!child.isNull()) {
    ids.emplace_back(m_graph.getName(child));
    child = m_graph.getNextSibling(child);
  }
  return ids;
}

std::string
SceneInstance::nextSiblingId(std::string_view id) const
{
  const SceneNodeHandle next = m_graph.getNextSibling(handleOf(id));
  return next.isNull() ? std::string() : std::string(m_graph.getName(next));
}

Matrix4
SceneInstance::worldMatrix(std::string_view id) const
{
  Matrix4 world(1.0f);
  m_graph.getWorldTransform(handleOf(id), &world);
  return world;
}

std::vector<std::string>
SceneInstance::subtreeIds(std::string_view id) const
{
  std::vector<std::string> ids;
  const SceneNodeHandle root = handleOf(id);
  if (root.isNull()) {
    return ids;
  }
  ids.emplace_back(id);
  SceneNodeHandle node = m_graph.nextNode(root);
  while (!node.isNull()) {
    SceneNodeHandle ancestor = m_graph.getParent(node);
    while (!ancestor.isNull() && ancestor != root) {
      ancestor = m_graph.getParent(ancestor);
    }
    if (ancestor != root) {
      break;
    }
    ids.emplace_back(m_graph.getName(node));
    node = m_graph.nextNode(node);
  }
  return ids;
}

std::string
SceneInstance::uniqueId(std::string_view prefix)
{
  while (true) {
    ++m_idCounter;
    std::string candidate = std::string(prefix) + std::to_string(m_idCounter);
    if (m_records.find(candidate) == m_records.end()) {
      return candidate;
    }
  }
}

bool
SceneInstance::validateNode(const SceneNode& node, std::string& error) const
{
  // The document validator checks one node against the asset table when the
  // node is presented as a root of an otherwise empty scene.
  SceneDocument probe;
  probe.assets = m_state.assets;
  probe.nodes.push_back(node);
  probe.nodes.back().parentId.clear();
  probe.environment.skybox.clear();
  return validateSceneDocument(probe, error);
}

bool
SceneInstance::buildNode(const SceneNode& node,
                         SceneNodeHandle parent,
                         SceneNodeHandle before,
                         std::string& error)
{
  SceneNodeDesc description;
  description.parent = parent;
  description.transform = node.transform;
  description.name = node.id;
  description.enabled = node.enabled;
  description.visible = node.visible;
  const SceneNodeHandle handle = m_graph.createNode(description);
  if (handle.isNull()) {
    error = "The scene graph rejected the node";
    return false;
  }
  if (!before.isNull() && !m_graph.setParent(handle, parent, before)) {
    m_graph.destroyNode(handle);
    error = "The insert position is not a child of the parent";
    return false;
  }
  std::unique_ptr<Record> entry = std::make_unique<Record>();
  entry->node = node;
  entry->node.parentId.clear();
  entry->handle = handle;
  Record& created = *entry;
  m_records.emplace(node.id, std::move(entry));
  buildAttachments(created);
  return true;
}

void
SceneInstance::destroyAttachments(Record& entry)
{
  if (entry.visuals.empty() && !entry.proxy) {
    return;
  }
  m_graph.invalidateSnapshots();
  for (std::unique_ptr<MeshVisual>& visual : entry.visuals) {
    m_graph.removeAttachment(entry.handle, visual.get());
  }
  if (entry.proxy) {
    m_graph.removeAttachment(entry.handle, entry.proxy.get());
  }
  entry.visuals.clear();
  entry.castShadows.clear();
  entry.proxy.reset();
}

SceneInstance::AssetSlot*
SceneInstance::assetSlot(std::string_view id)
{
  const std::unordered_map<std::string, std::unique_ptr<AssetSlot>>::iterator
    found = m_assetSlots.find(std::string(id));
  if (found == m_assetSlots.end()) {
    return nullptr;
  }
  AssetSlot& slot = *found->second;
  if (slot.attempted) {
    return &slot;
  }
  slot.attempted = true;
  const SceneAsset& asset = slot.asset;
  std::array<std::string, 6> resolved;
  const size_t count = asset.type == SceneAssetType::CubemapFaces ? 6 : 1;
  for (size_t index = 0; index < count; ++index) {
    const std::string& reference = asset.type == SceneAssetType::CubemapFaces
                                     ? asset.faces[index]
                                     : asset.path;
    if (!resolveSceneReference(m_packageRoot, reference, resolved[index])) {
      slot.failed = true;
      m_warnings.push_back("Asset \"" + asset.id + "\": reference \"" +
                           reference + "\" does not resolve");
      reportAssetWarning(m_warnings.back());
      return &slot;
    }
  }
  if (m_assets == nullptr) {
    slot.failed = true;
    m_warnings.push_back("Asset \"" + asset.id +
                         "\": no asset manager; drawing a placeholder");
    reportAssetWarning(m_warnings.back());
    return &slot;
  }
  switch (asset.type) {
    case SceneAssetType::Mesh: {
      MeshLoadOptions options;
      options.centerAndNormalize = asset.mesh.centerAndNormalize;
      options.targetRadius = asset.mesh.targetRadius;
      options.flipTexCoordsV = asset.mesh.flipV;
      options.generateNormalsIfMissing = asset.mesh.generateNormals;
      slot.mesh = m_assets->acquireMesh(resolved[0], options);
      slot.meshInfo = m_assets->getMeshInfo(slot.mesh);
      slot.failed = !slot.meshInfo.isValid();
      break;
    }
    case SceneAssetType::Texture:
    case SceneAssetType::Atlas: {
      TextureOptions options;
      options.filter = asset.texture.filter == SceneTextureFilter::Linear
                         ? TextureFilter::Linear
                         : TextureFilter::Nearest;
      options.wrapX = asset.texture.wrap == SceneTextureWrap::Repeat
                        ? TextureWrap::Repeat
                        : TextureWrap::ClampToEdge;
      options.wrapY = options.wrapX;
      options.generateMipmaps = asset.texture.mipmaps;
      slot.texture = m_assets->acquireTexture(
        resolved[0], options, AssetLoadMode::Synchronous);
      slot.failed =
        !slot.texture.isValid() ||
        m_assets->getState(slot.texture).state == AssetState::Failed;
      break;
    }
    case SceneAssetType::CubemapCross:
      slot.texture = m_assets->acquireCubemapFromCross(resolved[0]);
      slot.failed =
        !slot.texture.isValid() ||
        m_assets->getState(slot.texture).state == AssetState::Failed;
      break;
    case SceneAssetType::CubemapFaces:
      slot.texture = m_assets->acquireCubemap(resolved);
      slot.failed =
        !slot.texture.isValid() ||
        m_assets->getState(slot.texture).state == AssetState::Failed;
      break;
  }
  if (slot.failed) {
    m_warnings.push_back("Asset \"" + asset.id + "\" (" + resolved[0] +
                         ") failed to load; drawing a placeholder");
    reportAssetWarning(m_warnings.back());
  }
  return &slot;
}

MeshHandle
SceneInstance::primitiveMesh(ScenePrimitiveShape shape)
{
  const int index = scenePrimitiveMeshSlot(shape);
  if (index < 0 || m_assets == nullptr) {
    return MeshHandle{};
  }
  if (!m_primitiveMeshes[index].isValid()) {
    MeshData mesh;
    if (buildScenePrimitiveMesh(shape, mesh)) {
      m_primitiveMeshes[index] = m_assets->acquireMesh(mesh);
    }
  }
  return m_primitiveMeshes[index];
}

static bool
drawsVisual(const SceneComponent& component)
{
  return component.type() == SceneComponentType::Primitive ||
         component.type() == SceneComponentType::Mesh ||
         component.type() == SceneComponentType::Sprite;
}

bool
SceneInstance::configureVisual(MeshVisual& visual,
                               const SceneComponent& component,
                               bool* castShadows)
{
  visual.clearPrimitives();
  visual.clearMeshAsset();
  visual.setModelMatrix(Matrix4(1.0f));
  *castShadows = true;
  if (const ScenePrimitive* primitive =
        std::get_if<ScenePrimitive>(&component.value)) {
    visual.setModelMatrix(glm::scale(Matrix4(1.0f), primitive->extent));
    if (primitive->shape == ScenePrimitiveShape::WireCube) {
      visual.addWireCube(glm::vec3(0.0f), glm::vec3(1.0f), primitive->color);
    } else if (primitive->shape == ScenePrimitiveShape::WireSphere) {
      visual.addWireSphere(glm::vec3(0.0f), 1.0f, primitive->color);
    } else {
      const MeshHandle shared = primitiveMesh(primitive->shape);
      const MeshAssetInfo info =
        shared.isValid() ? m_assets->getMeshInfo(shared) : MeshAssetInfo{};
      if (info.isValid()) {
        visual.setMeshAsset(info, primitive->color);
      } else {
        MeshData mesh;
        buildScenePrimitiveMesh(primitive->shape, mesh);
        visual.addMesh(mesh, primitive->color);
      }
    }
    return true;
  }
  if (const SceneMeshRenderer* mesh =
        std::get_if<SceneMeshRenderer>(&component.value)) {
    *castShadows = mesh->castShadows;
    AssetSlot* slot = assetSlot(mesh->asset);
    if (slot != nullptr && !slot->failed) {
      visual.setMeshAsset(slot->meshInfo, mesh->tint);
    } else {
      visual.addSolidCube(glm::vec3(0.0f), glm::vec3(0.5f), kPlaceholderColor);
    }
    return true;
  }
  if (const SceneSprite* sprite = std::get_if<SceneSprite>(&component.value)) {
    *castShadows = false;
    AssetSlot* slot = assetSlot(sprite->texture);
    const glm::vec2 size(sprite->width, sprite->height);
    if (slot != nullptr && !slot->failed) {
      TextureRegion region;
      if (sprite->hasCell) {
        region = TextureRegion::gridCell(
          static_cast<unsigned int>(slot->asset.columns),
          static_cast<unsigned int>(slot->asset.rows),
          static_cast<unsigned int>(sprite->column),
          static_cast<unsigned int>(sprite->row));
      } else {
        region =
          TextureRegion{ sprite->u0, sprite->v0, sprite->u1, sprite->v1 };
      }
      if (sprite->flipX) {
        std::swap(region.u0, region.u1);
      }
      if (sprite->flipY) {
        std::swap(region.v0, region.v1);
      }
      visual.addSprite(slot->texture,
                       glm::vec3(0.0f),
                       size,
                       sprite->tint,
                       sprite->facing == SceneSpriteFacing::Billboard
                         ? MeshFacing::Billboard
                         : MeshFacing::World,
                       region);
    } else {
      visual.addQuad(glm::vec3(0.0f), size, kPlaceholderColor);
    }
    return true;
  }
  return false;
}

void
SceneInstance::buildAttachments(Record& entry)
{
  destroyAttachments(entry);
  entry.hasLight = false;
  for (const SceneComponent& component : entry.node.components) {
    if (component.type() == SceneComponentType::Light) {
      entry.hasLight = true;
      m_lightingDirty = true;
    }
    if (!drawsVisual(component)) {
      continue;
    }
    std::unique_ptr<MeshVisual> visual = std::make_unique<MeshVisual>();
    bool castShadows = true;
    configureVisual(*visual, component, &castShadows);
    if (m_renderer != nullptr) {
      visual->prepare(m_renderer);
    }
    applyLightingTo(*visual, castShadows);
    m_graph.addAttachment(entry.handle, visual.get());
    entry.visuals.push_back(std::move(visual));
    entry.castShadows.push_back(castShadows);
  }
  if (entry.visuals.empty() && m_options.pickProxies) {
    entry.proxy = std::make_unique<PickProxy>();
    m_graph.addAttachment(entry.handle, entry.proxy.get());
  }
}

bool
SceneInstance::reconfigureAttachments(Record& entry,
                                      const std::vector<SceneComponent>& before)
{
  // In place only when the same kinds of visual components appear in the
  // same order, so each existing visual keeps its identity.
  std::vector<const SceneComponent*> visuals;
  std::vector<SceneComponentType> previous;
  for (const SceneComponent& component : before) {
    if (drawsVisual(component)) {
      previous.push_back(component.type());
    }
  }
  bool hasLight = false;
  for (const SceneComponent& component : entry.node.components) {
    hasLight = hasLight || component.type() == SceneComponentType::Light;
    if (drawsVisual(component)) {
      visuals.push_back(&component);
    }
  }
  if (visuals.size() != previous.size() ||
      visuals.size() != entry.visuals.size()) {
    return false;
  }
  for (size_t index = 0; index < visuals.size(); ++index) {
    if (visuals[index]->type() != previous[index]) {
      return false;
    }
  }
  m_graph.invalidateSnapshots();
  for (size_t index = 0; index < visuals.size(); ++index) {
    bool castShadows = true;
    configureVisual(*entry.visuals[index], *visuals[index], &castShadows);
    entry.castShadows[index] = castShadows;
    applyLightingTo(*entry.visuals[index], castShadows);
  }
  m_lightingDirty = m_lightingDirty || hasLight || entry.hasLight;
  entry.hasLight = hasLight;
  return true;
}
void
SceneInstance::rebuildAllAttachments()
{
  m_graph.invalidateSnapshots();
  for (std::pair<const std::string, std::unique_ptr<Record>>& entry :
       m_records) {
    buildAttachments(*entry.second);
  }
}

void
SceneInstance::rebuildEnvironment()
{
  if (m_skybox) {
    m_graph.invalidateSnapshots();
    m_skybox.reset();
  }
  const SceneEnvironment& environment = m_state.environment;
  if (environment.skybox.empty()) {
    return;
  }
  AssetSlot* slot = assetSlot(environment.skybox);
  if (slot == nullptr || slot->failed) {
    return;
  }
  m_skybox = std::make_unique<SkyboxVisual>(slot->texture);
  m_skybox->setTint(glm::vec4(environment.skyboxTint, 1.0f));
  if (m_renderer != nullptr) {
    m_skybox->prepare(m_renderer);
  }
}

bool
SceneInstance::insertNode(const SceneNode& node,
                          std::string_view insertBeforeId,
                          std::string& error)
{
  if (!validateNode(node, error)) {
    return false;
  }
  if (record(node.id) != nullptr) {
    error = "Node id \"" + node.id + "\" is already used";
    return false;
  }
  SceneNodeHandle parent;
  if (!node.parentId.empty()) {
    parent = handleOf(node.parentId);
    if (parent.isNull()) {
      error = "Parent \"" + node.parentId + "\" does not exist";
      return false;
    }
  }
  SceneNodeHandle before;
  if (!insertBeforeId.empty()) {
    before = handleOf(insertBeforeId);
    if (before.isNull() || m_graph.getParent(before) != parent) {
      error = "Insert position is not a sibling under the parent";
      return false;
    }
  }
  if (m_records.size() >= SceneDocument::kMaximumNodes) {
    error = "The scene has the maximum number of nodes";
    return false;
  }
  if (!buildNode(node, parent, before, error)) {
    return false;
  }
  touch();
  return true;
}

bool
SceneInstance::removeSubtree(std::string_view id)
{
  const std::vector<std::string> ids = subtreeIds(id);
  if (ids.empty()) {
    return false;
  }
  m_graph.invalidateSnapshots();
  for (const std::string& member : ids) {
    Record* entry = record(member);
    if (entry != nullptr) {
      m_lightingDirty = m_lightingDirty || entry->hasLight;
      destroyAttachments(*entry);
    }
  }
  m_graph.destroyNode(handleOf(id));
  for (const std::string& member : ids) {
    m_records.erase(member);
  }
  touch();
  return true;
}

bool
SceneInstance::setParent(std::string_view id,
                         std::string_view parentId,
                         std::string_view insertBeforeId,
                         std::string& error)
{
  const SceneNodeHandle node = handleOf(id);
  if (node.isNull()) {
    error = "Node does not exist";
    return false;
  }
  SceneNodeHandle parent;
  if (!parentId.empty()) {
    parent = handleOf(parentId);
    if (parent.isNull()) {
      error = "Parent does not exist";
      return false;
    }
  }
  SceneNodeHandle before;
  if (!insertBeforeId.empty()) {
    before = handleOf(insertBeforeId);
    if (before.isNull()) {
      error = "Insert position does not exist";
      return false;
    }
  }
  if (!m_graph.setParent(node, parent, before)) {
    error = "That parent would create a cycle or the position is invalid";
    return false;
  }
  m_lightingDirty = true;
  touch();
  return true;
}

bool
SceneInstance::setTransform(std::string_view id, const Transform3D& transform)
{
  Record* entry = record(id);
  SceneNode probe;
  std::string error;
  if (entry == nullptr) {
    return false;
  }
  probe.id = entry->node.id;
  probe.transform = transform;
  if (!validateNode(probe, error) ||
      !m_graph.setLocalTransform(entry->handle, transform)) {
    return false;
  }
  entry->node.transform = transform;
  touch();
  return true;
}

bool
SceneInstance::setTransforms(const std::vector<std::string>& ids,
                             const std::vector<Transform3D>& transforms)
{
  if (ids.size() != transforms.size()) {
    return false;
  }
  std::vector<SceneNodeHandle> handles;
  handles.reserve(ids.size());
  for (size_t index = 0; index < ids.size(); ++index) {
    Record* entry = record(ids[index]);
    SceneNode probe;
    std::string error;
    if (entry == nullptr) {
      return false;
    }
    probe.id = entry->node.id;
    probe.transform = transforms[index];
    if (!validateNode(probe, error)) {
      return false;
    }
    handles.push_back(entry->handle);
  }
  if (!m_graph.setLocalTransforms(
        handles.data(), transforms.data(), handles.size())) {
    return false;
  }
  for (size_t index = 0; index < ids.size(); ++index) {
    record(ids[index])->node.transform = transforms[index];
  }
  touch();
  return true;
}

bool
SceneInstance::setName(std::string_view id, std::string_view name)
{
  Record* entry = record(id);
  if (entry == nullptr) {
    return false;
  }
  SceneNode probe;
  probe.id = entry->node.id;
  probe.name = std::string(name);
  std::string error;
  if (!validateNode(probe, error)) {
    return false;
  }
  entry->node.name = std::string(name);
  touch();
  return true;
}

bool
SceneInstance::setEnabled(std::string_view id, bool enabled)
{
  Record* entry = record(id);
  if (entry == nullptr || !m_graph.setEnabled(entry->handle, enabled)) {
    return false;
  }
  entry->node.enabled = enabled;
  m_lightingDirty = true;
  touch();
  return true;
}

bool
SceneInstance::setVisible(std::string_view id, bool visible)
{
  Record* entry = record(id);
  if (entry == nullptr || !m_graph.setVisible(entry->handle, visible)) {
    return false;
  }
  entry->node.visible = visible;
  m_lightingDirty = true;
  touch();
  return true;
}

bool
SceneInstance::setTags(std::string_view id,
                       const std::vector<std::string>& tags,
                       std::string& error)
{
  Record* entry = record(id);
  if (entry == nullptr) {
    error = "Node does not exist";
    return false;
  }
  SceneNode probe;
  probe.id = entry->node.id;
  probe.tags = tags;
  if (!validateNode(probe, error)) {
    return false;
  }
  entry->node.tags = tags;
  touch();
  return true;
}

bool
SceneInstance::setComponents(std::string_view id,
                             const std::vector<SceneComponent>& components,
                             std::string& error)
{
  Record* entry = record(id);
  if (entry == nullptr) {
    error = "Node does not exist";
    return false;
  }
  SceneNode probe;
  probe.id = entry->node.id;
  probe.components = components;
  if (!validateNode(probe, error)) {
    return false;
  }
  m_lightingDirty = m_lightingDirty || entry->hasLight;
  const std::vector<SceneComponent> before = entry->node.components;
  entry->node.components = components;
  if (!reconfigureAttachments(*entry, before)) {
    buildAttachments(*entry);
  }
  m_graph.notifyAttachmentChanged(entry->handle);
  touch();
  return true;
}

bool
SceneInstance::replaceNode(const SceneNode& node, std::string& error)
{
  Record* entry = record(node.id);
  if (entry == nullptr) {
    error = "Node does not exist";
    return false;
  }
  const std::string currentParent = idOf(m_graph.getParent(entry->handle));
  if (node.parentId != currentParent) {
    error = "replaceNode cannot change the parent; use setParent";
    return false;
  }
  if (!validateNode(node, error)) {
    return false;
  }
  m_graph.setLocalTransform(entry->handle, node.transform);
  m_graph.setEnabled(entry->handle, node.enabled);
  m_graph.setVisible(entry->handle, node.visible);
  const std::vector<SceneComponent> before = entry->node.components;
  entry->node = node;
  entry->node.parentId.clear();
  if (!reconfigureAttachments(*entry, before)) {
    buildAttachments(*entry);
  }
  m_graph.notifyAttachmentChanged(entry->handle);
  m_lightingDirty = true;
  touch();
  return true;
}

bool
SceneInstance::setAssets(const std::vector<SceneAsset>& assets,
                         std::string& error)
{
  SceneDocument probe = document();
  probe.assets = assets;
  if (!validateSceneDocument(probe, error)) {
    return false;
  }
  m_graph.invalidateSnapshots();
  for (std::pair<const std::string, std::unique_ptr<Record>>& entry :
       m_records) {
    destroyAttachments(*entry.second);
  }
  releaseAssets();
  m_warnings.clear();
  m_state.assets = assets;
  for (const SceneAsset& asset : assets) {
    std::unique_ptr<AssetSlot> slot = std::make_unique<AssetSlot>();
    slot->asset = asset;
    m_assetSlots.emplace(asset.id, std::move(slot));
  }
  rebuildAllAttachments();
  rebuildEnvironment();
  touch();
  return true;
}

bool
SceneInstance::setEnvironment(const SceneEnvironment& environment,
                              std::string& error)
{
  SceneDocument probe;
  probe.assets = m_state.assets;
  probe.environment = environment;
  if (!validateSceneDocument(probe, error)) {
    return false;
  }
  m_state.environment = environment;
  rebuildEnvironment();
  m_lightingDirty = true;
  update();
  touch();
  return true;
}

void
SceneInstance::setWorldMode(SceneWorldMode mode)
{
  if (m_state.worldMode == mode) {
    return;
  }
  m_state.worldMode = mode;
  m_lightingDirty = true;
  update();
  touch();
}

void
SceneInstance::setMetadata(const SceneMetadata& metadata)
{
  m_state.metadata = metadata;
  touch();
}

bool
SceneInstance::setExtensions(const std::vector<SceneExtension>& extensions,
                             std::string& error)
{
  SceneDocument probe;
  probe.extensions = extensions;
  if (!validateSceneDocument(probe, error)) {
    return false;
  }
  m_state.extensions = extensions;
  touch();
  return true;
}

void
SceneInstance::setEditorState(const SceneEditorState& state)
{
  m_state.hasEditor = true;
  m_state.editor = state;
  m_cacheDirty = true;
}

bool
SceneInstance::localBounds(std::string_view id,
                           AxisAlignedBounds3* bounds) const
{
  const Record* entry = record(id);
  if (entry == nullptr || bounds == nullptr) {
    return false;
  }
  bool any = false;
  AxisAlignedBounds3 combined;
  const size_t count = m_graph.getAttachmentCount(entry->handle);
  for (size_t index = 0; index < count; ++index) {
    AxisAlignedBounds3 local;
    const ISceneRenderAttachment* attachment =
      m_graph.getAttachment(entry->handle, index);
    if (attachment == nullptr || !attachment->getSceneLocalBounds(&local)) {
      continue;
    }
    if (!any) {
      combined = local;
      any = true;
    } else {
      combined.include(local);
    }
  }
  if (any) {
    *bounds = combined;
  }
  return any;
}

bool
SceneInstance::pickRay(const Vector3& origin,
                       const Vector3& direction,
                       std::string* id) const
{
  if (id == nullptr) {
    return false;
  }
  id->clear();
  for (int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) {
      return false;
    }
  }
  if (direction == Vector3(0.0f)) {
    return false;
  }
  m_graph.raycastCandidates(origin, direction, &m_candidates);
  double nearest = std::numeric_limits<double>::infinity();
  std::vector<SceneNodeHandle> tied;
  for (const SceneRayHit& candidate : m_candidates) {
    AxisAlignedBounds3 bounds;
    const std::string candidateId = idOf(candidate.node);
    if (!localBounds(candidateId, &bounds)) {
      continue;
    }
    Matrix4 worldValue(1.0f);
    m_graph.getWorldTransform(candidate.node, &worldValue);
    const glm::dmat4 world(worldValue);
    const double determinant = glm::determinant(world);
    if (!std::isfinite(determinant) || determinant == 0.0) {
      continue;
    }
    const glm::dmat4 inverse = glm::inverse(world);
    const glm::dvec3 localOrigin(inverse * glm::dvec4(origin, 1.0));
    // Not normalized, so t stays comparable across differently scaled nodes.
    const glm::dvec3 localDirection(inverse * glm::dvec4(direction, 0.0));
    const glm::dvec3 minimum(bounds.minimum);
    const glm::dvec3 maximum(bounds.maximum);
    double enter = 0.0;
    double leave = std::numeric_limits<double>::infinity();
    bool hit = true;
    for (int axis = 0; axis < 3 && hit; ++axis) {
      if (!std::isfinite(localOrigin[axis]) ||
          !std::isfinite(localDirection[axis])) {
        hit = false;
      } else if (localDirection[axis] == 0.0) {
        hit = localOrigin[axis] >= minimum[axis] &&
              localOrigin[axis] <= maximum[axis];
      } else {
        double first =
          (minimum[axis] - localOrigin[axis]) / localDirection[axis];
        double last =
          (maximum[axis] - localOrigin[axis]) / localDirection[axis];
        if (first > last) {
          std::swap(first, last);
        }
        enter = std::max(enter, first);
        leave = std::min(leave, last);
        hit = enter <= leave;
      }
    }
    if (!hit) {
      continue;
    }
    if (tied.empty()) {
      nearest = enter;
      tied.assign(1, candidate.node);
      continue;
    }
    const double tolerance = 1.0e-9 * std::max(1.0, std::fabs(nearest));
    if (enter < nearest - tolerance) {
      nearest = enter;
      tied.assign(1, candidate.node);
    } else if (std::fabs(enter - nearest) <= tolerance) {
      tied.push_back(candidate.node);
    }
  }
  if (tied.empty()) {
    return false;
  }
  SceneNodeHandle chosen = tied.front();
  if (tied.size() > 1) {
    // The last tied node in preorder draws on top.
    std::unordered_set<uint32_t> slots;
    for (const SceneNodeHandle& handle : tied) {
      slots.insert(handle.slot);
    }
    for (SceneNodeHandle node = m_graph.firstNode(); !node.isNull();
         node = m_graph.nextNode(node)) {
      if (slots.find(node.slot) != slots.end()) {
        chosen = node;
      }
    }
  }
  *id = idOf(chosen);
  return !id->empty();
}

bool
SceneInstance::primaryCameraId(std::string* id) const
{
  if (id == nullptr) {
    return false;
  }
  id->clear();
  for (SceneNodeHandle handle = m_graph.firstNode(); !handle.isNull();
       handle = m_graph.nextNode(handle)) {
    const Record* entry = record(m_graph.getName(handle));
    const SceneComponent* component =
      entry == nullptr ? nullptr : entry->node.find(SceneComponentType::Camera);
    if (component == nullptr) {
      continue;
    }
    if (id->empty()) {
      *id = entry->node.id;
    }
    if (std::get<SceneCamera>(component->value).primary) {
      *id = entry->node.id;
      return true;
    }
  }
  return !id->empty();
}

bool
SceneInstance::applyCamera(std::string_view id, Camera& camera) const
{
  const Record* entry = record(id);
  const SceneComponent* component =
    entry == nullptr ? nullptr : entry->node.find(SceneComponentType::Camera);
  if (component == nullptr) {
    return false;
  }
  const SceneCamera& settings = std::get<SceneCamera>(component->value);
  const Matrix4 world = worldMatrix(id);
  const Vector3 eye(world[3]);
  if (settings.projection == SceneProjection::Orthographic) {
    camera.setProjectionType(ProjectionType::Orthographic);
    camera.SetPositionPrecise(eye.x, eye.y);
    camera.SetZoom(settings.zoom);
    return true;
  }
  Vector3 forward(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
  Vector3 up(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
  if (glm::length(forward) < 1.0e-6f || glm::length(up) < 1.0e-6f) {
    return false;
  }
  forward = glm::normalize(forward);
  up = glm::normalize(up);
  camera.setProjectionType(ProjectionType::Perspective);
  camera.setPerspective(
    settings.fovDegrees, settings.nearPlane, settings.farPlane);
  camera.lookAt(eye, eye + forward, up);
  return true;
}
