#include "EditorAutosave.h"
#include "EditorScene.h"
#include "TestAccess.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/PackageMounts.h>
#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

// Autosave, recovery and the session record
// (docs/illed-editing-wave4-plan.md, 5.2) over the synchronous native
// oracle, whose storage names are files in the working directory.

static TestCounters g;

static std::filesystem::path
slotPath(int slot)
{
  return std::filesystem::current_path() / EditorAutosaveRecord::slotName(slot);
}

static void
clearSlots()
{
  for (int slot = 0; slot < EditorAutosaveRecord::kSlots; ++slot) {
    std::error_code ignored;
    std::filesystem::remove(slotPath(slot), ignored);
  }
}

static std::string
readFile(const std::filesystem::path& path)
{
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), {});
}

// A fresh settings file per fixture; `settings` is written into it first.
static std::filesystem::path
isolatedSettings(const std::string& settings)
{
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "illed-autosave-settings.json";
  std::error_code error;
  std::filesystem::remove(path, error);
  if (!settings.empty()) {
    std::ofstream(path) << settings;
  }
  return path;
}

// The OS window, recording the title the editor sets on it.
class TitledWindow final : public NullRenderWindow
{
public:
  std::string title;
  TitledWindow(int w, int h)
    : NullRenderWindow(w, h)
  {
  }
  void setTitle(const std::string& value) override { title = value; }
};

struct AutosaveFixture
{
  TitledWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  AssetManager assets;
  CommandRegistry registry;
  CommandLine console;
  InputManager input;
  DrawList scene;
  IllumoContext context;
  SceneDirector director;
  EditorScene& module;
  bool started;

  explicit AutosaveFixture(const std::string& settings = {})
    : window(1280, 720)
    , env(isolatedSettings(settings))
    , camera(glm::vec2(0.0f, 0.0f), 32.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false)
    , registry()
    , console(&env, &registry, &window, &renderer, "IllEd")
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               &assets, &env,    &camera,  &registry }
    , director(context)
    , module(director.emplace<EditorScene>("editor"))
    , started(false)
  {
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    env.setVar("fontSize", "13");
    mock.Initialize();
    started = director.switchTo("editor") && director.applyPending();
    // The slot scan runs on the first update (synchronous here).
    module.update(0.016);
  }

  void seconds(double count)
  {
    for (double elapsed = 0.0; elapsed < count; elapsed += 1.0) {
      module.update(1.0);
    }
  }
  void press(KeyCode key)
  {
    input.getKeyQueue().push({ key, InputAction::Press, 0 });
    module.update(0.016);
  }
};

static void
testAutosaveCadence()
{
  testSection("Autosave: a dirty document is written on a cadence and the "
              "slot is freed when it is clean");
  clearSlots();
  AutosaveFixture fixture;
  testTrue(g, fixture.started, "editor starts");
  testTrue(g,
           EditorSceneTestAccess::autosaveSlot(fixture.module) == -1,
           "no slot is taken before the first autosave");
  fixture.seconds(40.0);
  testTrue(g,
           !std::filesystem::exists(slotPath(0)),
           "a clean document is never autosaved");

  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  fixture.seconds(20.0);
  testTrue(g,
           !EditorSceneTestAccess::autosaveLive(fixture.module),
           "20 s after an edit nothing is written yet");
  fixture.seconds(12.0);
  testTrue(g,
           EditorSceneTestAccess::autosaveLive(fixture.module) &&
             std::filesystem::exists(slotPath(0)),
           "30 s after the last edit the slot holds the document");
  testTrue(g,
           EditorSceneTestAccess::autosaveSlot(fixture.module) == 0,
           "with every slot free the tab takes slot 0");
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  testTrue(g, document.isDirty(), "autosave leaves the document dirty");
  SceneDocument saved;
  EditorAutosaveRecord record;
  std::string error;
  testTrue(g,
           IlscCodec::parse(readFile(slotPath(0)), saved, error) &&
             saved.nodes.size() == 1 && record.read(saved) &&
             record.label == "-" && record.location.empty() &&
             record.root == EditorDocument::kLocalPackageRoot &&
             record.savedAt.size() == 16,
           "the slot carries the scene and the illed.autosave record");

  // Edits every 10 s keep the idle rule from firing; the 120 s rule does.
  const std::string first = readFile(slotPath(0));
  for (int round = 0; round < 13; ++round) {
    EditorSceneTestAccess::createNode(fixture.module,
                                      EditorCommand::CreateSphere);
    fixture.seconds(10.0);
  }
  testTrue(g,
           readFile(slotPath(0)) != first &&
             IlscCodec::parse(readFile(slotPath(0)), saved, error) &&
             saved.nodes.size() > 1,
           "continuous editing is still written within 120 s");

  document.setPath("test-autosave-saved.ilsc");
  EditorSceneTestAccess::handleCommand(fixture.module,
                                       EditorCommand::SaveDocument);
  fixture.module.update(0.016);
  const std::string afterSave =
    std::string("dirty=") + (document.isDirty() ? "1" : "0") + " live=" +
    (EditorSceneTestAccess::autosaveLive(fixture.module) ? "1" : "0") +
    " size=" +
    std::to_string(std::filesystem::exists(slotPath(0))
                     ? std::filesystem::file_size(slotPath(0))
                     : 0);
  testTrue(g,
           !document.isDirty() &&
             !EditorSceneTestAccess::autosaveLive(fixture.module) &&
             std::filesystem::exists(slotPath(0)) &&
             std::filesystem::file_size(slotPath(0)) == 0,
           ("a save frees the slot (an empty file): " + afterSave).c_str());
  std::filesystem::remove("test-autosave-saved.ilsc");

  // An approved discard on exit frees the slot before the window closes.
  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  EditorSceneTestAccess::autosaveNow(fixture.module);
  testTrue(g,
           EditorSceneTestAccess::autosaveLive(fixture.module),
           "scene_autosave writes the slot at once");
  EditorSceneTestAccess::handleCommand(fixture.module,
                                       EditorCommand::ExitEditor);
  fixture.press(KeyCode::N);
  testTrue(g,
           fixture.window.shouldWindowClose() &&
             std::filesystem::file_size(slotPath(0)) == 0,
           "discarding on exit empties the slot before the close");
  clearSlots();
}

// Writes a slot the way a crashed session leaves it.
static void
writeLostWork(int slot, const std::string& label, const std::string& location)
{
  EditorDocument lost;
  lost.createPrimitive(false, ScenePrimitiveShape::Pyramid, {}, Transform3D{});
  lost.createPrimitive(false, ScenePrimitiveShape::Cube, {}, Transform3D{});
  SceneDocument copy = lost.scene().document();
  EditorAutosaveRecord record;
  record.label = label;
  record.location = location;
  record.root = EditorDocument::kLocalPackageRoot;
  record.savedAt = "2026-10-21 09:30";
  record.write(copy);
  std::ofstream(slotPath(slot), std::ios::binary)
    << IlscCodec::encode(copy, true);
}

static void
testRecovery()
{
  testSection("Recovery: a live slot is offered at start");
  clearSlots();
  writeLostWork(0, "lost.ilsc", {});
  {
    AutosaveFixture fixture;
    testTrue(g,
             EditorSceneTestAccess::recoveryOpen(fixture.module),
             "the recovery dialog opens once the slots were read");
    testTrue(g,
             EditorSceneTestAccess::recoveryMessage(fixture.module) ==
               "Recover unsaved changes to lost.ilsc from 2026-10-21 09:30?",
             "the dialog names the document and when it was autosaved");
    testTrue(g,
             EditorSceneTestAccess::autosaveSlot(fixture.module) == -1,
             "no slot is taken until the tab needs one");
    fixture.press(KeyCode::Enter);
    // Recovered documents open through the queue, one per update.
    fixture.module.update(0.016);
    fixture.module.update(0.016);
    EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
    testTrue(g,
             !EditorSceneTestAccess::recoveryOpen(fixture.module) &&
               document.nodeCount() == 2 && document.isDirty(),
             "Recover loads the lost nodes as unsaved work");
    testTrue(g,
             document.displayName() == "lost.ilsc (recovered)" &&
               document.path().empty(),
             "a document without a tree location becomes a recovered copy");
    testTrue(g,
             document.scene().document().extensions.empty(),
             "the autosave record is stripped from the recovered document");
    testTrue(g,
             EditorSceneTestAccess::autosaveSlot(fixture.module) == 0 &&
               EditorSceneTestAccess::autosaveLive(fixture.module),
             "the session adopts the recovered slot");
    document.setPath("test-recovered-saved.ilsc");
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::SaveDocument);
    fixture.module.update(0.016);
    testTrue(g,
             !document.isDirty() &&
               std::filesystem::file_size(slotPath(0)) == 0,
             "saving the recovered document frees its slot");
    std::filesystem::remove("test-recovered-saved.ilsc");
  }

  testSection("Recovery: several documents come back as tabs");
  clearSlots();
  writeLostWork(0, "one.ilsc", {});
  writeLostWork(1, "two.ilsc", {});
  {
    std::ofstream(slotPath(1), std::ios::binary | std::ios::app) << "";
    AutosaveFixture fixture;
    EditorScene& module = fixture.module;
    testTrue(g,
             EditorSceneTestAccess::recoveryOpen(module) &&
               EditorSceneTestAccess::recoveryMessage(module).rfind(
                 "Recover 2 scenes with unsaved changes", 0) == 0,
             "the dialog counts the recoverable documents");
    fixture.press(KeyCode::Enter);
    for (int frame = 0; frame < 4; ++frame) {
      fixture.module.update(0.016);
    }
    testTrue(g,
             EditorSceneTestAccess::tabCount(module) == 2 &&
               EditorSceneTestAccess::tab(module, 0).document.isDirty() &&
               EditorSceneTestAccess::tab(module, 1).document.isDirty() &&
               EditorSceneTestAccess::tab(module, 0).document.nodeCount() ==
                 2 &&
               EditorSceneTestAccess::tab(module, 1).document.nodeCount() == 2,
             "Recover opens every document in its own tab, dirty");
    const int slotA = EditorSceneTestAccess::tab(module, 0).autosave.slot;
    const int slotB = EditorSceneTestAccess::tab(module, 1).autosave.slot;
    testTrue(g,
             EditorSceneTestAccess::tab(module, 0).autosave.live &&
               EditorSceneTestAccess::tab(module, 1).autosave.live &&
               slotA != slotB && slotA >= 0 && slotB >= 0 && slotA < 2 &&
               slotB < 2,
             "each tab adopts the slot it came from");
  }

  testSection("Recovery: Discard empties the slot, Later keeps it");
  clearSlots();
  writeLostWork(2, "older.ilsc", {});
  {
    AutosaveFixture fixture;
    testTrue(g,
             EditorSceneTestAccess::recoveryOpen(fixture.module) &&
               EditorSceneTestAccess::autosaveSlot(fixture.module) == -1,
             "a live slot other than 0 is found; none is taken yet");
    fixture.press(KeyCode::D);
    testTrue(g,
             !EditorSceneTestAccess::recoveryOpen(fixture.module) &&
               std::filesystem::file_size(slotPath(2)) == 0 &&
               EditorSceneTestAccess::document(fixture.module).nodeCount() == 0,
             "Discard empties the slot and leaves the document alone");
  }
  writeLostWork(3, "kept.ilsc", {});
  {
    AutosaveFixture fixture;
    fixture.press(KeyCode::Escape);
    testTrue(g,
             !EditorSceneTestAccess::recoveryOpen(fixture.module) &&
               std::filesystem::file_size(slotPath(3)) > 0,
             "Later keeps the slot for the next start");
    EditorSceneTestAccess::createNode(fixture.module,
                                      EditorCommand::CreateCube);
    EditorSceneTestAccess::autosaveNow(fixture.module);
    testTrue(g,
             std::filesystem::file_size(slotPath(0)) > 0 &&
               std::filesystem::file_size(slotPath(3)) > 0,
             "this session's autosave goes to its own slot");
  }
  clearSlots();
}

static void
testSessionRestore()
{
  testSection("Session: the last project scene reopens at start");
  clearSlots();
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illed-autosave-project";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root / "scenes");
  std::ofstream(root / "illumo.json")
    << R"({"format":"ilpk","format_version":1,"id":"sess","kind":"content"})";
  {
    EditorDocument saved;
    saved.createPrimitive(false, ScenePrimitiveShape::Cube, {}, Transform3D{});
    std::ofstream(root / "scenes" / "a.ilsc", std::ios::binary)
      << saved.encode();
  }
  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  VfsMount project;
  project.point = "/project";
  project.layers.push_back(
    { DirectoryVfsBackend::open(root, true, error), "sess" });
  tree->mount(project, error);
  IllEdNativeTree::install(tree);
  {
    EditorDocument second;
    second.createPrimitive(false, ScenePrimitiveShape::Cube, {}, Transform3D{});
    second.createPrimitive(
      false, ScenePrimitiveShape::Sphere, {}, Transform3D{});
    std::ofstream(root / "scenes" / "b.ilsc", std::ios::binary)
      << second.encode();
  }
  const std::string location = "vfs:/project/scenes/a.ilsc";
  const std::string other = "vfs:/project/scenes/b.ilsc";
  {
    AutosaveFixture fixture;
    EditorScene& module = fixture.module;
    EditorSceneTestAccess::openLocation(module, { location, "a.ilsc" });
    fixture.module.update(0.016);
    testTrue(g,
             fixture.env.getVar("sessionTabs").value == location &&
               fixture.env.getVar("sessionActive").value == "0",
             "opening a tree document records it as the session's scene");
    EditorSceneTestAccess::handleCommand(module, EditorCommand::NewDocument);
    fixture.module.update(0.016);
    testTrue(g,
             fixture.env.getVar("sessionTabs").value == location,
             "an untitled tab is not recorded");
    EditorSceneTestAccess::openLocation(module, { other, "b.ilsc" });
    fixture.module.update(0.016);
    testTrue(
      g,
      fixture.env.getVar("sessionTabs").value == location + ";" + other &&
        fixture.env.getVar("sessionActive").value == "1",
      "every tree document is recorded in tab order with the active one");
  }
  {
    AutosaveFixture fixture("{\"sessionTabs\": \"" + location + ";" + other +
                            "\", \"sessionActive\": \"1\"}");
    EditorScene& module = fixture.module;
    // One queued document opens per update.
    for (int frame = 0; frame < 4; ++frame) {
      fixture.module.update(0.016);
    }
    testTrue(
      g,
      EditorSceneTestAccess::tabCount(module) == 2 &&
        EditorSceneTestAccess::tab(module, 0).document.path() == location &&
        EditorSceneTestAccess::tab(module, 1).document.path() == other &&
        EditorSceneTestAccess::tab(module, 1).document.nodeCount() == 2 &&
        EditorSceneTestAccess::activeTab(module) == 1 &&
        !EditorSceneTestAccess::document(module).isDirty(),
      "the next start reopens the recorded scenes with the same active "
      "tab");
  }
  {
    AutosaveFixture fixture("{\"sessionTabs\": \"" + location +
                            "\", \"restoreSession\": \"0\"}");
    fixture.module.update(0.016);
    testTrue(g,
             EditorSceneTestAccess::tabCount(fixture.module) == 1 &&
               EditorSceneTestAccess::document(fixture.module).path().empty(),
             "restoreSession=0 starts empty");
  }
  {
    AutosaveFixture fixture(
      "{\"sessionTabs\": \"vfs:/project/scenes/missing.ilsc\"}");
    for (int frame = 0; frame < 3; ++frame) {
      fixture.module.update(0.016);
    }
    testTrue(g,
             EditorSceneTestAccess::tabCount(fixture.module) == 1 &&
               EditorSceneTestAccess::document(fixture.module).path().empty() &&
               !EditorSceneTestAccess::confirmationOpen(fixture.module),
             "a recorded scene that is gone starts empty without a prompt");
  }
  IllEdNativeTree::install(nullptr);
  std::filesystem::remove_all(root, ignored);
  clearSlots();
}

// A writable project with scenes/a.ilsc (one cube), installed as the native
// tree; the caller uninstalls it.
static std::filesystem::path
installProject(const char* name)
{
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / name;
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root / "scenes");
  std::ofstream(root / "illumo.json")
    << R"({"format":"ilpk","format_version":1,"id":"proj","kind":"content"})";
  EditorDocument saved;
  saved.createPrimitive(false, ScenePrimitiveShape::Cube, {}, Transform3D{});
  std::ofstream(root / "scenes" / "a.ilsc", std::ios::binary) << saved.encode();
  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  VfsMount project;
  project.point = "/project";
  project.layers.push_back(
    { DirectoryVfsBackend::open(root, true, error), "proj" });
  tree->mount(project, error);
  IllEdNativeTree::install(tree);
  return root;
}

static std::size_t
nodesIn(const std::filesystem::path& path)
{
  SceneDocument document;
  std::string error;
  return IlscCodec::parse(readFile(path), document, error)
           ? document.nodes.size()
           : 0;
}

static void
testBackup()
{
  testSection("Backup: saving a project scene keeps its previous version");
  clearSlots();
  const std::filesystem::path root = installProject("illed-backup-project");
  const std::filesystem::path backup = root / "scenes" / ".backup" / "a.ilsc";
  const std::string location = "vfs:/project/scenes/a.ilsc";
  {
    AutosaveFixture fixture;
    EditorSceneTestAccess::openLocation(fixture.module, { location, "a.ilsc" });
    EditorSceneTestAccess::createNode(fixture.module,
                                      EditorCommand::CreateCube);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::SaveDocument);
    fixture.module.update(0.016);
    testTrue(g,
             !EditorSceneTestAccess::document(fixture.module).isDirty() &&
               nodesIn(root / "scenes" / "a.ilsc") == 2,
             "the save writes the scene");
    testTrue(g,
             std::filesystem::exists(backup) && nodesIn(backup) == 1,
             "the version before the save is kept in scenes/.backup");
    EditorSceneTestAccess::createNode(fixture.module,
                                      EditorCommand::CreateSphere);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::SaveDocument);
    fixture.module.update(0.016);
    testTrue(g,
             nodesIn(root / "scenes" / "a.ilsc") == 3 && nodesIn(backup) == 2,
             "one generation: the backup is the save before the last");

    // Packing the project leaves the working files out.
    const std::filesystem::path packed =
      std::filesystem::temp_directory_path() / "illed-backup-project.ilpk";
    std::string error;
    testTrue(g,
             PackageMounts::packMounted(*IllEdNativeTree::current(),
                                        "/project",
                                        packed,
                                        64ull * 1024ull * 1024ull,
                                        error),
             ("the project packs: " + error).c_str());
    const std::shared_ptr<ArchiveVfsBackend> archive =
      ArchiveVfsBackend::openFile(packed, error);
    std::vector<VfsEntry> entries;
    bool sawScene = false;
    bool sawBackup = false;
    if (archive && archive->list("scenes", entries)) {
      for (const VfsEntry& entry : entries) {
        sawScene = sawScene || entry.name == "a.ilsc";
        sawBackup = sawBackup || entry.name == ".backup";
      }
    }
    testTrue(g,
             archive != nullptr && sawScene && !sawBackup,
             "the package holds the scene but not .backup");
    std::error_code ignored;
    std::filesystem::remove(packed, ignored);

    // A scene outside the project gets no backup.
    EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
    document.setPath("test-backup-host.ilsc");
    EditorSceneTestAccess::createNode(fixture.module,
                                      EditorCommand::CreateCube);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::SaveDocument);
    fixture.module.update(0.016);
    testTrue(
      g,
      !document.isDirty() &&
        !std::filesystem::exists(std::filesystem::current_path() / ".backup"),
      "a host file saves without a backup folder");
    std::filesystem::remove("test-backup-host.ilsc", ignored);
  }
  IllEdNativeTree::install(nullptr);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  clearSlots();
}

static void
testRevert()
{
  testSection("Revert to Saved: reloads the saved scene after a confirm");
  clearSlots();
  const std::filesystem::path root = installProject("illed-revert-project");
  const std::string location = "vfs:/project/scenes/a.ilsc";
  {
    AutosaveFixture fixture;
    EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(fixture.module);
    EditorSceneTestAccess::createNode(fixture.module,
                                      EditorCommand::CreateCube);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::RevertDocument);
    testTrue(g,
             !EditorSceneTestAccess::confirmationOpen(fixture.module) &&
               toolbar != nullptr &&
               toolbar->toastForTesting().find("never saved") !=
                 std::string::npos,
             "a never saved scene has nothing to revert to");
    EditorSceneTestAccess::openLocation(fixture.module, { location, "a.ilsc" });
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::RevertDocument);
    testTrue(g,
             !EditorSceneTestAccess::confirmationOpen(fixture.module) &&
               toolbar->toastForTesting() == "No changes to revert",
             "a clean scene has nothing to revert");
    EditorSceneTestAccess::createNode(fixture.module,
                                      EditorCommand::CreateCube);
    EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::RevertDocument);
    testTrue(g,
             EditorSceneTestAccess::confirmationOpen(fixture.module) &&
               EditorSceneTestAccess::confirmationMessage(fixture.module) ==
                 "Revert to the saved a.ilsc and lose the changes?",
             "a dirty scene asks before reverting");
    fixture.press(KeyCode::Escape);
    testTrue(g,
             !EditorSceneTestAccess::confirmationOpen(fixture.module) &&
               document.isDirty() && document.nodeCount() == 2,
             "Cancel keeps the changes");
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::RevertDocument);
    fixture.press(KeyCode::Enter);
    fixture.module.update(0.016);
    testTrue(g,
             !document.isDirty() && document.nodeCount() == 1 &&
               document.path() == location,
             "Revert reloads the saved scene, clean");
  }
  IllEdNativeTree::install(nullptr);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  clearSlots();
}

static void
testWindowTitle()
{
  testSection("Window title: the document's name and dirty mark");
  clearSlots();
  AutosaveFixture fixture;
  testTrue(g,
           fixture.window.title == "Untitled - IllEd",
           "a new editor titles the window Untitled");
  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  fixture.module.update(0.016);
  testTrue(g,
           fixture.window.title == "Untitled* - IllEd",
           "an edit adds the dirty mark");
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  document.setPath("test-title.ilsc");
  EditorSceneTestAccess::handleCommand(fixture.module,
                                       EditorCommand::SaveDocument);
  fixture.module.update(0.016);
  testTrue(g,
           fixture.window.title == "test-title.ilsc - IllEd",
           "a save names the file and drops the mark");
  std::error_code ignored;
  std::filesystem::remove("test-title.ilsc", ignored);
  clearSlots();
}

static void
testTabs()
{
  testSection("Tabs: documents open beside each other and keep their state");
  clearSlots();
  AutosaveFixture fixture;
  EditorScene& module = fixture.module;
  testTrue(g,
           EditorSceneTestAccess::tabCount(module) == 1 &&
             EditorSceneTestAccess::activeTab(module) == 0,
           "the editor starts with one tab");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::NewDocument);
  testTrue(g,
           EditorSceneTestAccess::tabCount(module) == 1,
           "New reuses an empty, untouched tab");
  const std::string cube =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  fixture.camera.SetZoom(48.0f);
  fixture.module.update(0.016);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::NewDocument);
  testTrue(g,
           EditorSceneTestAccess::tabCount(module) == 2 &&
             EditorSceneTestAccess::activeTab(module) == 1 &&
             EditorSceneTestAccess::document(module).nodeCount() == 0 &&
             !EditorSceneTestAccess::document(module).isDirty(),
           "New beside a used tab opens a second, empty one and activates it");
  testTrue(g,
           EditorSceneTestAccess::tab(module, 0).document.nodeCount() == 1 &&
             EditorSceneTestAccess::tab(module, 0).document.isDirty() &&
             EditorSceneTestAccess::tab(module, 0).selection.primary() == cube,
           "the first tab keeps its document, dirtiness and selection");
  testTrue(g,
           EditorSceneTestAccess::selection(module).empty() &&
             fixture.camera.GetTargetZoom() == 32.0f,
           "the new tab starts unselected at the default zoom");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::PreviousTab);
  testTrue(g,
           EditorSceneTestAccess::activeTab(module) == 0 &&
             EditorSceneTestAccess::selectedId(module) == cube &&
             EditorSceneTestAccess::document(module).nodeCount() == 1,
           "Previous Tab returns to the first tab with its selection");
  testTrue(g,
           fixture.camera.GetTargetZoom() == 48.0f,
           "the first tab's camera comes back with it");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::NextTab);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::NextTab);
  testTrue(
    g, EditorSceneTestAccess::activeTab(module) == 0, "Next Tab cycles around");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::ToggleIsolate);
  testTrue(g, EditorSceneTestAccess::isolated(module), "isolation is set");
  EditorSceneTestAccess::activateTab(module, 1);
  testTrue(g,
           !EditorSceneTestAccess::isolated(module),
           "isolation belongs to its tab");
  EditorSceneTestAccess::activateTab(module, 0);
  testTrue(
    g, EditorSceneTestAccess::isolated(module), "and comes back with it");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::ToggleIsolate);

  // The strip follows the tabs; a click on its second tab switches.
  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(module);
  fixture.module.update(0.016);
  GuiToolRect second;
  testTrue(g,
           toolbar != nullptr && toolbar->tabRectForTesting(1, &second) &&
             second.y >= toolbar->barHeight(),
           "the strip lays out one rectangle per tab under the menu bar");

  // Closing: a clean tab goes at once, a dirty one asks, the last one stays
  // as an empty document.
  EditorSceneTestAccess::activateTab(module, 1);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::CloseTab);
  testTrue(g,
           EditorSceneTestAccess::tabCount(module) == 1 &&
             EditorSceneTestAccess::activeTab(module) == 0 &&
             EditorSceneTestAccess::document(module).nodeCount() == 1,
           "a clean tab closes at once and its neighbour takes over");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::CloseTab);
  testTrue(g,
           EditorSceneTestAccess::confirmationOpen(module) &&
             EditorSceneTestAccess::confirmationMessage(module) ==
               "Save changes to Untitled before closing it?",
           "a dirty tab asks before closing");
  fixture.press(KeyCode::Escape);
  testTrue(g,
           EditorSceneTestAccess::tabCount(module) == 1 &&
             EditorSceneTestAccess::document(module).isDirty(),
           "Cancel keeps the tab");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::CloseTab);
  fixture.press(KeyCode::N);
  testTrue(g,
           EditorSceneTestAccess::tabCount(module) == 1 &&
             EditorSceneTestAccess::document(module).nodeCount() == 0 &&
             !EditorSceneTestAccess::document(module).isDirty(),
           "discarding the last tab leaves an empty document");
  clearSlots();
}

static void
testCloseAllTabs()
{
  testSection("Close: every dirty tab is asked in turn, with Save All");
  clearSlots();
  {
    AutosaveFixture fixture;
    EditorScene& module = fixture.module;
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
    EditorSceneTestAccess::document(module).setPath("test-close-a.ilsc");
    EditorSceneTestAccess::handleCommand(module, EditorCommand::NewDocument);
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateSphere);
    EditorSceneTestAccess::document(module).setPath("test-close-b.ilsc");
    testTrue(g, !module.closeRequested(), "two dirty tabs defer the close");
    testTrue(g,
             EditorSceneTestAccess::activeTab(module) == 0 &&
               EditorSceneTestAccess::confirmationMessage(module) ==
                 "Save changes to test-close-a.ilsc before closing?",
             "the first dirty tab is activated and asked");
    fixture.press(KeyCode::N);
    testTrue(g,
             !fixture.window.shouldWindowClose() &&
               EditorSceneTestAccess::activeTab(module) == 1 &&
               EditorSceneTestAccess::confirmationMessage(module) ==
                 "Save changes to test-close-b.ilsc before closing?",
             "discarding one moves on to the next dirty tab");
    fixture.press(KeyCode::Escape);
    testTrue(g,
             !fixture.window.shouldWindowClose() &&
               !EditorSceneTestAccess::confirmationOpen(module),
             "Cancel stops the walk");
    fixture.module.update(0.016);
    testTrue(g,
             !module.closeRequested() &&
               EditorSceneTestAccess::activeTab(module) == 0,
             "a later close starts over with the first dirty tab");
    fixture.press(KeyCode::A);
    fixture.module.update(0.016);
    testTrue(g,
             !EditorSceneTestAccess::tab(module, 0).document.isDirty() &&
               !EditorSceneTestAccess::tab(module, 1).document.isDirty() &&
               std::filesystem::exists("test-close-a.ilsc") &&
               std::filesystem::exists("test-close-b.ilsc"),
             "Save All saves both tabs");
    testTrue(g,
             fixture.window.shouldWindowClose() && module.closeRequested(),
             "and the window closes");
  }
  std::error_code ignored;
  std::filesystem::remove("test-close-a.ilsc", ignored);
  std::filesystem::remove("test-close-b.ilsc", ignored);
  {
    AutosaveFixture fixture;
    EditorScene& module = fixture.module;
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
    EditorSceneTestAccess::handleCommand(module, EditorCommand::NewDocument);
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
    testTrue(g, !module.closeRequested(), "two untitled dirty tabs ask");
    fixture.press(KeyCode::N);
    fixture.press(KeyCode::N);
    testTrue(g,
             fixture.window.shouldWindowClose() && module.closeRequested(),
             "discarding every tab closes the window");
  }
  clearSlots();
}

static void
testSaveAll()
{
  testSection("Save All: every dirty tab is written");
  clearSlots();
  AutosaveFixture fixture;
  EditorScene& module = fixture.module;
  EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  EditorSceneTestAccess::document(module).setPath("test-saveall-a.ilsc");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::NewDocument);
  EditorSceneTestAccess::createNode(module, EditorCommand::CreateSphere);
  EditorSceneTestAccess::document(module).setPath("test-saveall-b.ilsc");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::NewDocument);
  testTrue(g, EditorSceneTestAccess::tabCount(module) == 3, "three tabs");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::SaveAll);
  fixture.module.update(0.016);
  testTrue(g,
           !EditorSceneTestAccess::tab(module, 0).document.isDirty() &&
             !EditorSceneTestAccess::tab(module, 1).document.isDirty() &&
             std::filesystem::exists("test-saveall-a.ilsc") &&
             std::filesystem::exists("test-saveall-b.ilsc"),
           "both dirty tabs are saved; the clean one is skipped");
  std::error_code ignored;
  std::filesystem::remove("test-saveall-a.ilsc", ignored);
  std::filesystem::remove("test-saveall-b.ilsc", ignored);
  clearSlots();
}

static void
testProjects()
{
  testSection("Projects: the host's recent list, the dialogs and the switch");
  clearSlots();
  {
    IllEdNativeProjects::setAvailable(false);
    AutosaveFixture fixture;
    EditorScene& module = fixture.module;
    EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(module);
    float x = 0.0f;
    float y = 0.0f;
    testTrue(
      g,
      EditorSceneTestAccess::recentProjects(module).empty() &&
        toolbar != nullptr &&
        !toolbar->menuItemCenterForTesting(EditorCommand::OpenProject, &x, &y),
      "a host without projects shows no project items");
    EditorSceneTestAccess::handleCommand(module, EditorCommand::OpenProject);
    testTrue(g,
             toolbar->toastForTesting().find("keeps no projects") !=
               std::string::npos,
             "Open Project explains itself on such a host");
    testTrue(g,
             toolbar->statusForTesting().rfind("No project", 0) == 0,
             "the status bar says there is no project");
  }
  {
    IllEdNativeProjects::setAvailable(true);
    IllEdNativeProjects::entries() = { { 1, "forest" }, { 2, "town" } };
    AutosaveFixture fixture;
    EditorScene& module = fixture.module;
    EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(module);
    fixture.module.update(0.016);
    float x = 0.0f;
    float y = 0.0f;
    testTrue(
      g,
      EditorSceneTestAccess::recentProjects(module).size() == 2 &&
        toolbar->menuItemCenterForTesting(
          EditorCommand::RecentProject1, &x, &y) &&
        toolbar->menuItemCenterForTesting(EditorCommand::NewProject, &x, &y),
      "the recent projects and the dialogs sit in the File menu");
    toolbar->closeMenus();
    IllEdNativeProjects::nextPick().clear();
    EditorSceneTestAccess::handleCommand(module, EditorCommand::OpenProject);
    fixture.module.update(0.016);
    testTrue(g,
             !fixture.window.restartRequested() &&
               IllEdNativeProjects::opened() == 0 &&
               EditorSceneTestAccess::switchingTo(module) == 0,
             "a cancelled folder dialog switches nothing");
    IllEdNativeProjects::nextPick() = "meadow";
    EditorSceneTestAccess::handleCommand(module, EditorCommand::OpenProject);
    fixture.module.update(0.016);
    testTrue(g,
             IllEdNativeProjects::entries().front().label == "meadow" &&
               EditorSceneTestAccess::recentProjects(module).front().label ==
                 "meadow",
             "a chosen folder becomes the newest recent project");
    testTrue(g,
             IllEdNativeProjects::opened() == 1 &&
               fixture.window.restartRequested(),
             "with a clean document the editor restarts on it at once");
  }
  {
    IllEdNativeProjects::setAvailable(true);
    IllEdNativeProjects::entries() = { { 1, "forest" }, { 2, "town" } };
    AutosaveFixture fixture;
    EditorScene& module = fixture.module;
    fixture.module.update(0.016);
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
    EditorSceneTestAccess::handleCommand(module, EditorCommand::RecentProject1);
    testTrue(g,
             EditorSceneTestAccess::confirmationOpen(module) &&
               EditorSceneTestAccess::switchingTo(module) == 2,
             "a dirty document asks before the switch");
    fixture.press(KeyCode::Escape);
    testTrue(g,
             EditorSceneTestAccess::switchingTo(module) == 0 &&
               IllEdNativeProjects::opened() == 0 &&
               !fixture.window.restartRequested(),
             "Cancel drops the switch");
    EditorSceneTestAccess::handleCommand(module, EditorCommand::RecentProject1);
    fixture.press(KeyCode::N);
    testTrue(g,
             IllEdNativeProjects::opened() == 2 &&
               fixture.window.restartRequested(),
             "discarding the changes restarts the editor on the project");
    fixture.window.clearRestartRequest();
    EditorSceneTestAccess::handleCommand(module,
                                         EditorCommand::ClearRecentProjects);
    fixture.module.update(0.016);
    testTrue(g,
             IllEdNativeProjects::entries().empty() &&
               EditorSceneTestAccess::recentProjects(module).empty(),
             "Clear Recent Projects empties the host's list and the menu");
  }
  IllEdNativeProjects::setAvailable(false);
  clearSlots();
}

static int
runCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerEditorWorkflowTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Module.Tabs", []() { return runCase(testTabs); });
  registry.add("IllEd.Module.CloseAllTabs",
               []() { return runCase(testCloseAllTabs); });
  registry.add("IllEd.Module.SaveAll", []() { return runCase(testSaveAll); });
  registry.add("IllEd.Module.Projects", []() { return runCase(testProjects); });
  registry.add("IllEd.Module.Backup", []() { return runCase(testBackup); });
  registry.add("IllEd.Module.Revert", []() { return runCase(testRevert); });
  registry.add("IllEd.Module.WindowTitle",
               []() { return runCase(testWindowTitle); });
  registry.add("IllEd.Module.Autosave",
               []() { return runCase(testAutosaveCadence); });
  registry.add("IllEd.Module.Recovery", []() { return runCase(testRecovery); });
  registry.add("IllEd.Module.SessionRestore",
               []() { return runCase(testSessionRestore); });
}
