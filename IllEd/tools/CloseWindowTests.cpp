#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_NONE
#include "EditorScene.h"
#include "TestAccess.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

// Runs the editor scene on the native host, which still hosts modules until
// scene programs M5 removes them.
class EditorHostModule final : public IModule
{
public:
  explicit EditorHostModule(std::unique_ptr<EditorScene> scene)
    : m_scene(std::move(scene))
  {
  }
  bool Start(IllumoContext* context) override
  {
    return context != nullptr && m_scene->start(*context);
  }
  void Update(double dt) override { m_scene->update(dt); }
  void DispatchDrawables(Scene* scene) override
  {
    if (scene != nullptr) {
      m_scene->dispatch(*scene);
    }
  }
  void Exit() override { m_scene->stop(); }
  bool OnCloseRequested() override { return m_scene->closeRequested(); }

private:
  std::unique_ptr<EditorScene> m_scene;
};

static void
requireCloseCheck(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

static void
runNativeSceneEdits(Illumo& host, EditorScene& editor)
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
    host.render();
    requireCloseCheck(host.context().renderer->frameError().empty(),
                      "native edit frame submits");
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
  host.render();
  requireCloseCheck(!document.graph().isNodeValid(handle) &&
                      host.context().renderer->frameError().empty(),
                    "native deletion leaves no stale callback");
  document.clear();
  EditorSceneTestAccess::setSelectedId(editor, {});
  EditorSceneTestAccess::refreshView(editor);
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
  std::unique_ptr<EditorScene> owned = std::make_unique<EditorScene>();
  EditorScene* editor = owned.get();
  host.addModule(std::make_unique<EditorHostModule>(std::move(owned)),
                 ModuleRequirement::Required);
  requireCloseCheck(host.startModules(), "editor initialization");
  HWND window = glfwGetWin32Window(host.context().window->getWindowInstance());
  ShowWindow(window, SW_HIDE);
  runNativeSceneEdits(host, *editor);
  EditorDocument& document = EditorSceneTestAccess::document(*editor);
  EditorSceneTestAccess::createNode(*editor, EditorCommand::CreateCube);

  // WM_CLOSE is the title-bar close path; SC_CLOSE is the native Alt+F4 action.
  SendMessageW(window, WM_CLOSE, 0, 0);
  requireCloseCheck(host.shouldClose(), "WM_CLOSE reaches GLFW flag");
  requireCloseCheck(!host.processCloseRequest() && !host.shouldClose(),
                    "dirty close is deferred and cleared");
  requireCloseCheck(EditorSceneTestAccess::confirmationOpen(*editor),
                    "native close opens editor confirmation");
  host.render();
  host.context().inputManager->getKeyQueue().push(
    { KeyCode::Escape, InputAction::Press, 0 });
  host.update(0.01);
  requireCloseCheck(!host.processCloseRequest() &&
                      !EditorSceneTestAccess::confirmationOpen(*editor),
                    "cancel stays open without reopening");

  document.setPath((scratch / "missing" / "scene.ilsc").string());
  SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
  requireCloseCheck(host.shouldClose() && !host.processCloseRequest(),
                    "SC_CLOSE enters confirmation");
  host.context().inputManager->getKeyQueue().push(
    { KeyCode::Enter, InputAction::Press, 0 });
  host.update(0.01);
  requireCloseCheck(document.isDirty() && !host.processCloseRequest(),
                    "failed save preserves dirty editor");

  const std::filesystem::path saved = scratch / "scene.ilsc";
  document.setPath(saved.string());
  SendMessageW(window, WM_CLOSE, 0, 0);
  requireCloseCheck(!host.processCloseRequest(), "retry prompts");
  host.context().inputManager->getKeyQueue().push(
    { discard ? KeyCode::N : KeyCode::Enter, InputAction::Press, 0 });
  host.update(0.01);
  requireCloseCheck(host.processCloseRequest(), "confirmed exit is approved");
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
