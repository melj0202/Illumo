#include "EditorClipboard.h"
#include "EditorDocument.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>

static SceneAsset
textureAsset(const std::string& id, const std::string& path)
{
  SceneAsset asset;
  asset.id = id;
  asset.type = SceneAssetType::Texture;
  asset.path = path;
  return asset;
}

static SceneNode
spriteNode(const std::string& name, const std::string& texture)
{
  SceneNode node;
  node.name = name;
  SceneSprite sprite;
  sprite.texture = texture;
  SceneComponent component;
  component.value = sprite;
  node.components.push_back(component);
  return node;
}

static const SceneSprite*
spriteOf(const EditorDocument& document, const std::string& id)
{
  const SceneNode* node = document.findNode(id);
  if (node == nullptr || node->components.empty()) {
    return nullptr;
  }
  return std::get_if<SceneSprite>(&node->components.front().value);
}

static int
testRoundTripRemapsIds()
{
  TestCounters counters;
  EditorDocument source;
  std::string error;
  source.scene().setAssets({ textureAsset("tex", "textures/a.png") }, error);
  SceneNode parentTemplate = spriteNode("Parent", "tex");
  parentTemplate.transform =
    Transform3D::fromPosition(Vector3(3.0f, 0.0f, 0.0f));
  const std::string parent = source.createNode(parentTemplate, {});
  SceneNode childTemplate = spriteNode("Child", "tex");
  childTemplate.transform =
    Transform3D::fromPosition(Vector3(0.0f, 1.0f, 0.0f));
  const std::string child = source.createNode(childTemplate, parent);

  const std::string text = EditorClipboard::copy(source.scene(), { parent });
  testTrue(counters, !text.empty(), "a subtree copies to text");
  SceneDocument fragment;
  testTrue(counters,
           EditorClipboard::read(text, fragment, error),
           "the copied text reads back as a fragment");
  testEqSize(
    counters, fragment.nodes.size(), 2, "the fragment holds the subtree");
  testEqSize(counters, fragment.assets.size(), 1, "and its referenced asset");

  // Paste into the same document: fresh ids, identical asset reused.
  const std::vector<std::string> same = source.paste(fragment, {});
  testTrue(counters,
           same.size() == 1 && same.front() != parent &&
             source.findNode(same.front()) != nullptr,
           "a paste creates a new root with a fresh id");
  const std::vector<std::string> pastedChildren =
    source.scene().childIds(same.empty() ? std::string() : same.front());
  testTrue(counters,
           pastedChildren.size() == 1 && pastedChildren.front() != child,
           "the child is remapped under the new root");
  testEqSize(counters,
             source.scene().document().assets.size(),
             1,
             "an identical asset is reused");

  // Paste into another document whose "tex" is a different file, under a
  // translated parent.
  EditorDocument target;
  target.scene().setAssets({ textureAsset("tex", "textures/other.png") },
                           error);
  const std::string holder = target.createPrimitive(
    true,
    ScenePrimitiveShape::Cube,
    {},
    Transform3D::fromPosition(Vector3(1.0f, 0.0f, 0.0f)));
  const size_t historyBefore = target.history().size();
  const std::vector<std::string> pasted = target.paste(fragment, holder);
  testTrue(
    counters, pasted.size() == 1, "the fragment pastes into another scene");
  testEqSize(counters,
             target.history().size(),
             historyBefore + 1,
             "a paste is one undo step");
  const SceneAsset* renamed = target.scene().document().findAsset("tex_2");
  testTrue(counters,
           renamed != nullptr && renamed->path == "textures/a.png",
           "a conflicting asset id is renamed");
  const SceneSprite* sprite =
    spriteOf(target, pasted.empty() ? std::string() : pasted.front());
  testTrue(counters,
           sprite != nullptr && sprite->texture == "tex_2",
           "pasted components follow the renamed asset");
  const Matrix4 world =
    target.worldMatrix(pasted.empty() ? std::string() : pasted.front());
  testTrue(counters,
           std::fabs(world[3][0] - 3.0f) < 1e-4f &&
             std::fabs(target.findNode(pasted.front())->transform.position.x -
                       2.0f) < 1e-4f,
           "the pasted root keeps its world pose under the new parent");

  testTrue(counters, target.undo(), "the paste undoes");
  testEqSize(counters, target.nodeCount(), 1, "undo removes the pasted nodes");
  testEqSize(counters,
             target.scene().document().assets.size(),
             1,
             "and the added asset");
  testTrue(counters, target.redo(), "the paste redoes");
  testEqSize(counters, target.nodeCount(), 3, "redo restores the nodes");
  testTrue(counters,
           target.scene().document().findAsset("tex_2") != nullptr,
           "and the renamed asset");
  return counters.failures;
}

static int
testRejectsForeignOrOversize()
{
  TestCounters counters;
  SceneDocument fragment;
  std::string error;
  testTrue(counters,
           !EditorClipboard::read("hello", fragment, error),
           "plain text is not a fragment");
  SceneDocument scene;
  SceneNode node;
  node.id = "n1";
  node.name = "n1";
  scene.nodes.push_back(node);
  testTrue(counters,
           !EditorClipboard::read(IlscCodec::encode(scene), fragment, error),
           "a scene without the fragment tag is rejected");
  std::string oversize(EditorClipboard::kMaximumBytes + 1, ' ');
  testTrue(counters,
           !EditorClipboard::read(oversize, fragment, error) &&
             error.find("4 MiB") != std::string::npos,
           "text over 4 MiB is rejected before parsing");
  EditorDocument document;
  testTrue(counters,
           EditorClipboard::copy(document.scene(), { "missing" }).empty(),
           "copying nothing yields no text");
  testTrue(counters,
           document.paste(SceneDocument{}, {}).empty(),
           "an empty fragment pastes nothing");
  return counters.failures;
}

void
registerEditorClipboardTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Clipboard.RoundTripRemapsIds",
               []() { return testRoundTripRemapsIds(); });
  registry.add("IllEd.Clipboard.RejectsForeignOrOversize",
               []() { return testRejectsForeignOrOversize(); });
}
