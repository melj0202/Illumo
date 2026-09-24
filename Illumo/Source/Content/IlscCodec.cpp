#include <Illumo/Content/IlscCodec.h>

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

// ---------------------------------------------------------------------------
// Strict readers. Each returns false and sets error; callers prefix context.

static bool
fail(std::string& error, const std::string& message)
{
  error = message;
  return false;
}

static bool
onlyKeys(const Json& object,
         std::initializer_list<const char*> allowed,
         const char* where,
         std::string& error)
{
  if (!object.is_object()) {
    return fail(error, std::string(where) + " must be an object");
  }
  for (Json::const_iterator item = object.begin(); item != object.end();
       ++item) {
    bool known = false;
    for (const char* key : allowed) {
      known = known || item.key() == key;
    }
    if (!known) {
      return fail(
        error, std::string("unknown key \"") + item.key() + "\" in " + where);
    }
  }
  return true;
}

static bool
readFloat(const Json& value, float& output)
{
  if (!value.is_number()) {
    return false;
  }
  const double parsed = value.get<double>();
  if (!std::isfinite(parsed) ||
      std::fabs(parsed) >
        static_cast<double>(std::numeric_limits<float>::max())) {
    return false;
  }
  output = static_cast<float>(parsed);
  return true;
}

static bool
readInt(const Json& value, int minimum, int maximum, int& output)
{
  if (!value.is_number_integer()) {
    return false;
  }
  if (value.is_number_unsigned()) {
    const std::uint64_t parsed = value.get<std::uint64_t>();
    if (parsed > static_cast<std::uint64_t>(maximum)) {
      return false;
    }
    output = static_cast<int>(parsed);
    return output >= minimum;
  }
  const std::int64_t parsed = value.get<std::int64_t>();
  if (parsed < minimum || parsed > maximum) {
    return false;
  }
  output = static_cast<int>(parsed);
  return true;
}

static bool
readFloats(const Json& value, float* output, std::size_t count)
{
  if (!value.is_array() || value.size() != count) {
    return false;
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (!readFloat(value[index], output[index])) {
      return false;
    }
  }
  return true;
}

static bool
readVector3(const Json& value, Vector3& output)
{
  float values[3] = { 0.0f, 0.0f, 0.0f };
  if (!readFloats(value, values, 3)) {
    return false;
  }
  output = Vector3(values[0], values[1], values[2]);
  return true;
}

static bool
readColor(const Json& value, ColorRgba& output)
{
  if (!value.is_array() || value.size() != 4) {
    return false;
  }
  int channels[4] = { 0, 0, 0, 0 };
  for (std::size_t index = 0; index < 4; ++index) {
    if (!readInt(value[index], 0, 255, channels[index])) {
      return false;
    }
  }
  output = ColorRgba{ static_cast<unsigned char>(channels[0]),
                      static_cast<unsigned char>(channels[1]),
                      static_cast<unsigned char>(channels[2]),
                      static_cast<unsigned char>(channels[3]) };
  return true;
}

static bool
readQuaternion(const Json& value, Quaternion& output)
{
  float values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  if (!readFloats(value, values, 4)) {
    return false;
  }
  const double length = std::sqrt(static_cast<double>(values[0]) * values[0] +
                                  static_cast<double>(values[1]) * values[1] +
                                  static_cast<double>(values[2]) * values[2] +
                                  static_cast<double>(values[3]) * values[3]);
  if (length < 1.0e-6) {
    return false;
  }
  // File order is x, y, z, w. Values already unit within float precision are
  // kept bit-exact so re-encoding is stable; others are normalized once.
  if (std::fabs(length - 1.0) <= 1.0e-5) {
    output = Quaternion(values[3], values[0], values[1], values[2]);
    return true;
  }
  output = Quaternion(static_cast<float>(values[3] / length),
                      static_cast<float>(values[0] / length),
                      static_cast<float>(values[1] / length),
                      static_cast<float>(values[2] / length));
  return true;
}

static bool
readBool(const Json& object, const char* key, bool& output)
{
  if (!object.contains(key)) {
    return true;
  }
  if (!object[key].is_boolean()) {
    return false;
  }
  output = object[key].get<bool>();
  return true;
}

static bool
readText(const Json& object, const char* key, std::string& output)
{
  if (!object.contains(key)) {
    return true;
  }
  if (!object[key].is_string()) {
    return false;
  }
  output = object[key].get<std::string>();
  return true;
}

// ---------------------------------------------------------------------------
// Section readers.

static bool
readTextureOptions(const Json& options,
                   bool atlas,
                   SceneAsset& asset,
                   std::string& error)
{
  if (atlas) {
    if (!onlyKeys(options,
                  { "filter", "wrap", "mipmaps", "grid" },
                  "atlas options",
                  error)) {
      return false;
    }
  } else if (!onlyKeys(options,
                       { "filter", "wrap", "mipmaps" },
                       "texture options",
                       error)) {
    return false;
  }
  std::string filter = "linear";
  std::string wrap = "clamp";
  if (!readText(options, "filter", filter) ||
      (filter != "linear" && filter != "nearest") ||
      !readText(options, "wrap", wrap) ||
      (wrap != "clamp" && wrap != "repeat") ||
      !readBool(options, "mipmaps", asset.texture.mipmaps)) {
    return fail(error,
                "texture options need filter linear|nearest, wrap "
                "clamp|repeat and a boolean mipmaps");
  }
  asset.texture.filter = filter == "linear" ? SceneTextureFilter::Linear
                                            : SceneTextureFilter::Nearest;
  asset.texture.wrap =
    wrap == "clamp" ? SceneTextureWrap::Clamp : SceneTextureWrap::Repeat;
  if (atlas) {
    if (!options.contains("grid") || !options["grid"].is_array() ||
        options["grid"].size() != 2 ||
        !readInt(options["grid"][0], 1, 4096, asset.columns) ||
        !readInt(options["grid"][1], 1, 4096, asset.rows)) {
      return fail(error, "atlas options need grid [columns, rows] of 1-4096");
    }
  }
  return true;
}

static bool
readAsset(const Json& entry, SceneAsset& asset, std::string& error)
{
  if (!onlyKeys(
        entry, { "id", "type", "path", "faces", "options" }, "asset", error)) {
    return false;
  }
  std::string type;
  if (!entry.contains("id") || !entry["id"].is_string() ||
      !readText(entry, "type", type)) {
    return fail(error, "asset needs a string id and type");
  }
  asset.id = entry["id"].get<std::string>();
  const std::string where = "asset \"" + asset.id + "\": ";
  if (type == "mesh") {
    asset.type = SceneAssetType::Mesh;
  } else if (type == "texture") {
    asset.type = SceneAssetType::Texture;
  } else if (type == "atlas") {
    asset.type = SceneAssetType::Atlas;
  } else if (type == "cubemap_cross") {
    asset.type = SceneAssetType::CubemapCross;
  } else if (type == "cubemap_faces") {
    asset.type = SceneAssetType::CubemapFaces;
  } else {
    return fail(error, where + "unknown asset type \"" + type + "\"");
  }
  if (asset.type == SceneAssetType::CubemapFaces) {
    const Json* faces = entry.contains("faces") ? &entry["faces"] : nullptr;
    if (entry.contains("path") || faces == nullptr || !faces->is_array() ||
        faces->size() != 6) {
      return fail(error,
                  where + "cubemap_faces needs faces [6 paths] and no "
                          "path");
    }
    for (std::size_t index = 0; index < 6; ++index) {
      if (!(*faces)[index].is_string()) {
        return fail(error, where + "cubemap faces must be strings");
      }
      asset.faces[index] = (*faces)[index].get<std::string>();
    }
  } else if (entry.contains("faces") || !entry.contains("path") ||
             !entry["path"].is_string()) {
    return fail(error, where + "needs a string path");
  } else {
    asset.path = entry["path"].get<std::string>();
  }
  const bool hasOptions = entry.contains("options");
  const Json emptyOptions = Json::object();
  const Json& options = hasOptions ? entry["options"] : emptyOptions;
  switch (asset.type) {
    case SceneAssetType::Mesh: {
      if (!onlyKeys(options,
                    { "center_and_normalize",
                      "target_radius",
                      "flip_v",
                      "generate_normals" },
                    "mesh options",
                    error)) {
        error = where + error;
        return false;
      }
      if (!readBool(
            options, "center_and_normalize", asset.mesh.centerAndNormalize) ||
          !readBool(options, "flip_v", asset.mesh.flipV) ||
          !readBool(options, "generate_normals", asset.mesh.generateNormals) ||
          (options.contains("target_radius") &&
           !readFloat(options["target_radius"], asset.mesh.targetRadius))) {
        return fail(error, where + "invalid mesh options");
      }
      break;
    }
    case SceneAssetType::Texture:
    case SceneAssetType::Atlas:
      if (!readTextureOptions(
            options, asset.type == SceneAssetType::Atlas, asset, error)) {
        error = where + error;
        return false;
      }
      break;
    case SceneAssetType::CubemapCross:
    case SceneAssetType::CubemapFaces:
      if (hasOptions) {
        return fail(error, where + "cubemaps take no options");
      }
      break;
  }
  return true;
}

static bool
readTransform(const Json& value, Transform3D& transform, std::string& error)
{
  if (!onlyKeys(
        value, { "position", "rotation", "scale" }, "transform", error)) {
    return false;
  }
  if ((value.contains("position") &&
       !readVector3(value["position"], transform.position)) ||
      (value.contains("rotation") &&
       !readQuaternion(value["rotation"], transform.rotation)) ||
      (value.contains("scale") &&
       !readVector3(value["scale"], transform.scale))) {
    return fail(error,
                "transform needs position [3], rotation [x, y, z, w] "
                "and scale [3] of finite numbers");
  }
  return true;
}

// Returns false with an error for an invalid component; sets newerFormat when
// the type is an unknown core (non-namespaced) type.
static bool
readComponent(const Json& entry,
              SceneComponent& component,
              bool& newerFormat,
              std::string& error)
{
  if (!entry.is_object() || !entry.contains("type") ||
      !entry["type"].is_string()) {
    return fail(error, "component needs a string type");
  }
  const std::string type = entry["type"].get<std::string>();
  if (type.find('.') != std::string::npos) {
    SceneOpaqueComponent opaque;
    opaque.type = type;
    Json data = entry;
    data.erase("type");
    opaque.data = data.dump(-1, ' ', false, Json::error_handler_t::replace);
    component.value = std::move(opaque);
    return true;
  }
  if (type == "primitive") {
    ScenePrimitive primitive;
    std::string shape;
    if (!onlyKeys(entry,
                  { "type", "shape", "extent", "color" },
                  "primitive component",
                  error)) {
      return false;
    }
    if (!readText(entry, "shape", shape) ||
        !parseScenePrimitiveShape(shape, primitive.shape) ||
        (entry.contains("extent") &&
         !readVector3(entry["extent"], primitive.extent)) ||
        (entry.contains("color") &&
         !readColor(entry["color"], primitive.color))) {
      return fail(error,
                  "primitive needs a known shape, extent [3] and color "
                  "[r, g, b, a] of 0-255");
    }
    component.value = primitive;
    return true;
  }
  if (type == "mesh") {
    SceneMeshRenderer mesh;
    if (!onlyKeys(entry,
                  { "type", "asset", "tint", "cast_shadows" },
                  "mesh component",
                  error)) {
      return false;
    }
    if (!readText(entry, "asset", mesh.asset) ||
        (entry.contains("tint") && !readColor(entry["tint"], mesh.tint)) ||
        !readBool(entry, "cast_shadows", mesh.castShadows)) {
      return fail(error,
                  "mesh component needs an asset id, tint [4] and a "
                  "boolean cast_shadows");
    }
    component.value = mesh;
    return true;
  }
  if (type == "sprite") {
    SceneSprite sprite;
    if (!onlyKeys(entry,
                  { "type",
                    "texture",
                    "region",
                    "cell",
                    "size",
                    "facing",
                    "tint",
                    "flip" },
                  "sprite component",
                  error)) {
      return false;
    }
    std::string facing = "world";
    float region[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    float size[2] = { 1.0f, 1.0f };
    if (entry.contains("region") && entry.contains("cell")) {
      return fail(error, "sprite takes either region or cell, not both");
    }
    if (!readText(entry, "texture", sprite.texture) ||
        (entry.contains("region") && !readFloats(entry["region"], region, 4)) ||
        (entry.contains("size") && !readFloats(entry["size"], size, 2)) ||
        !readText(entry, "facing", facing) ||
        (facing != "world" && facing != "billboard") ||
        (entry.contains("tint") && !readColor(entry["tint"], sprite.tint))) {
      return fail(error,
                  "sprite needs a texture id, region [4], size [2], "
                  "facing world|billboard and tint [4]");
    }
    if (entry.contains("cell")) {
      const Json& cell = entry["cell"];
      if (!cell.is_array() || cell.size() != 2 ||
          !readInt(cell[0], 0, 4095, sprite.column) ||
          !readInt(cell[1], 0, 4095, sprite.row)) {
        return fail(error, "sprite cell must be [column, row]");
      }
      sprite.hasCell = true;
    }
    if (entry.contains("flip")) {
      const Json& flip = entry["flip"];
      if (!flip.is_array() || flip.size() != 2 || !flip[0].is_boolean() ||
          !flip[1].is_boolean()) {
        return fail(error, "sprite flip must be [bool, bool]");
      }
      sprite.flipX = flip[0].get<bool>();
      sprite.flipY = flip[1].get<bool>();
    }
    sprite.u0 = region[0];
    sprite.v0 = region[1];
    sprite.u1 = region[2];
    sprite.v1 = region[3];
    sprite.width = size[0];
    sprite.height = size[1];
    sprite.facing = facing == "world" ? SceneSpriteFacing::World
                                      : SceneSpriteFacing::Billboard;
    component.value = sprite;
    return true;
  }
  if (type == "light") {
    SceneLight light;
    std::string kind = "directional";
    if (!onlyKeys(entry,
                  { "type", "kind", "color", "intensity", "shadows" },
                  "light component",
                  error)) {
      return false;
    }
    if (!readText(entry, "kind", kind) || kind != "directional" ||
        (entry.contains("color") &&
         !readVector3(entry["color"], light.color)) ||
        (entry.contains("intensity") &&
         !readFloat(entry["intensity"], light.intensity)) ||
        !readBool(entry, "shadows", light.shadows)) {
      return fail(error,
                  "light needs kind directional, color [3], intensity "
                  "and a boolean shadows");
    }
    component.value = light;
    return true;
  }
  if (type == "camera") {
    SceneCamera camera;
    std::string projection = "perspective";
    if (!onlyKeys(entry,
                  { "type",
                    "projection",
                    "fov_degrees",
                    "near",
                    "far",
                    "zoom",
                    "primary" },
                  "camera component",
                  error)) {
      return false;
    }
    if (!readText(entry, "projection", projection) ||
        (projection != "perspective" && projection != "orthographic") ||
        (entry.contains("fov_degrees") &&
         !readFloat(entry["fov_degrees"], camera.fovDegrees)) ||
        (entry.contains("near") &&
         !readFloat(entry["near"], camera.nearPlane)) ||
        (entry.contains("far") && !readFloat(entry["far"], camera.farPlane)) ||
        (entry.contains("zoom") && !readFloat(entry["zoom"], camera.zoom)) ||
        !readBool(entry, "primary", camera.primary)) {
      return fail(error,
                  "camera needs projection perspective|orthographic "
                  "and numeric fov_degrees, near, far and zoom");
    }
    camera.projection = projection == "perspective"
                          ? SceneProjection::Perspective
                          : SceneProjection::Orthographic;
    component.value = camera;
    return true;
  }
  newerFormat = true;
  return fail(error, "unknown component type \"" + type + "\"");
}

static bool
readNode(const Json& entry,
         SceneNode& node,
         bool& newerFormat,
         std::string& error)
{
  if (!onlyKeys(entry,
                { "id",
                  "parent",
                  "name",
                  "enabled",
                  "visible",
                  "transform",
                  "tags",
                  "components" },
                "node",
                error)) {
    return false;
  }
  if (!entry.contains("id") || !entry["id"].is_string()) {
    return fail(error, "node needs a string id");
  }
  node.id = entry["id"].get<std::string>();
  const std::string where = "node \"" + node.id + "\": ";
  if (entry.contains("parent")) {
    const Json& parent = entry["parent"];
    if (parent.is_string()) {
      node.parentId = parent.get<std::string>();
      if (node.parentId.empty()) {
        return fail(error, where + "parent must be null or a node id");
      }
    } else if (!parent.is_null()) {
      return fail(error, where + "parent must be null or a node id");
    }
  }
  node.name = node.id;
  if (!readText(entry, "name", node.name) ||
      !readBool(entry, "enabled", node.enabled) ||
      !readBool(entry, "visible", node.visible)) {
    return fail(error,
                where + "name must be a string; enabled and visible "
                        "booleans");
  }
  if (entry.contains("transform") &&
      !readTransform(entry["transform"], node.transform, error)) {
    error = where + error;
    return false;
  }
  if (entry.contains("tags")) {
    const Json& tags = entry["tags"];
    if (!tags.is_array()) {
      return fail(error, where + "tags must be an array of strings");
    }
    for (const Json& tag : tags) {
      if (!tag.is_string()) {
        return fail(error, where + "tags must be an array of strings");
      }
      node.tags.push_back(tag.get<std::string>());
    }
  }
  if (entry.contains("components")) {
    const Json& components = entry["components"];
    if (!components.is_array() ||
        components.size() > SceneDocument::kMaximumComponents) {
      return fail(error, where + "components must be an array of at most 16");
    }
    for (const Json& value : components) {
      SceneComponent component;
      if (!readComponent(value, component, newerFormat, error)) {
        error = where + error;
        return false;
      }
      node.components.push_back(std::move(component));
    }
  }
  return true;
}

static bool
readEnvironment(const Json& value,
                SceneEnvironment& environment,
                std::string& error)
{
  if (!onlyKeys(value,
                { "skybox", "skybox_tint", "ambient", "sun" },
                "environment",
                error)) {
    return false;
  }
  if (value.contains("skybox")) {
    const Json& skybox = value["skybox"];
    if (skybox.is_string()) {
      environment.skybox = skybox.get<std::string>();
    } else if (!skybox.is_null()) {
      return fail(error, "environment skybox must be null or an asset id");
    }
  }
  if ((value.contains("skybox_tint") &&
       !readVector3(value["skybox_tint"], environment.skyboxTint)) ||
      (value.contains("ambient") &&
       !readVector3(value["ambient"], environment.ambient))) {
    return fail(error, "environment skybox_tint and ambient must be [3]");
  }
  if (value.contains("sun")) {
    const Json& sun = value["sun"];
    if (sun.is_null()) {
      environment.hasSun = false;
      return true;
    }
    if (!onlyKeys(sun,
                  { "direction", "color", "intensity", "shadows" },
                  "environment sun",
                  error)) {
      return false;
    }
    if ((sun.contains("direction") &&
         !readVector3(sun["direction"], environment.sun.direction)) ||
        (sun.contains("color") &&
         !readVector3(sun["color"], environment.sun.color)) ||
        (sun.contains("intensity") &&
         !readFloat(sun["intensity"], environment.sun.intensity)) ||
        !readBool(sun, "shadows", environment.sun.shadows)) {
      return fail(error,
                  "environment sun needs direction [3], color [3], "
                  "intensity and a boolean shadows");
    }
  }
  return true;
}

static bool
readEditor(const Json& value, SceneEditorState& editor, std::string& error)
{
  if (!onlyKeys(value, { "camera", "grid", "snap" }, "editor", error)) {
    return false;
  }
  if (value.contains("camera")) {
    const Json& camera = value["camera"];
    if (!onlyKeys(camera,
                  { "x", "y", "zoom", "yaw", "pitch" },
                  "editor camera",
                  error)) {
      return false;
    }
    if ((camera.contains("x") && (!camera["x"].is_number() ||
                                  !std::isfinite(camera["x"].get<double>()))) ||
        (camera.contains("y") && (!camera["y"].is_number() ||
                                  !std::isfinite(camera["y"].get<double>()))) ||
        (camera.contains("zoom") && !readFloat(camera["zoom"], editor.zoom)) ||
        (camera.contains("yaw") && !readFloat(camera["yaw"], editor.yaw)) ||
        (camera.contains("pitch") &&
         !readFloat(camera["pitch"], editor.pitch))) {
      return fail(error, "editor camera values must be finite numbers");
    }
    if (camera.contains("x")) {
      editor.cameraX = camera["x"].get<double>();
    }
    if (camera.contains("y")) {
      editor.cameraY = camera["y"].get<double>();
    }
  }
  if (value.contains("grid")) {
    const Json& grid = value["grid"];
    if (!onlyKeys(grid, { "spacing", "visible" }, "editor grid", error)) {
      return false;
    }
    if ((grid.contains("spacing") &&
         !readFloat(grid["spacing"], editor.gridSpacing)) ||
        !readBool(grid, "visible", editor.gridVisible)) {
      return fail(error,
                  "editor grid needs numeric spacing and boolean "
                  "visible");
    }
  }
  if (value.contains("snap")) {
    const Json& snap = value["snap"];
    if (!onlyKeys(snap,
                  { "enabled", "translate", "rotate_degrees", "scale" },
                  "editor snap",
                  error)) {
      return false;
    }
    if (!readBool(snap, "enabled", editor.snapEnabled) ||
        (snap.contains("translate") &&
         !readFloat(snap["translate"], editor.snapTranslate)) ||
        (snap.contains("rotate_degrees") &&
         !readFloat(snap["rotate_degrees"], editor.snapRotateDegrees)) ||
        (snap.contains("scale") &&
         !readFloat(snap["scale"], editor.snapScale))) {
      return fail(error,
                  "editor snap values must be numbers and enabled a "
                  "boolean");
    }
  }
  return true;
}

bool
IlscCodec::parse(std::string_view text,
                 SceneDocument& document,
                 std::string& error)
{
  if (text.size() > kMaximumBytes) {
    return fail(error, "Scene file is larger than 256 MiB");
  }
  const Json root = Json::parse(text.begin(), text.end(), nullptr, false);
  if (root.is_discarded()) {
    return fail(error, "Scene file is not valid JSON");
  }
  if (!root.is_object() || !root.contains("format") ||
      root["format"] != "ilsc") {
    return fail(error, "Scene file needs \"format\": \"ilsc\"");
  }
  if (!root.contains("format_version") && root.contains("version")) {
    return fail(error,
                "Scene format 1 is no longer supported; recreate the scene "
                "in the current IllEd (format 2)");
  }
  int major = 0;
  int minor = 0;
  if (!root.contains("format_version") || !root["format_version"].is_array() ||
      root["format_version"].size() != 2 ||
      !readInt(root["format_version"][0], 0, 1000000, major) ||
      !readInt(root["format_version"][1], 0, 1000000, minor)) {
    return fail(error, "Scene file needs \"format_version\": [major, minor]");
  }
  if (major != SceneDocument::kFormatMajor) {
    return fail(error,
                "Scene format " + std::to_string(major) + "." +
                  std::to_string(minor) + " is not supported (expected 2.x)");
  }
  if (minor > SceneDocument::kFormatMinor) {
    return fail(error,
                "Scene requires format 2." + std::to_string(minor) +
                  "; this reader supports 2." +
                  std::to_string(SceneDocument::kFormatMinor));
  }
  std::string detail;
  if (!onlyKeys(root,
                { "format",
                  "format_version",
                  "metadata",
                  "settings",
                  "assets",
                  "nodes",
                  "extensions",
                  "editor" },
                "scene",
                detail)) {
    return fail(error, detail);
  }

  SceneDocument parsed;
  parsed.formatMinor = minor;
  if (root.contains("metadata")) {
    const Json& metadata = root["metadata"];
    if (!onlyKeys(
          metadata, { "title", "author", "description" }, "metadata", detail)) {
      return fail(error, detail);
    }
    if (!readText(metadata, "title", parsed.metadata.title) ||
        !readText(metadata, "author", parsed.metadata.author) ||
        !readText(metadata, "description", parsed.metadata.description)) {
      return fail(error, "metadata values must be strings");
    }
  }
  if (root.contains("settings")) {
    const Json& settings = root["settings"];
    if (!onlyKeys(
          settings, { "world_mode", "environment" }, "settings", detail)) {
      return fail(error, detail);
    }
    std::string mode = "2d";
    if (!readText(settings, "world_mode", mode) ||
        (mode != "2d" && mode != "3d")) {
      return fail(error, "settings world_mode must be \"2d\" or \"3d\"");
    }
    parsed.worldMode =
      mode == "2d" ? SceneWorldMode::World2D : SceneWorldMode::World3D;
    if (settings.contains("environment") &&
        !readEnvironment(settings["environment"], parsed.environment, detail)) {
      return fail(error, detail);
    }
  }
  if (root.contains("assets")) {
    const Json& assets = root["assets"];
    if (!assets.is_array() || assets.size() > SceneDocument::kMaximumAssets) {
      return fail(error, "assets must be an array of at most 65536 entries");
    }
    parsed.assets.reserve(assets.size());
    for (const Json& entry : assets) {
      SceneAsset asset;
      if (!readAsset(entry, asset, detail)) {
        return fail(error, detail);
      }
      parsed.assets.push_back(std::move(asset));
    }
  }
  if (!root.contains("nodes") || !root["nodes"].is_array() ||
      root["nodes"].size() > SceneDocument::kMaximumNodes) {
    return fail(error, "Scene file needs a nodes array of at most 262144");
  }
  const Json& nodes = root["nodes"];
  parsed.nodes.reserve(nodes.size());
  for (const Json& entry : nodes) {
    SceneNode node;
    bool newerFormat = false;
    if (!readNode(entry, node, newerFormat, detail)) {
      if (newerFormat) {
        return fail(error,
                    "Scene requires a newer format 2.x (" + detail +
                      "); namespaced types need a dot, such as vendor.name");
      }
      return fail(error, detail);
    }
    parsed.nodes.push_back(std::move(node));
  }
  if (root.contains("extensions")) {
    const Json& extensions = root["extensions"];
    if (!extensions.is_object()) {
      return fail(error, "extensions must be an object");
    }
    for (Json::const_iterator item = extensions.begin();
         item != extensions.end();
         ++item) {
      SceneExtension extension;
      extension.key = item.key();
      extension.data =
        item.value().dump(-1, ' ', false, Json::error_handler_t::replace);
      parsed.extensions.push_back(std::move(extension));
    }
  }
  if (root.contains("editor")) {
    parsed.hasEditor = true;
    if (!readEditor(root["editor"], parsed.editor, detail)) {
      return fail(error, detail);
    }
  }
  if (!validateSceneDocument(parsed, detail)) {
    return fail(error, detail);
  }
  document = std::move(parsed);
  error.clear();
  return true;
}

// ---------------------------------------------------------------------------
// Canonical writer.

// The shortest decimal that reads back as the same float, stored as the
// double of that decimal so the JSON printer emits it unchanged ("0.45",
// not "0.44999998807907104").
static OrderedJson
number(float value)
{
  char buffer[64];
  const std::to_chars_result result =
    std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (result.ec != std::errc()) {
    return OrderedJson(static_cast<double>(value));
  }
  *result.ptr = '\0';
  return OrderedJson(std::strtod(buffer, nullptr));
}

static OrderedJson
vector3(const Vector3& value)
{
  return OrderedJson::array(
    { number(value.x), number(value.y), number(value.z) });
}

static OrderedJson
color(const ColorRgba& value)
{
  return OrderedJson::array({ static_cast<int>(value.r),
                              static_cast<int>(value.g),
                              static_cast<int>(value.b),
                              static_cast<int>(value.a) });
}

static OrderedJson
parseStored(const std::string& data)
{
  OrderedJson value = OrderedJson::parse(data, nullptr, false);
  return value.is_discarded() ? OrderedJson() : value;
}

static OrderedJson
writeComponent(const SceneComponent& component)
{
  OrderedJson out = OrderedJson::object();
  if (const ScenePrimitive* primitive =
        std::get_if<ScenePrimitive>(&component.value)) {
    out["type"] = "primitive";
    out["shape"] = scenePrimitiveShapeName(primitive->shape);
    out["extent"] = vector3(primitive->extent);
    out["color"] = color(primitive->color);
  } else if (const SceneMeshRenderer* mesh =
               std::get_if<SceneMeshRenderer>(&component.value)) {
    out["type"] = "mesh";
    out["asset"] = mesh->asset;
    out["tint"] = color(mesh->tint);
    out["cast_shadows"] = mesh->castShadows;
  } else if (const SceneSprite* sprite =
               std::get_if<SceneSprite>(&component.value)) {
    out["type"] = "sprite";
    out["texture"] = sprite->texture;
    if (sprite->hasCell) {
      out["cell"] = OrderedJson::array({ sprite->column, sprite->row });
    } else {
      out["region"] = OrderedJson::array({ number(sprite->u0),
                                           number(sprite->v0),
                                           number(sprite->u1),
                                           number(sprite->v1) });
    }
    out["size"] =
      OrderedJson::array({ number(sprite->width), number(sprite->height) });
    out["facing"] =
      sprite->facing == SceneSpriteFacing::World ? "world" : "billboard";
    out["tint"] = color(sprite->tint);
    out["flip"] = OrderedJson::array({ sprite->flipX, sprite->flipY });
  } else if (const SceneLight* light =
               std::get_if<SceneLight>(&component.value)) {
    out["type"] = "light";
    out["kind"] = "directional";
    out["color"] = vector3(light->color);
    out["intensity"] = number(light->intensity);
    out["shadows"] = light->shadows;
  } else if (const SceneCamera* camera =
               std::get_if<SceneCamera>(&component.value)) {
    out["type"] = "camera";
    out["projection"] = camera->projection == SceneProjection::Perspective
                          ? "perspective"
                          : "orthographic";
    out["fov_degrees"] = number(camera->fovDegrees);
    out["near"] = number(camera->nearPlane);
    out["far"] = number(camera->farPlane);
    out["zoom"] = number(camera->zoom);
    out["primary"] = camera->primary;
  } else {
    const SceneOpaqueComponent& opaque =
      std::get<SceneOpaqueComponent>(component.value);
    out["type"] = opaque.type;
    const OrderedJson data = parseStored(opaque.data);
    if (data.is_object()) {
      for (OrderedJson::const_iterator item = data.begin(); item != data.end();
           ++item) {
        if (item.key() != "type") {
          out[item.key()] = item.value();
        }
      }
    }
  }
  return out;
}

static OrderedJson
writeAsset(const SceneAsset& asset)
{
  OrderedJson out = OrderedJson::object();
  out["id"] = asset.id;
  out["type"] = sceneAssetTypeName(asset.type);
  if (asset.type == SceneAssetType::CubemapFaces) {
    OrderedJson faces = OrderedJson::array();
    for (const std::string& face : asset.faces) {
      faces.push_back(face);
    }
    out["faces"] = faces;
    return out;
  }
  out["path"] = asset.path;
  if (asset.type == SceneAssetType::Mesh) {
    OrderedJson options = OrderedJson::object();
    options["center_and_normalize"] = asset.mesh.centerAndNormalize;
    options["target_radius"] = number(asset.mesh.targetRadius);
    options["flip_v"] = asset.mesh.flipV;
    options["generate_normals"] = asset.mesh.generateNormals;
    out["options"] = options;
  } else if (asset.type == SceneAssetType::Texture ||
             asset.type == SceneAssetType::Atlas) {
    OrderedJson options = OrderedJson::object();
    options["filter"] =
      asset.texture.filter == SceneTextureFilter::Linear ? "linear" : "nearest";
    options["wrap"] =
      asset.texture.wrap == SceneTextureWrap::Clamp ? "clamp" : "repeat";
    options["mipmaps"] = asset.texture.mipmaps;
    if (asset.type == SceneAssetType::Atlas) {
      options["grid"] = OrderedJson::array({ asset.columns, asset.rows });
    }
    out["options"] = options;
  }
  return out;
}

static OrderedJson
writeNode(const SceneNode& node)
{
  OrderedJson out = OrderedJson::object();
  out["id"] = node.id;
  out["parent"] =
    node.parentId.empty() ? OrderedJson(nullptr) : OrderedJson(node.parentId);
  out["name"] = node.name;
  out["enabled"] = node.enabled;
  out["visible"] = node.visible;
  OrderedJson transform = OrderedJson::object();
  transform["position"] = vector3(node.transform.position);
  transform["rotation"] =
    OrderedJson::array({ number(node.transform.rotation.x),
                         number(node.transform.rotation.y),
                         number(node.transform.rotation.z),
                         number(node.transform.rotation.w) });
  transform["scale"] = vector3(node.transform.scale);
  out["transform"] = transform;
  if (!node.tags.empty()) {
    OrderedJson tags = OrderedJson::array();
    for (const std::string& tag : node.tags) {
      tags.push_back(tag);
    }
    out["tags"] = tags;
  }
  OrderedJson components = OrderedJson::array();
  for (const SceneComponent& component : node.components) {
    components.push_back(writeComponent(component));
  }
  out["components"] = components;
  return out;
}

static bool
inlineValue(const OrderedJson& value)
{
  if (!value.is_structured() || value.empty()) {
    return true;
  }
  if (!value.is_array()) {
    return false;
  }
  for (const OrderedJson& element : value) {
    if (element.is_structured()) {
      return false;
    }
  }
  return true;
}

static std::string
dumpScalar(const OrderedJson& value)
{
  return value.dump(-1, ' ', false, OrderedJson::error_handler_t::replace);
}

// Iterative pretty printer: two-space indentation, objects and arrays of
// structures one member per line, arrays of scalars kept on one line.
static std::string
prettyPrint(const OrderedJson& root)
{
  struct Frame
  {
    const OrderedJson* value;
    OrderedJson::const_iterator next;
    bool any;
  };
  std::string out;
  std::vector<Frame> stack;
  const OrderedJson* pending = &root;
  while (true) {
    if (pending != nullptr) {
      if (inlineValue(*pending)) {
        if (pending->is_array() && !pending->empty()) {
          out += '[';
          bool first = true;
          for (const OrderedJson& element : *pending) {
            out += first ? "" : ", ";
            out += dumpScalar(element);
            first = false;
          }
          out += ']';
        } else {
          out += dumpScalar(*pending);
        }
      } else {
        out += pending->is_object() ? '{' : '[';
        stack.push_back(Frame{ pending, pending->cbegin(), false });
      }
      pending = nullptr;
    }
    if (stack.empty()) {
      break;
    }
    Frame& top = stack.back();
    if (top.next == top.value->cend()) {
      out += '\n';
      out.append((stack.size() - 1) * 2, ' ');
      out += top.value->is_object() ? '}' : ']';
      stack.pop_back();
      continue;
    }
    out += top.any ? ",\n" : "\n";
    top.any = true;
    out.append(stack.size() * 2, ' ');
    if (top.value->is_object()) {
      out += dumpScalar(OrderedJson(top.next.key()));
      out += ": ";
    }
    pending = &(*top.next);
    ++top.next;
  }
  out += '\n';
  return out;
}

std::string
IlscCodec::encode(const SceneDocument& document, bool includeEditor)
{
  OrderedJson root = OrderedJson::object();
  root["format"] = "ilsc";
  root["format_version"] = OrderedJson::array(
    { SceneDocument::kFormatMajor, SceneDocument::kFormatMinor });
  OrderedJson metadata = OrderedJson::object();
  metadata["title"] = document.metadata.title;
  metadata["author"] = document.metadata.author;
  metadata["description"] = document.metadata.description;
  root["metadata"] = metadata;

  OrderedJson environment = OrderedJson::object();
  environment["skybox"] = document.environment.skybox.empty()
                            ? OrderedJson(nullptr)
                            : OrderedJson(document.environment.skybox);
  environment["skybox_tint"] = vector3(document.environment.skyboxTint);
  environment["ambient"] = vector3(document.environment.ambient);
  if (document.environment.hasSun) {
    OrderedJson sun = OrderedJson::object();
    sun["direction"] = vector3(document.environment.sun.direction);
    sun["color"] = vector3(document.environment.sun.color);
    sun["intensity"] = number(document.environment.sun.intensity);
    sun["shadows"] = document.environment.sun.shadows;
    environment["sun"] = sun;
  } else {
    environment["sun"] = nullptr;
  }
  OrderedJson settings = OrderedJson::object();
  settings["world_mode"] =
    document.worldMode == SceneWorldMode::World2D ? "2d" : "3d";
  settings["environment"] = environment;
  root["settings"] = settings;

  OrderedJson assets = OrderedJson::array();
  for (const SceneAsset& asset : document.assets) {
    assets.push_back(writeAsset(asset));
  }
  root["assets"] = assets;
  OrderedJson nodes = OrderedJson::array();
  for (const SceneNode& node : document.nodes) {
    nodes.push_back(writeNode(node));
  }
  root["nodes"] = nodes;

  // Extensions in sorted key order, independent of insertion order.
  Json sortedExtensions = Json::object();
  for (const SceneExtension& extension : document.extensions) {
    sortedExtensions[extension.key] =
      Json::parse(extension.data, nullptr, false);
  }
  OrderedJson extensions = OrderedJson::object();
  for (Json::const_iterator item = sortedExtensions.begin();
       item != sortedExtensions.end();
       ++item) {
    extensions[item.key()] = parseStored(
      item.value().dump(-1, ' ', false, Json::error_handler_t::replace));
  }
  root["extensions"] = extensions;

  if (includeEditor && document.hasEditor) {
    const SceneEditorState& state = document.editor;
    OrderedJson camera = OrderedJson::object();
    camera["x"] = state.cameraX;
    camera["y"] = state.cameraY;
    camera["zoom"] = number(state.zoom);
    camera["yaw"] = number(state.yaw);
    camera["pitch"] = number(state.pitch);
    OrderedJson grid = OrderedJson::object();
    grid["spacing"] = number(state.gridSpacing);
    grid["visible"] = state.gridVisible;
    OrderedJson snap = OrderedJson::object();
    snap["enabled"] = state.snapEnabled;
    snap["translate"] = number(state.snapTranslate);
    snap["rotate_degrees"] = number(state.snapRotateDegrees);
    snap["scale"] = number(state.snapScale);
    OrderedJson editor = OrderedJson::object();
    editor["camera"] = camera;
    editor["grid"] = grid;
    editor["snap"] = snap;
    root["editor"] = editor;
  }
  return prettyPrint(root);
}

std::string
IlscCodec::withExtension(const std::string& name)
{
  const std::string extension = kExtension;
  if (name.size() >= extension.size()) {
    bool matches = true;
    for (std::size_t index = 0; index < extension.size(); ++index) {
      char character = name[name.size() - extension.size() + index];
      if (character >= 'A' && character <= 'Z') {
        character = static_cast<char>(character - 'A' + 'a');
      }
      matches = matches && character == extension[index];
    }
    if (matches) {
      return name;
    }
  }
  return name + extension;
}
