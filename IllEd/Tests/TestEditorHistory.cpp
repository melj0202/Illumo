#include "EditorDocument.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <string>
#include <vector>

static std::string
cube(EditorDocument& document,
     const std::string& parent = {},
     const Vector3& position = Vector3(0.0f))
{
  return document.createPrimitive(false,
                                  ScenePrimitiveShape::Cube,
                                  parent,
                                  Transform3D::fromPosition(position));
}

// Canonical text without the editor block: the scene content itself.
static std::string
snapshot(const EditorDocument& document)
{
  return IlscCodec::encode(document.scene().document(), false);
}

static int
testUndoRedoEveryCommand()
{
  TestCounters counters;
  EditorDocument document;
  std::vector<std::string> states{ snapshot(document) };
  const std::string a = cube(document);
  states.push_back(snapshot(document));
  const std::string b = cube(document, a, Vector3(1.0f, 0.0f, 0.0f));
  states.push_back(snapshot(document));
  const std::string c = cube(document, {}, Vector3(2.0f, 0.0f, 0.0f));
  states.push_back(snapshot(document));
  document.setName(b, "Child");
  states.push_back(snapshot(document));
  document.setColor(a, ColorRgba{ 1, 2, 3, 255 });
  states.push_back(snapshot(document));
  document.setVisible(c, false);
  states.push_back(snapshot(document));
  document.setEnabled(b, false);
  states.push_back(snapshot(document));
  document.setParent(c, a, b);
  states.push_back(snapshot(document));
  document.translate(std::vector<std::string>{ a, c }, Vector3(0, 3, 0));
  states.push_back(snapshot(document));
  const std::vector<std::string> copies = document.duplicate({ a });
  states.push_back(snapshot(document));
  document.setWorldMode(SceneWorldMode::World3D);
  states.push_back(snapshot(document));
  document.destroyNodes({ a });
  states.push_back(snapshot(document));
  testEqSize(counters, copies.size(), 1, "duplicate returns the new root");
  testEqSize(counters,
             document.history().size(),
             states.size() - 1,
             "every edit is one command");

  bool undoOk = true;
  for (size_t index = states.size() - 1; index > 0; --index) {
    undoOk = document.undo() && undoOk;
    if (snapshot(document) != states[index - 1]) {
      std::printf("FAIL: undo to state %zu differs\n", index - 1);
      ++counters.failures;
    }
  }
  testTrue(counters, undoOk && !document.history().canUndo(), "undo to start");
  bool redoOk = true;
  for (size_t index = 1; index < states.size(); ++index) {
    redoOk = document.redo() && redoOk;
    if (snapshot(document) != states[index]) {
      std::printf("FAIL: redo to state %zu differs\n", index);
      ++counters.failures;
    }
  }
  testTrue(counters, redoOk && !document.history().canRedo(), "redo to end");

  // An edit after undo discards the redo tail.
  document.undo();
  document.undo();
  cube(document);
  testTrue(counters, !document.history().canRedo(), "new edit clears redo");
  return counters.failures;
}

static int
testDeleteRestoresSiblingOrder()
{
  TestCounters counters;
  EditorDocument document;
  const std::string p = cube(document);
  const std::string x = cube(document, p);
  const std::string a = cube(document, p);
  const std::string b = cube(document, p);
  const std::string y = cube(document, p);
  const std::string before = snapshot(document);
  document.destroyNodes({ a, b });
  testEqSize(counters, document.scene().childIds(p).size(), 2, "two deleted");
  document.undo();
  const std::vector<std::string> children = document.scene().childIds(p);
  testTrue(counters,
           children.size() == 4 && children[0] == x && children[1] == a &&
             children[2] == b && children[3] == y,
           "adjacent deleted siblings return to their positions");
  testEqStr(counters, snapshot(document), before, "scene fully restored");
  return counters.failures;
}

static int
testDragMerges()
{
  TestCounters counters;
  EditorDocument document;
  const std::string a = cube(document);
  const size_t commands = document.history().size();
  for (int step = 0; step < 30; ++step) {
    document.translate(
      std::vector<std::string>{ a }, Vector3(0.1f, 0.0f, 0.0f), "drag:1");
  }
  testEqSize(counters,
             document.history().size(),
             commands + 1,
             "one drag is one command");
  document.translate(
    std::vector<std::string>{ a }, Vector3(1.0f, 0.0f, 0.0f), "drag:2");
  testEqSize(counters,
             document.history().size(),
             commands + 2,
             "a new drag starts a new command");
  document.undo();
  document.undo();
  testTrue(counters,
           std::abs(document.findNode(a)->transform.position.x) < 1e-5f,
           "undoing the merged drag returns to the start");
  testTrue(counters,
           !document.setTransform(a, document.findNode(a)->transform) ||
             document.history().size() == commands + 2,
           "an edit that changes nothing is not recorded");
  return counters.failures;
}

static int
testCapBytes()
{
  TestCounters counters;
  EditorDocument document;
  const std::string a = cube(document);
  for (size_t step = 0; step < EditorHistory::kMaximumCommands + 40; ++step) {
    document.translate(std::vector<std::string>{ a }, Vector3(1.0f, 0, 0));
  }
  testTrue(counters,
           document.history().size() <= EditorHistory::kMaximumCommands,
           "command count is bounded");
  testTrue(counters,
           document.history().bytes() <= EditorHistory::kMaximumBytes,
           "estimated bytes are bounded");
  size_t undone = 0;
  while (document.undo()) {
    ++undone;
  }
  testEqSize(counters,
             undone,
             document.history().size(),
             "every retained command undoes");
  testTrue(counters,
           document.findNode(a) != nullptr,
           "the oldest commands fell off, so the node survives full undo");
  return counters.failures;
}

static int
testDirtyTracksSavedCursor()
{
  TestCounters counters;
  EditorDocument document;
  testTrue(counters, !document.isDirty(), "a new document is clean");
  SceneEditorState view = document.editorState();
  view.cameraX = 25.0;
  view.zoom = 12.0f;
  document.setEditorState(view);
  testTrue(counters, !document.isDirty(), "camera state never dirties");
  const std::string a = cube(document);
  testTrue(counters, document.isDirty(), "an edit dirties");
  document.markSaved("scene.ilsc", "scene.ilsc");
  testTrue(counters, !document.isDirty(), "saving cleans");
  document.translate(std::vector<std::string>{ a }, Vector3(1, 0, 0), "drag:7");
  testTrue(counters, document.isDirty(), "editing after save dirties");
  document.undo();
  testTrue(counters, !document.isDirty(), "undo back to the save is clean");
  document.redo();
  testTrue(counters, document.isDirty(), "redo past the save dirties");
  document.markSaved("scene.ilsc", "scene.ilsc");
  document.translate(std::vector<std::string>{ a }, Vector3(1, 0, 0), "drag:7");
  testTrue(counters,
           document.isDirty(),
           "a drag continuing past a save never merges into the saved step");
  document.undo();
  testTrue(counters, !document.isDirty(), "and undoes back to the save");
  testTrue(counters,
           document.encode().find("\"x\": 25.0") != std::string::npos,
           "view state is still written with the scene");
  return counters.failures;
}

void
registerEditorHistoryTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.History.UndoRedoEveryCommand",
               []() { return testUndoRedoEveryCommand(); });
  registry.add("IllEd.History.DeleteRestoresSiblingOrder",
               []() { return testDeleteRestoresSiblingOrder(); });
  registry.add("IllEd.History.DragMerges", []() { return testDragMerges(); });
  registry.add("IllEd.History.CapBytes", []() { return testCapBytes(); });
  registry.add("IllEd.History.DirtyTracksSavedCursor",
               []() { return testDirtyTracksSavedCursor(); });
}
