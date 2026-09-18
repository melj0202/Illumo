#include "EditorDocument.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

struct EditorDocument::RuntimeNode : ISceneRenderAttachment
{
  SceneNodeHandle handle;
  AxisAlignedBounds3 bounds;
  uint64_t revision = 1;
  bool getSceneLocalBounds(AxisAlignedBounds3* output) const override
  {
    *output = bounds;
    return bounds.isValid();
  }
  uint64_t getSceneBoundsRevision() const override { return revision; }
  void appendSceneCommands(Renderer*, const Matrix4&) override {}
};

EditorDocument::~EditorDocument()
{
  m_graph.clear();
}

SceneNodeHandle
EditorDocument::nodeHandle(const std::string& id) const
{
  return id.empty() ? SceneNodeHandle{} : m_graph.findByName(id);
}

void
EditorDocument::updateProxy(size_t index)
{
  const IlscNode& node = m_document.nodes[index];
  Vector3 half(0.2f);
  if (IlscCodec::kindHasGeometry(node.kind)) {
    half = node.primitive.extent;
    if (node.kind == SceneNodeKind::WireSphere) {
      half = Vector3(std::max(half.x, std::max(half.y, half.z)));
    } else if (node.kind == SceneNodeKind::FilledEllipse) {
      half = Vector3(half.x);
    } else if (node.kind == SceneNodeKind::FilledRect) {
      half.z = 0.02f;
    }
  }
  m_runtime[index]->bounds = AxisAlignedBounds3{ -half, half };
  ++m_runtime[index]->revision;
}

void
EditorDocument::rebuildRuntime()
{
  m_graph.clear();
  m_runtime.clear();
  m_runtime.reserve(m_document.nodes.size());
  // Resolve ids in two linear passes. This temporary map is discarded after
  // loading; graph names/userData own live runtime identity.
  std::unordered_map<std::string, size_t> indices;
  indices.reserve(m_document.nodes.size());
  for (size_t i = 0; i < m_document.nodes.size(); ++i) {
    const IlscNode& node = m_document.nodes[i];
    std::unique_ptr<RuntimeNode> runtime = std::make_unique<RuntimeNode>();
    SceneNodeDesc description;
    description.name = node.id;
    description.transform = node.transform;
    description.userData = i;
    description.enabled = node.enabled;
    description.visible = node.visible;
    runtime->handle = m_graph.createNode(description);
    m_runtime.push_back(std::move(runtime));
    updateProxy(i);
    m_graph.addAttachment(m_runtime[i]->handle, m_runtime[i].get());
    indices.emplace(node.id, i);
  }
  for (size_t i = 0; i < m_document.nodes.size(); ++i) {
    const std::string& parent = m_document.nodes[i].parentId;
    if (!parent.empty()) {
      m_graph.setParent(m_runtime[i]->handle,
                        m_runtime[indices.at(parent)]->handle);
    }
  }
}

EditorDocument::EditorDocument()
  : m_dirty(false)
  , m_nextId(1)
{
  clear();
}

void
EditorDocument::clear()
{
  m_graph.clear();
  m_runtime.clear();
  m_document = IlscDocument{};
  m_document.camera.zoom = 32.0f;
  m_path.clear();
  m_dirty = false;
  m_nextId = 1;
}

void
EditorDocument::setCamera(const IlscCameraState& camera)
{
  m_document.camera = camera;
  m_dirty = true;
}

void
EditorDocument::setWorldMode(IlscWorldMode mode)
{
  if (m_document.worldMode == mode) {
    return;
  }
  m_document.worldMode = mode;
  for (const std::unique_ptr<RuntimeNode>& runtime : m_runtime) {
    m_graph.notifyAttachmentChanged(runtime->handle);
  }
  m_dirty = true;
}

const IlscNode*
EditorDocument::nodeAt(size_t index) const
{
  if (index >= m_document.nodes.size()) {
    return nullptr;
  }
  return &m_document.nodes[index];
}

const IlscNode*
EditorDocument::findNode(const std::string& id) const
{
  const size_t index = indexOf(id);
  if (index >= m_document.nodes.size()) {
    return nullptr;
  }
  return &m_document.nodes[index];
}

IlscNode*
EditorDocument::mutableNode(const std::string& id)
{
  const size_t index = indexOf(id);
  if (index >= m_document.nodes.size()) {
    return nullptr;
  }
  return &m_document.nodes[index];
}

size_t
EditorDocument::indexOf(const std::string& id) const
{
  uint64_t index = 0;
  const SceneNodeHandle handle = nodeHandle(id);
  return m_graph.getUserData(handle, &index) && index < m_document.nodes.size()
           ? static_cast<size_t>(index)
           : m_document.nodes.size();
}

std::string
EditorDocument::allocateId()
{
  const unsigned int first = m_nextId;
  do {
    const std::string candidate = "n" + std::to_string(m_nextId);
    m_nextId =
      m_nextId == std::numeric_limits<unsigned int>::max() ? 1u : m_nextId + 1u;
    if (findNode(candidate) == nullptr) {
      return candidate;
    }
  } while (m_nextId != first);
  return {};
}

bool
EditorDocument::loadFromText(const std::string& text, std::string* error)
{
  IlscDocument loaded;
  if (!IlscCodec::parse(text, &loaded, error)) {
    return false;
  }
  m_document = std::move(loaded);
  rebuildRuntime();
  m_nextId = 1;
  m_dirty = false;
  return true;
}

bool
EditorDocument::loadFromFile(const std::string& path, std::string* error)
{
  IlscDocument loaded;
  if (!IlscCodec::readFile(path, &loaded, error)) {
    return false;
  }
  m_document = std::move(loaded);
  rebuildRuntime();
  m_path = path;
  m_nextId = 1;
  m_dirty = false;
  return true;
}

bool
EditorDocument::saveToFile(const std::string& path, std::string* error)
{
  const std::string resolved = IlscCodec::withIlscExtension(path);
  if (!IlscCodec::writeFile(resolved, serializationDocument(), error)) {
    return false;
  }
  m_path = resolved;
  m_dirty = false;
  return true;
}

std::string
EditorDocument::encode() const
{
  return IlscCodec::encode(serializationDocument());
}

IlscDocument
EditorDocument::serializationDocument() const
{
  IlscDocument serialized;
  serialized.version = m_document.version;
  serialized.worldMode = m_document.worldMode;
  serialized.camera = m_document.camera;
  serialized.nodes.reserve(m_document.nodes.size());
  for (SceneNodeHandle handle = m_graph.firstNode(); !handle.isNull();
       handle = m_graph.nextNode(handle)) {
    uint64_t index = 0;
    if (m_graph.getUserData(handle, &index) &&
        index < m_document.nodes.size()) {
      serialized.nodes.push_back(m_document.nodes[static_cast<size_t>(index)]);
    }
  }
  return serialized;
}

std::string
EditorDocument::createNode(SceneNodeKind kind, const std::string& parentId)
{
  if (!parentId.empty() && findNode(parentId) == nullptr) {
    return {};
  }
  IlscNode node;
  node.id = allocateId();
  if (node.id.empty()) {
    return {};
  }
  node.parentId = parentId;
  node.kind = kind;
  node.name = IlscCodec::kindName(kind);
  if (kind == SceneNodeKind::FilledRect) {
    node.name = "Rect";
  } else if (kind == SceneNodeKind::FilledEllipse) {
    node.name = "Ellipse";
  } else if (kind == SceneNodeKind::FilledTriangle) {
    node.name = "Triangle";
  } else if (kind == SceneNodeKind::SolidCube) {
    node.name = "Cube";
  } else if (kind == SceneNodeKind::SolidPyramid) {
    node.name = "Pyramid";
  } else if (kind == SceneNodeKind::WireSphere) {
    node.name = "Sphere";
  } else {
    node.name = "Empty";
  }
  const size_t index = m_document.nodes.size();
  std::unique_ptr<RuntimeNode> runtime = std::make_unique<RuntimeNode>();
  if (m_runtime.capacity() <= index) {
    m_runtime.reserve(std::max<size_t>(16, index * 2));
  }
  if (m_document.nodes.capacity() <= index) {
    m_document.nodes.reserve(std::max<size_t>(16, index * 2));
  }
  SceneNodeDesc description;
  description.parent = nodeHandle(parentId);
  description.name = node.id;
  description.transform = node.transform;
  description.userData = index;
  runtime->handle = m_graph.createNode(description);
  m_document.nodes.push_back(node);
  m_runtime.push_back(std::move(runtime));
  updateProxy(index);
  m_graph.addAttachment(m_runtime[index]->handle, m_runtime[index].get());
  m_dirty = true;
  return node.id;
}

bool
EditorDocument::destroySubtree(const std::string& id)
{
  if (!m_graph.destroyNode(nodeHandle(id))) {
    return false;
  }
  size_t kept = 0;
  for (size_t i = 0; i < m_runtime.size(); ++i) {
    if (!m_graph.isNodeValid(m_runtime[i]->handle)) {
      continue;
    }
    if (kept != i) {
      m_document.nodes[kept] = std::move(m_document.nodes[i]);
      m_runtime[kept] = std::move(m_runtime[i]);
    }
    m_graph.setUserData(m_runtime[kept]->handle, kept);
    ++kept;
  }
  m_document.nodes.resize(kept);
  m_runtime.resize(kept);
  m_dirty = true;
  return true;
}

bool
EditorDocument::canSetParent(const std::string& id,
                             const std::string& parentId) const
{
  const IlscNode* node = findNode(id);
  if (node == nullptr) {
    return false;
  }
  if (!parentId.empty() && findNode(parentId) == nullptr) {
    return false;
  }
  return m_graph.canSetParent(nodeHandle(id), nodeHandle(parentId));
}

bool
EditorDocument::setParent(const std::string& id, const std::string& parentId)
{
  if (!canSetParent(id, parentId)) {
    return false;
  }
  IlscNode* node = mutableNode(id);
  if (node == nullptr) {
    return false;
  }
  if (!m_graph.setParent(nodeHandle(id), nodeHandle(parentId))) {
    return false;
  }
  node->parentId = parentId;
  m_dirty = true;
  return true;
}

bool
EditorDocument::setTransform(const std::string& id,
                             const Transform3D& transform)
{
  IlscNode* node = mutableNode(id);
  if (node == nullptr) {
    return false;
  }
  if (!m_graph.setLocalTransform(nodeHandle(id), transform)) {
    return false;
  }
  node->transform = transform;
  m_dirty = true;
  return true;
}

bool
EditorDocument::setName(const std::string& id, const std::string& name)
{
  IlscNode* node = mutableNode(id);
  if (node == nullptr || name.empty()) {
    return false;
  }
  node->name = name;
  m_dirty = true;
  return true;
}

bool
EditorDocument::setExtent(const std::string& id, const Vector3& extent)
{
  IlscNode* node = mutableNode(id);
  if (node == nullptr || !IlscCodec::kindHasGeometry(node->kind)) {
    return false;
  }
  if (extent.x <= 0.0f || extent.y <= 0.0f || extent.z <= 0.0f) {
    return false;
  }
  node->primitive.extent = extent;
  updateProxy(indexOf(id));
  m_graph.notifyAttachmentChanged(nodeHandle(id));
  m_dirty = true;
  return true;
}

bool
EditorDocument::setColor(const std::string& id, ColorRgba color)
{
  IlscNode* node = mutableNode(id);
  if (node == nullptr || !IlscCodec::kindHasGeometry(node->kind)) {
    return false;
  }
  node->primitive.color = color;
  m_graph.notifyAttachmentChanged(nodeHandle(id));
  m_dirty = true;
  return true;
}

bool
EditorDocument::translate(const std::string& id, float dx, float dy)
{
  const Vector3 worldDelta(
    dx,
    m_document.worldMode == IlscWorldMode::World3D ? 0.0f : dy,
    m_document.worldMode == IlscWorldMode::World3D ? dy : 0.0f);
  return translate(id, worldDelta);
}

bool
EditorDocument::translate(const std::string& id, const Vector3& deltaWorld)
{
  IlscNode* node = mutableNode(id);
  if (node == nullptr) {
    return false;
  }
  if (!node->parentId.empty()) {
    const Matrix4 parentWorld = worldMatrix(node->parentId);
    const Matrix4 invParent = glm::inverse(parentWorld);
    const Vector3 localDelta = glm::mat3(invParent) * deltaWorld;
    node->transform.position += localDelta;
  } else {
    node->transform.position += deltaWorld;
  }
  m_graph.setLocalTransform(nodeHandle(id), node->transform);
  m_dirty = true;
  return true;
}

Transform3D
EditorDocument::makeEditPlaneTransform(float planeX, float planeY) const
{
  if (m_document.worldMode == IlscWorldMode::World3D) {
    return Transform3D::fromPosition(Vector3(planeX, 0.0f, planeY));
  }
  return Transform3D::fromPosition(Vector3(planeX, planeY, 0.0f));
}

Matrix4
EditorDocument::worldMatrix(const std::string& id) const
{
  Matrix4 world(1.0f);
  m_graph.getWorldTransform(nodeHandle(id), &world);
  return world;
}

bool
EditorDocument::pick(float worldX, float worldY, std::string* id) const
{
  if (id == nullptr) {
    return false;
  }
  id->clear();
  const bool planeXZ = m_document.worldMode == IlscWorldMode::World3D;
  for (size_t i = m_document.nodes.size(); i > 0; --i) {
    const IlscNode& node = m_document.nodes[i - 1];
    const Matrix4 mat = worldMatrix(node.id);
    const Transform3D worldTransform = Transform3D::fromMatrix(mat);
    const float centerX = worldTransform.position.x;
    const float centerY =
      planeXZ ? worldTransform.position.z : worldTransform.position.y;
    const float scaleX = worldTransform.scale.x;
    const float scaleY =
      planeXZ ? worldTransform.scale.z : worldTransform.scale.y;
    float halfX = 0.2f * std::fabs(scaleX);
    float halfY = 0.2f * std::fabs(scaleY);
    if (IlscCodec::kindHasGeometry(node.kind)) {
      halfX = node.primitive.extent.x * std::fabs(scaleX);
      halfY = (planeXZ ? node.primitive.extent.z : node.primitive.extent.y) *
              std::fabs(scaleY);
    }
    if (worldX >= centerX - halfX && worldX <= centerX + halfX &&
        worldY >= centerY - halfY && worldY <= centerY + halfY) {
      *id = node.id;
      return true;
    }
  }
  return false;
}

bool
EditorDocument::pickRay(const Vector3& origin,
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
  double nearest = std::numeric_limits<double>::infinity();
  m_graph.raycastCandidates(origin, direction, &m_pickCandidates);
  // Preserve reverse-document tie order after the graph's broad phase. The
  // exact transformed local-box test remains editor policy.
  std::sort(m_pickCandidates.begin(),
            m_pickCandidates.end(),
            [this](const SceneRayHit& a, const SceneRayHit& b) {
              uint64_t ai = 0, bi = 0;
              m_graph.getUserData(a.node, &ai);
              m_graph.getUserData(b.node, &bi);
              return ai > bi;
            });
  for (const SceneRayHit& candidate : m_pickCandidates) {
    uint64_t index = 0;
    if (!m_graph.getUserData(candidate.node, &index) ||
        index >= m_document.nodes.size()) {
      continue;
    }
    const IlscNode& node = m_document.nodes[static_cast<size_t>(index)];
    const glm::dmat4 world(worldMatrix(node.id));
    const double determinant = glm::determinant(world);
    if (!std::isfinite(determinant) || determinant == 0.0) {
      continue;
    }
    const glm::dmat4 inverse = glm::inverse(world);
    const glm::dvec3 localOrigin(inverse * glm::dvec4(origin, 1.0));
    // Do not normalize: t must remain comparable across differently scaled
    // nodes.
    const glm::dvec3 localDirection(inverse * glm::dvec4(direction, 0.0));
    glm::dvec3 half(0.2);
    if (IlscCodec::kindHasGeometry(node.kind)) {
      half = glm::dvec3(node.primitive.extent);
      if (node.kind == SceneNodeKind::WireSphere) {
        half = glm::dvec3(std::max(half.x, std::max(half.y, half.z)));
      } else if (node.kind == SceneNodeKind::FilledEllipse) {
        half = glm::dvec3(half.x);
      } else if (node.kind == SceneNodeKind::FilledRect) {
        half.z = 0.02;
      }
    }
    double enter = 0.0;
    double leave = nearest;
    bool hit = true;
    for (int axis = 0; axis < 3; ++axis) {
      if (!std::isfinite(localOrigin[axis]) ||
          !std::isfinite(localDirection[axis]) || !std::isfinite(half[axis]) ||
          half[axis] < 0.0) {
        hit = false;
        break;
      }
      if (localDirection[axis] == 0.0) {
        if (localOrigin[axis] < -half[axis] || localOrigin[axis] > half[axis]) {
          hit = false;
          break;
        }
        continue;
      }
      double first = (-half[axis] - localOrigin[axis]) / localDirection[axis];
      double last = (half[axis] - localOrigin[axis]) / localDirection[axis];
      if (first > last) {
        std::swap(first, last);
      }
      enter = std::max(enter, first);
      leave = std::min(leave, last);
      if (enter > leave) {
        hit = false;
        break;
      }
    }
    if (hit && enter < nearest) {
      nearest = enter;
      *id = node.id;
    }
  }
  return !id->empty();
}

EditorSceneDetail
EditorDocument::sceneDetail(const std::string& selectedId) const
{
  EditorSceneDetail detail;
  detail.nodeCount = nodeCount();
  detail.worldMode = m_document.worldMode;
  const IlscNode* node = findNode(selectedId);
  if (node == nullptr) {
    return detail;
  }
  detail.hasSelection = true;
  detail.selectedId = node->id;
  detail.selectedName = node->name;
  detail.selectedKind = node->kind;
  detail.transform = node->transform;
  detail.extent = node->primitive.extent;
  detail.color = node->primitive.color;
  return detail;
}
bool
EditorDocument::setEnabled(const std::string& id, bool enabled)
{
  IlscNode* node = mutableNode(id);
  if (node == nullptr || !m_graph.setEnabled(nodeHandle(id), enabled)) {
    return false;
  }
  node->enabled = enabled;
  m_dirty = true;
  return true;
}
bool
EditorDocument::setVisible(const std::string& id, bool visible)
{
  IlscNode* node = mutableNode(id);
  if (node == nullptr || !m_graph.setVisible(nodeHandle(id), visible)) {
    return false;
  }
  node->visible = visible;
  m_dirty = true;
  return true;
}
