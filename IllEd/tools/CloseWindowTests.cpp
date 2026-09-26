#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_NONE
#include "EditorScene.h"
#include "TestAccess.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

// The editor scene on the native engine, driven through the runtime's frame
// phases (D-E31) by a director instead of a WASM program.
static void
updateFrame(Illumo& host, SceneDirector& scenes, double dt)
{
  host.beginUpdate(dt);
  scenes.update(dt);
  host.endUpdate();
}

static void
renderFrame(Illumo& host, SceneDirector& scenes)
{
  if (Scene* scene = host.beginRender()) {
    scenes.dispatch(*scene);
  }
  host.endRender();
}

// The runtime's close negotiation: true once a requested close is approved;
// a declined request is cleared.
static bool
closeApproved(Illumo& host, SceneDirector& scenes)
{
  if (!host.shouldClose()) {
    return false;
  }
  if (scenes.closeRequested()) {
    return true;
  }
  host.deferClose();
  return false;
}

static void
requireCloseCheck(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

static void
runNativeSceneEdits(Illumo& host, SceneDirector& scenes, EditorScene& editor)
{
  EditorDocument& document = EditorSceneTestAccess::document(editor);
  EditorSceneTestAccess::handleCommand(editor, EditorCommand::SetMode3D);
  std::string parent;
  for (size_t i = 0; i < 64; ++i) {
    parent = document.createPrimitive(
      true, ScenePrimitiveShape::Cube, parent, Transform3D{});
  }
  const std::string child = document.createPrimitive(
    false, ScenePrimitiveShape::Cube, parent, Transform3D{});
  const SceneNodeHandle handle = document.nodeHandle(child);
  EditorSceneTestAccess::setSelectedId(editor, child);
  EditorSceneTestAccess::refreshView(editor);
  ISceneRenderAttachment* retained = document.graph().getAttachment(handle, 0);
  for (size_t step = 0; step < 6; ++step) {
    if (step == 1) {
      document.translate(child, Vector3(0.5f, 0.5f, 0));
    }
    if (step == 2) {
      EditorSceneTestAccess::handleCommand(editor, EditorCommand::CycleColor);
    }
    if (step == 3) {
      document.setParent(child, {});
    }
    if (step == 4) {
      document.setParent(child, parent);
    }
    EditorSceneTestAccess::refreshView(editor);
    const uint64_t extracted = document.graph().getStatistics().extractions;
    renderFrame(host, scenes);
    const std::string frameError = host.context().renderer->frameError();
    requireCloseCheck(frameError.empty(),
                      ("native edit frame submits: " + frameError).c_str());
    requireCloseCheck(document.nodeHandle(child) == handle &&
                        document.graph().getAttachment(handle, 0) == retained,
                      "native edits retain graph and visual identities");
    requireCloseCheck(document.graph().getStatistics().extractions ==
                        extracted + 1,
                      "native frame extracts one snapshot");
  }
  requireCloseCheck(document.destroySubtree(parent),
                    "native deep subtree deletes");
  EditorSceneTestAccess::refreshView(editor);
  renderFrame(host, scenes);
  requireCloseCheck(!document.graph().isNodeValid(handle) &&
                      host.context().renderer->frameError().empty(),
                    "native deletion leaves no stale callback");
  document.clear();
  EditorSceneTestAccess::setSelectedId(editor, {});
  EditorSceneTestAccess::refreshView(editor);
}

// The director and its scenes live in this frame, so they are destroyed,
// releasing their assets, before the engine shuts down.
static void
runEditorCloseFlow(Illumo& host,
                   bool discard,
                   const std::filesystem::path& scratch)
{
  SceneDirector scenes(host.context());
  host.context().scenes = &scenes;
  EditorScene* editor = &scenes.emplace<EditorScene>("editor");
  requireCloseCheck(scenes.switchTo("editor") && scenes.applyPending(),
                    "editor initialization");
  HWND window = glfwGetWin32Window(host.context().window->getWindowInstance());
  ShowWindow(window, SW_HIDE);
  runNativeSceneEdits(host, scenes, *editor);
  EditorDocument& document = EditorSceneTestAccess::document(*editor);
  EditorSceneTestAccess::createNode(*editor, EditorCommand::CreateCube);

  // WM_CLOSE is the title-bar close path; SC_CLOSE is the native Alt+F4 action.
  SendMessageW(window, WM_CLOSE, 0, 0);
  requireCloseCheck(host.shouldClose(), "WM_CLOSE reaches GLFW flag");
  requireCloseCheck(!closeApproved(host, scenes) && !host.shouldClose(),
                    "dirty close is deferred and cleared");
  requireCloseCheck(EditorSceneTestAccess::confirmationOpen(*editor),
                    "native close opens editor confirmation");
  renderFrame(host, scenes);
  host.context().inputManager->getKeyQueue().push(
    { KeyCode::Escape, InputAction::Press, 0 });
  updateFrame(host, scenes, 0.01);
  requireCloseCheck(!closeApproved(host, scenes) &&
                      !EditorSceneTestAccess::confirmationOpen(*editor),
                    "cancel stays open without reopening");

  document.setPath((scratch / "missing" / "scene.ilsc").string());
  SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
  requireCloseCheck(host.shouldClose() && !closeApproved(host, scenes),
                    "SC_CLOSE enters confirmation");
  host.context().inputManager->getKeyQueue().push(
    { KeyCode::Enter, InputAction::Press, 0 });
  updateFrame(host, scenes, 0.01);
  requireCloseCheck(document.isDirty() && !closeApproved(host, scenes),
                    "failed save preserves dirty editor");

  const std::filesystem::path saved = scratch / "scene.ilsc";
  document.setPath(saved.string());
  SendMessageW(window, WM_CLOSE, 0, 0);
  requireCloseCheck(!closeApproved(host, scenes), "retry prompts");
  host.context().inputManager->getKeyQueue().push(
    { discard ? KeyCode::N : KeyCode::Enter, InputAction::Press, 0 });
  updateFrame(host, scenes, 0.01);
  requireCloseCheck(closeApproved(host, scenes), "confirmed exit is approved");
  if (discard) {
    requireCloseCheck(document.isDirty(),
                      "discard does not mutate dirty state");
  } else {
    EditorDocument reloaded;
    std::string error;
    std::string text;
    requireCloseCheck(
      !document.isDirty() &&
        IllEdNativeFiles::readText(saved.string(), &text, &error) &&
        reloaded.loadFromText(text, &error) &&
        reloaded.nodeCount() == document.nodeCount(),
      "saved scene survives native close");
  }
  scenes.stopAll();
  host.context().scenes = nullptr;
}

static void
runNativeCloseCase(bool discard, const std::filesystem::path& scratch)
{
  IllumoConfig config;
  config.applicationName = "IllEd native close regression";
  config.environmentPath = (scratch / "envvars.json").string();
  Illumo host(config);
  host.environment().setVar("WinX", 640);
  host.environment().setVar("WinY", 480);
  requireCloseCheck(host.initialize(), "host initialization");
  runEditorCloseFlow(host, discard, scratch);
  host.shutdown();
}

int
main()
{
  Logger::setConsoleToStderr(true);
  const std::filesystem::path scratch =
    std::filesystem::temp_directory_path() /
    ("illed-close-" + std::to_string(GetCurrentProcessId()));
  if (!std::filesystem::create_directory(scratch)) {
    std::cerr << "Scratch directory already exists\n";
    return 1;
  }
  int result = 0;
  try {
    runNativeCloseCase(false, scratch);
    runNativeCloseCase(true, scratch);
    std::cout << "Native scene edits, close, cancel, failed save, save, and "
                 "discard passed\n";
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    result = 1;
  }
  std::error_code error;
  std::filesystem::remove(scratch / "envvars.json", error);
  std::filesystem::remove(scratch / "scene.ilsc", error);
  std::filesystem::remove(scratch, error);
  return result;
}
