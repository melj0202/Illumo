#include "EditorDocument.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

EditorDocument::EditorDocument()
  : m_dirty(false)
  , m_nextId(1)
{
  clear();
}

void
EditorDocument::clear()
{
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
EditorDocument::findNode(const std::string& id)
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
  for (size_t i = 0; i < m_document.nodes.size(); ++i) {
    if (m_document.nodes[i].id == id) {
      return i;
    }
  }
  return m_document.nodes.size();
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
  m_path = path;
  m_nextId = 1;
  m_dirty = false;
  return true;
}

bool
EditorDocument::saveToFile(const std::string& path, std::string* error)
{
  const std::string resolved = IlscCodec::withIlscExtension(path);
  if (!IlscCodec::writeFile(resolved, m_document, error)) {
    return false;
  }
  m_path = resolved;
  m_dirty = false;
  return true;
}

std::string
EditorDocument::encode() const
{
  return IlscCodec::encode(m_document);
}

bool
EditorDocument::wouldCreateCycle(const std::string& id,
                                 const std::string& parentId) const
{
  if (parentId.empty()) {
    return false;
  }
  if (id == parentId) {
    return true;
  }
  return isDescendant(id, parentId);
}

bool
EditorDocument::isDescendant(const std::string& ancestorId,
                             const std::string& nodeId) const
{
  std::string current = nodeId;
  std::unordered_set<std::string> seen;
  while (!current.empty()) {
    if (seen.find(current) != seen.end()) {
      return true;
    }
    if (current == ancestorId) {
      return true;
    }
    seen.insert(current);
    const IlscNode* node = findNode(current);
    if (node == nullptr) {
      return false;
    }
    current = node->parentId;
  }
  return false;
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
  m_document.nodes.push_back(node);
  m_dirty = true;
  return node.id;
}

bool
EditorDocument::destroySubtree(const std::string& id)
{
  if (findNode(id) == nullptr) {
    return false;
  }
  std::unordered_set<std::string> doomed;
  doomed.insert(id);
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (const IlscNode& node : m_document.nodes) {
      if (doomed.find(node.id) != doomed.end()) {
        continue;
      }
      if (!node.parentId.empty() &&
          doomed.find(node.parentId) != doomed.end()) {
        doomed.insert(node.id);
        progressed = true;
      }
    }
  }
  std::vector<IlscNode> kept;
  kept.reserve(m_document.nodes.size());
  for (const IlscNode& node : m_document.nodes) {
    if (doomed.find(node.id) == doomed.end()) {
      kept.push_back(node);
    }
  }
  m_document.nodes = std::move(kept);
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
  if (wouldCreateCycle(id, parentId)) {
    return false;
  }
  return true;
}

bool
EditorDocument::setParent(const std::string& id, const std::string& parentId)
{
  if (!canSetParent(id, parentId)) {
    return false;
  }
  IlscNode* node = findNode(id);
  if (node == nullptr) {
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
  IlscNode* node = findNode(id);
  if (node == nullptr) {
    return false;
  }
  node->transform = transform;
  m_dirty = true;
  return true;
}

bool
EditorDocument::setName(const std::string& id, const std::string& name)
{
  IlscNode* node = findNode(id);
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
  IlscNode* node = findNode(id);
  if (node == nullptr || !IlscCodec::kindHasGeometry(node->kind)) {
    return false;
  }
  if (extent.x <= 0.0f || extent.y <= 0.0f || extent.z <= 0.0f) {
    return false;
  }
  node->primitive.extent = extent;
  m_dirty = true;
  return true;
}

bool
EditorDocument::setColor(const std::string& id, ColorRgba color)
{
  IlscNode* node = findNode(id);
  if (node == nullptr || !IlscCodec::kindHasGeometry(node->kind)) {
    return false;
  }
  node->primitive.color = color;
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
  IlscNode* node = findNode(id);
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
  const IlscNode* node = findNode(id);
  if (node == nullptr) {
    return Matrix4(1.0f);
  }
  std::vector<const IlscNode*> chain;
  const IlscNode* current = node;
  std::unordered_set<std::string> visited;
  while (current != nullptr && visited.find(current->id) == visited.end()) {
    chain.push_back(current);
    visited.insert(current->id);
    if (current->parentId.empty()) {
      break;
    }
    current = findNode(current->parentId);
  }
  Matrix4 worldMat = Matrix4(1.0f);
  for (size_t i = chain.size(); i > 0; --i) {
    worldMat = worldMat * chain[i - 1]->transform.toMatrix();
  }
  return worldMat;
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
  for (size_t index = m_document.nodes.size(); index > 0; --index) {
    const IlscNode& node = m_document.nodes[index - 1];
    const IlscNode* ancestor = &node;
    bool eligible = true;
    size_t remaining = m_document.nodes.size();
    while (ancestor != nullptr) {
      if (remaining == 0 || !ancestor->enabled || !ancestor->visible) {
        eligible = false;
        break;
      }
      --remaining;
      if (ancestor->parentId.empty()) {
        break;
      }
      ancestor = findNode(ancestor->parentId);
      if (ancestor == nullptr) {
        eligible = false;
      }
    }
    if (!eligible) {
      continue;
    }
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
