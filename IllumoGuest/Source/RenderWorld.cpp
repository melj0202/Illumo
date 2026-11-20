#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <IllumoGuest/RenderWorld.h>
#include <algorithm>
#include <cmath>

static bool
finiteValues(const float* values, std::size_t count)
{
  for (std::size_t index = 0; index < count; ++index) {
    if (!std::isfinite(values[index])) {
      return false;
    }
  }
  return true;
}

static constexpr std::size_t kMaximumGuestLights = 1024;
static constexpr std::size_t kMaximumGuestMaterials = 4096;

static bool
sameResourceId(const GuestResourceId& left, const GuestResourceId& right)
{
  return left.owner == right.owner && left.kind == right.kind &&
         left.slot == right.slot && left.generation == right.generation;
}

static bool
validGuestLightDesc(const RenderLightDesc& desc)
{
  if (desc.kind != RenderLightKind::Point &&
      desc.kind != RenderLightKind::Spot) {
    return false;
  }
  if (!finiteValues(desc.position.data(), desc.position.size()) ||
      !finiteValues(desc.direction.data(), desc.direction.size()) ||
      !finiteValues(desc.color.data(), desc.color.size()) ||
      !std::isfinite(desc.intensity) || !std::isfinite(desc.range) ||
      !std::isfinite(desc.innerConeCos) || !std::isfinite(desc.outerConeCos)) {
    return false;
  }
  if (desc.intensity < 0.0f || desc.range <= 0.0f) {
    return false;
  }
  if (desc.kind == RenderLightKind::Spot) {
    const float dirLenSq = desc.direction[0] * desc.direction[0] +
                           desc.direction[1] * desc.direction[1] +
                           desc.direction[2] * desc.direction[2];
    if (!std::isfinite(dirLenSq) || dirLenSq <= 1e-8f ||
        desc.innerConeCos < desc.outerConeCos || desc.outerConeCos <= -1.0f ||
        desc.innerConeCos > 1.0f) {
      return false;
    }
  }
  return true;
}

static GuestWorldLight
wireLight(const RenderLightDesc& desc)
{
  GuestWorldLight light;
  light.kind = desc.kind == RenderLightKind::Spot ? 1u : 0u;
  light.position = desc.position;
  light.direction = desc.direction;
  light.color = { std::max(desc.color[0], 0.0f),
                  std::max(desc.color[1], 0.0f),
                  std::max(desc.color[2], 0.0f) };
  light.intensity = desc.intensity;
  light.range = desc.range;
  light.innerCos = desc.innerConeCos;
  light.outerCos = desc.outerConeCos;
  light.castsShadow = desc.castsShadow;
  light.visible = desc.visible;
  return light;
}

GuestRenderWorld::GuestRenderWorld(const GuestRecordingBackend& backend)
  : m_backend(backend)
{
}

bool
GuestRenderWorld::validMaterial(const RenderMaterialDesc& desc) const
{
  if (!finiteValues(desc.tint.data(), desc.tint.size()) ||
      !std::isfinite(desc.metallic) || !std::isfinite(desc.roughness) ||
      !finiteValues(desc.emissiveFactor.data(), desc.emissiveFactor.size())) {
    return false;
  }
  const TextureHandle handles[4] = { desc.albedoTexture,
                                     desc.normalTexture,
                                     desc.metallicRoughnessTexture,
                                     desc.emissiveTexture };
  for (const TextureHandle& h : handles) {
    if (h.isValid() && !m_backend.isUsable2DTexture(h)) {
      return false;
    }
  }
  return true;
}

GuestWorldMaterial
GuestRenderWorld::buildWireMaterial(
  const RenderMaterialDesc& desc,
  std::array<GuestResourceId, 4>* outIds) const
{
  GuestWorldMaterial material;
  material.tint = desc.tint;
  material.receivesShadow = desc.receivesShadow;
  material.castsShadow = desc.castsShadow;
  material.blend = desc.blend;
  material.albedoTexture = m_backend.hostTexture(desc.albedoTexture);
  material.normalTexture = m_backend.hostTexture(desc.normalTexture);
  material.metallicRoughnessTexture =
    m_backend.hostTexture(desc.metallicRoughnessTexture);
  material.emissiveTexture = m_backend.hostTexture(desc.emissiveTexture);
  material.metallic = std::clamp(desc.metallic, 0.0f, 1.0f);
  material.roughness = std::clamp(desc.roughness, 0.0f, 1.0f);
  material.emissiveFactor = { std::max(desc.emissiveFactor[0], 0.0f),
                              std::max(desc.emissiveFactor[1], 0.0f),
                              std::max(desc.emissiveFactor[2], 0.0f) };
  if (outIds != nullptr) {
    *outIds = { material.albedoTexture,
                material.normalTexture,
                material.metallicRoughnessTexture,
                material.emissiveTexture };
  }
  return material;
}

bool
GuestRenderWorld::createMaterial(RenderMaterialId id,
                                 const RenderMaterialDesc& desc)
{
  if (id == 0 || m_materials.contains(id) ||
      m_materials.size() >= kMaximumGuestMaterials || !validMaterial(desc)) {
    return false;
  }
  MaterialState state;
  state.desc = desc;
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::MaterialCreate;
  operation.id = id;
  operation.material = buildWireMaterial(desc, &state.sentTextures);
  m_materials[id] = state;
  m_queue.push_back(operation);
  return true;
}

bool
GuestRenderWorld::updateMaterial(RenderMaterialId id,
                                 const RenderMaterialDesc& desc)
{
  std::unordered_map<RenderMaterialId, MaterialState>::iterator found =
    m_materials.find(id);
  if (found == m_materials.end() || !validMaterial(desc)) {
    return false;
  }
  found->second.desc = desc;
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::MaterialUpdate;
  operation.id = id;
  operation.material = buildWireMaterial(desc, &found->second.sentTextures);
  m_queue.push_back(operation);
  return true;
}

bool
GuestRenderWorld::destroyMaterial(RenderMaterialId id)
{
  std::unordered_map<RenderMaterialId, MaterialState>::iterator found =
    m_materials.find(id);
  if (found == m_materials.end() || found->second.users != 0) {
    return false;
  }
  m_materials.erase(found);
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::MaterialDestroy;
  operation.id = id;
  m_queue.push_back(operation);
  return true;
}

GuestWorldOperation
GuestRenderWorld::createOperation(RenderInstanceId id,
                                  const RenderInstanceDesc& desc,
                                  const GuestResourceId& mesh) const
{
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::InstanceCreate;
  operation.id = id;
  operation.mesh = mesh;
  operation.firstIndex = desc.firstIndex;
  operation.indexCount = desc.indexCount;
  operation.materialId = desc.material;
  operation.transform = desc.world;
  operation.tint = desc.tint;
  operation.visible = desc.visible;
  return operation;
}

bool
GuestRenderWorld::createInstance(RenderInstanceId id,
                                 const RenderInstanceDesc& desc)
{
  if (id == 0 || m_instances.contains(id) ||
      !m_materials.contains(desc.material) || desc.indexCount == 0 ||
      desc.indexCount % 3 != 0 ||
      !finiteValues(desc.world.data(), desc.world.size()) ||
      !finiteValues(desc.tint.data(), desc.tint.size())) {
    return false;
  }
  GuestResourceId mesh;
  std::uint32_t indexCount = 0;
  const GuestRecordingBackend::HostMeshState state =
    m_backend.hostMeshState(desc.mesh, &mesh, &indexCount);
  if (state == GuestRecordingBackend::HostMeshState::Missing ||
      state == GuestRecordingBackend::HostMeshState::Unusable ||
      (state == GuestRecordingBackend::HostMeshState::Ready &&
       (desc.firstIndex > indexCount ||
        desc.indexCount > indexCount - desc.firstIndex))) {
    return false;
  }
  Instance instance;
  instance.desc = desc;
  instance.waiting = state == GuestRecordingBackend::HostMeshState::Pending;
  m_instances[id] = instance;
  m_materials[desc.material].users += 1;
  if (instance.waiting) {
    m_waiting.push_back(id);
  } else {
    m_queue.push_back(createOperation(id, desc, mesh));
  }
  return true;
}

bool
GuestRenderWorld::setInstanceTransform(RenderInstanceId id,
                                       const std::array<float, 16>& world)
{
  std::unordered_map<RenderInstanceId, Instance>::iterator found =
    m_instances.find(id);
  if (found == m_instances.end() || !finiteValues(world.data(), world.size())) {
    return false;
  }
  found->second.desc.world = world;
  if (!found->second.waiting) {
    GuestWorldOperation operation;
    operation.op = GuestWorldOp::InstanceTransform;
    operation.id = id;
    operation.transform = world;
    m_queue.push_back(operation);
  }
  return true;
}

bool
GuestRenderWorld::setInstanceTint(RenderInstanceId id,
                                  const std::array<float, 4>& tint)
{
  std::unordered_map<RenderInstanceId, Instance>::iterator found =
    m_instances.find(id);
  if (found == m_instances.end() || !finiteValues(tint.data(), tint.size())) {
    return false;
  }
  found->second.desc.tint = tint;
  if (!found->second.waiting) {
    GuestWorldOperation operation;
    operation.op = GuestWorldOp::InstanceUpdate;
    operation.id = id;
    operation.tint = tint;
    operation.visible = found->second.desc.visible;
    m_queue.push_back(operation);
  }
  return true;
}

bool
GuestRenderWorld::setInstanceVisible(RenderInstanceId id, bool shown)
{
  std::unordered_map<RenderInstanceId, Instance>::iterator found =
    m_instances.find(id);
  if (found == m_instances.end()) {
    return false;
  }
  found->second.desc.visible = shown;
  if (!found->second.waiting) {
    GuestWorldOperation operation;
    operation.op = GuestWorldOp::InstanceUpdate;
    operation.id = id;
    operation.tint = found->second.desc.tint;
    operation.visible = shown;
    m_queue.push_back(operation);
  }
  return true;
}

bool
GuestRenderWorld::destroyInstance(RenderInstanceId id)
{
  std::unordered_map<RenderInstanceId, Instance>::iterator found =
    m_instances.find(id);
  if (found == m_instances.end()) {
    return false;
  }
  m_materials[found->second.desc.material].users -= 1;
  if (found->second.waiting) {
    m_waiting.erase(std::find(m_waiting.begin(), m_waiting.end(), id));
  } else {
    GuestWorldOperation operation;
    operation.op = GuestWorldOp::InstanceDestroy;
    operation.id = id;
    m_queue.push_back(operation);
  }
  m_instances.erase(found);
  return true;
}

void
GuestRenderWorld::setEnvironment(const RenderEnvironment& environment)
{
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::Environment;
  GuestWorldEnvironment& wire = operation.environment;
  wire.lightDirection = environment.lightDirection;
  wire.lightColor = environment.lightColor;
  wire.ambientColor = environment.ambientColor;
  wire.shadowsEnabled = environment.shadowsEnabled;
  wire.shadowPcf = environment.shadowPcf;
  wire.shadowBias = environment.shadowBias;
  wire.shadowSlopeScale = environment.shadowSlopeScale;
  wire.shadowNormalOffset = environment.shadowNormalOffset;
  wire.shadowMapSize =
    static_cast<std::uint32_t>(std::clamp(environment.shadowMapSize, 64, 8192));
  wire.shadowMinimumRadius = environment.shadowMinimumRadius;
  wire.shadowLightDistance = environment.shadowLightDistance;
  wire.shadowCasterDistance = environment.shadowCasterDistance;
  const float scalars[] = {
    wire.shadowBias,          wire.shadowSlopeScale,
    wire.shadowNormalOffset,  wire.shadowMinimumRadius,
    wire.shadowLightDistance, wire.shadowCasterDistance
  };
  if (!finiteValues(wire.lightDirection.data(), 3) ||
      !finiteValues(wire.lightColor.data(), 3) ||
      !finiteValues(wire.ambientColor.data(), 3) || !finiteValues(scalars, 6)) {
    Logger::LogWarning("Render world environment ignored: non-finite value");
    return;
  }
  m_queue.push_back(operation);
}

bool
GuestRenderWorld::setSkybox(const RenderSkyboxDesc& skybox)
{
  if (!finiteValues(skybox.tint.data(), skybox.tint.size())) {
    return false;
  }
  m_sky = m_backend.hostCubemap(skybox.cubemap);
  m_skyTint = skybox.tint;
  m_skyShown = m_sky.owner != 0;
  return true;
}

bool
GuestRenderWorld::createLight(RenderLightId id, const RenderLightDesc& desc)
{
  if (id == 0 || m_lights.contains(id) ||
      m_lights.size() >= kMaximumGuestLights || !validGuestLightDesc(desc)) {
    return false;
  }
  m_lights.insert(id);
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::LightCreate;
  operation.id = id;
  operation.light = wireLight(desc);
  m_queue.push_back(operation);
  return true;
}

bool
GuestRenderWorld::updateLight(RenderLightId id, const RenderLightDesc& desc)
{
  if (!m_lights.contains(id) || !validGuestLightDesc(desc)) {
    return false;
  }
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::LightUpdate;
  operation.id = id;
  operation.light = wireLight(desc);
  m_queue.push_back(operation);
  return true;
}

bool
GuestRenderWorld::destroyLight(RenderLightId id)
{
  std::unordered_set<RenderLightId>::iterator found = m_lights.find(id);
  if (found == m_lights.end()) {
    return false;
  }
  m_lights.erase(found);
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::LightDestroy;
  operation.id = id;
  m_queue.push_back(operation);
  return true;
}

void
GuestRenderWorld::takeOperations(std::vector<GuestWorldOperation>& output,
                                 std::size_t limit)
{
  ILLUMO_PROFILE_ZONE("RenderWorld.takeOperations");
  const std::uint64_t epoch = m_backend.textureEpoch();
  if (epoch != m_checkedTextureEpoch) {
    m_checkedTextureEpoch = epoch;
    for (std::unordered_map<RenderMaterialId, MaterialState>::iterator it =
           m_materials.begin();
         it != m_materials.end();
         ++it) {
      MaterialState& mat = it->second;
      std::array<GuestResourceId, 4> currentIds = { GuestResourceId{},
                                                    GuestResourceId{},
                                                    GuestResourceId{},
                                                    GuestResourceId{} };
      const GuestWorldMaterial wire = buildWireMaterial(mat.desc, &currentIds);
      bool changed = false;
      for (std::size_t slot = 0; slot < 4; ++slot) {
        if (!sameResourceId(currentIds[slot], mat.sentTextures[slot])) {
          changed = true;
          break;
        }
      }
      if (changed) {
        mat.sentTextures = currentIds;
        GuestWorldOperation op;
        op.op = GuestWorldOp::MaterialUpdate;
        op.id = it->first;
        op.material = wire;
        m_queue.push_back(op);
      }
    }
  }
  // The sky change goes first: it never depends on queued operations.
  const GuestResourceId sky = m_skyShown ? m_sky : GuestResourceId{};
  const bool sameSky = sky.owner == m_sentSky.owner &&
                       sky.slot == m_sentSky.slot &&
                       sky.generation == m_sentSky.generation &&
                       (sky.owner == 0 || m_skyTint == m_sentSkyTint);
  if (!sameSky && output.size() < limit) {
    GuestWorldOperation operation;
    operation.op = GuestWorldOp::Skybox;
    operation.texture = sky;
    operation.tint = m_skyTint;
    output.push_back(operation);
    m_sentSky = sky;
    m_sentSkyTint = m_skyTint;
  }
  while (!m_queue.empty() && output.size() < limit) {
    output.push_back(m_queue.front());
    m_queue.pop_front();
  }
  if (!m_queue.empty()) {
    return;
  }
  for (std::vector<RenderInstanceId>::iterator it = m_waiting.begin();
       it != m_waiting.end() && output.size() < limit;) {
    Instance& instance = m_instances.at(*it);
    GuestResourceId mesh;
    std::uint32_t indexCount = 0;
    const GuestRecordingBackend::HostMeshState state =
      m_backend.hostMeshState(instance.desc.mesh, &mesh, &indexCount);
    if (state == GuestRecordingBackend::HostMeshState::Pending) {
      ++it;
      continue;
    }
    const RenderInstanceDesc& desc = instance.desc;
    if (state != GuestRecordingBackend::HostMeshState::Ready ||
        desc.firstIndex > indexCount ||
        desc.indexCount > indexCount - desc.firstIndex) {
      // The mesh was destroyed, replaced or failed while this waited.
      Logger::LogWarning("Render world instance " + std::to_string(*it) +
                         " dropped: its mesh can no longer be used");
      m_materials[desc.material].users -= 1;
      m_instances.erase(*it);
      it = m_waiting.erase(it);
      continue;
    }
    output.push_back(createOperation(*it, desc, mesh));
    instance.waiting = false;
    it = m_waiting.erase(it);
  }
}
