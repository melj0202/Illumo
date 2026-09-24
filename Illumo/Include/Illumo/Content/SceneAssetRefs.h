#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <string>
#include <string_view>
#include <vector>

// Scene asset references are package-relative ("meshes/tree.obj") or
// absolute virtual paths ("/engine/Skybox/sky.png"). A relative reference
// resolves against the root of the package that holds the scene, so a scene
// keeps working when its project directory is packed and mounted elsewhere.

// The package root holding a normalized virtual path: "/packages/<id>" for
// paths under /packages, otherwise the first component ("/app", "/project",
// "/engine", "/local"). Returns false for "/", "/packages" itself, or a path
// that is not normalized.
bool
scenePackageRoot(std::string_view scenePath, std::string& root);

// Resolves one reference against a package root into a normalized absolute
// virtual path.
bool
resolveSceneReference(std::string_view packageRoot,
                      std::string_view reference,
                      std::string& resolved);

// A reference from inside packageRoot, written package-relative when the
// target lies in the same package and absolute otherwise. Used when an asset
// is added to a scene by its virtual path.
std::string
sceneReferenceFor(std::string_view packageRoot, std::string_view target);

// Every distinct resolved path the document's assets need, in first-use
// order (asset table order; cubemap faces in face order). References that do
// not resolve are reported in unresolved (as written) and skipped.
struct SceneFetchList
{
  std::vector<std::string> paths;
  std::vector<std::string> unresolved;
};

SceneFetchList
collectSceneFetches(const SceneDocument& document,
                    std::string_view packageRoot);

// Material library names ("mtllib x.mtl") an OBJ file declares, resolved
// against the OBJ's own directory, in declaration order without duplicates.
std::vector<std::string>
objMaterialLibraries(std::string_view objPath, std::string_view objText);
