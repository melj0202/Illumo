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

static GuestWorldMaterial
wireMaterial(const RenderMaterialDesc& desc)
{
  GuestWorldMaterial material;
  material.tint = desc.tint;
  material.receivesShadow = desc.receivesShadow;
  material.castsShadow = desc.castsShadow;
  material.blend = desc.blend;
  return material;
}

GuestRenderWorld::GuestRenderWorld(const GuestRecordingBackend& backend)
  : m_backend(backend)
{
}

bool
GuestRenderWorld::createMaterial(RenderMaterialId id,
                                 const RenderMaterialDesc& desc)
{
  if (id == 0 || m_materials.contains(id) ||
      !finiteValues(desc.tint.data(), desc.tint.size())) {
    return false;
  }
  m_materials[id] = 0;
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::MaterialCreate;
  operation.id = id;
  operation.material = wireMaterial(desc);
  m_queue.push_back(operation);
  return true;
}

bool
GuestRenderWorld::updateMaterial(RenderMaterialId id,
                                 const RenderMaterialDesc& desc)
{
  if (!m_materials.contains(id) ||
      !finiteValues(desc.tint.data(), desc.tint.size())) {
    return false;
  }
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::MaterialUpdate;
  operation.id = id;
  operation.material = wireMaterial(desc);
  m_queue.push_back(operation);
  return true;
}

bool
GuestRenderWorld::destroyMaterial(RenderMaterialId id)
{
  std::unordered_map<RenderMaterialId, std::size_t>::iterator found =
    m_materials.find(id);
  if (found == m_materials.end() || found->second != 0) {
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
  m_materials[desc.material] += 1;
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
  m_materials[found->second.desc.material] -= 1;
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

void
GuestRenderWorld::takeOperations(std::vector<GuestWorldOperation>& output,
                                 std::size_t limit)
{
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
      m_materials[desc.material] -= 1;
      m_instances.erase(*it);
      it = m_waiting.erase(it);
      continue;
    }
    output.push_back(createOperation(*it, desc, mesh));
    instance.waiting = false;
    it = m_waiting.erase(it);
  }
}
