#include "EditorDocument.h"
#include "EditorAssets.h"
#include <Illumo/Content/SceneAssetRefs.h>

#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <unordered_map>
#include <utility>

static const char*
shapeLabel(ScenePrimitiveShape shape)
{
  switch (shape) {
    case ScenePrimitiveShape::Rect:
      return "Rect";
    case ScenePrimitiveShape::Ellipse:
      return "Ellipse";
    case ScenePrimitiveShape::Triangle:
      return "Triangle";
    case ScenePrimitiveShape::Cube:
      return "Cube";
    case ScenePrimitiveShape::Pyramid:
      return "Pyramid";
    case ScenePrimitiveShape::Sphere:
      return "Sphere";
    case ScenePrimitiveShape::WireCube:
      return "Wire Cube";
    case ScenePrimitiveShape::WireSphere:
      return "Wire Sphere";
  }
  return "Node";
}

EditorDocument::EditorDocument()
  : m_scene(makeScene())
{
}

EditorDocument::~EditorDocument() = default;

std::unique_ptr<SceneInstance>
EditorDocument::makeScene() const
{
  SceneInstanceOptions options;
  options.pickProxies = true;
  std::unique_ptr<SceneInstance> scene =
    std::make_unique<SceneInstance>(m_assets, options);
  scene->setRenderer(m_renderer);
  std::string ignored;
  scene->load(SceneDocument{}, kLocalPackageRoot, ignored);
  return scene;
}

void
EditorDocument::setAssetManager(AssetManager* assets)
{
  if (assets == m_assets) {
    return;
  }
  const SceneDocument current = m_scene->document();
  const std::string root(m_scene->packageRoot());
  m_assets = assets;
  std::unique_ptr<SceneInstance> scene = makeScene();
  std::string ignored;
  if (!scene->load(current, root, ignored)) {
    Logger::LogWarning("The scene could not be rebuilt for the new asset "
                       "manager and is now empty: " +
                       ignored);
  }
  m_scene = std::move(scene);
}

void
EditorDocument::rebase(const std::string& packageRoot)
{
  if (packageRoot.empty() || packageRoot == m_scene->packageRoot()) {
    return;
  }
  const SceneDocument current = m_scene->document();
  std::unique_ptr<SceneInstance> scene = makeScene();
  std::string error;
  if (scene->load(current, packageRoot, error)) {
    m_scene = std::move(scene);
    Logger::LogTrace("Scene assets now resolve against " + packageRoot);
  } else {
    Logger::LogWarning(
      "The scene stays under " + std::string(m_scene->packageRoot()) +
      "; rebasing onto " + packageRoot + " was refused: " + error);
  }
}

void
EditorDocument::setRenderer(Renderer* renderer)
{
  m_renderer = renderer;
  m_scene->setRenderer(renderer);
}

void
EditorDocument::clear()
{
  m_scene = makeScene();
  m_history.clear();
  m_savedUid = 0;
  m_path.clear();
  m_label.clear();
}

bool
EditorDocument::loadFromText(const std::string& text,
                             std::string* error,
                             const std::string& packageRoot)
{
  SceneDocument document;
  std::string message;
  if (!IlscCodec::parse(text, document, message)) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  }
  std::unique_ptr<SceneInstance> scene = makeScene();
  if (!scene->load(document,
                   packageRoot.empty() ? std::string(kLocalPackageRoot)
                                       : packageRoot,
                   message)) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  }
  m_scene = std::move(scene);
  m_history.clear();
  m_savedUid = 0;
  return true;
}

std::string
EditorDocument::encode() const
{
  return IlscCodec::encode(m_scene->document(), true);
}

bool
EditorDocument::isDirty() const
{
  return m_history.currentUid() != m_savedUid;
}

void
EditorDocument::markSaved(const std::string& location, const std::string& label)
{
  setLocation(location, label);
  m_savedUid = m_history.currentUid();
  m_history.setMergeBarrier(m_savedUid);
}

const SceneEditorState&
EditorDocument::editorState() const
{
  return m_scene->document().editor;
}

void
EditorDocument::setEditorState(const SceneEditorState& state)
{
  m_scene->setEditorState(state);
}

SceneWorldMode
EditorDocument::worldMode() const
{
  return m_scene->document().worldMode;
}

bool
EditorDocument::setWorldMode(SceneWorldMode mode)
{
  if (worldMode() == mode) {
    return false;
  }
  EditorCommandRecord command;
  command.label =
    mode == SceneWorldMode::World2D ? "Switch to 2D" : "Switch to 3D";
  command.hasSettings = true;
  command.settingsBefore = EditorHistory::captureSettings(*m_scene);
  m_scene->setWorldMode(mode);
  command.settingsAfter = EditorHistory::captureSettings(*m_scene);
  m_history.push(std::move(command));
  return true;
}

const SceneNode*
EditorDocument::findNode(const std::string& id) const
{
  return m_scene->findNode(id);
}

SceneNodeHandle
EditorDocument::nodeHandle(const std::string& id) const
{
  return m_scene->handleOf(id);
}

bool
EditorDocument::undo(std::string* label)
{
  return m_history.undo(*m_scene, label);
}

bool
EditorDocument::redo(std::string* label)
{
  return m_history.redo(*m_scene, label);
}

std::vector<EditorNodeState>
EditorDocument::captureAll(const std::vector<std::string>& ids) const
{
  std::vector<EditorNodeState> states;
  states.reserve(ids.size());
  for (const std::string& id : ids) {
    states.push_back(EditorHistory::capture(*m_scene, id));
  }
  return states;
}

static bool
sameState(const EditorNodeState& a, const EditorNodeState& b)
{
  if (a.present != b.present) {
    return false;
  }
  return !a.present ||
         (a.nextSibling == b.nextSibling && sceneNodesEqual(a.node, b.node));
}

bool
EditorDocument::record(const std::string& label,
                       const std::string& mergeKey,
                       const std::vector<EditorNodeState>& before)
{
  std::vector<std::string> ids;
  ids.reserve(before.size());
  for (const EditorNodeState& state : before) {
    ids.push_back(state.id);
  }
  std::vector<EditorNodeState> after = captureAll(ids);
  bool changed = false;
  for (size_t index = 0; index < before.size() && !changed; ++index) {
    changed = !sameState(before[index], after[index]);
  }
  if (!changed) {
    return false;
  }
  EditorCommandRecord command;
  command.label = label;
  command.mergeKey = mergeKey;
  command.before = before;
  command.after = std::move(after);
  m_history.push(std::move(command));
  return true;
}

std::string
EditorDocument::createNode(const SceneNode& templateNode,
                           const std::string& parentId,
                           const std::string& insertBeforeId)
{
  SceneNode node = templateNode;
  node.id = m_scene->uniqueId("n");
  node.parentId = parentId;
  if (node.name.empty()) {
    node.name = node.id;
  }
  std::vector<EditorNodeState> before = captureAll({ node.id });
  std::string error;
  if (!m_scene->insertNode(node, insertBeforeId, error)) {
    return {};
  }
  record("Create " + node.name, {}, before);
  return node.id;
}

std::string
EditorDocument::createPrimitive(bool empty,
                                ScenePrimitiveShape shape,
                                const std::string& parentId,
                                const Transform3D& transform)
{
  SceneNode node;
  node.name = empty ? "Empty" : shapeLabel(shape);
  node.transform = transform;
  if (!empty) {
    ScenePrimitive primitive;
    primitive.shape = shape;
    SceneComponent component;
    component.value = primitive;
    node.components.push_back(component);
  }
  return createNode(node, parentId);
}

bool
EditorDocument::destroySubtree(const std::string& id)
{
  return destroyNodes({ id });
}

bool
EditorDocument::destroyNodes(const std::vector<std::string>& roots)
{
  std::vector<std::string> ids;
  for (const std::string& root : roots) {
    if (std::find(ids.begin(), ids.end(), root) != ids.end()) {
      continue;
    }
    const std::vector<std::string> subtree = m_scene->subtreeIds(root);
    ids.insert(ids.end(), subtree.begin(), subtree.end());
  }
  if (ids.empty()) {
    return false;
  }
  std::vector<EditorNodeState> before = captureAll(ids);
  for (const std::string& root : roots) {
    m_scene->removeSubtree(root);
  }
  record(ids.size() == 1 ? "Delete " + before.front().node.name
                         : "Delete " + std::to_string(roots.size()) + " nodes",
         {},
         before);
  return true;
}

bool
EditorDocument::canSetParent(const std::string& id,
                             const std::string& parentId) const
{
  const SceneNodeHandle node = m_scene->handleOf(id);
  const SceneNodeHandle parent =
    parentId.empty() ? SceneNodeHandle{} : m_scene->handleOf(parentId);
  if (node.isNull() || (!parentId.empty() && parent.isNull())) {
    return false;
  }
  return m_scene->graph().canSetParent(node, parent);
}

bool
EditorDocument::setParent(const std::string& id,
                          const std::string& parentId,
                          const std::string& insertBeforeId)
{
  if (!canSetParent(id, parentId)) {
    return false;
  }
  std::vector<EditorNodeState> before = captureAll({ id });
  // Keep the node where it is in the world.
  const Matrix4 world = m_scene->worldMatrix(id);
  const Matrix4 parentWorld =
    parentId.empty() ? Matrix4(1.0f) : m_scene->worldMatrix(parentId);
  std::string error;
  if (!m_scene->setParent(id, parentId, insertBeforeId, error)) {
    return false;
  }
  const double determinant = glm::determinant(glm::dmat4(parentWorld));
  if (std::isfinite(determinant) && determinant != 0.0) {
    const Transform3D local =
      Transform3D::fromMatrix(glm::inverse(parentWorld) * world);
    m_scene->setTransform(id, local);
  }
  record(parentId.empty() ? "Unparent " + before.front().node.name
                          : "Reparent " + before.front().node.name,
         {},
         before);
  return true;
}

bool
EditorDocument::setTransform(const std::string& id,
                             const Transform3D& transform,
                             const std::string& mergeKey)
{
  std::vector<EditorNodeState> before = captureAll({ id });
  if (!before.front().present || !m_scene->setTransform(id, transform)) {
    return false;
  }
  record("Transform " + before.front().node.name, mergeKey, before);
  return true;
}

bool
EditorDocument::setName(const std::string& id, const std::string& name)
{
  std::vector<EditorNodeState> before = captureAll({ id });
  if (!before.front().present || !m_scene->setName(id, name)) {
    return false;
  }
  record("Rename " + before.front().node.name, {}, before);
  return true;
}

bool
EditorDocument::setEnabled(const std::string& id, bool enabled)
{
  std::vector<EditorNodeState> before = captureAll({ id });
  if (!before.front().present || !m_scene->setEnabled(id, enabled)) {
    return false;
  }
  record(enabled ? "Enable " + before.front().node.name
                 : "Disable " + before.front().node.name,
         {},
         before);
  return true;
}

bool
EditorDocument::setVisible(const std::string& id, bool visible)
{
  std::vector<EditorNodeState> before = captureAll({ id });
  if (!before.front().present || !m_scene->setVisible(id, visible)) {
    return false;
  }
  record(visible ? "Show " + before.front().node.name
                 : "Hide " + before.front().node.name,
         {},
         before);
  return true;
}

bool
EditorDocument::setComponents(const std::string& id,
                              const std::vector<SceneComponent>& components)
{
  std::vector<EditorNodeState> before = captureAll({ id });
  std::string error;
  if (!before.front().present ||
      !m_scene->setComponents(id, components, error)) {
    return false;
  }
  record("Edit " + before.front().node.name, {}, before);
  return true;
}

bool
EditorDocument::setExtent(const std::string& id, const Vector3& extent)
{
  const SceneNode* node = findNode(id);
  if (node == nullptr) {
    return false;
  }
  std::vector<SceneComponent> components = node->components;
  for (SceneComponent& component : components) {
    if (ScenePrimitive* primitive =
          std::get_if<ScenePrimitive>(&component.value)) {
      primitive->extent = extent;
      return setComponents(id, components);
    }
  }
  return false;
}

bool
EditorDocument::setColor(const std::string& id, ColorRgba color)
{
  const SceneNode* node = findNode(id);
  if (node == nullptr) {
    return false;
  }
  std::vector<SceneComponent> components = node->components;
  for (SceneComponent& component : components) {
    if (ScenePrimitive* primitive =
          std::get_if<ScenePrimitive>(&component.value)) {
      primitive->color = color;
      return setComponents(id, components);
    }
  }
  return false;
}

bool
EditorDocument::translate(const std::vector<std::string>& ids,
                          const Vector3& deltaWorld,
                          const std::string& mergeKey)
{
  std::vector<std::string> moved;
  std::vector<Transform3D> transforms;
  for (const std::string& id : ids) {
    const SceneNode* node = findNode(id);
    if (node == nullptr) {
      continue;
    }
    const SceneNodeHandle parent =
      m_scene->graph().getParent(m_scene->handleOf(id));
    Matrix4 parentWorld(1.0f);
    m_scene->graph().getWorldTransform(parent, &parentWorld);
    const glm::dmat3 linear(parentWorld);
    const double determinant = glm::determinant(linear);
    if (!std::isfinite(determinant) || determinant == 0.0) {
      continue;
    }
    const Vector3 localDelta(glm::inverse(linear) * glm::dvec3(deltaWorld));
    Transform3D transform = node->transform;
    transform.position += localDelta;
    moved.push_back(id);
    transforms.push_back(transform);
  }
  if (moved.empty()) {
    return false;
  }
  std::vector<EditorNodeState> before = captureAll(moved);
  if (!m_scene->setTransforms(moved, transforms)) {
    return false;
  }
  record(moved.size() == 1 ? "Move " + before.front().node.name
                           : "Move " + std::to_string(moved.size()) + " nodes",
         mergeKey,
         before);
  return true;
}

bool
EditorDocument::setTransforms(const std::vector<std::string>& ids,
                              const std::vector<Transform3D>& transforms,
                              const std::string& mergeKey)
{
  if (ids.empty() || ids.size() != transforms.size()) {
    return false;
  }
  const std::vector<EditorNodeState> before = captureAll(ids);
  if (!m_scene->setTransforms(ids, transforms)) {
    return false;
  }
  const std::string label =
    ids.size() == 1 ? "Transform " + before.front().node.name
                    : "Transform " + std::to_string(ids.size()) + " nodes";
  record(label, mergeKey, before);
  return true;
}

bool
EditorDocument::translate(const std::string& id, const Vector3& deltaWorld)
{
  return translate(std::vector<std::string>{ id }, deltaWorld);
}

bool
EditorDocument::editNodes(const std::vector<std::string>& ids,
                          const std::string& label,
                          const std::string& mergeKey,
                          const std::function<bool(SceneNode&)>& mutate)
{
  std::vector<std::string> touched;
  std::vector<SceneNode> updated;
  for (const std::string& id : ids) {
    const SceneNode* current = findNode(id);
    if (current == nullptr) {
      continue;
    }
    EditorNodeState state = EditorHistory::capture(*m_scene, id);
    SceneNode node = state.node;
    if (!mutate(node)) {
      continue;
    }
    node.id = current->id;
    node.parentId = state.node.parentId;
    touched.push_back(id);
    updated.push_back(std::move(node));
  }
  if (touched.empty()) {
    return false;
  }
  const std::vector<EditorNodeState> before = captureAll(touched);
  std::string error;
  for (size_t index = 0; index < updated.size(); ++index) {
    if (!m_scene->replaceNode(updated[index], error)) {
      // Restore what was already applied so a rejected batch is atomic.
      std::string ignored;
      EditorHistory::applyStates(*m_scene, before, ignored);
      return false;
    }
  }
  return record(label, mergeKey, before);
}

bool
EditorDocument::recordSettings(const std::string& label,
                               const std::string& mergeKey,
                               const EditorSceneSettings& before)
{
  EditorCommandRecord command;
  command.label = label;
  command.mergeKey = mergeKey;
  command.hasSettings = true;
  command.settingsBefore = before;
  command.settingsAfter = EditorHistory::captureSettings(*m_scene);
  m_history.push(std::move(command));
  return true;
}

bool
EditorDocument::setEnvironment(const SceneEnvironment& environment,
                               const std::string& mergeKey)
{
  const EditorSceneSettings before = EditorHistory::captureSettings(*m_scene);
  std::string error;
  if (!m_scene->setEnvironment(environment, error)) {
    return false;
  }
  return recordSettings("Edit environment", mergeKey, before);
}

bool
EditorDocument::setMetadata(const SceneMetadata& metadata)
{
  const EditorSceneSettings before = EditorHistory::captureSettings(*m_scene);
  SceneDocument probe;
  probe.metadata = metadata;
  std::string error;
  if (!validateSceneDocument(probe, error)) {
    return false;
  }
  m_scene->setMetadata(metadata);
  return recordSettings("Edit metadata", {}, before);
}

std::vector<std::string>
EditorDocument::duplicate(const std::vector<std::string>& roots)
{
  std::vector<std::string> created;
  std::vector<EditorNodeState> before;
  for (const std::string& root : roots) {
    const std::vector<std::string> subtree = m_scene->subtreeIds(root);
    if (subtree.empty()) {
      continue;
    }
    std::unordered_map<std::string, std::string> mapped;
    std::vector<SceneNode> copies;
    for (const std::string& id : subtree) {
      SceneNode copy = *m_scene->findNode(id);
      copy.parentId =
        m_scene->idOf(m_scene->graph().getParent(m_scene->handleOf(id)));
      const std::string newId = m_scene->uniqueId("n");
      mapped.emplace(id, newId);
      copy.id = newId;
      const std::unordered_map<std::string, std::string>::const_iterator
        parent = mapped.find(copy.parentId);
      if (parent != mapped.end()) {
        copy.parentId = parent->second;
      }
      copies.push_back(std::move(copy));
    }
    const std::string after = m_scene->nextSiblingId(root);
    std::string error;
    bool first = true;
    for (const SceneNode& copy : copies) {
      EditorNodeState absent;
      absent.id = copy.id;
      before.push_back(absent);
      m_scene->insertNode(copy, first ? after : std::string(), error);
      first = false;
    }
    created.push_back(copies.front().id);
  }
  if (created.empty()) {
    return created;
  }
  record(created.size() == 1
           ? "Duplicate"
           : "Duplicate " + std::to_string(created.size()) + " nodes",
         {},
         before);
  return created;
}

static bool
sameAsset(const SceneAsset& a, const SceneAsset& b)
{
  return a.type == b.type && a.path == b.path && a.faces == b.faces &&
         a.columns == b.columns && a.rows == b.rows &&
         a.texture.filter == b.texture.filter &&
         a.texture.wrap == b.texture.wrap &&
         a.texture.mipmaps == b.texture.mipmaps &&
         a.mesh.centerAndNormalize == b.mesh.centerAndNormalize &&
         a.mesh.targetRadius == b.mesh.targetRadius &&
         a.mesh.flipV == b.mesh.flipV &&
         a.mesh.generateNormals == b.mesh.generateNormals;
}

static void
remapAssetReference(std::string& reference,
                    const std::unordered_map<std::string, std::string>& mapped)
{
  const std::unordered_map<std::string, std::string>::const_iterator found =
    mapped.find(reference);
  if (found != mapped.end()) {
    reference = found->second;
  }
}

std::vector<std::string>
EditorDocument::paste(const SceneDocument& fragment,
                      const std::string& parentId,
                      const std::string& insertBeforeId)
{
  std::vector<std::string> roots;
  if (fragment.nodes.empty() ||
      (!parentId.empty() && m_scene->handleOf(parentId).isNull())) {
    return roots;
  }
  const EditorSceneSettings settingsBefore =
    EditorHistory::captureSettings(*m_scene);

  // Merge the asset table first so pasted components resolve.
  std::vector<SceneAsset> assets = settingsBefore.assets;
  std::unordered_map<std::string, std::string> assetIds;
  for (const SceneAsset& incoming : fragment.assets) {
    std::vector<SceneAsset>::const_iterator existing =
      std::find_if(assets.begin(), assets.end(), [&](const SceneAsset& asset) {
        return asset.id == incoming.id;
      });
    if (existing != assets.end() && sameAsset(*existing, incoming)) {
      continue;
    }
    SceneAsset added = incoming;
    int suffix = 2;
    while (existing != assets.end()) {
      added.id = incoming.id + "_" + std::to_string(suffix++);
      existing =
        std::find_if(assets.begin(), assets.end(), [&](const SceneAsset& a) {
          return a.id == added.id;
        });
    }
    assetIds.emplace(incoming.id, added.id);
    assets.push_back(added);
  }
  const bool assetsChanged = assets.size() != settingsBefore.assets.size();
  std::string error;
  if (assetsChanged && !m_scene->setAssets(assets, error)) {
    return roots;
  }

  const Matrix4 parentWorld =
    parentId.empty() ? Matrix4(1.0f) : m_scene->worldMatrix(parentId);
  const double determinant = glm::determinant(glm::dmat4(parentWorld));
  const bool invertible = std::isfinite(determinant) && determinant != 0.0;
  std::unordered_map<std::string, std::string> nodeIds;
  std::vector<EditorNodeState> before;
  for (const SceneNode& source : fragment.nodes) {
    SceneNode node = source;
    node.id = m_scene->uniqueId("n");
    const bool isRoot = source.parentId.empty();
    if (isRoot) {
      node.parentId = parentId;
      if (invertible) {
        node.transform = Transform3D::fromMatrix(glm::inverse(parentWorld) *
                                                 source.transform.toMatrix());
      }
    } else {
      node.parentId = nodeIds[source.parentId];
    }
    for (SceneComponent& component : node.components) {
      SceneMeshRenderer* mesh =
        std::get_if<SceneMeshRenderer>(&component.value);
      SceneSprite* sprite = std::get_if<SceneSprite>(&component.value);
      if (mesh != nullptr) {
        remapAssetReference(mesh->asset, assetIds);
      }
      if (sprite != nullptr) {
        remapAssetReference(sprite->texture, assetIds);
      }
    }
    EditorNodeState absent;
    absent.id = node.id;
    if (!m_scene->insertNode(
          node, isRoot ? insertBeforeId : std::string(), error)) {
      // Roll back what this paste already inserted.
      std::string ignored;
      EditorHistory::applyStates(*m_scene, before, ignored);
      if (assetsChanged) {
        m_scene->setAssets(settingsBefore.assets, ignored);
      }
      return {};
    }
    nodeIds.emplace(source.id, node.id);
    before.push_back(absent);
    if (isRoot) {
      roots.push_back(node.id);
    }
  }

  EditorCommandRecord command;
  command.label = roots.size() == 1
                    ? std::string("Paste")
                    : "Paste " + std::to_string(roots.size()) + " nodes";
  command.before = before;
  std::vector<std::string> ids;
  ids.reserve(before.size());
  for (const EditorNodeState& state : before) {
    ids.push_back(state.id);
  }
  command.after = captureAll(ids);
  if (assetsChanged) {
    command.hasSettings = true;
    command.settingsBefore = settingsBefore;
    command.settingsAfter = EditorHistory::captureSettings(*m_scene);
  }
  m_history.push(std::move(command));
  return roots;
}

std::string
EditorDocument::placeAsset(const std::string& virtualPath,
                           const Transform3D& transform)
{
  const EditorAssetKind kind = EditorAssets::kindFor(virtualPath);
  if (kind != EditorAssetKind::Mesh && kind != EditorAssetKind::Texture) {
    return {};
  }
  // A one-root fragment: paste gives the node a fresh id and merges the
  // asset (reusing an identical entry, renaming a conflicting id).
  SceneDocument fragment;
  SceneAsset asset;
  asset.id = EditorAssets::assetIdFor(virtualPath, m_scene->document());
  asset.type = kind == EditorAssetKind::Mesh ? SceneAssetType::Mesh
                                             : SceneAssetType::Texture;
  asset.path = sceneReferenceFor(m_scene->packageRoot(), virtualPath);
  // The same file placed again shares its existing entry.
  for (const SceneAsset& existing : m_scene->document().assets) {
    if (existing.type == asset.type && existing.path == asset.path) {
      asset = existing;
      break;
    }
  }
  SceneNode node;
  node.id = "placed";
  if (!EditorAssets::nodeFor(asset, &node)) {
    return {};
  }
  node.transform = transform;
  fragment.assets.push_back(asset);
  fragment.nodes.push_back(node);
  const std::vector<std::string> placed = paste(fragment, {});
  return placed.empty() ? std::string() : placed.front();
}

bool
EditorDocument::pickRay(const Vector3& origin,
                        const Vector3& direction,
                        std::string* id) const
{
  return m_scene->pickRay(origin, direction, id);
}

Matrix4
EditorDocument::worldMatrix(const std::string& id) const
{
  return m_scene->worldMatrix(id);
}

Transform3D
EditorDocument::makeEditPlaneTransform(float planeX, float planeY) const
{
  return worldMode() == SceneWorldMode::World3D
           ? Transform3D::fromPosition(Vector3(planeX, 0.0f, planeY))
           : Transform3D::fromPosition(Vector3(planeX, planeY, 0.0f));
}

static std::string
kindLabel(const SceneNode& node)
{
  if (node.components.empty()) {
    return "Empty";
  }
  const SceneComponent& component = node.components.front();
  switch (component.type()) {
    case SceneComponentType::Primitive:
      return shapeLabel(std::get<ScenePrimitive>(component.value).shape);
    case SceneComponentType::Mesh:
      return "Mesh";
    case SceneComponentType::Sprite:
      return "Sprite";
    case SceneComponentType::Light:
      return "Light";
    case SceneComponentType::Camera:
      return "Camera";
    case SceneComponentType::Opaque:
      return std::get<SceneOpaqueComponent>(component.value).type;
  }
  return "Node";
}

EditorSceneDetail
EditorDocument::sceneDetail(const EditorSelection& selection) const
{
  EditorSceneDetail detail;
  detail.nodeCount = nodeCount();
  detail.selectionCount = selection.size();
  detail.worldMode = worldMode();
  const SceneNode* node = findNode(selection.primary());
  if (node == nullptr) {
    return detail;
  }
  detail.hasSelection = true;
  detail.selectedId = node->id;
  detail.selectedName = node->name;
  detail.kindLabel = kindLabel(*node);
  detail.transform = node->transform;
  const SceneComponent* component = node->find(SceneComponentType::Primitive);
  if (component != nullptr) {
    const ScenePrimitive& primitive =
      std::get<ScenePrimitive>(component->value);
    detail.hasPrimitive = true;
    detail.extent = primitive.extent;
    detail.color = primitive.color;
  }
  return detail;
}
