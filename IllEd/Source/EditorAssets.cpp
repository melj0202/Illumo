#include "EditorAssets.h"

#include <algorithm>
#include <climits>

// The engine links stb_image (AssetManager.cpp); only the header probe is
// needed here.
extern "C" int
stbi_info_from_memory(const unsigned char* buffer,
                      int length,
                      int* width,
                      int* height,
                      int* components);

static std::string
lowerExtension(const std::string& path)
{
  const std::size_t slash = path.find_last_of("/\\");
  const std::size_t dot = path.rfind('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
    return std::string();
  }
  std::string extension = path.substr(dot + 1);
  std::transform(
    extension.begin(), extension.end(), extension.begin(), [](char c) {
      return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
  return extension;
}

EditorAssetKind
EditorAssets::kindFor(const std::string& path)
{
  const std::string extension = lowerExtension(path);
  if (extension == "obj") {
    return EditorAssetKind::Mesh;
  }
  if (extension == "png" || extension == "jpg" || extension == "jpeg" ||
      extension == "tga" || extension == "bmp") {
    return EditorAssetKind::Texture;
  }
  if (extension == "ilsc") {
    return EditorAssetKind::Scene;
  }
  return EditorAssetKind::Other;
}

const char*
EditorAssets::importFolder(EditorAssetKind kind)
{
  switch (kind) {
    case EditorAssetKind::Mesh:
      return "meshes";
    case EditorAssetKind::Texture:
      return "textures";
    case EditorAssetKind::Scene:
      return "scenes";
    case EditorAssetKind::Other:
      break;
  }
  return "assets";
}

bool
EditorAssets::validateImport(const std::string& name,
                             const std::vector<unsigned char>& bytes,
                             std::string* error)
{
  if (kindFor(name) != EditorAssetKind::Texture) {
    return true;
  }
  int width = 0;
  int height = 0;
  int components = 0;
  if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX) ||
      stbi_info_from_memory(bytes.data(),
                            static_cast<int>(bytes.size()),
                            &width,
                            &height,
                            &components) == 0) {
    if (error != nullptr) {
      *error = name + " is not a readable image";
    }
    return false;
  }
  const std::size_t uploaded =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
  if (uploaded > kMaximumTextureBytes) {
    if (error != nullptr) {
      *error = name + " is " + std::to_string(width) + "x" +
               std::to_string(height) + "; textures must fit one 16 MiB upload";
    }
    return false;
  }
  return true;
}

std::string
EditorAssets::assetIdFor(const std::string& path, const SceneDocument& document)
{
  const std::size_t slash = path.find_last_of("/\\");
  std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
  const std::size_t dot = stem.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    stem = stem.substr(0, dot);
  }
  std::string id;
  for (char character : stem) {
    const bool keep = (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') ||
                      character == '-' || character == '_';
    id.push_back(keep ? character : '_');
  }
  if (id.empty()) {
    id = "asset";
  }
  const std::string base = id;
  int suffix = 2;
  while (document.findAsset(id) != nullptr) {
    id = base + "_" + std::to_string(suffix++);
  }
  return id;
}

bool
EditorAssets::nodeFor(const SceneAsset& asset, SceneNode* node)
{
  SceneComponent component;
  if (asset.type == SceneAssetType::Mesh) {
    SceneMeshRenderer mesh;
    mesh.asset = asset.id;
    component.value = mesh;
  } else if (asset.type == SceneAssetType::Texture) {
    SceneSprite sprite;
    sprite.texture = asset.id;
    component.value = sprite;
  } else {
    return false;
  }
  node->name = asset.id;
  node->components.clear();
  node->components.push_back(component);
  return true;
}
