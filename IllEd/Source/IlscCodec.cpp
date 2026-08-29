#include "IlscCodec.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

static bool
isFiniteNumber(double value)
{
  return std::isfinite(value);
}

static bool
readDouble(const nlohmann::json& value, double* number)
{
  if (number == nullptr || !value.is_number()) {
    return false;
  }
  *number = value.get<double>();
  return isFiniteNumber(*number);
}

static bool
readFloat(const nlohmann::json& value, float* number)
{
  double parsed = 0.0;
  if (!readDouble(value, &parsed)) {
    return false;
  }
  *number = static_cast<float>(parsed);
  return true;
}

static bool
readInt(const nlohmann::json& value, int* number)
{
  if (number == nullptr || !value.is_number_integer()) {
    return false;
  }
  *number = value.get<int>();
  return true;
}

static bool
readVec3(const nlohmann::json& value, Vector3* vector)
{
  if (vector == nullptr || !value.is_array() || value.size() != 3) {
    return false;
  }
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!readFloat(value[0], &x) || !readFloat(value[1], &y) ||
      !readFloat(value[2], &z)) {
    return false;
  }
  vector->x = x;
  vector->y = y;
  vector->z = z;
  return true;
}

static bool
readColor(const nlohmann::json& value, ColorRgba* color)
{
  if (color == nullptr || !value.is_array() || value.size() != 4) {
    return false;
  }
  int channels[4] = { 0, 0, 0, 0 };
  for (size_t i = 0; i < 4; ++i) {
    if (!readInt(value[i], &channels[i]) || channels[i] < 0 ||
        channels[i] > 255) {
      return false;
    }
  }
  color->r = static_cast<unsigned char>(channels[0]);
  color->g = static_cast<unsigned char>(channels[1]);
  color->b = static_cast<unsigned char>(channels[2]);
  color->a = static_cast<unsigned char>(channels[3]);
  return true;
}

static bool
readQuaternion(const nlohmann::json& value, Quaternion* rotation)
{
  if (rotation == nullptr || !value.is_object() || !value.contains("x") ||
      !value.contains("y") || !value.contains("z") || !value.contains("w")) {
    return false;
  }
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
  if (!readFloat(value["x"], &x) || !readFloat(value["y"], &y) ||
      !readFloat(value["z"], &z) || !readFloat(value["w"], &w)) {
    return false;
  }
  *rotation = Quaternion(w, x, y, z);
  return true;
}

static bool
hasCycle(const std::vector<IlscNode>& nodes)
{
  std::unordered_map<std::string, std::string> parentById;
  parentById.reserve(nodes.size());
  for (const IlscNode& node : nodes) {
    parentById[node.id] = node.parentId;
  }

  for (const IlscNode& node : nodes) {
    std::unordered_set<std::string> seen;
    std::string current = node.id;
    while (!current.empty()) {
      if (seen.find(current) != seen.end()) {
        return true;
      }
      seen.insert(current);
      const std::unordered_map<std::string, std::string>::const_iterator found =
        parentById.find(current);
      if (found == parentById.end()) {
        return true;
      }
      current = found->second;
    }
  }
  return false;
}

void
IlscCodec::setError(std::string* error, const char* message)
{
  if (error != nullptr) {
    *error = message != nullptr ? message : "Unknown .ilsc error";
  }
}

const char*
IlscCodec::kindName(SceneNodeKind kind)
{
  if (kind == SceneNodeKind::FilledRect) {
    return "filled_rect";
  }
  if (kind == SceneNodeKind::FilledEllipse) {
    return "filled_ellipse";
  }
  if (kind == SceneNodeKind::FilledTriangle) {
    return "filled_triangle";
  }
  if (kind == SceneNodeKind::SolidCube) {
    return "solid_cube";
  }
  if (kind == SceneNodeKind::SolidPyramid) {
    return "solid_pyramid";
  }
  if (kind == SceneNodeKind::WireSphere) {
    return "wire_sphere";
  }
  return "empty";
}

bool
IlscCodec::parseKind(const std::string& text, SceneNodeKind* kind)
{
  if (kind == nullptr) {
    return false;
  }
  if (text == "empty") {
    *kind = SceneNodeKind::Empty;
    return true;
  }
  if (text == "filled_rect") {
    *kind = SceneNodeKind::FilledRect;
    return true;
  }
  if (text == "filled_ellipse") {
    *kind = SceneNodeKind::FilledEllipse;
    return true;
  }
  if (text == "filled_triangle") {
    *kind = SceneNodeKind::FilledTriangle;
    return true;
  }
  if (text == "solid_cube") {
    *kind = SceneNodeKind::SolidCube;
    return true;
  }
  if (text == "solid_pyramid") {
    *kind = SceneNodeKind::SolidPyramid;
    return true;
  }
  if (text == "wire_sphere") {
    *kind = SceneNodeKind::WireSphere;
    return true;
  }
  return false;
}

bool
IlscCodec::kindHasGeometry(SceneNodeKind kind)
{
  return kind != SceneNodeKind::Empty;
}

bool
IlscCodec::kindIs2D(SceneNodeKind kind)
{
  return kind == SceneNodeKind::FilledRect ||
         kind == SceneNodeKind::FilledEllipse ||
         kind == SceneNodeKind::FilledTriangle;
}

bool
IlscCodec::kindIs3D(SceneNodeKind kind)
{
  return kind == SceneNodeKind::SolidCube ||
         kind == SceneNodeKind::SolidPyramid ||
         kind == SceneNodeKind::WireSphere;
}

const char*
IlscCodec::worldModeName(IlscWorldMode mode)
{
  if (mode == IlscWorldMode::World3D) {
    return "3d";
  }
  return "2d";
}

bool
IlscCodec::parseWorldMode(const std::string& text, IlscWorldMode* mode)
{
  if (mode == nullptr) {
    return false;
  }
  if (text == "2d") {
    *mode = IlscWorldMode::World2D;
    return true;
  }
  if (text == "3d") {
    *mode = IlscWorldMode::World3D;
    return true;
  }
  return false;
}

std::string
IlscCodec::withIlscExtension(const std::string& filename)
{
  const std::string extension = ".ilsc";
  if (filename.size() >= extension.size()) {
    std::string ending = filename.substr(filename.size() - extension.size());
    for (size_t i = 0; i < ending.size(); ++i) {
      ending[i] =
        static_cast<char>(std::tolower(static_cast<unsigned char>(ending[i])));
    }
    if (ending == extension) {
      return filename;
    }
  }
  if (filename.empty()) {
    return "Scene.ilsc";
  }
  return filename + extension;
}

bool
IlscCodec::parse(const std::string& text,
                 IlscDocument* document,
                 std::string* error)
{
  if (document == nullptr) {
    setError(error, "Document pointer is null");
    return false;
  }

  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
  } catch (...) {
    setError(error, "Scene file is not valid JSON");
    return false;
  }
  if (!root.is_object()) {
    setError(error, "Scene file root must be an object");
    return false;
  }
  if (!root.contains("format") || !root["format"].is_string() ||
      root["format"].get<std::string>() != "ilsc") {
    setError(error, "Scene file format must be ilsc");
    return false;
  }
  int version = 0;
  if (!root.contains("version") || !readInt(root["version"], &version) ||
      version != kVersion) {
    setError(error, "Scene file version must be 1");
    return false;
  }

  IlscDocument parsed;
  parsed.version = version;
  if (root.contains("world_mode")) {
    if (!root["world_mode"].is_string() ||
        !parseWorldMode(root["world_mode"].get<std::string>(),
                        &parsed.worldMode)) {
      setError(error, "world_mode must be 2d or 3d");
      return false;
    }
  }
  if (root.contains("camera")) {
    const nlohmann::json& cameraJson = root["camera"];
    if (!cameraJson.is_object()) {
      setError(error, "Camera metadata must be an object");
      return false;
    }
    if (cameraJson.contains("x") &&
        !readDouble(cameraJson["x"], &parsed.camera.x)) {
      setError(error, "Camera x is invalid");
      return false;
    }
    if (cameraJson.contains("y") &&
        !readDouble(cameraJson["y"], &parsed.camera.y)) {
      setError(error, "Camera y is invalid");
      return false;
    }
    if (cameraJson.contains("zoom") &&
        !readFloat(cameraJson["zoom"], &parsed.camera.zoom)) {
      setError(error, "Camera zoom is invalid");
      return false;
    }
    if (cameraJson.contains("yaw") &&
        !readFloat(cameraJson["yaw"], &parsed.camera.yaw)) {
      setError(error, "Camera yaw is invalid");
      return false;
    }
    if (cameraJson.contains("pitch") &&
        !readFloat(cameraJson["pitch"], &parsed.camera.pitch)) {
      setError(error, "Camera pitch is invalid");
      return false;
    }
    if (parsed.camera.zoom < 0.1f || parsed.camera.zoom > 100.0f) {
      setError(error, "Camera zoom is out of range");
      return false;
    }
  }

  if (!root.contains("nodes") || !root["nodes"].is_array()) {
    setError(error, "Scene file must contain a nodes array");
    return false;
  }
  const nlohmann::json& nodesJson = root["nodes"];
  std::unordered_set<std::string> ids;
  parsed.nodes.reserve(nodesJson.size());
  for (size_t i = 0; i < nodesJson.size(); ++i) {
    const nlohmann::json& nodeJson = nodesJson[i];
    if (!nodeJson.is_object()) {
      setError(error, "Each node must be an object");
      return false;
    }
    IlscNode node;
    if (!nodeJson.contains("id") || !nodeJson["id"].is_string()) {
      setError(error, "Each node needs a string id");
      return false;
    }
    node.id = nodeJson["id"].get<std::string>();
    if (node.id.empty() || ids.find(node.id) != ids.end()) {
      setError(error, "Node ids must be unique and non-empty");
      return false;
    }
    ids.insert(node.id);

    if (nodeJson.contains("parent") && !nodeJson["parent"].is_null()) {
      if (!nodeJson["parent"].is_string()) {
        setError(error, "Node parent must be a string or null");
        return false;
      }
      node.parentId = nodeJson["parent"].get<std::string>();
    }
    if (nodeJson.contains("name") && nodeJson["name"].is_string()) {
      node.name = nodeJson["name"].get<std::string>();
    } else {
      node.name = node.id;
    }
    if (nodeJson.contains("enabled")) {
      if (!nodeJson["enabled"].is_boolean()) {
        setError(error, "Node enabled must be a boolean");
        return false;
      }
      node.enabled = nodeJson["enabled"].get<bool>();
    }
    if (nodeJson.contains("visible")) {
      if (!nodeJson["visible"].is_boolean()) {
        setError(error, "Node visible must be a boolean");
        return false;
      }
      node.visible = nodeJson["visible"].get<bool>();
    }
    if (nodeJson.contains("transform")) {
      const nlohmann::json& transformJson = nodeJson["transform"];
      if (!transformJson.is_object()) {
        setError(error, "Node transform must be an object");
        return false;
      }
      if (transformJson.contains("position") &&
          !readVec3(transformJson["position"], &node.transform.position)) {
        setError(error, "Node position is invalid");
        return false;
      }
      if (transformJson.contains("rotation") &&
          !readQuaternion(transformJson["rotation"],
                          &node.transform.rotation)) {
        setError(error, "Node rotation is invalid");
        return false;
      }
      if (transformJson.contains("scale") &&
          !readVec3(transformJson["scale"], &node.transform.scale)) {
        setError(error, "Node scale is invalid");
        return false;
      }
      if (std::fabs(node.transform.scale.x) < 0.0001f ||
          std::fabs(node.transform.scale.y) < 0.0001f ||
          std::fabs(node.transform.scale.z) < 0.0001f) {
        setError(error, "Node scale components must be non-zero");
        return false;
      }
    }
    if (!nodeJson.contains("kind") || !nodeJson["kind"].is_string() ||
        !parseKind(nodeJson["kind"].get<std::string>(), &node.kind)) {
      setError(error, "Node kind is unknown");
      return false;
    }
    if (kindHasGeometry(node.kind)) {
      nlohmann::json payloadJson;
      if (nodeJson.contains("primitive") && nodeJson["primitive"].is_object()) {
        payloadJson = nodeJson["primitive"];
      } else if (nodeJson.contains("solid_cube") &&
                 nodeJson["solid_cube"].is_object()) {
        payloadJson = nodeJson["solid_cube"];
      }
      if (!payloadJson.is_null() && payloadJson.is_object()) {
        if (payloadJson.contains("extent") &&
            !readVec3(payloadJson["extent"], &node.primitive.extent)) {
          setError(error, "Primitive extent is invalid");
          return false;
        }
        if (payloadJson.contains("half_extent") &&
            !readVec3(payloadJson["half_extent"], &node.primitive.extent)) {
          setError(error, "Primitive half_extent is invalid");
          return false;
        }
        if (node.primitive.extent.x <= 0.0f ||
            node.primitive.extent.y <= 0.0f ||
            node.primitive.extent.z <= 0.0f) {
          setError(error, "Primitive extent must be positive");
          return false;
        }
        if (payloadJson.contains("color") &&
            !readColor(payloadJson["color"], &node.primitive.color)) {
          setError(error, "Primitive color is invalid");
          return false;
        }
      }
    }
    parsed.nodes.push_back(node);
  }

  for (const IlscNode& node : parsed.nodes) {
    if (node.parentId.empty()) {
      continue;
    }
    if (ids.find(node.parentId) == ids.end()) {
      setError(error, "Node parent id is missing");
      return false;
    }
  }
  if (hasCycle(parsed.nodes)) {
    setError(error, "Scene nodes contain a parent cycle");
    return false;
  }

  *document = std::move(parsed);
  return true;
}

std::string
IlscCodec::encode(const IlscDocument& document)
{
  nlohmann::json root;
  root["format"] = "ilsc";
  root["version"] = kVersion;
  root["world_mode"] = worldModeName(document.worldMode);
  root["camera"]["x"] = document.camera.x;
  root["camera"]["y"] = document.camera.y;
  root["camera"]["zoom"] = document.camera.zoom;
  root["camera"]["yaw"] = document.camera.yaw;
  root["camera"]["pitch"] = document.camera.pitch;
  nlohmann::json nodes = nlohmann::json::array();
  for (const IlscNode& node : document.nodes) {
    nlohmann::json nodeJson;
    nodeJson["id"] = node.id;
    if (node.parentId.empty()) {
      nodeJson["parent"] = nullptr;
    } else {
      nodeJson["parent"] = node.parentId;
    }
    nodeJson["name"] = node.name;
    nodeJson["enabled"] = node.enabled;
    nodeJson["visible"] = node.visible;
    nodeJson["transform"]["position"] =
      nlohmann::json::array({ node.transform.position.x,
                              node.transform.position.y,
                              node.transform.position.z });
    nodeJson["transform"]["rotation"]["x"] = node.transform.rotation.x;
    nodeJson["transform"]["rotation"]["y"] = node.transform.rotation.y;
    nodeJson["transform"]["rotation"]["z"] = node.transform.rotation.z;
    nodeJson["transform"]["rotation"]["w"] = node.transform.rotation.w;
    nodeJson["transform"]["scale"] =
      nlohmann::json::array({ node.transform.scale.x,
                              node.transform.scale.y,
                              node.transform.scale.z });
    nodeJson["kind"] = kindName(node.kind);
    if (kindHasGeometry(node.kind)) {
      nodeJson["primitive"]["extent"] =
        nlohmann::json::array({ node.primitive.extent.x,
                                node.primitive.extent.y,
                                node.primitive.extent.z });
      nodeJson["primitive"]["color"] =
        nlohmann::json::array({ node.primitive.color.r,
                                node.primitive.color.g,
                                node.primitive.color.b,
                                node.primitive.color.a });
    }
    nodes.push_back(nodeJson);
  }
  root["nodes"] = nodes;
  return root.dump(2);
}

bool
IlscCodec::readFile(const std::string& path,
                    IlscDocument* document,
                    std::string* error)
{
  if (path.empty()) {
    setError(error, "Scene path is empty");
    return false;
  }
  std::ifstream file(path);
  if (!file.is_open()) {
    setError(error, "Failed to open scene file");
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (!file.good() && !file.eof()) {
    setError(error, "Failed while reading scene file");
    return false;
  }
  return parse(buffer.str(), document, error);
}

bool
IlscCodec::writeFile(const std::string& path,
                     const IlscDocument& document,
                     std::string* error)
{
  if (path.empty()) {
    setError(error, "Scene path is empty");
    return false;
  }
  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) {
    setError(error, "Failed to open scene file for writing");
    return false;
  }
  const std::string text = encode(document);
  file << text;
  if (!file.good()) {
    setError(error, "Failed while writing scene file");
    return false;
  }
  return true;
}
