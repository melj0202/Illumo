#include "EditorInspector.h"
#include "PanelTestHelpers.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>

static std::string
cube(EditorDocument& document, const Vector3& position = Vector3(0.0f))
{
  return document.createPrimitive(
    false, ScenePrimitiveShape::Cube, {}, Transform3D::fromPosition(position));
}

// Types text into the focused field and presses Enter (or Escape).
static void
typeAndFinish(EditorInspector& inspector,
              InputManager& input,
              EditorDocument& document,
              EditorSelection& selection,
              const std::string& text,
              KeyCode finish = KeyCode::Enter)
{
  for (const char character : text) {
    input.getCharQueue().push(static_cast<unsigned char>(character));
  }
  inspector.update(&input, &document, &selection, 0.016f);
  input.getKeyQueue().push({ finish, InputAction::Press, 0 });
  inspector.update(&input, &document, &selection, 0.016f);
}

static int
testInspectorCommitCreatesCommand()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 300.0f, 240.0f, 400.0f));
  InputManager input(nullptr);
  EditorDocument document;
  EditorSelection selection;
  const std::string id = cube(document);
  selection.set(id);
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("position.x") != nullptr &&
             inspector.field("primitive.extent.y") != nullptr &&
             inspector.field("primitive.shape") != nullptr,
           "a cube shows transform and primitive fields");
  const size_t commands = document.history().size();
  inspector.activateField("position.x", &document, &selection);
  testTrue(counters, inspector.editing(), "clicking a number edits it");
  typeAndFinish(inspector, input, document, selection, "4.5");
  testTrue(counters,
           document.findNode(id)->transform.position.x == 4.5f &&
             !inspector.editing(),
           "Enter commits the typed value");
  testEqSize(counters,
             document.history().size(),
             commands + 1,
             "the commit is one undoable command");
  inspector.activateField("rotation.z", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "90");
  const Vector3 euler =
    glm::degrees(glm::eulerAngles(document.findNode(id)->transform.rotation));
  testTrue(counters,
           std::fabs(euler.z - 90.0f) < 0.01f,
           "rotation is edited in Euler degrees");
  inspector.activateField("primitive.color.r", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "300");
  const SceneComponent* primitive =
    document.findNode(id)->find(SceneComponentType::Primitive);
  testTrue(counters,
           std::get<ScenePrimitive>(primitive->value).color.r == 255,
           "color channels clamp to a byte");
  inspector.activateField("name", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "Crate");
  testEqStr(counters, document.findNode(id)->name, "Crate", "rename by typing");
  document.undo();
  testEqStr(counters, document.findNode(id)->name, "Cube", "rename undoes");
  inspector.activateField("primitive.shape", &document, &selection);
  primitive = document.findNode(id)->find(SceneComponentType::Primitive);
  testTrue(counters,
           std::get<ScenePrimitive>(primitive->value).shape ==
             ScenePrimitiveShape::Pyramid,
           "a choice advances to the next shape");
  inspector.activateField("visible", &document, &selection);
  testTrue(
    counters, !document.findNode(id)->visible, "a toggle flips immediately");
  return counters.failures;
}

static int
testInspectorInvalidAndEscape()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 300.0f, 240.0f, 400.0f));
  InputManager input(nullptr);
  EditorDocument document;
  EditorSelection selection;
  const std::string id = cube(document, Vector3(2.0f, 0.0f, 0.0f));
  selection.set(id);
  inspector.update(&input, &document, &selection, 0.016f);
  const size_t commands = document.history().size();
  inspector.activateField("position.x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "abc");
  testTrue(counters,
           inspector.editing() && inspector.invalidInput(),
           "non-numeric text stays in the field marked invalid");
  testTrue(counters,
           document.findNode(id)->transform.position.x == 2.0f &&
             document.history().size() == commands,
           "invalid text changes nothing");
  inspector.activateField("scale.x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "0");
  testTrue(counters,
           inspector.editing() && inspector.invalidInput() &&
             document.findNode(id)->transform.scale.x == 1.0f,
           "a value the scene rejects (zero scale) stays invalid");
  input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           !inspector.editing() && document.history().size() == commands,
           "Escape cancels without an edit");
  inspector.activateField("position.y", &document, &selection);
  input.getKeyQueue().push({ KeyCode::Delete, InputAction::Press, 0 });
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           document.findNode(id) != nullptr && input.getKeyQueue().empty(),
           "Delete while typing edits the field, never the scene");
  return counters.failures;
}

static int
testInspectorMixedMultiValue()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 300.0f, 240.0f, 400.0f));
  InputManager input(nullptr);
  EditorDocument document;
  EditorSelection selection;
  const std::string a = cube(document, Vector3(1.0f, 5.0f, 0.0f));
  const std::string b = cube(document, Vector3(3.0f, 5.0f, 0.0f));
  selection.set(std::vector<std::string>{ a, b });
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("position.x")->mixed &&
             !inspector.field("position.y")->mixed,
           "differing values are mixed, shared values are not");
  const size_t commands = document.history().size();
  inspector.activateField("position.x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "7");
  testTrue(counters,
           document.findNode(a)->transform.position.x == 7.0f &&
             document.findNode(b)->transform.position.x == 7.0f,
           "one entry sets every selected node");
  testEqSize(counters,
             document.history().size(),
             commands + 1,
             "a multi-node edit is one command");
  for (int step = 0; step < 10; ++step) {
    inspector.scrubForTesting("position.y", 20.0f, &document, &selection);
  }
  testTrue(
    counters,
    std::fabs(document.findNode(a)->transform.position.y - 15.0f) < 1e-3f &&
      std::fabs(document.findNode(b)->transform.position.y - 15.0f) < 1e-3f,
    "scrubbing moves every node by the same amount");
  testEqSize(counters,
             document.history().size(),
             commands + 2,
             "one scrub is one command");
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("name")->kind == InspectorFieldKind::ReadOnly,
           "names are not bulk-edited");
  return counters.failures;
}

static int
testInspectorComponentsAndScene()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 300.0f, 240.0f, 400.0f));
  InputManager input(nullptr);
  EditorDocument document;
  EditorSelection selection;
  const std::string id = document.createPrimitive(
    true, ScenePrimitiveShape::Cube, {}, Transform3D{});
  selection.set(id);
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("component.add.light") != nullptr &&
             inspector.field("primitive.shape") == nullptr,
           "an empty node offers components to add");
  inspector.activateField("component.add.light", &document, &selection);
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           document.findNode(id)->find(SceneComponentType::Light) != nullptr &&
             inspector.field("light.intensity") != nullptr,
           "adding a light shows its fields");
  inspector.activateField("light.intensity", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "2.5");
  const SceneComponent* light =
    document.findNode(id)->find(SceneComponentType::Light);
  testTrue(counters,
           std::get<SceneLight>(light->value).intensity == 2.5f,
           "light intensity edit");
  inspector.activateField("component.remove.light", &document, &selection);
  testTrue(counters,
           document.findNode(id)->find(SceneComponentType::Light) == nullptr,
           "removing the light");
  document.undo();
  testTrue(counters,
           document.findNode(id)->find(SceneComponentType::Light) != nullptr,
           "component removal undoes");

  SceneDocument opaque;
  SceneNode node;
  node.id = "o";
  SceneComponent physics;
  physics.value = SceneOpaqueComponent{ "studio.physics", R"({"mass":2})" };
  node.components.push_back(physics);
  opaque.nodes.push_back(node);
  document.loadFromText(IlscCodec::encode(opaque), nullptr);
  selection.set("o");
  inspector.update(&input, &document, &selection, 0.016f);
  const InspectorField* data = inspector.field("opaque.studio.physics");
  testTrue(counters,
           data != nullptr && data->kind == InspectorFieldKind::ReadOnly &&
             data->value == R"({"mass":2})",
           "namespaced components are shown read-only");

  selection.clear();
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("env.ambient.r") != nullptr &&
             inspector.field("scene.mode") != nullptr,
           "with nothing selected the scene environment is inspected");
  inspector.activateField("env.ambient.g", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "0.75");
  testTrue(counters,
           document.scene().document().environment.ambient.y == 0.75f,
           "environment edits apply");
  document.undo();
  testTrue(counters,
           document.scene().document().environment.ambient.y == 0.27f,
           "environment edits undo");
  inspector.activateField("scene.mode", &document, &selection);
  testTrue(counters,
           document.worldMode() == SceneWorldMode::World3D,
           "the mode choice switches the world");
  return counters.failures;
}

static SceneAsset
asset(const std::string& id,
      SceneAssetType type,
      const std::string& path,
      int columns = 1,
      int rows = 1)
{
  SceneAsset entry;
  entry.id = id;
  entry.type = type;
  entry.path = path;
  entry.columns = columns;
  entry.rows = rows;
  return entry;
}

// A texture, a 4x2 atlas, a mesh and a cubemap cross, with a sprite node "s"
// on the texture, an atlas-cell sprite "c" and an empty node "e".
static void
loadAssetScene(EditorDocument& document)
{
  SceneDocument scene;
  scene.assets.push_back(
    asset("tex", SceneAssetType::Texture, "textures/a.png"));
  scene.assets.push_back(
    asset("atlas", SceneAssetType::Atlas, "textures/b.png", 4, 2));
  scene.assets.push_back(asset("mesh", SceneAssetType::Mesh, "meshes/m.obj"));
  scene.assets.push_back(
    asset("sky", SceneAssetType::CubemapCross, "textures/sky.png"));
  SceneNode spriteNode;
  spriteNode.id = "s";
  spriteNode.name = "Sprite";
  SceneSprite sprite;
  sprite.texture = "tex";
  SceneComponent spriteComponent;
  spriteComponent.value = sprite;
  spriteNode.components.push_back(spriteComponent);
  scene.nodes.push_back(spriteNode);
  SceneNode cellNode;
  cellNode.id = "c";
  cellNode.name = "Cell";
  sprite.texture = "atlas";
  sprite.hasCell = true;
  sprite.column = 3;
  spriteComponent.value = sprite;
  cellNode.components.push_back(spriteComponent);
  scene.nodes.push_back(cellNode);
  SceneNode empty;
  empty.id = "e";
  empty.name = "Empty";
  scene.nodes.push_back(empty);
  std::string error;
  document.loadFromText(IlscCodec::encode(scene), &error);
}

static const SceneSprite&
spriteOf(EditorDocument& document, const std::string& id)
{
  return std::get<SceneSprite>(
    document.findNode(id)->find(SceneComponentType::Sprite)->value);
}

static int
testInspectorSpriteSourceTagsAndAssets()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 300.0f, 240.0f, 400.0f));
  InputManager input(nullptr);
  EditorDocument document;
  EditorSelection selection;
  loadAssetScene(document);
  testEqSize(counters, document.nodeCount(), 3, "the asset scene loads");
  selection.set("s");
  inspector.update(&input, &document, &selection, 0.016f);
  const InspectorField* texture = inspector.field("sprite.texture");
  testTrue(counters,
           texture != nullptr && texture->kind == InspectorFieldKind::Choice &&
             texture->choices == std::vector<std::string>({ "tex", "atlas" }) &&
             texture->choice == 0,
           "a sprite picks among texture and atlas assets");
  testTrue(counters,
           inspector.field("sprite.region.u1") != nullptr &&
             inspector.field("sprite.cell.column") == nullptr,
           "a region sprite shows its region");
  inspector.activateField("sprite.region.u1", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "0.5");
  testTrue(counters, spriteOf(document, "s").u1 == 0.5f, "region edit");
  inspector.activateField("sprite.region.v0", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "2");
  testTrue(counters,
           inspector.invalidInput() && spriteOf(document, "s").v0 == 0.0f,
           "a region outside [0, 1] is refused");
  input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  inspector.update(&input, &document, &selection, 0.016f);

  inspector.activateField("sprite.source", &document, &selection);
  testTrue(counters,
           !spriteOf(document, "s").hasCell,
           "a plain texture has no cells to switch to");
  inspector.activateField("sprite.texture", &document, &selection);
  inspector.activateField("sprite.source", &document, &selection);
  testTrue(counters,
           spriteOf(document, "s").texture == "atlas" &&
             spriteOf(document, "s").hasCell,
           "an atlas sprite switches to a cell");
  inspector.update(&input, &document, &selection, 0.016f);
  const InspectorField* column = inspector.field("sprite.cell.column");
  testTrue(counters,
           column != nullptr && column->integer,
           "the cell column is a whole number");
  const size_t commands = document.history().size();
  inspector.scrubForTesting("sprite.cell.column", 15.0f, &document, &selection);
  inspector.scrubForTesting("sprite.cell.column", 5.0f, &document, &selection);
  testEqInt(counters,
            spriteOf(document, "s").column,
            2,
            "integer scrubs carry fractions between frames");
  testEqSize(counters,
             document.history().size(),
             commands + 1,
             "one integer scrub is one command");
  inspector.activateField("sprite.cell.column", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "9");
  testTrue(counters,
           inspector.invalidInput() && spriteOf(document, "s").column == 2,
           "a cell outside the atlas grid is refused");
  input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  inspector.update(&input, &document, &selection, 0.016f);
  inspector.activateField("sprite.texture", &document, &selection);
  testTrue(counters,
           spriteOf(document, "s").texture == "tex" &&
             !spriteOf(document, "s").hasCell,
           "picking a plain texture returns the sprite to its region");

  inspector.activateField("tags", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "hero, enemy ,, hero");
  testTrue(counters,
           document.findNode("s")->tags ==
             std::vector<std::string>({ "hero", "enemy" }),
           "tags are typed comma separated, trimmed and deduplicated");
  selection.set(std::vector<std::string>{ "s", "e" });
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("tags")->mixed,
           "differing tags across the selection are mixed");

  selection.set("e");
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("component.add.mesh") != nullptr &&
             inspector.field("component.add.sprite") != nullptr,
           "with mesh and image assets a node offers Mesh and Sprite");
  inspector.activateField("component.add.mesh", &document, &selection);
  const SceneComponent* mesh =
    document.findNode("e")->find(SceneComponentType::Mesh);
  testTrue(counters,
           mesh != nullptr &&
             std::get<SceneMeshRenderer>(mesh->value).asset == "mesh",
           "Add Mesh starts on the first mesh asset");
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("mesh.asset") != nullptr &&
             inspector.field("mesh.asset")->kind ==
               InspectorFieldKind::Choice &&
             inspector.field("component.add.mesh") == nullptr,
           "the mesh asset is a choice and Mesh is no longer offered");
  return counters.failures;
}

static int
testInspectorSceneAssetsSection()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 300.0f, 240.0f, 400.0f));
  InputManager input(nullptr);
  EditorDocument document;
  EditorSelection selection;
  loadAssetScene(document);
  inspector.update(&input, &document, &selection, 0.016f);

  inspector.activateField("meta.description", &document, &selection);
  const std::string description(600, 'd');
  typeAndFinish(inspector, input, document, selection, description);
  testEqStr(counters,
            document.scene().document().metadata.description,
            description,
            "descriptions are longer than one-line fields");

  const InspectorField* skybox = inspector.field("env.skybox");
  testTrue(counters,
           skybox != nullptr && skybox->kind == InspectorFieldKind::Choice &&
             skybox->choices == std::vector<std::string>({ "(none)", "sky" }) &&
             inspector.field("env.tint.r") == nullptr,
           "the skybox picks among cubemaps, and no sky has no tint");
  inspector.activateField("env.skybox", &document, &selection);
  testEqStr(counters,
            document.scene().document().environment.skybox,
            "sky",
            "a cubemap becomes the skybox");
  inspector.update(&input, &document, &selection, 0.016f);
  inspector.activateField("env.tint.g", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "0.5");
  testTrue(counters,
           document.scene().document().environment.skyboxTint.y == 0.5f,
           "the sky tint is editable");

  testTrue(counters,
           inspector.field("asset.type:tex") != nullptr &&
             inspector.field("asset.type:mesh")->kind ==
               InspectorFieldKind::ReadOnly &&
             inspector.field("asset.radius:mesh") != nullptr &&
             !inspector.field("asset.radius:mesh")->scrub,
           "every asset lists its options");
  testTrue(counters,
           inspector.field("asset.remove:mesh") != nullptr &&
             inspector.field("asset.remove:tex") == nullptr &&
             inspector.field("asset.remove:sky") == nullptr,
           "only unreferenced assets offer Remove");

  inspector.activateField("asset.filter:tex", &document, &selection);
  inspector.activateField("asset.mipmaps:tex", &document, &selection);
  const SceneAsset* tex = document.scene().document().findAsset("tex");
  testTrue(counters,
           tex->texture.filter == SceneTextureFilter::Nearest &&
             tex->texture.mipmaps,
           "texture sampling options apply");
  inspector.activateField("asset.type:tex", &document, &selection);
  testTrue(counters,
           document.scene().document().findAsset("tex")->type ==
             SceneAssetType::Atlas,
           "an image becomes an atlas");
  inspector.update(&input, &document, &selection, 0.016f);
  inspector.activateField("asset.columns:tex", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "3");
  testEqInt(counters,
            document.scene().document().findAsset("tex")->columns,
            3,
            "atlas columns apply");
  inspector.activateField("asset.columns:atlas", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "2");
  testTrue(counters,
           inspector.invalidInput() &&
             document.scene().document().findAsset("atlas")->columns == 4,
           "a grid that would orphan a sprite cell is refused");
  input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  inspector.update(&input, &document, &selection, 0.016f);
  inspector.activateField("asset.type:sky", &document, &selection);
  testTrue(counters,
           document.scene().document().findAsset("sky")->type ==
             SceneAssetType::CubemapCross,
           "the skybox cannot stop being a cubemap");

  const size_t assets = document.scene().document().assets.size();
  inspector.activateField("asset.remove:mesh", &document, &selection);
  testEqSize(counters,
             document.scene().document().assets.size(),
             assets - 1,
             "an unused asset is removed");
  document.undo();
  testTrue(counters,
           document.scene().document().findAsset("mesh") != nullptr,
           "asset removal undoes");
  return counters.failures;
}

// Arithmetic and relative edits, Ctrl+wheel steps, folding and the
// right-click menu (reset, copy and paste, remove).
static int
testInspectorTweaks()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 300.0f, 240.0f, 400.0f));
  InputManager input(nullptr);
  EditorDocument document;
  EditorSelection selection;
  const std::string a = cube(document, Vector3(1.0f, 0.0f, 0.0f));
  const std::string b = cube(document, Vector3(3.0f, 0.0f, 0.0f));
  selection.set(a);
  inspector.update(&input, &document, &selection, 0.016f);
  const std::function<float()> x = [&]() {
    return document.findNode(a)->transform.position.x;
  };
  inspector.activateField("position.x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "2*(3+1)");
  testTrue(counters, x() == 8.0f, "typed arithmetic is evaluated");
  inspector.activateField("position.x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "+=2");
  inspector.activateField("position.x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "*=0.5");
  testTrue(counters, x() == 5.0f, "relative edits apply to the value");
  inspector.activateField("position.x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "/=0");
  testTrue(counters,
           inspector.invalidInput() && x() == 5.0f,
           "a division by zero is refused");
  input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  inspector.update(&input, &document, &selection, 0.016f);
  selection.set(std::vector<std::string>{ b, a });
  inspector.activateField("position.x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "-=1");
  testTrue(counters,
           x() == 4.0f && document.findNode(b)->transform.position.x == 2.0f,
           "a relative edit moves each selected node from its own value");

  // Ctrl+wheel over a number steps it by ten scrub steps.
  selection.set(a);
  inspector.update(&input, &document, &selection, 0.016f);
  const InspectorField* field = inspector.field("position.y");
  fixture.window.mouseX = field->x + field->width * 0.5f;
  fixture.window.mouseY = field->y + 6.0f;
  inspector.update(&input, &document, &selection, 0.016f);
  InputManagerTestAccess::setModifierFlags(input, 0x2);
  *input.getMouseScrollOffset() = 2.0;
  const size_t commands = document.history().size();
  inspector.update(&input, &document, &selection, 0.016f);
  InputManagerTestAccess::setModifierFlags(input, 0);
  testTrue(counters,
           std::fabs(document.findNode(a)->transform.position.y - 1.0f) <
               1.0e-4f &&
             document.history().size() == commands + 1,
           "Ctrl+wheel steps the number under the pointer");

  // Folding a section hides its fields until unfolded.
  inspector.toggleSectionForTesting("Transform");
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.sectionFolded("Transform") &&
             inspector.field("position.x")->hidden &&
             !inspector.field("primitive.shape")->hidden,
           "a folded section hides only its own fields");
  inspector.toggleSectionForTesting("Transform");
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           !inspector.field("position.x")->hidden,
           "unfolding shows them again");

  // The right-click menu.
  testTrue(
    counters,
    inspector.openMenuForTesting("position.x", {}, &document, &selection) &&
      inspector.menuLabelsForTesting() ==
        std::vector<std::string>{ "Reset Position",
                                  "Reset Transform",
                                  "Copy Transform",
                                  "Paste Transform Values" },
    "a field's menu resets its row or its section");
  inspector.chooseMenuForTesting("Reset Position", &document, &selection);
  testTrue(counters,
           document.findNode(a)->transform.position == Vector3(0.0f),
           "Reset Position zeroes the row");
  document.setColor(a, ColorRgba{ 10, 20, 30, 255 });
  inspector.openMenuForTesting({}, "Primitive", &document, &selection);
  inspector.chooseMenuForTesting("Copy Primitive", &document, &selection);
  selection.set(b);
  inspector.openMenuForTesting({}, "Primitive", &document, &selection);
  inspector.chooseMenuForTesting(
    "Paste Primitive Values", &document, &selection);
  const SceneComponent* pasted =
    document.findNode(b)->find(SceneComponentType::Primitive);
  testTrue(counters,
           std::get<ScenePrimitive>(pasted->value).color.r == 10,
           "copied component values paste onto another node");
  inspector.openMenuForTesting({}, "Primitive", &document, &selection);
  inspector.chooseMenuForTesting("Reset Primitive", &document, &selection);
  const SceneComponent* reset =
    document.findNode(b)->find(SceneComponentType::Primitive);
  testTrue(counters,
           std::get<ScenePrimitive>(reset->value).color.r ==
               ScenePrimitive{}.color.r &&
             std::get<ScenePrimitive>(reset->value).shape ==
               ScenePrimitiveShape::Cube,
           "Reset keeps the shape and restores the defaults");
  inspector.openMenuForTesting({}, "Primitive", &document, &selection);
  inspector.chooseMenuForTesting("Remove Primitive", &document, &selection);
  testTrue(counters,
           document.findNode(b)->find(SceneComponentType::Primitive) == nullptr,
           "Remove drops the component");
  return counters.failures;
}

// Color rows carry a swatch that opens a picker; picking writes the row's
// channels (bytes or 0-1 floats) as one command per open picker.
static int
testInspectorColorPicker()
{
  TestCounters counters;
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 100.0f, 240.0f, 600.0f));
  InputManager input(nullptr);
  EditorDocument document;
  EditorSelection selection;
  const std::string id = cube(document);
  selection.set(id);
  inspector.update(&input, &document, &selection, 0.016f);
  const InspectorField* swatch = inspector.field("swatch:primitive.color.r");
  testTrue(counters,
           swatch != nullptr && swatch->kind == InspectorFieldKind::Swatch &&
             swatch->channels.size() == 4,
           "a color row starts with a swatch for its channels");
  inspector.activateField("swatch:primitive.color.r", &document, &selection);
  testTrue(counters, inspector.pickerOpen(), "clicking the swatch opens it");
  const size_t commands = document.history().size();
  inspector.pickForTesting(0.0f, 1.0f, 1.0f, 0.5f, &document, &selection);
  inspector.pickForTesting(0.0f, 1.0f, 0.5f, 0.5f, &document, &selection);
  const ColorRgba color =
    std::get<ScenePrimitive>(
      document.findNode(id)->find(SceneComponentType::Primitive)->value)
      .color;
  testTrue(counters,
           color.r == 128 && color.g == 0 && color.b == 0 && color.a == 128 &&
             document.history().size() == commands + 1,
           "picking writes the color, one command per open picker");
  testTrue(
    counters,
    inspector.typeHexForTesting("#00FF00FF", &document, &selection) &&
      std::get<ScenePrimitive>(
        document.findNode(id)->find(SceneComponentType::Primitive)->value)
          .color.g == 255,
    "a hex color applies");
  testTrue(counters,
           !inspector.typeHexForTesting("#12345", &document, &selection),
           "a malformed hex color is refused");
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("swatch:primitive.color.r")->swatch.g == 255,
           "the swatch shows the new color");

  // Clicking outside the picker closes it.
  const InspectorField* name = inspector.field("name");
  fixture.window.mouseX = name->x + 4.0f;
  fixture.window.mouseY = name->y + 4.0f;
  inspector.update(&input, &document, &selection, 0.016f);
  InputManagerTestAccess::setAction(
    input, KeyCode::MouseLeft, InputAction::Press);
  inspector.update(&input, &document, &selection, 0.016f);
  InputManagerTestAccess::setAction(
    input, KeyCode::MouseLeft, InputAction::Release);
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters, !inspector.pickerOpen(), "a press elsewhere closes it");

  // Light colors are 0-1 floats.
  SceneNode light;
  light.name = "Lamp";
  SceneComponent lamp;
  lamp.value = SceneLight{};
  light.components.push_back(lamp);
  const std::string lightId = document.createNode(light, {});
  selection.set(lightId);
  inspector.update(&input, &document, &selection, 0.016f);
  inspector.activateField("swatch:light.color.r", &document, &selection);
  inspector.pickForTesting(
    1.0f / 3.0f, 1.0f, 1.0f, 1.0f, &document, &selection);
  const Vector3 lit =
    std::get<SceneLight>(
      document.findNode(lightId)->find(SceneComponentType::Light)->value)
      .color;
  testTrue(counters,
           std::fabs(lit.x) < 1.0e-3f && std::fabs(lit.y - 1.0f) < 1.0e-3f &&
             std::fabs(lit.z) < 1.0e-3f,
           "float channels take 0-1 values");
  return counters.failures;
}

void
registerEditorInspectorTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Inspector.ColorPicker",
               []() { return testInspectorColorPicker(); });
  registry.add("IllEd.Inspector.Tweaks",
               []() { return testInspectorTweaks(); });
  registry.add("IllEd.Inspector.SpriteSourceTagsAndAssets",
               []() { return testInspectorSpriteSourceTagsAndAssets(); });
  registry.add("IllEd.Inspector.SceneAssetsSection",
               []() { return testInspectorSceneAssetsSection(); });
  registry.add("IllEd.Inspector.CommitCreatesCommand",
               []() { return testInspectorCommitCreatesCommand(); });
  registry.add("IllEd.Inspector.InvalidAndEscape",
               []() { return testInspectorInvalidAndEscape(); });
  registry.add("IllEd.Inspector.MixedMultiValue",
               []() { return testInspectorMixedMultiValue(); });
  registry.add("IllEd.Inspector.ComponentsAndScene",
               []() { return testInspectorComponentsAndScene(); });
}