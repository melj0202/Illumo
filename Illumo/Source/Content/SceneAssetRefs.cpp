#include <Illumo/Content/SceneAssetRefs.h>
#include <Illumo/Content/VirtualPath.h>
#include <Illumo/Rendering/MeshLoader.h>

#include <algorithm>
#include <unordered_set>
#include <utility>

bool
scenePackageRoot(std::string_view scenePath, std::string& root)
{
  std::string normalized;
  if (!VirtualPath::normalize(scenePath, normalized) ||
      normalized != scenePath || normalized == "/") {
    return false;
  }
  const std::string_view mount = VirtualPath::mountName(normalized);
  if (mount != "packages") {
    root = "/" + std::string(mount);
    return true;
  }
  const std::string_view below =
    VirtualPath::relativeTo(normalized, "/packages");
  if (below.empty()) {
    return false;
  }
  const std::size_t separator = below.find('/');
  root = "/packages/" + std::string(below.substr(0, separator));
  return true;
}

bool
resolveSceneReference(std::string_view packageRoot,
                      std::string_view reference,
                      std::string& resolved)
{
  if (reference.empty()) {
    return false;
  }
  if (reference.front() != '/' && !VirtualPath::validRelative(reference)) {
    return false;
  }
  std::string output;
  if (!VirtualPath::join(packageRoot, reference, output) || output == "/") {
    return false;
  }
  resolved = std::move(output);
  return true;
}

std::string
sceneReferenceFor(std::string_view packageRoot, std::string_view target)
{
  if (packageRoot != "/" && VirtualPath::isWithin(target, packageRoot) &&
      target.size() > packageRoot.size()) {
    return std::string(VirtualPath::relativeTo(target, packageRoot));
  }
  return std::string(target);
}

SceneFetchList
collectSceneFetches(const SceneDocument& document, std::string_view packageRoot)
{
  SceneFetchList list;
  std::unordered_set<std::string> seen;
  for (const SceneAsset& asset : document.assets) {
    std::vector<const std::string*> references;
    if (asset.type == SceneAssetType::CubemapFaces) {
      for (const std::string& face : asset.faces) {
        references.push_back(&face);
      }
    } else {
      references.push_back(&asset.path);
    }
    for (const std::string* reference : references) {
      std::string resolved;
      if (!resolveSceneReference(packageRoot, *reference, resolved)) {
        list.unresolved.push_back(*reference);
        continue;
      }
      if (seen.insert(resolved).second) {
        list.paths.push_back(std::move(resolved));
      }
    }
  }
  return list;
}

std::vector<std::string>
objMaterialLibraries(std::string_view objPath, std::string_view objText)
{
  std::vector<std::string> libraries;
  const std::string directory = VirtualPath::parent(objPath);
  for (const std::string& relative :
       MeshLoader::materialLibraryNames(std::string(objText))) {
    std::string resolved;
    if (!VirtualPath::validRelative(relative) ||
        !VirtualPath::join(directory, relative, resolved)) {
      continue;
    }
    if (std::find(libraries.begin(), libraries.end(), resolved) ==
        libraries.end()) {
      libraries.push_back(std::move(resolved));
    }
  }
  return libraries;
}