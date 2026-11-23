#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <Illumo/Rendering/RenderWorld.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/WorldLook.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

static bool
finiteValues(const float* values, size_t count)
{
  for (size_t index = 0; index < count; ++index) {
    if (!std::isfinite(values[index])) {
      return false;
    }
  }
  return true;
}

static bool
validMaterialDesc(const RenderMaterialDesc& desc)
{
  return finiteValues(desc.tint.data(), desc.tint.size()) &&
         std::isfinite(desc.metallic) && std::isfinite(desc.roughness) &&
         finiteValues(desc.emissiveFactor.data(), desc.emissiveFactor.size());
}

static bool
validLightDesc(const RenderLightDesc& desc)
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
  if (desc.range <= 0.0f || desc.intensity < 0.0f) {
    return false;
  }
  if (desc.kind == RenderLightKind::Spot) {
    const glm::vec3 dir(
      desc.direction[0], desc.direction[1], desc.direction[2]);
    const float len = glm::length(dir);
    if (!std::isfinite(len) || len <= 0.0001f) {
      return false;
    }
    if (desc.innerConeCos < desc.outerConeCos || desc.outerConeCos <= -1.0f ||
        desc.innerConeCos > 1.0f) {
      return false;
    }
  }
  return true;
}

bool
RenderWorld::BucketKey::operator==(const BucketKey& other) const
{
  return meshSlot == other.meshSlot && meshGeneration == other.meshGeneration &&
         firstIndex == other.firstIndex && indexCount == other.indexCount &&
         material == other.material && blendOwner == other.blendOwner;
}

size_t
RenderWorld::BucketKeyHash::operator()(const BucketKey& key) const
{
  const uint32_t fields[] = { key.meshSlot,   key.meshGeneration,
                              key.firstIndex, key.indexCount,
                              key.material,   key.blendOwner };
  size_t hash = static_cast<size_t>(1469598103934665603ull);
  for (uint32_t field : fields) {
    hash = (hash ^ field) * static_cast<size_t>(1099511628211ull);
  }
  return hash;
}

RenderWorld::RenderWorld() = default;

RenderWorld::~RenderWorld() = default;

void
RenderWorld::releaseResources()
{
  if (m_renderer == nullptr) {
    return;
  }
  if (m_skyboxMesh.isValid()) {
    m_renderer->destroyMesh(m_skyboxMesh);
    m_skyboxMesh = MeshHandle{};
  }
  for (std::unique_ptr<Bucket>& bucket : m_buckets) {
    if (bucket->colorBuffer.isValid()) {
      m_renderer->destroyBuffer(bucket->colorBuffer);
    }
    bucket->colorBuffer = BufferHandle{};
    bucket->colorUpload.valid = false;
    for (size_t q = 0; q < Renderer::kShadowAtlasQuadrants; ++q) {
      if (bucket->shadowBuffers[q].isValid()) {
        m_renderer->destroyBuffer(bucket->shadowBuffers[q]);
      }
      bucket->shadowBuffers[q] = BufferHandle{};
      bucket->shadowUploads[q].valid = false;
    }
    bucket->capacity = 0;
    bucket->dirty = true;
  }
}

bool
RenderWorld::createMaterial(RenderMaterialId id, const RenderMaterialDesc& desc)
{
  if (id == 0 || m_materials.contains(id) || !validMaterialDesc(desc)) {
    return false;
  }
  Material material;
  material.desc = desc;
  m_materials[id] = material;
  return true;
}

bool
RenderWorld::updateMaterial(RenderMaterialId id, const RenderMaterialDesc& desc)
{
  std::unordered_map<RenderMaterialId, Material>::iterator found =
    m_materials.find(id);
  if (found == m_materials.end() || !validMaterialDesc(desc)) {
    return false;
  }
  const bool rebucket = found->second.desc.blend != desc.blend;
  found->second.desc = desc;
  if (!rebucket) {
    markMaterialDirty(id);
    return true;
  }
  // Blending changes which instances may share a bucket.
  for (size_t slot = 0; slot < m_instances.size(); ++slot) {
    if (m_instances[slot].alive && m_instances[slot].desc.material == id) {
      removeFromBucket(slot);
      addToBucket(slot);
    }
  }
  return true;
}

bool
RenderWorld::destroyMaterial(RenderMaterialId id)
{
  std::unordered_map<RenderMaterialId, Material>::iterator found =
    m_materials.find(id);
  if (found == m_materials.end() || found->second.users != 0) {
    return false;
  }
  m_materials.erase(found);
  return true;
}

bool
RenderWorld::createInstance(RenderInstanceId id, const RenderInstanceDesc& desc)
{
  std::unordered_map<RenderMaterialId, Material>::iterator material =
    m_materials.find(desc.material);
  if (id == 0 || m_instanceSlots.contains(id) ||
      material == m_materials.end() || !desc.mesh.isValid() ||
      desc.indexCount == 0 ||
      !finiteValues(desc.world.data(), desc.world.size()) ||
      !finiteValues(desc.tint.data(), desc.tint.size()) ||
      (desc.hasBounds && !desc.localBounds.isValid())) {
    return false;
  }
  size_t slot = m_instances.size();
  if (!m_freeInstances.empty()) {
    slot = m_freeInstances.back();
    m_freeInstances.pop_back();
  } else {
    m_instances.emplace_back();
  }
  Instance& instance = m_instances[slot];
  instance = Instance{};
  instance.id = id;
  instance.alive = true;
  instance.desc = desc;
  instance.previousWorld = desc.world;
  computeWorldBounds(instance);
  m_instanceSlots[id] = slot;
  material->second.users += 1;
  addToBucket(slot);
  m_stats.instances += 1;
  return true;
}

bool
RenderWorld::setInstanceTransform(RenderInstanceId id,
                                  const std::array<float, 16>& world)
{
  Instance* instance = findInstance(id);
  if (instance == nullptr || !finiteValues(world.data(), world.size())) {
    return false;
  }
  // The first move before a frame keeps what that frame's previous image
  // showed, for motion vectors.
  if (instance->movedFrame != m_frame + 1) {
    instance->previousWorld = instance->desc.world;
    instance->movedFrame = m_frame + 1;
  }
  instance->desc.world = world;
  computeWorldBounds(*instance);
  touchBucket(*instance);
  m_buckets[instance->bucket]->movedFrame = m_frame + 1;
  return true;
}

bool
RenderWorld::setInstanceTint(RenderInstanceId id,
                             const std::array<float, 4>& tint)
{
  Instance* instance = findInstance(id);
  if (instance == nullptr || !finiteValues(tint.data(), tint.size())) {
    return false;
  }
  instance->desc.tint = tint;
  touchBucket(*instance);
  return true;
}

bool
RenderWorld::setInstanceVisible(RenderInstanceId id, bool shown)
{
  Instance* instance = findInstance(id);
  if (instance == nullptr) {
    return false;
  }
  instance->desc.visible = shown;
  touchBucket(*instance);
  return true;
}

bool
RenderWorld::destroyInstance(RenderInstanceId id)
{
  std::unordered_map<RenderInstanceId, size_t>::iterator found =
    m_instanceSlots.find(id);
  if (found == m_instanceSlots.end()) {
    return false;
  }
  const size_t slot = found->second;
  removeFromBucket(slot);
  m_materials.at(m_instances[slot].desc.material).users -= 1;
  m_instances[slot].alive = false;
  m_instanceSlots.erase(found);
  m_freeInstances.push_back(slot);
  m_stats.instances -= 1;
  return true;
}

void
RenderWorld::setEnvironment(const RenderEnvironment& environment)
{
  m_environment = environment;
  for (std::unique_ptr<Bucket>& bucket : m_buckets) {
    bucket->dirty = true;
  }
}

RenderEnvironment
RenderWorld::effectiveEnvironment(const Renderer* renderer) const
{
  RenderEnvironment resolved = m_environment;
  if (renderer != nullptr) {
    const Renderer::ShadowDebugOverrides& overrides =
      renderer->getShadowDebugOverrides();
    if (overrides.active) {
      resolved.shadowsEnabled = overrides.shadowsEnabled;
      resolved.shadowPcf = overrides.shadowPcf;
      resolved.shadowMapSize = std::max(overrides.shadowMapSize, 64);
      resolved.shadowBias = overrides.shadowBias;
      resolved.shadowSlopeScale = overrides.shadowSlopeScale;
      resolved.shadowNormalOffset = overrides.shadowNormalOffset;
    }
  }
  return resolved;
}

bool
RenderWorld::createLight(RenderLightId id, const RenderLightDesc& desc)
{
  if (id == 0 || m_lights.contains(id) || !validLightDesc(desc)) {
    return false;
  }
  m_lights[id] = desc;
  m_stats.lights += 1;
  return true;
}

bool
RenderWorld::updateLight(RenderLightId id, const RenderLightDesc& desc)
{
  std::unordered_map<RenderLightId, RenderLightDesc>::iterator found =
    m_lights.find(id);
  if (found == m_lights.end() || !validLightDesc(desc)) {
    return false;
  }
  found->second = desc;
  return true;
}

bool
RenderWorld::destroyLight(RenderLightId id)
{
  std::unordered_map<RenderLightId, RenderLightDesc>::iterator found =
    m_lights.find(id);
  if (found == m_lights.end()) {
    return false;
  }
  m_lights.erase(found);
  m_stats.lights -= 1;
  return true;
}

bool
RenderWorld::betterLightCandidate(const LightCandidate& left,
                                  const LightCandidate& right)
{
  if (left.score != right.score) {
    return left.score > right.score;
  }
  return left.id < right.id;
}

void
RenderWorld::gatherLights(Renderer* renderer)
{
  m_stats.activeLights = 0;
  if (m_lights.empty()) {
    renderer->setFramePunctualLights(nullptr, 0, nullptr);
    return;
  }
  m_lightCandidates.clear();
  const std::array<float, 3> camPos =
    renderer->getWorldCameraReferencePosition();
  const glm::vec3 eye(camPos[0], camPos[1], camPos[2]);
  for (std::unordered_map<RenderLightId, RenderLightDesc>::const_iterator it =
         m_lights.begin();
       it != m_lights.end();
       ++it) {
    const RenderLightDesc& light = it->second;
    if (!light.visible || light.intensity <= 0.0f) {
      continue;
    }
    const glm::vec3 center(
      light.position[0], light.position[1], light.position[2]);
    const glm::vec3 extent(light.range);
    const AxisAlignedBounds3 bounds{ center - extent, center + extent };
    if (!renderer->isWorldBoundsVisible(bounds)) {
      continue;
    }
    const glm::vec3 diff = center - eye;
    const float distSq = glm::dot(diff, diff);
    LightCandidate candidate;
    candidate.id = it->first;
    candidate.score =
      (light.intensity * light.range * light.range) / std::max(distSq, 1.0f);
    candidate.desc = light;
    m_lightCandidates.push_back(candidate);
  }
  const size_t selected =
    std::min(m_lightCandidates.size(), Renderer::kMaximumForwardLights);
  if (m_lightCandidates.size() > Renderer::kMaximumForwardLights) {
    std::partial_sort(m_lightCandidates.begin(),
                      m_lightCandidates.begin() +
                        static_cast<std::ptrdiff_t>(selected),
                      m_lightCandidates.end(),
                      betterLightCandidate);
  } else {
    std::sort(
      m_lightCandidates.begin(), m_lightCandidates.end(), betterLightCandidate);
  }
  const bool shadowsAllowed = effectiveEnvironment(renderer).shadowsEnabled;
  Renderer::SpotShadowLightDesc spotShadow{};
  bool hasSpotShadow = false;
  for (size_t index = 0; index < selected; ++index) {
    const RenderLightDesc& src = m_lightCandidates[index].desc;
    Renderer::GpuPunctualLight& dst = m_packedLights[index];
    const float safeRange = std::max(src.range, 0.0001f);
    dst.positionAndInvRange = {
      src.position[0], src.position[1], src.position[2], 1.0f / safeRange
    };
    const float r = std::max(src.color[0], 0.0f) * src.intensity;
    const float g = std::max(src.color[1], 0.0f) * src.intensity;
    const float b = std::max(src.color[2], 0.0f) * src.intensity;
    if (src.kind == RenderLightKind::Spot) {
      const glm::vec3 rawDir(
        src.direction[0], src.direction[1], src.direction[2]);
      const float dirLen = glm::length(rawDir);
      const glm::vec3 normDir =
        dirLen > 0.0001f ? (rawDir / dirLen) : glm::vec3(0.0f, -1.0f, 0.0f);
      const float angleScale =
        1.0f / std::max(src.innerConeCos - src.outerConeCos, 0.0001f);
      dst.radianceAndOuterCos = { r, g, b, src.outerConeCos };
      dst.directionAndAngleScale = {
        normDir.x, normDir.y, normDir.z, angleScale
      };
      if (!hasSpotShadow && src.castsShadow && shadowsAllowed) {
        spotShadow.position = src.position;
        spotShadow.direction = { normDir.x, normDir.y, normDir.z };
        spotShadow.range = safeRange;
        spotShadow.outerConeCos = src.outerConeCos;
        spotShadow.lightIndex = static_cast<int>(index);
        hasSpotShadow = true;
      }
    } else {
      dst.radianceAndOuterCos = { r, g, b, -2.0f };
      dst.directionAndAngleScale = { 0.0f, -1.0f, 0.0f, 1.0f };
    }
  }
  m_stats.activeLights = selected;
  renderer->setFramePunctualLights(selected > 0 ? m_packedLights.data()
                                                : nullptr,
                                   selected,
                                   hasSpotShadow ? &spotShadow : nullptr);
}

RenderWorld::Instance*
RenderWorld::findInstance(RenderInstanceId id)
{
  std::unordered_map<RenderInstanceId, size_t>::iterator found =
    m_instanceSlots.find(id);
  return found == m_instanceSlots.end() ? nullptr : &m_instances[found->second];
}

void
RenderWorld::addToBucket(size_t slot)
{
  Instance& instance = m_instances[slot];
  const Material& material = m_materials.at(instance.desc.material);
  BucketKey key;
  key.meshSlot = instance.desc.mesh.slot;
  key.meshGeneration = instance.desc.mesh.generation;
  key.firstIndex = instance.desc.firstIndex;
  key.indexCount = instance.desc.indexCount;
  key.material = instance.desc.material;
  key.blendOwner = material.desc.blend ? instance.id : 0;
  size_t index = 0;
  std::unordered_map<BucketKey, size_t, BucketKeyHash>::iterator found =
    m_bucketIndex.find(key);
  if (found != m_bucketIndex.end()) {
    index = found->second;
  } else {
    if (!m_freeBuckets.empty()) {
      index = m_freeBuckets.back();
      m_freeBuckets.pop_back();
    } else {
      index = m_buckets.size();
      m_buckets.push_back(std::make_unique<Bucket>());
    }
    Bucket& bucket = *m_buckets[index];
    bucket.key = key;
    bucket.inUse = true;
    bucket.members.clear();
    bucket.dirty = true;
    bucket.colorUpload.valid = false;
    for (size_t q = 0; q < Renderer::kShadowAtlasQuadrants; ++q) {
      bucket.shadowUploads[q].valid = false;
    }
    m_bucketIndex[key] = index;
    if (key.blendOwner != 0) {
      m_blendOrder.push_back(index);
    }
    m_stats.buckets += 1;
  }
  Bucket& bucket = *m_buckets[index];
  instance.bucket = index;
  instance.bucketPosition = bucket.members.size();
  bucket.members.push_back(slot);
  bucket.revision += 1;
  if (instance.movedFrame > bucket.movedFrame) {
    bucket.movedFrame = instance.movedFrame;
  }
}

void
RenderWorld::touchBucket(const Instance& instance)
{
  m_buckets[instance.bucket]->revision += 1;
}

void
RenderWorld::removeFromBucket(size_t slot)
{
  Instance& instance = m_instances[slot];
  Bucket& bucket = *m_buckets[instance.bucket];
  const size_t last = bucket.members.back();
  bucket.members[instance.bucketPosition] = last;
  m_instances[last].bucketPosition = instance.bucketPosition;
  bucket.members.pop_back();
  bucket.revision += 1;
  if (!bucket.members.empty()) {
    return;
  }
  // An empty bucket keeps its buffers for reuse by the next new key.
  m_bucketIndex.erase(bucket.key);
  bucket.inUse = false;
  m_freeBuckets.push_back(instance.bucket);
  std::vector<size_t>::iterator ordered =
    std::find(m_blendOrder.begin(), m_blendOrder.end(), instance.bucket);
  if (ordered != m_blendOrder.end()) {
    m_blendOrder.erase(ordered);
  }
  m_stats.buckets -= 1;
}

void
RenderWorld::markMaterialDirty(RenderMaterialId id)
{
  for (std::unique_ptr<Bucket>& bucket : m_buckets) {
    if (bucket->inUse && bucket->key.material == id) {
      bucket->dirty = true;
    }
  }
}

bool
RenderWorld::computeWorldBounds(Instance& instance)
{
  instance.hasWorldBounds =
    instance.desc.hasBounds &&
    instance.desc.localBounds.transformed(
      glm::make_mat4(instance.desc.world.data()), &instance.worldBounds);
  return instance.hasWorldBounds;
}

bool
RenderWorld::prepareFrame(Renderer* renderer)
{
  const uint64_t serial = renderer->getFrameContext().frameSerial;
  if (m_frame != 0 && serial == m_preparedSerial) {
    return m_ready;
  }
  ILLUMO_PROFILE_ZONE("RenderWorld.prepareFrame");
  m_preparedSerial = serial;
  m_frame += 1;
  m_renderer = renderer;
  const uint64_t overrideRevision =
    renderer->getShadowDebugOverrides().revision;
  if (overrideRevision != m_shadowOverrideRevision) {
    m_shadowOverrideRevision = overrideRevision;
    for (std::unique_ptr<Bucket>& bucket : m_buckets) {
      bucket->dirty = true;
    }
  }
  m_stats.drawnInstances = 0;
  m_stats.shadowInstances = 0;
  m_stats.uploadedInstances = 0;
  renderer->ensureBuiltinStyles();
  m_ready = renderer->getStyle(RenderStyleId::LitMeshInstanced) != nullptr &&
            renderer->getStyle(RenderStyleId::ShadowDepthInstanced) != nullptr;
  if (m_ready) {
    renderer->ensureLightUniformBuffer();
    renderer->whiteTexture();
    renderer->flatNormalTexture();
    renderer->defaultOrmTexture();
    gatherLights(renderer);
  }
  for (std::unique_ptr<Bucket>& bucket : m_buckets) {
    if (!m_ready || !bucket->inUse) {
      continue;
    }
    if (!ensureBuffers(renderer, *bucket)) {
      m_ready = false;
      break;
    }
    if (bucket->dirty) {
      recordColor(renderer, *bucket);
      recordShadow(renderer, *bucket);
      bucket->dirty = false;
    }
  }
  if (!m_ready && !m_unsupportedReported) {
    m_unsupportedReported = true;
    Logger::LogWarning("RenderWorld: the renderer has no instanced styles or "
                       "instance buffers; the world is not drawn");
  }
  return m_ready;
}

bool
RenderWorld::ensureBuffers(Renderer* renderer, Bucket& bucket)
{
  const size_t needed = std::max<size_t>(bucket.members.size(), 1);
  const bool shadows = effectiveEnvironment(renderer).shadowsEnabled;
  bool allShadowsValid = true;
  if (shadows) {
    for (size_t q = 0; q < Renderer::kShadowAtlasQuadrants; ++q) {
      allShadowsValid = allShadowsValid && bucket.shadowBuffers[q].isValid();
    }
  }
  if (bucket.capacity >= needed && bucket.colorBuffer.isValid() &&
      allShadowsValid) {
    return true;
  }
  size_t capacity = std::max<size_t>(bucket.capacity, 16);
  while (capacity < needed) {
    capacity *= 2;
  }
  if (capacity != bucket.capacity) {
    if (bucket.colorBuffer.isValid()) {
      renderer->destroyBuffer(bucket.colorBuffer);
    }
    bucket.colorBuffer = BufferHandle{};
    for (size_t q = 0; q < Renderer::kShadowAtlasQuadrants; ++q) {
      if (bucket.shadowBuffers[q].isValid()) {
        renderer->destroyBuffer(bucket.shadowBuffers[q]);
      }
      bucket.shadowBuffers[q] = BufferHandle{};
    }
    bucket.capacity = capacity;
  }
  const size_t bytes = capacity * sizeof(GpuInstance);
  if (!bucket.colorBuffer.isValid()) {
    bucket.colorBuffer = renderer->enrollBuffer(BufferUsage::Instance, bytes);
  }
  allShadowsValid = true;
  if (shadows) {
    for (size_t q = 0; q < Renderer::kShadowAtlasQuadrants; ++q) {
      if (!bucket.shadowBuffers[q].isValid()) {
        bucket.shadowBuffers[q] =
          renderer->enrollBuffer(BufferUsage::Instance, bytes);
      }
      allShadowsValid = allShadowsValid && bucket.shadowBuffers[q].isValid();
    }
  }
  bucket.colorStaging.reserve(capacity);
  for (size_t q = 0; q < Renderer::kShadowAtlasQuadrants; ++q) {
    bucket.shadowStaging[q].reserve(capacity);
    bucket.shadowUploads[q].valid = false;
  }
  // New buffer handles live in the recorded lists, and hold nothing yet.
  bucket.dirty = true;
  bucket.colorUpload.valid = false;
  return bucket.colorBuffer.isValid() && allShadowsValid;
}

void
RenderWorld::recordColor(Renderer* renderer, Bucket& bucket)
{
  const RenderMaterialDesc& material = m_materials.at(bucket.key.material).desc;
  const RenderEnvironment environment = effectiveEnvironment(renderer);
  bucket.color.clear();
  renderer->beginRecording(&bucket.color);
  renderer->bindStyle(RenderStyleId::LitMeshInstanced);
  if (material.blend) {
    PipelineState state =
      renderer->getStyle(RenderStyleId::LitMeshInstanced)->pipeline;
    state.blendEnabled = true;
    renderer->pushPipelineState(state);
  }
  renderer->pushUniformVec3(WorldLook::kLightDirUniform,
                            environment.lightDirection[0],
                            environment.lightDirection[1],
                            environment.lightDirection[2]);
  renderer->pushUniformVec3(WorldLook::kLightColorUniform,
                            environment.lightColor[0],
                            environment.lightColor[1],
                            environment.lightColor[2]);
  renderer->pushUniformVec3(WorldLook::kAmbientColorUniform,
                            environment.ambientColor[0],
                            environment.ambientColor[1],
                            environment.ambientColor[2]);
  renderer->pushUniformInt(
    WorldLook::kShadowsEnabledUniform,
    environment.shadowsEnabled && material.receivesShadow ? 1 : 0);
  renderer->pushUniformFloat(WorldLook::kShadowBiasUniform,
                             environment.shadowBias);
  renderer->pushUniformFloat(WorldLook::kShadowSlopeScaleUniform,
                             environment.shadowSlopeScale);
  renderer->pushUniformFloat(WorldLook::kShadowNormalOffsetUniform,
                             environment.shadowNormalOffset);
  renderer->pushUniformInt(WorldLook::kShadowPcfUniform,
                           environment.shadowPcf ? 1 : 0);
  renderer->pushUniformInt(WorldLook::kMotionBlurEnabledUniform, 1);
  renderer->pushUniformVec4(WorldLook::kTintUniform,
                            material.tint[0],
                            material.tint[1],
                            material.tint[2],
                            material.tint[3]);
  renderer->pushUniformInt(WorldLook::kShadowMapUniform,
                           WorldLook::kShadowTextureUnit);
  const TextureHandle white = renderer->whiteTexture();
  const TextureHandle flatNormal = renderer->flatNormalTexture();
  const TextureHandle defaultOrm = renderer->defaultOrmTexture();
  const TextureHandle albedoTex =
    material.albedoTexture.isValid() ? material.albedoTexture : white;
  const TextureHandle normalTex =
    material.normalTexture.isValid() ? material.normalTexture : flatNormal;
  const TextureHandle ormTex = material.metallicRoughnessTexture.isValid()
                                 ? material.metallicRoughnessTexture
                                 : defaultOrm;
  const TextureHandle emissiveTex =
    material.emissiveTexture.isValid() ? material.emissiveTexture : white;
  const bool texturesBound = albedoTex.isValid() && normalTex.isValid() &&
                             ormTex.isValid() && emissiveTex.isValid();
  if (texturesBound) {
    renderer->pushSetTexture(
      albedoTex, static_cast<unsigned int>(WorldLook::kAlbedoTextureUnit));
    renderer->pushUniformInt(WorldLook::kAlbedoMapUniform,
                             WorldLook::kAlbedoTextureUnit);
    renderer->pushSetTexture(
      normalTex, static_cast<unsigned int>(WorldLook::kNormalTextureUnit));
    renderer->pushUniformInt(WorldLook::kNormalMapUniform,
                             WorldLook::kNormalTextureUnit);
    renderer->pushSetTexture(
      ormTex, static_cast<unsigned int>(WorldLook::kOrmTextureUnit));
    renderer->pushUniformInt(WorldLook::kOrmMapUniform,
                             WorldLook::kOrmTextureUnit);
    renderer->pushSetTexture(
      emissiveTex, static_cast<unsigned int>(WorldLook::kEmissiveTextureUnit));
    renderer->pushUniformInt(WorldLook::kEmissiveMapUniform,
                             WorldLook::kEmissiveTextureUnit);
  }
  renderer->pushUniformVec4(WorldLook::kMaterialFactorsUniform,
                            std::clamp(material.metallic, 0.0f, 1.0f),
                            std::clamp(material.roughness, 0.045f, 1.0f),
                            material.normalTexture.isValid() ? 1.0f : 0.0f,
                            texturesBound ? 1.0f : 0.0f);
  renderer->pushUniformVec3(WorldLook::kEmissiveFactorUniform,
                            std::max(material.emissiveFactor[0], 0.0f),
                            std::max(material.emissiveFactor[1], 0.0f),
                            std::max(material.emissiveFactor[2], 0.0f));
  renderer->pushUniformInt(WorldLook::kDitherAlphaUniform,
                           material.blend ? 0 : 1);
  renderer->pushSetMesh(
    MeshHandle{ bucket.key.meshSlot, bucket.key.meshGeneration });
  renderer->pushInstanceStream(
    bucket.colorBuffer, 0, InstanceLayout::LitModelTint);
  renderer->pushDrawIndexedInstanced(
    bucket.key.indexCount, bucket.key.firstIndex, 0);
  bucket.colorDraw = bucket.color.size() - 1;
  renderer->endRecording();
  m_stats.recordings += 1;
}

void
RenderWorld::recordShadow(Renderer* renderer, Bucket& bucket)
{
  for (size_t q = 0; q < Renderer::kShadowAtlasQuadrants; ++q) {
    bucket.shadow[q].clear();
  }
  if (!bucket.shadowBuffers[0].isValid()) {
    return;
  }
  for (size_t q = 0; q < Renderer::kShadowAtlasQuadrants; ++q) {
    if (!bucket.shadowBuffers[q].isValid()) {
      continue;
    }
    renderer->beginRecording(&bucket.shadow[q]);
    renderer->pushSetMesh(
      MeshHandle{ bucket.key.meshSlot, bucket.key.meshGeneration });
    renderer->pushInstanceStream(
      bucket.shadowBuffers[q], 0, InstanceLayout::LitModelTint);
    renderer->pushDrawIndexedInstanced(
      bucket.key.indexCount, bucket.key.firstIndex, 0);
    bucket.shadowDraw = bucket.shadow[q].size() - 1;
    renderer->endRecording();
  }
  m_stats.recordings += 1;
}

void
RenderWorld::fillRecord(const Instance& instance, GpuInstance& record) const
{
  record.model = instance.desc.world;
  record.previousModel = instance.movedFrame == m_frame ? instance.previousWorld
                                                        : instance.desc.world;
  record.tint = instance.desc.tint;
}

template<typename Visible>
size_t
RenderWorld::streamPass(Renderer* renderer,
                        Bucket& bucket,
                        PassUpload& upload,
                        std::vector<GpuInstance>& staging,
                        BufferHandle buffer,
                        uint64_t cullRevision,
                        const Visible& visible)
{
  // Buffers keep their contents until written. The held records still match
  // when no member changed since and no member's move targeted the writing
  // frame (its record then carried the previous transform for motion vectors,
  // which it no longer does); culling is unchanged as well when the cull
  // revision holds.
  const bool recordsCurrent = upload.valid &&
                              upload.revision == bucket.revision &&
                              bucket.movedFrame != upload.frame;
  if (recordsCurrent && upload.cullRevision == cullRevision) {
    return upload.slots.size();
  }
  m_visible.clear();
  for (size_t slot : bucket.members) {
    const Instance& instance = m_instances[slot];
    if (instance.desc.visible &&
        (!instance.hasWorldBounds || visible(instance))) {
      m_visible.push_back(slot);
    }
  }
  if (m_visible.empty()) {
    return 0;
  }
  if (!recordsCurrent || upload.slots != m_visible) {
    staging.clear();
    for (size_t slot : m_visible) {
      fillRecord(m_instances[slot], staging.emplace_back());
    }
    renderer->pushWriteBuffer(
      buffer,
      0,
      static_cast<unsigned int>(staging.size() * sizeof(GpuInstance)),
      staging.data());
    upload.valid = true;
    upload.frame = m_frame;
    upload.revision = bucket.revision;
    upload.slots.assign(m_visible.begin(), m_visible.end());
    m_stats.uploadedInstances += staging.size();
  }
  upload.cullRevision = cullRevision;
  return m_visible.size();
}

void
RenderWorld::CollectShadowCasters(Renderer* renderer)
{
  if (!isVisible() || renderer == nullptr) {
    return;
  }
  const RenderEnvironment environment = effectiveEnvironment(renderer);
  if ((!m_lights.empty() || environment.shadowsEnabled) &&
      !prepareFrame(renderer)) {
    return;
  }
  if (!environment.shadowsEnabled) {
    return;
  }
  ILLUMO_PROFILE_ZONE("RenderWorld.CollectShadowCasters");
  Renderer::ShadowCasterDesc caster;
  caster.lightDirection = environment.lightDirection;
  caster.mapSize = environment.shadowMapSize;
  caster.minimumRadius = environment.shadowMinimumRadius;
  caster.lightDistance = environment.shadowLightDistance;
  caster.casterDistance = environment.shadowCasterDistance;
  for (std::unique_ptr<Bucket>& bucket : m_buckets) {
    if (!bucket->inUse ||
        !m_materials.at(bucket->key.material).desc.castsShadow) {
      continue;
    }
    for (size_t slot : bucket->members) {
      const Instance& instance = m_instances[slot];
      if (!instance.desc.visible || !instance.hasWorldBounds) {
        continue;
      }
      const AxisAlignedBounds3& bounds = instance.worldBounds;
      caster.boundsMin = { bounds.minimum.x,
                           bounds.minimum.y,
                           bounds.minimum.z };
      caster.boundsMax = { bounds.maximum.x,
                           bounds.maximum.y,
                           bounds.maximum.z };
      renderer->registerShadowCaster(caster);
    }
  }
}

void
RenderWorld::AppendShadowCommands(Renderer* renderer)
{
  if (!isVisible() || renderer == nullptr ||
      !effectiveEnvironment(renderer).shadowsEnabled ||
      !prepareFrame(renderer) || !renderer->getShadowFrameContext().active) {
    return;
  }
  const size_t quadrant = renderer->getActiveShadowQuadrant();
  if (quadrant >= Renderer::kShadowAtlasQuadrants) {
    return;
  }
  ILLUMO_PROFILE_ZONE("RenderWorld.AppendShadowCommands");
  bool bound = false;
  for (std::unique_ptr<Bucket>& bucket : m_buckets) {
    if (!bucket->inUse || bucket->dirty ||
        bucket->shadow[quadrant].size() == 0 ||
        !bucket->shadowBuffers[quadrant].isValid() ||
        !m_materials.at(bucket->key.material).desc.castsShadow) {
      continue;
    }
    const unsigned int count = static_cast<unsigned int>(
      streamPass(renderer,
                 *bucket,
                 bucket->shadowUploads[quadrant],
                 bucket->shadowStaging[quadrant],
                 bucket->shadowBuffers[quadrant],
                 renderer->getShadowQuadrantCullRevision(quadrant),
                 [renderer, quadrant](const Instance& instance) {
                   return renderer->isShadowQuadrantRelevant(
                     quadrant, instance.worldBounds);
                 }));
    if (count == 0) {
      continue;
    }
    if (!bound) {
      if (!renderer->useFrameUniforms() || !renderer->useLightUniforms() ||
          !renderer->bindStyle(RenderStyleId::ShadowDepthInstanced)) {
        return;
      }
      renderer->pushUniformInt(WorldLook::kShadowQuadrantUniform,
                               static_cast<int>(quadrant));
      bound = true;
    }
    bucket->shadow[quadrant]
      .at(bucket->shadowDraw)
      .drawIndexedInstanced.instanceCount = count;
    renderer->pushExecuteList(&bucket->shadow[quadrant]);
    m_stats.shadowInstances += count;
  }
  // Drawables after this one expect the shared shadow style still bound.
  if (bound) {
    renderer->bindStyle(RenderStyleId::ShadowDepth);
  }
}

void
RenderWorld::drawColor(Renderer* renderer, Bucket& bucket, bool& bound)
{
  // A bucket created after this frame was prepared draws from the next one.
  if (bucket.dirty || bucket.color.size() == 0) {
    return;
  }
  const unsigned int count = static_cast<unsigned int>(
    streamPass(renderer,
               bucket,
               bucket.colorUpload,
               bucket.colorStaging,
               bucket.colorBuffer,
               renderer->getCullRevision(),
               [renderer](const Instance& instance) {
                 return renderer->isWorldBoundsVisible(instance.worldBounds);
               }));
  if (count == 0) {
    return;
  }
  if (!bound) {
    if (!renderer->useFrameUniforms() || !renderer->useLightUniforms()) {
      return;
    }
    const Renderer::ShadowFrameContext& shadow =
      renderer->getShadowFrameContext();
    if (shadow.active && shadow.depthTexture.isValid()) {
      renderer->pushSetTexture(shadow.depthTexture,
                               WorldLook::kShadowTextureUnit);
    }
    bound = true;
  }
  bucket.color.at(bucket.colorDraw).drawIndexedInstanced.instanceCount = count;
  renderer->pushExecuteList(&bucket.color);
  m_stats.drawnInstances += count;
}

bool
RenderWorld::setSkybox(const RenderSkyboxDesc& skybox)
{
  if (!finiteValues(skybox.tint.data(), skybox.tint.size())) {
    return false;
  }
  m_skybox = skybox;
  return true;
}

// SkyboxVisual draws projection * rotation-only view. With only the frame's
// view projection VP = P * R * T(-eye), the same matrix is VP * T(eye), and
// the camera position is where every clip-space ray starts: the preimage of
// (0, 0, 1, 0).
void
RenderWorld::drawSkybox(Renderer* renderer)
{
  const Renderer::FrameContext& frame = renderer->getFrameContext();
  if (!m_skybox.cubemap.isValid() || !frame.hasWorldMvp) {
    return;
  }
  if (!m_skyboxMesh.isValid()) {
    const std::array<float, 24>& vertices = SkyboxVisual::cubeVertices();
    const std::array<unsigned int, 36>& indices = SkyboxVisual::cubeIndices();
    m_skyboxMesh = renderer->enrollMesh(vertices.data(),
                                        sizeof(vertices),
                                        indices.data(),
                                        sizeof(indices),
                                        MeshVertexLayout::Pos3,
                                        false);
    m_renderer = renderer;
    if (!m_skyboxMesh.isValid()) {
      return;
    }
  }
  const glm::mat4 viewProjection = glm::make_mat4(frame.worldMvp.data());
  glm::mat4 skyboxMvp = viewProjection;
  const glm::vec4 eye = glm::inverse(viewProjection) * glm::vec4(0, 0, 1, 0);
  // An orthographic camera has no finite eye; its sky keeps the translation.
  if (std::abs(eye.w) > 1e-12f) {
    skyboxMvp =
      viewProjection * glm::translate(glm::mat4(1.0f), glm::vec3(eye) / eye.w);
  }
  if (!renderer->bindStyle(RenderStyleId::Skybox)) {
    return;
  }
  (void)renderer->useLightUniforms();
  const std::array<float, 4>& tint = m_skybox.tint;
  renderer->pushSetTexture(m_skybox.cubemap, 0);
  renderer->pushUniformInt("uSkybox", 0);
  renderer->pushUniformVec4("uTint", tint[0], tint[1], tint[2], tint[3]);
  renderer->pushUniformMat4("uViewProjection", glm::value_ptr(skyboxMvp));
  renderer->pushSetMesh(m_skyboxMesh);
  renderer->pushDrawIndexed(36, 0);
}

bool
RenderWorld::AppendCommands(Renderer* renderer)
{
  if (!isVisible() || renderer == nullptr) {
    return true;
  }
  ILLUMO_PROFILE_ZONE("RenderWorld.AppendCommands");
  drawSkybox(renderer);
  if (!prepareFrame(renderer)) {
    return true;
  }
  bool bound = false;
  for (std::unique_ptr<Bucket>& bucket : m_buckets) {
    if (bucket->inUse && bucket->key.blendOwner == 0) {
      drawColor(renderer, *bucket, bound);
    }
  }
  for (size_t index : m_blendOrder) {
    drawColor(renderer, *m_buckets[index], bound);
  }
  ILLUMO_PROFILE_PLOT("RenderWorld.Instances", m_stats.instances);
  ILLUMO_PROFILE_PLOT("RenderWorld.Buckets", m_stats.buckets);
  ILLUMO_PROFILE_PLOT("RenderWorld.DrawnInstances", m_stats.drawnInstances);
  ILLUMO_PROFILE_PLOT("RenderWorld.ShadowInstances", m_stats.shadowInstances);
  ILLUMO_PROFILE_PLOT("RenderWorld.Recordings", m_stats.recordings);
  return true;
}
