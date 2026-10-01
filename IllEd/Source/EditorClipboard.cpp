#include "EditorClipboard.h"

#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneInstance.h>
#include <algorithm>

static void
addReferencedAsset(const SceneInstance& scene,
                   const std::string& id,
                   SceneDocument& fragment)
{
  if (id.empty() || fragment.findAsset(id) != nullptr) {
    return;
  }
  const SceneAsset* asset = scene.document().findAsset(id);
  if (asset != nullptr) {
    fragment.assets.push_back(*asset);
  }
}

std::string
EditorClipboard::copy(const SceneInstance& scene,
                      const std::vector<std::string>& roots)
{
  SceneDocument fragment;
  fragment.worldMode = scene.document().worldMode;
  for (const std::string& root : roots) {
    const std::vector<std::string> subtree = scene.subtreeIds(root);
    for (const std::string& id : subtree) {
      if (fragment.findNode(id) != nullptr) {
        continue;
      }
      const SceneNode* source = scene.findNode(id);
      if (source == nullptr) {
        continue;
      }
      SceneNode node = *source;
      node.parentId = scene.parentOf(id);
      if (id == root) {
        node.parentId.clear();
        node.transform = Transform3D::fromMatrix(scene.worldMatrix(id));
      }
      for (const SceneComponent& component : node.components) {
        const SceneMeshRenderer* mesh =
          std::get_if<SceneMeshRenderer>(&component.value);
        const SceneSprite* sprite = std::get_if<SceneSprite>(&component.value);
        if (mesh != nullptr) {
          addReferencedAsset(scene, mesh->asset, fragment);
        }
        if (sprite != nullptr) {
          addReferencedAsset(scene, sprite->texture, fragment);
        }
      }
      fragment.nodes.push_back(std::move(node));
    }
  }
  if (fragment.nodes.empty()) {
    return {};
  }
  SceneExtension tag;
  tag.key = kExtensionKey;
  tag.data = "{\"version\":1}";
  fragment.extensions.push_back(tag);
  std::string error;
  if (!validateSceneDocument(fragment, error)) {
    return {};
  }
  std::string text = IlscCodec::encode(fragment, false);
  if (text.size() > kMaximumBytes) {
    return {};
  }
  return text;
}

bool
EditorClipboard::read(std::string_view text,
                      SceneDocument& fragment,
                      std::string& error)
{
  if (text.size() > kMaximumBytes) {
    error = "Clipboard contents exceed the 4 MiB paste limit";
    return false;
  }
  SceneDocument parsed;
  if (!IlscCodec::parse(text, parsed, error)) {
    error = "Clipboard does not hold IllEd nodes";
    return false;
  }
  const bool tagged = std::any_of(parsed.extensions.begin(),
                                  parsed.extensions.end(),
                                  [](const SceneExtension& extension) {
                                    return extension.key == kExtensionKey;
                                  });
  if (!tagged || parsed.nodes.empty()) {
    error = "Clipboard does not hold IllEd nodes";
    return false;
  }
  fragment = std::move(parsed);
  return true;
}
