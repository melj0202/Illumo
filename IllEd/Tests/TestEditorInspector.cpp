#include "EditorInspector.h"
#include "PanelTestHelpers.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Services/InputManager.h>
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

void
registerEditorInspectorTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Inspector.CommitCreatesCommand",
               []() { return testInspectorCommitCreatesCommand(); });
  registry.add("IllEd.Inspector.InvalidAndEscape",
               []() { return testInspectorInvalidAndEscape(); });
  registry.add("IllEd.Inspector.MixedMultiValue",
               []() { return testInspectorMixedMultiValue(); });
  registry.add("IllEd.Inspector.ComponentsAndScene",
               []() { return testInspectorComponentsAndScene(); });
}