#include <Illumo/Content/SceneDocument.h>
#include <Illumo/Content/VirtualPath.h>

#include <cmath>
#include <initializer_list>
#include <set>
#include <unordered_map>
#include <unordered_set>

const SceneComponent*
SceneNode::find(SceneComponentType type) const
{
  for (const SceneComponent& component : components) {
    if (component.type() == type) {
      return &component;
    }
  }
  return nullptr;
}

SceneComponent*
SceneNode::find(SceneComponentType type)
{
  for (SceneComponent& component : components) {
    if (component.type() == type) {
      return &component;
    }
  }
  return nullptr;
}

const SceneComponent*
SceneNode::findOpaque(std::string_view type) const
{
  for (const SceneComponent& component : components) {
    const SceneOpaqueComponent* opaque =
      std::get_if<SceneOpaqueComponent>(&component.value);
    if (opaque != nullptr && opaque->type == type) {
      return &component;
    }
  }
  return nullptr;
}

const SceneNode*
SceneDocument::findNode(std::string_view id) const
{
  for (const SceneNode& node : nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

SceneNode*
SceneDocument::findNode(std::string_view id)
{
  for (SceneNode& node : nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

const SceneAsset*
SceneDocument::findAsset(std::string_view id) const
{
  for (const SceneAsset& asset : assets) {
    if (asset.id == id) {
      return &asset;
    }
  }
  return nullptr;
}

static bool
sameColor(const ColorRgba& a, const ColorRgba& b)
{
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool
sceneComponentsEqual(const SceneComponent& a, const SceneComponent& b)
{
  if (a.value.index() != b.value.index()) {
    return false;
  }
  if (const ScenePrimitive* left = std::get_if<ScenePrimitive>(&a.value)) {
    const ScenePrimitive& right = std::get<ScenePrimitive>(b.value);
    return left->shape == right.shape && left->extent == right.extent &&
           sameColor(left->color, right.color);
  }
  if (const SceneMeshRenderer* left =
        std::get_if<SceneMeshRenderer>(&a.value)) {
    const SceneMeshRenderer& right = std::get<SceneMeshRenderer>(b.value);
    return left->asset == right.asset && sameColor(left->tint, right.tint) &&
           left->castShadows == right.castShadows;
  }
  if (const SceneSprite* left = std::get_if<SceneSprite>(&a.value)) {
    const SceneSprite& right = std::get<SceneSprite>(b.value);
    return left->texture == right.texture && left->u0 == right.u0 &&
           left->v0 == right.v0 && left->u1 == right.u1 &&
           left->v1 == right.v1 && left->hasCell == right.hasCell &&
           left->column == right.column && left->row == right.row &&
           left->width == right.width && left->height == right.height &&
           left->facing == right.facing && sameColor(left->tint, right.tint) &&
           left->flipX == right.flipX && left->flipY == right.flipY;
  }
  if (const SceneLight* left = std::get_if<SceneLight>(&a.value)) {
    const SceneLight& right = std::get<SceneLight>(b.value);
    return left->color == right.color && left->intensity == right.intensity &&
           left->shadows == right.shadows;
  }
  if (const SceneCamera* left = std::get_if<SceneCamera>(&a.value)) {
    const SceneCamera& right = std::get<SceneCamera>(b.value);
    return left->projection == right.projection &&
           left->fovDegrees == right.fovDegrees &&
           left->nearPlane == right.nearPlane &&
           left->farPlane == right.farPlane && left->zoom == right.zoom &&
           left->primary == right.primary;
  }
  const SceneOpaqueComponent& left = std::get<SceneOpaqueComponent>(a.value);
  const SceneOpaqueComponent& right = std::get<SceneOpaqueComponent>(b.value);
  return left.type == right.type && left.data == right.data;
}

bool
sceneNodesEqual(const SceneNode& a, const SceneNode& b)
{
  if (a.id != b.id || a.parentId != b.parentId || a.name != b.name ||
      a.enabled != b.enabled || a.visible != b.visible ||
      a.transform.position != b.transform.position ||
      a.transform.rotation != b.transform.rotation ||
      a.transform.scale != b.transform.scale || a.tags != b.tags ||
      a.components.size() != b.components.size()) {
    return false;
  }
  for (size_t index = 0; index < a.components.size(); ++index) {
    if (!sceneComponentsEqual(a.components[index], b.components[index])) {
      return false;
    }
  }
  return true;
}

static bool
plainLine(std::string_view text, std::size_t minimum, std::size_t maximum)
{
  if (text.size() < minimum || text.size() > maximum) {
    return false;
  }
  for (const char value : text) {
    const unsigned char character = static_cast<unsigned char>(value);
    if (character < 32u || character == 127u) {
      return false;
    }
  }
  return true;
}

bool
validSceneId(std::string_view id)
{
  return plainLine(id, 1, 128);
}

bool
validSceneNamespace(std::string_view name)
{
  if (name.size() < 3 || name.size() > 64 || name.front() == '.' ||
      name.back() == '.' || name.find('.') == std::string_view::npos ||
      name.find("..") != std::string_view::npos) {
    return false;
  }
  for (const char character : name) {
    if (!((character >= 'a' && character <= 'z') ||
          (character >= '0' && character <= '9') || character == '.' ||
          character == '-' || character == '_')) {
      return false;
    }
  }
  return true;
}

const char*
sceneAssetTypeName(SceneAssetType type)
{
  switch (type) {
    case SceneAssetType::Mesh:
      return "mesh";
    case SceneAssetType::Texture:
      return "texture";
    case SceneAssetType::Atlas:
      return "atlas";
    case SceneAssetType::CubemapCross:
      return "cubemap_cross";
    case SceneAssetType::CubemapFaces:
      return "cubemap_faces";
  }
  return "texture";
}

const char*
scenePrimitiveShapeName(ScenePrimitiveShape shape)
{
  switch (shape) {
    case ScenePrimitiveShape::Rect:
      return "rect";
    case ScenePrimitiveShape::Ellipse:
      return "ellipse";
    case ScenePrimitiveShape::Triangle:
      return "triangle";
    case ScenePrimitiveShape::Cube:
      return "cube";
    case ScenePrimitiveShape::Pyramid:
      return "pyramid";
    case ScenePrimitiveShape::Sphere:
      return "sphere";
    case ScenePrimitiveShape::WireCube:
      return "wire_cube";
    case ScenePrimitiveShape::WireSphere:
      return "wire_sphere";
  }
  return "cube";
}

bool
parseScenePrimitiveShape(std::string_view text, ScenePrimitiveShape& shape)
{
  const ScenePrimitiveShape shapes[] = {
    ScenePrimitiveShape::Rect,     ScenePrimitiveShape::Ellipse,
    ScenePrimitiveShape::Triangle, ScenePrimitiveShape::Cube,
    ScenePrimitiveShape::Pyramid,  ScenePrimitiveShape::Sphere,
    ScenePrimitiveShape::WireCube, ScenePrimitiveShape::WireSphere
  };
  for (const ScenePrimitiveShape candidate : shapes) {
    if (text == scenePrimitiveShapeName(candidate)) {
      shape = candidate;
      return true;
    }
  }
  return false;
}

static bool
finite(float value)
{
  return std::isfinite(value);
}

static bool
inRange(float value, float minimum, float maximum)
{
  return finite(value) && value >= minimum && value <= maximum;
}

static bool
vectorInRange(const Vector3& value, float minimum, float maximum)
{
  return inRange(value.x, minimum, maximum) &&
         inRange(value.y, minimum, maximum) &&
         inRange(value.z, minimum, maximum);
}

static bool
validReference(const std::string& reference)
{
  if (reference.empty()) {
    return false;
  }
  if (reference.front() == '/') {
    std::string normalized;
    return VirtualPath::normalize(reference, normalized) &&
           normalized == reference && normalized != "/";
  }
  return VirtualPath::validRelative(reference);
}

static bool
fail(std::string& error, const std::string& message)
{
  error = message;
  return false;
}

static bool
validateTransform(const Transform3D& transform, std::string& error)
{
  const float limit = 1.0e30f;
  if (!vectorInRange(transform.position, -limit, limit) ||
      !vectorInRange(transform.scale, -limit, limit) ||
      !inRange(transform.rotation.x, -2.0f, 2.0f) ||
      !inRange(transform.rotation.y, -2.0f, 2.0f) ||
      !inRange(transform.rotation.z, -2.0f, 2.0f) ||
      !inRange(transform.rotation.w, -2.0f, 2.0f)) {
    return fail(error, "transform values must be finite and in range");
  }
  const float scales[] = { transform.scale.x,
                           transform.scale.y,
                           transform.scale.z };
  for (const float scale : scales) {
    if (std::fabs(scale) < 1.0e-4f) {
      return fail(error, "transform scale components must be at least 1e-4");
    }
  }
  const float length = std::sqrt(transform.rotation.x * transform.rotation.x +
                                 transform.rotation.y * transform.rotation.y +
                                 transform.rotation.z * transform.rotation.z +
                                 transform.rotation.w * transform.rotation.w);
  if (std::fabs(length - 1.0f) > 1.0e-3f) {
    return fail(error, "transform rotation must be a unit quaternion");
  }
  return true;
}

static bool
validateAsset(const SceneAsset& asset, std::string& error)
{
  if (!validSceneId(asset.id)) {
    return fail(error, "asset id must be 1-128 printable bytes");
  }
  const std::string where = "asset \"" + asset.id + "\": ";
  if (asset.type == SceneAssetType::CubemapFaces) {
    if (!asset.path.empty()) {
      return fail(error, where + "cubemap_faces uses faces, not path");
    }
    for (const std::string& face : asset.faces) {
      if (!validReference(face)) {
        return fail(error, where + "every cubemap face needs a valid path");
      }
    }
  } else if (!validReference(asset.path)) {
    return fail(error,
                where + "path must be a valid package-relative or "
                        "absolute virtual path");
  }
  if (asset.type == SceneAssetType::Atlas &&
      (asset.columns < 1 || asset.columns > 4096 || asset.rows < 1 ||
       asset.rows > 4096)) {
    return fail(error, where + "atlas grid must be 1-4096 cells per axis");
  }
  if (asset.type == SceneAssetType::Mesh &&
      !inRange(asset.mesh.targetRadius, 1.0e-4f, 1.0e6f)) {
    return fail(error, where + "mesh target_radius is out of range");
  }
  return true;
}

static bool
assetHasType(const SceneDocument& document,
             const std::unordered_map<std::string, std::size_t>& assets,
             const std::string& id,
             std::initializer_list<SceneAssetType> types)
{
  const std::unordered_map<std::string, std::size_t>::const_iterator found =
    assets.find(id);
  if (found == assets.end()) {
    return false;
  }
  for (const SceneAssetType type : types) {
    if (document.assets[found->second].type == type) {
      return true;
    }
  }
  return false;
}

static bool
validateComponent(const SceneDocument& document,
                  const std::unordered_map<std::string, std::size_t>& assets,
                  const SceneComponent& component,
                  std::string& error)
{
  if (const ScenePrimitive* primitive =
        std::get_if<ScenePrimitive>(&component.value)) {
    if (!inRange(primitive->extent.x, 1.0e-4f, 1.0e6f) ||
        !inRange(primitive->extent.y, 1.0e-4f, 1.0e6f) ||
        !inRange(primitive->extent.z, 1.0e-4f, 1.0e6f)) {
      return fail(error, "primitive extent must be in [1e-4, 1e6]");
    }
    return true;
  }
  if (const SceneMeshRenderer* mesh =
        std::get_if<SceneMeshRenderer>(&component.value)) {
    if (!assetHasType(
          document, assets, mesh->asset, { SceneAssetType::Mesh })) {
      return fail(error, "mesh component must name a mesh asset");
    }
    return true;
  }
  if (const SceneSprite* sprite = std::get_if<SceneSprite>(&component.value)) {
    if (!assetHasType(document,
                      assets,
                      sprite->texture,
                      { SceneAssetType::Texture, SceneAssetType::Atlas })) {
      return fail(error, "sprite must name a texture or atlas asset");
    }
    if (!inRange(sprite->width, 1.0e-4f, 1.0e6f) ||
        !inRange(sprite->height, 1.0e-4f, 1.0e6f)) {
      return fail(error, "sprite size must be in [1e-4, 1e6]");
    }
    if (sprite->hasCell) {
      const SceneAsset& atlas = document.assets[assets.at(sprite->texture)];
      if (atlas.type != SceneAssetType::Atlas || sprite->column < 0 ||
          sprite->row < 0 || sprite->column >= atlas.columns ||
          sprite->row >= atlas.rows) {
        return fail(error, "sprite cell must lie inside an atlas grid");
      }
    } else if (!inRange(sprite->u0, 0.0f, 1.0f) ||
               !inRange(sprite->v0, 0.0f, 1.0f) ||
               !inRange(sprite->u1, 0.0f, 1.0f) ||
               !inRange(sprite->v1, 0.0f, 1.0f)) {
      return fail(error, "sprite region must lie in [0, 1]");
    }
    return true;
  }
  if (const SceneLight* light = std::get_if<SceneLight>(&component.value)) {
    if (!vectorInRange(light->color, 0.0f, 16.0f) ||
        !inRange(light->intensity, 0.0f, 100.0f)) {
      return fail(error, "light color or intensity is out of range");
    }
    return true;
  }
  if (const SceneCamera* camera = std::get_if<SceneCamera>(&component.value)) {
    if (!inRange(camera->fovDegrees, 1.0f, 179.0f) ||
        !inRange(camera->nearPlane, 1.0e-5f, 1.0e7f) ||
        !inRange(camera->farPlane, 1.0e-5f, 1.0e7f) ||
        camera->farPlane <= camera->nearPlane ||
        !inRange(camera->zoom, 0.01f, 1.0e5f)) {
      return fail(error, "camera projection values are out of range");
    }
    return true;
  }
  const SceneOpaqueComponent& opaque =
    std::get<SceneOpaqueComponent>(component.value);
  if (!validSceneNamespace(opaque.type)) {
    return fail(error,
                "component type \"" + opaque.type +
                  "\" must be namespaced (vendor.name)");
  }
  if (opaque.data.empty() || opaque.data.front() != '{') {
    return fail(error, "opaque component data must be a JSON object");
  }
  return true;
}

bool
validateSceneDocument(const SceneDocument& document, std::string& error)
{
  if (document.formatMinor < 0 ||
      document.formatMinor > SceneDocument::kFormatMinor) {
    return fail(error,
                "Scene requires format 2." +
                  std::to_string(document.formatMinor) +
                  "; this reader supports 2." +
                  std::to_string(SceneDocument::kFormatMinor));
  }
  if (!plainLine(document.metadata.title, 0, 256) ||
      !plainLine(document.metadata.author, 0, 256) ||
      document.metadata.description.size() > 4096) {
    return fail(error,
                "metadata title/author must be single lines of at most "
                "256 bytes and description at most 4096 bytes");
  }
  if (document.assets.size() > SceneDocument::kMaximumAssets ||
      document.nodes.size() > SceneDocument::kMaximumNodes) {
    return fail(error, "scene has too many assets or nodes");
  }
  std::unordered_map<std::string, std::size_t> assets;
  assets.reserve(document.assets.size());
  for (std::size_t index = 0; index < document.assets.size(); ++index) {
    const SceneAsset& asset = document.assets[index];
    if (!validateAsset(asset, error)) {
      return false;
    }
    if (!assets.emplace(asset.id, index).second) {
      return fail(error, "duplicate asset id \"" + asset.id + "\"");
    }
  }

  const SceneEnvironment& environment = document.environment;
  if (!environment.skybox.empty() &&
      !assetHasType(
        document,
        assets,
        environment.skybox,
        { SceneAssetType::CubemapCross, SceneAssetType::CubemapFaces })) {
    return fail(error, "environment skybox must name a cubemap asset");
  }
  if (!vectorInRange(environment.skyboxTint, 0.0f, 4.0f) ||
      !vectorInRange(environment.ambient, 0.0f, 4.0f)) {
    return fail(error, "environment skybox_tint and ambient must be in [0, 4]");
  }
  if (environment.hasSun) {
    const SceneSun& sun = environment.sun;
    const float length = std::sqrt(sun.direction.x * sun.direction.x +
                                   sun.direction.y * sun.direction.y +
                                   sun.direction.z * sun.direction.z);
    if (!vectorInRange(sun.direction, -1.0e6f, 1.0e6f) || length < 1.0e-6f ||
        !vectorInRange(sun.color, 0.0f, 16.0f) ||
        !inRange(sun.intensity, 0.0f, 100.0f)) {
      return fail(error,
                  "environment sun direction, color or intensity is "
                  "invalid");
    }
  }

  std::unordered_set<std::string> seen;
  seen.reserve(document.nodes.size());
  for (const SceneNode& node : document.nodes) {
    if (!validSceneId(node.id)) {
      return fail(error, "node id must be 1-128 printable bytes");
    }
    const std::string where = "node \"" + node.id + "\": ";
    if (seen.find(node.id) != seen.end()) {
      return fail(error, "duplicate node id \"" + node.id + "\"");
    }
    if (!node.parentId.empty() && seen.find(node.parentId) == seen.end()) {
      return fail(error,
                  where + "parent \"" + node.parentId +
                    "\" must exist and appear earlier (preorder)");
    }
    if (!plainLine(node.name, 0, 256)) {
      return fail(error,
                  where + "name must be a single line of at most 256 "
                          "bytes");
    }
    if (!validateTransform(node.transform, error)) {
      error = where + error;
      return false;
    }
    if (node.tags.size() > SceneDocument::kMaximumTags) {
      return fail(error, where + "too many tags");
    }
    std::set<std::string> tags;
    for (const std::string& tag : node.tags) {
      if (!plainLine(tag, 1, 64) || !tags.insert(tag).second) {
        return fail(error,
                    where + "tags must be unique single lines of 1-64 "
                            "bytes");
      }
    }
    if (node.components.size() > SceneDocument::kMaximumComponents) {
      return fail(error, where + "too many components");
    }
    std::set<std::string> kinds;
    for (const SceneComponent& component : node.components) {
      if (!validateComponent(document, assets, component, error)) {
        error = where + error;
        return false;
      }
      const SceneOpaqueComponent* opaque =
        std::get_if<SceneOpaqueComponent>(&component.value);
      const std::string kind =
        opaque != nullptr ? opaque->type
                          : std::to_string(static_cast<int>(component.type()));
      if (!kinds.insert(kind).second) {
        return fail(error, where + "a component type appears twice");
      }
    }
    seen.insert(node.id);
  }

  std::unordered_set<std::string> extensions;
  for (const SceneExtension& extension : document.extensions) {
    if (!validSceneNamespace(extension.key) ||
        !extensions.insert(extension.key).second || extension.data.empty()) {
      return fail(error,
                  "extension keys must be unique and namespaced "
                  "(vendor.name)");
    }
  }

  if (document.hasEditor) {
    const SceneEditorState& editor = document.editor;
    if (!std::isfinite(editor.cameraX) || !std::isfinite(editor.cameraY) ||
        !inRange(editor.zoom, 0.1f, 100.0f) ||
        !inRange(editor.yaw, -1000.0f, 1000.0f) ||
        !inRange(editor.pitch, -1000.0f, 1000.0f) ||
        !inRange(editor.gridSpacing, 1.0e-3f, 1.0e4f) ||
        !inRange(editor.snapTranslate, 1.0e-4f, 1.0e4f) ||
        !inRange(editor.snapRotateDegrees, 0.01f, 360.0f) ||
        !inRange(editor.snapScale, 1.0e-4f, 1.0e4f)) {
      return fail(error, "editor view state is out of range");
    }
  }
  error.clear();
  return true;
}
