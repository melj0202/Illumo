#include "EditorBehaviours.h"
#include "EditorInspector.h"
#include "IllEdPlatform.h"
#include "PanelTestHelpers.h"
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>
#include <fstream>

// A behaviours.json with every field kind.
static const char* const kSchema = R"({
  "format": "illumo-behaviours",
  "format_version": 1,
  "behaviours": [
    { "type": "test.mover", "title": "Mover",
      "fields": [
        { "name": "speed", "title": "Speed", "kind": "number",
          "default": 2, "min": 0, "max": 10 },
        { "name": "count", "kind": "integer", "default": 3 },
        { "name": "active", "kind": "bool", "default": true },
        { "name": "label", "kind": "text", "default": "go" },
        { "name": "tint", "kind": "color", "default": [255, 0, 0, 255] },
        { "name": "axis", "kind": "vector3", "default": [0, 1, 0] },
        { "name": "mode", "kind": "choice", "options": ["walk", "run"] },
        { "name": "texture", "kind": "asset" },
        { "name": "target", "kind": "node" } ] },
    { "type": "test.idle", "title": "Idle", "fields": [] }
  ]
})";

static std::string
cube(EditorDocument& document, const std::string& name = {})
{
  const std::string id = document.createPrimitive(
    false, ScenePrimitiveShape::Cube, {}, Transform3D::fromPosition({}));
  if (!name.empty()) {
    document.setName(id, name);
  }
  return id;
}

static std::string
dataOf(const EditorDocument& document,
       const std::string& id,
       const std::string& type)
{
  const SceneNode* node = document.findNode(id);
  if (node == nullptr) {
    return {};
  }
  for (const SceneComponent& component : node->components) {
    const SceneOpaqueComponent* opaque =
      std::get_if<SceneOpaqueComponent>(&component.value);
    if (opaque != nullptr && opaque->type == type) {
      return opaque->data;
    }
  }
  return {};
}

static BehaviourValues
valuesOf(const EditorBehaviours& behaviours,
         const EditorDocument& document,
         const std::string& id)
{
  return BehaviourSchema::decode(*behaviours.schema().find("test.mover"),
                                 dataOf(document, id, "test.mover"),
                                 nullptr);
}

static void
typeAndFinish(EditorInspector& inspector,
              InputManager& input,
              EditorDocument& document,
              EditorSelection& selection,
              const std::string& text)
{
  for (const char character : text) {
    input.getCharQueue().push(static_cast<unsigned char>(character));
  }
  inspector.update(&input, &document, &selection, 0.016f);
  input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  inspector.update(&input, &document, &selection, 0.016f);
}

static int
testBehaviourInspectorFields()
{
  TestCounters counters;
  EditorBehaviours behaviours;
  testTrue(counters,
           behaviours.addSource("/apps/test/behaviours.json", "test", kSchema),
           "the test schema is known");
  HeadlessRenderFixture fixture(1280, 720);
  EditorInspector inspector(&fixture.window, &fixture.renderer);
  inspector.setPlacement(panelArea(1040.0f, 300.0f, 240.0f, 400.0f));
  inspector.setBehaviours(&behaviours);
  InputManager input(nullptr);
  EditorDocument document;
  document.setBehaviourSchema(&behaviours.schema());
  EditorSelection selection;
  const std::string id = cube(document, "Hero");
  const std::string other = cube(document, "Goal");
  selection.set(id);
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("behaviour.add:test.mover") != nullptr &&
             inspector.field("behaviour.add:test.idle") != nullptr,
           "Add behaviour lists every known behaviour");
  const size_t commands = document.history().size();
  inspector.activateField("behaviour.add:test.mover", &document, &selection);
  testTrue(counters,
           document.history().size() == commands + 1 &&
             valuesOf(behaviours, document, id).number("speed") == 2.0,
           "adding a behaviour is one command with every default");
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("behaviour.add:test.mover") == nullptr &&
             inspector.field("behaviour:test.mover:speed") != nullptr &&
             inspector.field("behaviour:test.mover:speed")->value == "2" &&
             inspector.field("behaviour:test.mover:axis:y") != nullptr &&
             inspector.field("behaviour:test.mover:tint:a") != nullptr,
           "a held behaviour shows typed fields and leaves the add list");

  inspector.activateField("behaviour:test.mover:speed", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "4.5");
  testTrue(counters,
           valuesOf(behaviours, document, id).number("speed") == 4.5,
           "typing a number sets the field");
  inspector.activateField("behaviour:test.mover:speed", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "99");
  testTrue(counters,
           valuesOf(behaviours, document, id).number("speed") == 10.0,
           "numbers clamp to the field's range");
  inspector.activateField("behaviour:test.mover:axis:x", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "3");
  testTrue(counters,
           valuesOf(behaviours, document, id).vector("axis").x == 3.0f,
           "a vector part is edited on its own");
  inspector.activateField("behaviour:test.mover:tint:g", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "300");
  testTrue(counters,
           valuesOf(behaviours, document, id).color("tint").g == 255,
           "color channels clamp to a byte");
  inspector.activateField("behaviour:test.mover:label", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "jump");
  testEqStr(counters,
            valuesOf(behaviours, document, id).text("label"),
            "jump",
            "text fields take typed text");
  inspector.activateField("behaviour:test.mover:active", &document, &selection);
  testTrue(counters,
           !valuesOf(behaviours, document, id).flag("active"),
           "a bool field toggles");
  inspector.activateField("behaviour:test.mover:mode", &document, &selection);
  testEqStr(counters,
            valuesOf(behaviours, document, id).text("mode"),
            "run",
            "a choice advances to the next option");
  inspector.update(&input, &document, &selection, 0.016f);
  const InspectorField* target = inspector.field("behaviour:test.mover:target");
  testTrue(counters,
           target != nullptr && target->choices.size() == 3 &&
             target->choices[0] == "(none)" && target->value == "(none)",
           "a node field offers (none) and every node");
  inspector.activateField("behaviour:test.mover:target", &document, &selection);
  testEqStr(counters,
            valuesOf(behaviours, document, id).text("target"),
            id,
            "a node field names the chosen node");
  testTrue(counters,
           dataOf(document, id, "test.mover").find("\"speed\":10") !=
             std::string::npos,
           "data stays canonical compact JSON");

  // Two nodes, the primary (last) holding the behaviour: the other node
  // edits nothing.
  selection.set(std::vector<std::string>{ other, id });
  inspector.activateField("behaviour:test.mover:speed", &document, &selection);
  typeAndFinish(inspector, input, document, selection, "1");
  testTrue(counters,
           valuesOf(behaviours, document, id).number("speed") == 1.0 &&
             dataOf(document, other, "test.mover").empty(),
           "an edit reaches the selected nodes that hold the behaviour");

  selection.set(id);
  inspector.update(&input, &document, &selection, 0.016f);
  inspector.activateField("behaviour.remove:test.mover", &document, &selection);
  testTrue(counters,
           dataOf(document, id, "test.mover").empty(),
           "Remove drops the behaviour");
  document.undo();
  testTrue(counters,
           valuesOf(behaviours, document, id).number("speed") == 1.0,
           "removing undoes with the values intact");

  // Without the description the component shows its JSON.
  inspector.setBehaviours(nullptr);
  inspector.update(&input, &document, &selection, 0.016f);
  testTrue(counters,
           inspector.field("opaque.test.mover") != nullptr &&
             inspector.field("behaviour:test.mover:speed") == nullptr,
           "an unknown behaviour stays read-only JSON");
  return counters.failures;
}

static int
testBehaviourCopiesRemap()
{
  TestCounters counters;
  EditorBehaviours behaviours;
  behaviours.addSource("/apps/test/behaviours.json", "test", kSchema);
  const BehaviourType& mover = *behaviours.schema().find("test.mover");
  EditorDocument document;
  document.setBehaviourSchema(&behaviours.schema());
  const std::string beacon = cube(document, "Beacon");
  const std::string moon = cube(document, "Moon");
  const std::string outside = cube(document, "Outside");
  document.setParent(moon, beacon);
  const std::function<void(const std::string&, const std::string&)> aim =
    [&](const std::string& id, const std::string& target) {
      document.editNodes({ id }, "Aim", {}, [&](SceneNode& node) {
        SceneOpaqueComponent component =
          BehaviourSchema::defaultComponent(mover);
        BehaviourValues values =
          BehaviourSchema::decode(mover, component.data, nullptr);
        BehaviourValue value = *values.find("target");
        value.text = target;
        values.set("target", value);
        component.data = BehaviourSchema::encode(mover, values);
        SceneComponent added;
        added.value = component;
        node.components.push_back(added);
        return true;
      });
    };
  aim(moon, beacon);
  aim(beacon, outside);

  const std::vector<std::string> copies = document.duplicate({ beacon });
  testTrue(counters, copies.size() == 1, "the subtree duplicates");
  const std::string beaconCopy = copies.empty() ? std::string() : copies[0];
  std::string moonCopy;
  for (const std::string& id : document.scene().subtreeIds(beaconCopy)) {
    if (id != beaconCopy) {
      moonCopy = id;
    }
  }
  testEqStr(counters,
            valuesOf(behaviours, document, moonCopy).text("target"),
            beaconCopy,
            "a reference inside the copied subtree follows the copy");
  testEqStr(counters,
            valuesOf(behaviours, document, beaconCopy).text("target"),
            outside,
            "a reference outside the subtree stays");
  testEqStr(counters,
            valuesOf(behaviours, document, moon).text("target"),
            beacon,
            "the original is untouched");
  document.undo();
  testTrue(counters,
           document.findNode(beaconCopy) == nullptr,
           "the duplicate with its remap is one command");

  // A pasted fragment: the child names its parent, which is listed first,
  // and a texture reference follows an asset renamed on paste.
  SceneDocument fragment;
  SceneAsset texture;
  texture.id = "tex";
  texture.type = SceneAssetType::Texture;
  texture.path = "a.png";
  fragment.assets.push_back(texture);
  SceneAsset taken = texture;
  taken.path = "b.png";
  document.setAssets({ taken }, "Assets");
  SceneNode parent;
  parent.id = "p";
  parent.name = "Parent";
  SceneNode child;
  child.id = "c";
  child.name = "Child";
  child.parentId = "p";
  SceneOpaqueComponent component = BehaviourSchema::defaultComponent(mover);
  BehaviourValues values =
    BehaviourSchema::decode(mover, component.data, nullptr);
  BehaviourValue target = *values.find("target");
  target.text = "p";
  values.set("target", target);
  BehaviourValue image = *values.find("texture");
  image.text = "tex";
  values.set("texture", image);
  component.data = BehaviourSchema::encode(mover, values);
  SceneComponent behaviour;
  behaviour.value = component;
  child.components.push_back(behaviour);
  fragment.nodes = { parent, child };
  const std::vector<std::string> roots = document.paste(fragment, {});
  testTrue(counters, roots.size() == 1, "the fragment pastes");
  std::string pastedChild;
  for (const std::string& id :
       document.scene().subtreeIds(roots.empty() ? std::string() : roots[0])) {
    if (!roots.empty() && id != roots[0]) {
      pastedChild = id;
    }
  }
  const BehaviourValues pasted = valuesOf(behaviours, document, pastedChild);
  testTrue(counters,
           !roots.empty() && pasted.text("target") == roots[0] &&
             pasted.text("texture") == "tex_2",
           "pasted node and asset references follow their new ids");
  return counters.failures;
}

static void
writeText(const std::filesystem::path& path, const std::string& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

static int
testBehaviourDiscovery()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illed-behaviour-discovery";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  writeText(root / "game" / "behaviours.json", kSchema);
  writeText(root / "viewer" / "illumo.json", "{}");
  writeText(root / "extra" / "behaviours.json",
            R"({"format":"illumo-behaviours","format_version":1,
                "behaviours":[{"type":"extra.glow","fields":[]},
                              {"type":"test.idle","fields":[]}]})");
  writeText(root / "broken" / "behaviours.json", "{not json");
  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  const std::function<void(const std::string&, const std::string&)> mount =
    [&](const std::string& point, const std::string& directory) {
      VfsMount entry;
      entry.point = point;
      entry.layers.push_back(
        { DirectoryVfsBackend::open(root / directory, false, error),
          directory });
      tree->mount(entry, error);
    };
  mount("/apps/game", "game");
  mount("/apps/viewer", "viewer");
  mount("/packages/extra", "extra");
  mount("/packages/broken", "broken");
  IllEdNativeTree::install(tree);

  EditorBehaviours behaviours;
  const std::uint64_t before = behaviours.revision();
  behaviours.discover();
  testTrue(counters,
           !behaviours.pending() && behaviours.revision() != before,
           "discovery finishes and announces a new revision");
  testTrue(counters,
           behaviours.schema().find("test.mover") != nullptr &&
             behaviours.schema().find("extra.glow") != nullptr,
           "applications and packages contribute behaviours");
  testTrue(counters,
           behaviours.games() == std::vector<std::string>{ "game" },
           "only applications with a behaviours.json are games");
  testEqStr(counters,
            behaviours.gameFor("test.mover"),
            "game",
            "a behaviour names the game that describes it");
  testEqStr(counters,
            behaviours.gameFor("extra.glow"),
            "",
            "a package's behaviour belongs to no game");
  testTrue(counters,
           behaviours.schema().types().size() == 3,
           "a type described twice keeps the application's description and "
           "a broken file is ignored");
  IllEdNativeTree::install(nullptr);
  behaviours.discover();
  testTrue(counters,
           behaviours.schema().empty() && behaviours.games().empty(),
           "discovering again starts over");
  std::filesystem::remove_all(root, code);
  return counters.failures;
}

void
registerEditorBehaviourTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Behaviours.InspectorFields",
               []() { return testBehaviourInspectorFields(); });
  registry.add("IllEd.Behaviours.CopiesRemap",
               []() { return testBehaviourCopiesRemap(); });
  registry.add("IllEd.Behaviours.Discovery",
               []() { return testBehaviourDiscovery(); });
}
