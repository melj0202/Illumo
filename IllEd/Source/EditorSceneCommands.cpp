#include "EditorClipboard.h"
#include "EditorScene.h"

#include "EditorAssets.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneAssetRefs.h>
#include <Illumo/Content/ScenePlay.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

static const ColorRgba kToastGood{ 60, 220, 120, 255 };
static const ColorRgba kToastInfo{ 66, 214, 210, 255 };
static const ColorRgba kToastBad{ 245, 100, 110, 255 };

void
EditorScene::toast(const std::string& message, ColorRgba color)
{
  if (m_toolbar) {
    m_toolbar->showToast(message, color);
  }
}

std::string
EditorScene::playApplication() const
{
  const std::string chosen = document().playApplication();
  if (!chosen.empty()) {
    return chosen;
  }
  for (const SceneNode& node : document().scene().document().nodes) {
    for (const SceneComponent& component : node.components) {
      const SceneOpaqueComponent* opaque =
        std::get_if<SceneOpaqueComponent>(&component.value);
      const std::string game =
        opaque != nullptr ? m_behaviours.gameFor(opaque->type) : std::string();
      if (!game.empty()) {
        return game;
      }
    }
  }
  return m_behaviours.games().size() == 1 ? m_behaviours.games().front()
                                          : std::string();
}

// The launch document's file name: the document's, as a plain file name.
static std::string
playFileName(const std::string& label)
{
  std::string name;
  for (char character : label) {
    const bool plain = (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') ||
                       character == '.' || character == '-' || character == '_';
    name.push_back(plain ? character : '_');
  }
  while (!name.empty() && name.front() == '.') {
    name.erase(name.begin());
  }
  if (name.size() < 5 || name.substr(name.size() - 5) != ".ilsc") {
    name += ".ilsc";
  }
  if (name.size() > 64 || name == ".ilsc") {
    name = "scene.ilsc";
  }
  return name;
}

void
EditorScene::togglePlay()
{
  IllEdPlatform& platform = IllEdPlatform::current();
  if (m_playStarting) {
    return;
  }
  if (!m_playApplication.empty()) {
    platform.stopApp();
    m_playApplication.clear();
    toast("Stopped playing", kToastInfo);
    return;
  }
  if (!platform.canLaunch()) {
    toast("Play needs IllumoRuntime with app launching", kToastBad);
    return;
  }
  const std::string application = playApplication();
  if (application.empty()) {
    toast("No game plays this scene: pick one in the scene's Play with",
          kToastBad);
    return;
  }
  // A crash while the game runs must not lose the scene it plays.
  autosaveNow(activeTab());
  // The game plays the scene as it is now, saved or not; relative references
  // resolve against the editor's package root.
  SceneDocument copy = document().scene().document();
  ScenePlay play;
  ScenePlay::read(copy.extensions, &play);
  play.application = application;
  play.root = document().packageRoot();
  ScenePlay::write(copy.extensions, play);
  m_playStarting = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  platform.launchApp(application,
                     playFileName(document().displayName().empty()
                                    ? std::string("Untitled")
                                    : document().displayName()),
                     IlscCodec::encode(copy, false),
                     [this, alive, application](bool started) {
                       if (alive.expired()) {
                         return;
                       }
                       m_playStarting = false;
                       if (!started) {
                         toast("Could not start " + application, kToastBad);
                         return;
                       }
                       m_playApplication = application;
                       toast("Playing in " + application, kToastGood);
                       Logger::LogInfo("Playing the scene in " + application);
                     });
}

void
EditorScene::updatePlay()
{
  // The game window was closed (or failed) since Play started it.
  if (!m_playApplication.empty() && !m_playStarting &&
      !IllEdPlatform::current().appRunning()) {
    m_playApplication.clear();
    toast("Stopped playing", kToastInfo);
  }
  if (m_toolbar) {
    m_toolbar->setPlaying(!m_playApplication.empty());
  }
}

void
EditorScene::groupSelection()
{
  const std::string group =
    document().groupNodes(selection().topLevel(document().scene()));
  if (group.empty()) {
    toast("Select nodes to group", kToastInfo);
    return;
  }
  selection().set(group);
  toast("Grouped", kToastGood);
}

void
EditorScene::ungroupSelection()
{
  const std::vector<std::string> lifted =
    document().ungroupNodes(selection().ids());
  if (lifted.empty()) {
    toast("Nothing to ungroup: select a node with children", kToastInfo);
    return;
  }
  selection().set(lifted);
  toast("Ungrouped " + std::to_string(lifted.size()) + " nodes", kToastGood);
}

void
EditorScene::selectRelatives(bool parents)
{
  std::vector<std::string> next;
  for (const std::string& id : selection().ids()) {
    std::vector<std::string> found;
    if (parents) {
      const std::string parent = document().scene().parentOf(id);
      if (!parent.empty()) {
        found.push_back(parent);
      }
    } else {
      found = document().scene().childIds(id);
    }
    for (const std::string& relative : found) {
      if (std::find(next.begin(), next.end(), relative) == next.end()) {
        next.push_back(relative);
      }
    }
  }
  if (next.empty()) {
    toast(parents ? "The selection is at the root" : "No children to select",
          kToastInfo);
    return;
  }
  selection().set(next);
}

void
EditorScene::toggleLockSelection()
{
  const std::vector<std::string>& ids = selection().ids();
  if (ids.empty()) {
    return;
  }
  bool allLocked = true;
  for (const std::string& id : ids) {
    allLocked = allLocked && document().isLockedSelf(id);
  }
  if (document().setLocked(ids, !allLocked)) {
    toast(allLocked ? "Unlocked: clicks select it again"
                    : "Locked: clicks in the viewport pass through",
          kToastInfo);
  }
}

void
EditorScene::toggleIsolate()
{
  if (activeTab().isolated) {
    activeTab().isolated = false;
    document().setViewHidden({});
    toast("Showing everything", kToastInfo);
    return;
  }
  if (selection().ids().empty()) {
    toast("Select what to isolate", kToastInfo);
    return;
  }
  // The selection's subtrees and their ancestors stay; everything else
  // hides from view (hiding a node hides its subtree).
  std::unordered_set<std::string> keep;
  for (const std::string& id : selection().ids()) {
    for (const std::string& below : document().scene().subtreeIds(id)) {
      keep.insert(below);
    }
    for (std::string up = document().scene().parentOf(id); !up.empty();
         up = document().scene().parentOf(up)) {
      keep.insert(up);
    }
  }
  std::vector<std::string> hidden;
  const SceneGraph& graph = document().graph();
  for (SceneNodeHandle node = graph.firstNode(); !node.isNull();
       node = graph.nextNode(node)) {
    const std::string id(graph.getName(node));
    if (!keep.contains(id)) {
      hidden.push_back(id);
    }
  }
  document().setViewHidden(hidden);
  activeTab().isolated = true;
  activeTab().isolatedGeneration = document().sceneGeneration();
  toast("Isolated (Shift+H shows everything)", kToastInfo);
}

void
EditorScene::dropSelectionToFloor()
{
  std::vector<std::string> ids;
  std::vector<Vector3> deltas;
  for (const std::string& id : selection().topLevel(document().scene())) {
    AxisAlignedBounds3 bounds;
    if (!document().subtreeWorldBounds(id, &bounds)) {
      continue;
    }
    // Cast down from the subtree's base, through itself, to the first
    // surface below; with none, the ground plane (y = 0).
    std::unordered_set<std::string> self;
    for (const std::string& below : document().scene().subtreeIds(id)) {
      self.insert(below);
    }
    const Vector3 base((bounds.minimum.x + bounds.maximum.x) * 0.5f,
                       bounds.minimum.y + 0.001f,
                       (bounds.minimum.z + bounds.maximum.z) * 0.5f);
    float floor = 0.0f;
    std::string hit;
    AxisAlignedBounds3 surface;
    if (document().pickRay(base, Vector3(0.0f, -1.0f, 0.0f), &hit, &self) &&
        document().graph().getWorldBounds(document().nodeHandle(hit),
                                          &surface) &&
        surface.maximum.y <= base.y) {
      floor = surface.maximum.y;
    }
    const float delta = floor - bounds.minimum.y;
    if (std::fabs(delta) > 1.0e-5f) {
      ids.push_back(id);
      deltas.push_back(Vector3(0.0f, delta, 0.0f));
    }
  }
  if (ids.empty()) {
    toast("Already on the floor", kToastInfo);
    return;
  }
  document().translateEach(ids, deltas, "Drop to Floor");
}

void
EditorScene::nudgeSelection(const Vector3& direction)
{
  const std::vector<std::string> ids = selection().topLevel(document().scene());
  if (ids.empty()) {
    return;
  }
  const float step = std::max(0.001f, document().editorState().snapTranslate);
  document().translate(
    ids, direction * step, "nudge:" + std::to_string(m_nudgeSerial));
}

SaveLoadDialogSpec
EditorScene::dialogSpec() const
{
  SaveLoadDialogSpec specification;
  specification.fileDescription = "Illumo Scene";
  specification.defaultFilename = "Scene.ilsc";
  specification.extensionPattern = "*.ilsc";
  return specification;
}

void
EditorScene::saveDocument(bool saveAs, std::function<void(bool saved)> done)
{
  if (m_busy) {
    if (done) {
      done(false);
    }
    return;
  }
  if (!saveAs && !document().path().empty()) {
    writeDocument({ document().path(), document().displayName() },
                  std::move(done));
    return;
  }
  m_busy = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().chooseSaveLocation(
    dialogSpec(), [this, alive, done](const IllEdLocation& chosen) {
      if (alive.expired()) {
        return;
      }
      m_busy = false;
      if (chosen.empty()) {
        finishBusy();
        if (done) {
          done(false);
        }
        return;
      }
      writeDocument(chosen, done);
    });
}

// The backup location of a project scene ("vfs:/project/scenes/a.ilsc" ->
// "vfs:/project/scenes/.backup/a.ilsc"), or empty for any other location.
static std::string
backupLocation(const std::string& location)
{
  if (!IllEdPlatform::isTreeLocation(location)) {
    return {};
  }
  const std::string path = IllEdPlatform::treePath(location);
  const std::size_t slash = path.find_last_of('/');
  if (path.rfind("/project/", 0) != 0 || slash == std::string::npos ||
      slash + 1 >= path.size()) {
    return {};
  }
  return std::string(IllEdPlatform::kTreePrefix) + path.substr(0, slash) +
         "/.backup/" + path.substr(slash + 1);
}

void
EditorScene::writeDocument(const IllEdLocation& location,
                            std::function<void(bool saved)> done)
{
  const std::string backup = backupLocation(location.location);
  if (backup.empty()) {
    writeDocumentNow(location, std::move(done));
    return;
  }
  // Keep what is there now; a scene saved for the first time has nothing to
  // keep, and a backup that cannot be written never blocks the save.
  m_busy = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().read(
    location.location,
    [this, alive, location, backup, done](
      bool exists, const std::string& previous, const std::string&) {
      if (alive.expired()) {
        return;
      }
      if (!exists) {
        writeDocumentNow(location, done);
        return;
      }
      IllEdPlatform::current().write(
        backup,
        previous,
        [this, alive, location, backup, done](bool kept,
                                              const std::string& error) {
          if (alive.expired()) {
            return;
          }
          if (!kept) {
            Logger::LogWarning("Could not keep a backup at " + backup + ": " +
                               error);
          }
          writeDocumentNow(location, done);
        });
    });
}

void
EditorScene::writeDocumentNow(const IllEdLocation& location,
                              std::function<void(bool saved)> done)
{
  m_busy = true;
  storeCameraState();
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().write(
    location.location,
    document().encode(),
    [this, alive, location, done](bool saved, const std::string& error) {
      if (alive.expired()) {
        return;
      }
      m_busy = false;
      if (saved) {
        // Input was held while the write was in flight, so nothing changed
        // since the encoded snapshot.
        document().markSaved(location.location, location.label);
        Logger::LogInfo("Saved scene " + document().displayName() + " (" +
                        std::to_string(document().nodeCount()) + " nodes)");
        toast("Saved scene: " + document().displayName(), kToastGood);
      } else {
        if (ic != nullptr && ic->commandLine != nullptr) {
          ic->commandLine->logError(error);
        }
        toast("Failed to save: " + error, kToastBad);
      }
      updateStatus();
      if (done) {
        done(saved);
      }
      finishBusy();
    });
}

void
EditorScene::revertDocument()
{
  if (m_busy) {
    return;
  }
  if (document().path().empty()) {
    toast("Nothing to revert to: the scene was never saved", kToastBad);
    return;
  }
  if (!document().isDirty()) {
    toast("No changes to revert", kToastInfo);
    return;
  }
  m_pendingAction = EditorPendingAction::RevertDocument;
  if (m_confirm) {
    m_confirm->openDiscard("Revert to the saved " + document().displayName() +
                             " and lose the changes?",
                           "Revert");
  }
}

void
EditorScene::applyWindowTitle()
{
  if (ic == nullptr) {
    return;
  }
  std::string title = document().displayName().empty()
                        ? std::string("Untitled")
                        : document().displayName();
  if (document().isDirty()) {
    title += "*";
  }
  title += " - IllEd";
  if (title == m_appliedTitle) {
    return;
  }
  m_appliedTitle = title;
  if (ic->panelSurfaces != nullptr && ic->panelSurfaces->available()) {
    ic->panelSurfaces->setTitle(IPanelSurfaces::kMainSurface, title);
  } else if (ic->window != nullptr) {
    ic->window->setTitle(title);
  }
}

void
EditorScene::openDocument()
{
  if (m_busy) {
    return;
  }
  m_busy = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().chooseOpenLocation(
    dialogSpec(), [this, alive](const IllEdLocation& chosen) {
      if (alive.expired()) {
        return;
      }
      m_busy = false;
      if (chosen.empty()) {
        finishBusy();
        return;
      }
      // Into its own tab (or the active one while it is still empty).
      tabForOpening();
      loadDocument(chosen, false);
    });
}

std::string
EditorScene::documentRoot(const std::string& location) const
{
  std::string root;
  if (IllEdPlatform::isTreeLocation(location) &&
      scenePackageRoot(IllEdPlatform::treePath(location), root)) {
    return root;
  }
  return IllEdPlatform::current().hasProject()
           ? std::string("/project")
           : std::string(EditorDocument::kLocalPackageRoot);
}

void
EditorScene::loadDocument(const IllEdLocation& location, bool initial)
{
  m_busy = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().read(
    location.location,
    [this, alive, location, initial](
      bool success, const std::string& text, const std::string& readError) {
      if (alive.expired()) {
        return;
      }
      // Collect the scene's references and fetch them before it
      // instantiates, so AssetManager finds every byte synchronously.
      SceneDocument parsed;
      std::string error = readError;
      const std::string root = documentRoot(location.location);
      if (!success || !IlscCodec::parse(text, parsed, error)) {
        finishLoad(location, initial, {}, root, { error });
        return;
      }
      // The tab's earlier assets go; other tabs keep theirs.
      const SceneFetchList fetches = collectSceneFetches(parsed, root);
      EditorTab& tab = activeTab();
      if (tab.fetchTicket != 0) {
        IllEdPlatform::current().releaseAssets(tab.fetchTicket);
      }
      tab.fetchTicket = IllEdPlatform::current().fetchAssets(
        fetches.paths,
        [this, alive, location, initial, text, root](
          std::vector<std::string> missing) {
          if (!alive.expired()) {
            finishLoad(location, initial, text, root, missing);
          }
        });
    });
}

void
EditorScene::finishLoad(const IllEdLocation& location,
                         bool initial,
                         const std::string& text,
                         const std::string& packageRoot,
                         const std::vector<std::string>& missing)
{
  m_busy = false;
  std::string error;
  if (!text.empty() && document().loadFromText(text, &error, packageRoot)) {
    document().setLocation(location.location, location.label);
    selection().clear();
    restoreCameraState();
    Logger::LogInfo(
      "Opened scene " + document().displayName() + " (" +
      std::to_string(document().nodeCount()) + " nodes, " +
      std::to_string(document().scene().document().assets.size()) +
      " assets, root " + packageRoot + ")");
    if (!missing.empty() && ic != nullptr && ic->commandLine != nullptr) {
      for (const std::string& path : missing) {
        ic->commandLine->logError("Scene asset is missing: " + path);
      }
    }
    if (!initial) {
      toast(missing.empty()
              ? "Opened scene: " + document().displayName()
              : "Opened scene with " + std::to_string(missing.size()) +
                  " missing asset(s)",
            missing.empty() ? kToastInfo : kToastBad);
    }
  } else {
    if (error.empty() && !missing.empty()) {
      error = missing.front();
    }
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError(error);
    }
    if (initial) {
      document().clear();
      document().rebase(documentRoot({}));
    } else {
      toast("Failed to load: " + error, kToastBad);
    }
  }
  refreshView();
  updateStatus();
  finishBusy();
}

void
EditorScene::saveToProject()
{
  if (!IllEdPlatform::current().hasProject()) {
    toast("Start IllEd with --project <dir> to save into a project", kToastBad);
    return;
  }
  // Keep a project scene's own path; anything else goes to /project/scenes.
  const std::string path = IllEdPlatform::treePath(document().path());
  if (path.rfind("/project/", 0) == 0) {
    saveDocument(false);
    return;
  }
  std::string name = document().displayName();
  const std::size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }
  if (name.empty()) {
    name = "Untitled";
  }
  name = IlscCodec::withExtension(name);
  document().rebase("/project");
  writeDocument(
    { std::string(IllEdPlatform::kTreePrefix) + "/project/scenes/" + name,
      name },
    {});
}

void
EditorScene::importAsset()
{
  if (!IllEdPlatform::current().hasProject()) {
    toast("Start IllEd with --project <dir> to import files", kToastBad);
    return;
  }
  SaveLoadDialogSpec specification;
  specification.fileDescription = "Mesh or texture";
  specification.defaultFilename = "";
  specification.extensionPattern = "*.obj;*.png;*.jpg;*.jpeg;*.tga;*.bmp";
  m_busy = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().importIntoProject(
    specification,
    {},
    [this,
     alive](bool imported, const std::string& path, const std::string& error) {
      if (alive.expired()) {
        return;
      }
      m_busy = false;
      if (imported) {
        Logger::LogInfo("Imported " + path + " into the project");
        toast("Imported " + path, kToastGood);
        if (m_assetBrowser) {
          m_assetBrowser->refresh();
        }
      } else if (!error.empty()) {
        Logger::LogError("Import into the project was refused: " + error);
        toast("Import failed: " + error, kToastBad);
      }
      finishBusy();
    });
}

void
EditorScene::packProject()
{
  if (!IllEdPlatform::current().hasProject()) {
    toast("Start IllEd with --project <dir> to pack a project", kToastBad);
    return;
  }
  SaveLoadDialogSpec specification;
  specification.fileDescription = "Illumo Package";
  specification.defaultFilename = "Project.ilpk";
  specification.extensionPattern = "*.ilpk";
  m_busy = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().packProject(
    specification, [this, alive](bool packed, const std::string& error) {
      if (alive.expired()) {
        return;
      }
      m_busy = false;
      if (packed) {
        Logger::LogInfo("Packed /project into an .ilpk package");
        toast("Packed the project", kToastGood);
      } else if (!error.empty()) {
        Logger::LogError("Packing the project did not complete: " + error);
        toast("Pack failed: " + error, kToastBad);
      }
      finishBusy();
    });
}

void
EditorScene::createLightOrCamera(bool light)
{
  SceneNode node;
  SceneComponent component;
  const bool is3D = document().worldMode() == SceneWorldMode::World3D;
  if (light) {
    node.name = "Light";
    component.value = SceneLight{};
    // A light shines along its local -Y: raised above the plane, it lights
    // the scene from above.
    node.transform = Transform3D::fromPosition(
      is3D ? Vector3(0.0f, 4.0f, 0.0f) : Vector3(0.0f, 0.0f, 4.0f));
  } else {
    node.name = "Camera";
    component.value = SceneCamera{};
    node.transform = Transform3D::fromPosition(
      is3D ? Vector3(0.0f, 2.0f, 6.0f) : Vector3(0.0f, 0.0f, 10.0f));
  }
  node.components.push_back(component);
  const std::string id = document().createNode(node, {});
  if (!id.empty()) {
    selection().set(id);
    toast("Created " + node.name, kToastGood);
  }
}

void
EditorScene::placeDroppedAsset(const std::string& path,
                                float screenX,
                                float screenY)
{
  const EditorAssetKind kind = EditorAssets::kindFor(path);
  if (kind != EditorAssetKind::Mesh && kind != EditorAssetKind::Texture) {
    toast("Only meshes and textures can be placed", kToastBad);
    return;
  }
  const float uiScale =
    ic != nullptr
      ? GuiPanelLayout::viewport(ic->window, ic->renderer).layoutScale
      : 1.0f;
  float worldX = 0.0f;
  float worldY = 0.0f;
  if (uiBlocksWorld(screenX / uiScale, screenY / uiScale) ||
      !screenToWorld(screenX, screenY, &worldX, &worldY)) {
    return;
  }
  placeAssetAt(path, document().makeEditPlaneTransform(worldX, worldY));
}

void
EditorScene::placeAssetAt(const std::string& path,
                           const Transform3D& transform)
{
  const EditorAssetKind kind = EditorAssets::kindFor(path);
  if (kind != EditorAssetKind::Mesh && kind != EditorAssetKind::Texture) {
    toast("Only meshes and textures can be placed", kToastBad);
    return;
  }
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().fetchAssets(
    { path }, [this, alive, path, transform](std::vector<std::string> missing) {
      if (alive.expired()) {
        return;
      }
      if (!missing.empty()) {
        Logger::LogWarning("Asset to place could not be read: " + path);
        toast("Cannot read " + path, kToastBad);
        return;
      }
      const std::string id = document().placeAsset(path, transform);
      if (id.empty()) {
        Logger::LogWarning("The scene refused to place " + path);
        toast("Cannot place " + path, kToastBad);
        return;
      }
      Logger::LogTrace("Placed " + path + " as node " + id);
      selection().set(id);
      toast("Placed " + path, kToastGood);
      refreshView();
    });
}
void
EditorScene::finishBusy()
{
  // A window close that arrived mid-operation is negotiated once the
  // operation has settled.
  if (m_busy || !m_closeAfterBusy) {
    return;
  }
  m_closeAfterBusy = false;
  requestAction(EditorPendingAction::ExitEditor);
}

void
EditorScene::newDocument()
{
  document().clear();
  document().rebase(documentRoot({}));
  selection().clear();
  if (ic != nullptr && ic->camera != nullptr) {
    ic->camera->Reset();
    ic->camera->SetZoom(32.0f);
  }
  activeTab().cameraTargetY = 0.0f;
  Logger::LogInfo("Created a new scene under " + document().packageRoot());
  toast("Created new scene", kToastInfo);
  refreshView();
  updateStatus();
}

void
EditorScene::undo()
{
  std::string label;
  if (document().undo(&label)) {
    selection().prune(document().scene());
    toast("Undo: " + label, kToastInfo);
  }
}

void
EditorScene::redo()
{
  std::string label;
  if (document().redo(&label)) {
    selection().prune(document().scene());
    toast("Redo: " + label, kToastInfo);
  }
}

void
EditorScene::duplicateSelection()
{
  const std::vector<std::string> roots =
    selection().topLevel(document().scene());
  if (roots.empty()) {
    return;
  }
  const std::vector<std::string> created = document().duplicate(roots);
  if (!created.empty()) {
    selection().set(created);
    toast(created.size() == 1
            ? std::string("Duplicated node")
            : "Duplicated " + std::to_string(created.size()) + " nodes",
          kToastGood);
  }
}

void
EditorScene::deleteSelection()
{
  const std::vector<std::string> roots =
    selection().topLevel(document().scene());
  if (roots.empty()) {
    return;
  }
  document().destroyNodes(roots);
  selection().clear();
  toast(roots.size() == 1
          ? std::string("Deleted selected node")
          : "Deleted " + std::to_string(roots.size()) + " nodes",
        kToastBad);
}

void
EditorScene::unparentSelection()
{
  bool changed = false;
  for (const std::string& id : selection().topLevel(document().scene())) {
    changed = document().setParent(id, {}) || changed;
  }
  if (changed) {
    toast("Unparented to root", kToastInfo);
  }
}

void
EditorScene::copySelection(bool cut)
{
  const std::vector<std::string> roots =
    selection().topLevel(document().scene());
  if (roots.empty()) {
    return;
  }
  const std::string text = EditorClipboard::copy(document().scene(), roots);
  if (text.empty()) {
    Logger::LogWarning("Copy refused: the selection exceeds the 4 MiB "
                       "clipboard limit");
    toast("Selection is too large to copy (4 MiB limit)", kToastBad);
    return;
  }
  IllEdPlatform::current().setClipboardText(text);
  const std::string count = roots.size() == 1
                              ? std::string("node")
                              : std::to_string(roots.size()) + " nodes";
  if (cut) {
    document().destroyNodes(roots);
    selection().clear();
    toast("Cut " + count, kToastInfo);
  } else {
    toast("Copied " + count, kToastInfo);
  }
}

void
EditorScene::pasteClipboard()
{
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().requestClipboardText(
    [this, alive](const std::string& text) {
      if (alive.expired()) {
        return;
      }
      const uint64_t revision = document().revision();
      pasteText(text);
      if (document().revision() != revision) {
        refreshView();
      }
    });
}

bool
EditorScene::pasteText(const std::string& text)
{
  SceneDocument fragment;
  std::string error;
  if (!EditorClipboard::read(text, fragment, error)) {
    Logger::LogWarning("Paste refused: " + error);
    toast(error, kToastBad);
    return false;
  }
  std::string parentId;
  std::string insertBefore;
  const std::string primary = selection().primary();
  if (!primary.empty() && document().findNode(primary) != nullptr) {
    parentId = document().scene().parentOf(primary);
    insertBefore = document().scene().nextSiblingId(primary);
  }
  const std::vector<std::string> pasted =
    document().paste(fragment, parentId, insertBefore);
  if (pasted.empty()) {
    Logger::LogWarning("Paste refused: the scene rejected the clipboard "
                       "fragment");
    toast("Could not paste nodes", kToastBad);
    return false;
  }
  selection().set(pasted);
  toast(pasted.size() == 1
          ? std::string("Pasted node")
          : "Pasted " + std::to_string(pasted.size()) + " nodes",
        kToastGood);
  return true;
}

void
EditorScene::toggleSelectionFlag(bool visibility)
{
  const std::vector<std::string>& ids = selection().ids();
  if (ids.empty()) {
    return;
  }
  bool anyOn = false;
  for (const std::string& id : ids) {
    const SceneNode* node = document().findNode(id);
    if (node != nullptr) {
      anyOn = anyOn || (visibility ? node->visible : node->enabled);
    }
  }
  const bool value = !anyOn;
  const std::string label =
    visibility ? (value ? "Show" : "Hide") : (value ? "Enable" : "Disable");
  document().editNodes(ids, label, {}, [visibility, value](SceneNode& node) {
    bool& flag = visibility ? node.visible : node.enabled;
    if (flag == value) {
      return false;
    }
    flag = value;
    return true;
  });
}

void
EditorScene::createChildOfPrimary()
{
  const std::string parent = selection().primary();
  const std::string id = document().createPrimitive(
    true,
    ScenePrimitiveShape::Cube,
    document().findNode(parent) != nullptr ? parent : std::string(),
    Transform3D{});
  if (!id.empty()) {
    selection().set(id);
  }
}

void
EditorScene::selectAll()
{
  std::vector<std::string> ids;
  const SceneGraph& graph = document().graph();
  for (SceneNodeHandle node = graph.firstNode(); !node.isNull();
       node = graph.nextNode(node)) {
    ids.emplace_back(graph.getName(node));
  }
  selection().set(ids);
}

void
EditorScene::nudgeSelectedExtent()
{
  const SceneNode* node = document().findNode(selection().primary());
  const SceneComponent* component =
    node == nullptr ? nullptr : node->find(SceneComponentType::Primitive);
  if (component == nullptr) {
    return;
  }
  const Vector3 extent =
    std::get<ScenePrimitive>(component->value).extent * 1.15f;
  if (document().setExtent(node->id, extent)) {
    toast("Nudged node size", kToastInfo);
  }
}

void
EditorScene::cycleSelectedColor()
{
  const SceneNode* node = document().findNode(selection().primary());
  const SceneComponent* component =
    node == nullptr ? nullptr : node->find(SceneComponentType::Primitive);
  if (component == nullptr) {
    return;
  }
  ColorRgba next = std::get<ScenePrimitive>(component->value).color;
  if (next.r >= 180 && next.g < 180) {
    next = ColorRgba{ 80, 180, 90, 255 };
  } else if (next.g >= 180 && next.b < 180) {
    next = ColorRgba{ 70, 140, 220, 255 };
  } else {
    next = ColorRgba{ 210, 90, 70, 255 };
  }
  if (document().setColor(node->id, next)) {
    toast("Updated node color", next);
  }
}

// Common values the grid and snap steppers walk through.
static const std::vector<float> kGridLadder = { 0.1f, 0.25f, 0.5f, 1.0f,
                                                2.0f, 5.0f,  10.0f };
static const std::vector<float> kMoveLadder = { 0.05f, 0.1f, 0.25f, 0.5f,
                                                1.0f,  2.0f, 5.0f };
static const std::vector<float> kRotateLadder = { 1.0f,  5.0f,  10.0f, 15.0f,
                                                  30.0f, 45.0f, 90.0f };
static const std::vector<float> kScaleLadder = { 0.01f, 0.05f, 0.1f,
                                                 0.25f, 0.5f,  1.0f };

// The next ladder value above (or below) `value`; a value between steps, as
// a hand-edited scene may hold, moves to the nearest step in that direction,
// and the ends hold.
static float
stepLadder(float value, const std::vector<float>& ladder, bool up)
{
  const float tolerance = 1.0e-4f * std::max(1.0f, std::fabs(value));
  if (up) {
    for (const float step : ladder) {
      if (step > value + tolerance) {
        return step;
      }
    }
    return ladder.back();
  }
  for (size_t index = ladder.size(); index > 0; --index) {
    if (ladder[index - 1] < value - tolerance) {
      return ladder[index - 1];
    }
  }
  return ladder.front();
}

bool
EditorScene::handleSettingCommand(EditorCommand command)
{
  SceneEditorState state = document().editorState();
  switch (command) {
    case EditorCommand::ToggleGrid:
      state.gridVisible = !state.gridVisible;
      break;
    case EditorCommand::GridSpacingDown:
    case EditorCommand::GridSpacingUp:
      state.gridSpacing = stepLadder(state.gridSpacing,
                                     kGridLadder,
                                     command == EditorCommand::GridSpacingUp);
      break;
    case EditorCommand::SnapMoveDown:
    case EditorCommand::SnapMoveUp:
      state.snapTranslate = stepLadder(
        state.snapTranslate, kMoveLadder, command == EditorCommand::SnapMoveUp);
      break;
    case EditorCommand::SnapRotateDown:
    case EditorCommand::SnapRotateUp:
      state.snapRotateDegrees =
        stepLadder(state.snapRotateDegrees,
                   kRotateLadder,
                   command == EditorCommand::SnapRotateUp);
      break;
    case EditorCommand::SnapScaleDown:
    case EditorCommand::SnapScaleUp:
      state.snapScale = stepLadder(
        state.snapScale, kScaleLadder, command == EditorCommand::SnapScaleUp);
      break;
    default:
      return false;
  }
  const bool respace =
    state.gridSpacing != document().editorState().gridSpacing;
  // View state is saved with the scene but never marks it dirty.
  document().setEditorState(state);
  if (respace) {
    rebuildGrid();
  }
  return true;
}

bool
EditorScene::handleArrangeCommand(EditorCommand command)
{
  int axis = -1;
  int side = 0;
  bool distribute = false;
  switch (command) {
    case EditorCommand::AlignMinX:
    case EditorCommand::AlignCenterX:
    case EditorCommand::AlignMaxX:
      axis = 0;
      side = command == EditorCommand::AlignMinX   ? -1
             : command == EditorCommand::AlignMaxX ? 1
                                                   : 0;
      break;
    case EditorCommand::AlignMinY:
    case EditorCommand::AlignCenterY:
    case EditorCommand::AlignMaxY:
      axis = 1;
      side = command == EditorCommand::AlignMinY   ? -1
             : command == EditorCommand::AlignMaxY ? 1
                                                   : 0;
      break;
    case EditorCommand::AlignMinZ:
    case EditorCommand::AlignCenterZ:
    case EditorCommand::AlignMaxZ:
      axis = 2;
      side = command == EditorCommand::AlignMinZ   ? -1
             : command == EditorCommand::AlignMaxZ ? 1
                                                   : 0;
      break;
    case EditorCommand::DistributeX:
    case EditorCommand::DistributeY:
    case EditorCommand::DistributeZ:
      distribute = true;
      axis = command == EditorCommand::DistributeX   ? 0
             : command == EditorCommand::DistributeY ? 1
                                                     : 2;
      break;
    default:
      return false;
  }
  const std::vector<std::string> roots =
    selection().topLevel(document().scene());
  const size_t needed = distribute ? 3u : 2u;
  if (roots.size() < needed) {
    toast(distribute ? "Select three or more nodes to distribute"
                     : "Select two or more nodes to align",
          kToastBad);
    return true;
  }
  const bool moved = distribute ? document().distributeNodes(roots, axis)
                                : document().alignNodes(roots, axis, side);
  if (moved) {
    toast(document().history().undoLabel(), kToastInfo);
  }
  return true;
}

void
EditorScene::requestAction(EditorPendingAction action)
{
  m_pendingAction = action;
  if (action == EditorPendingAction::ExitEditor) {
    // Every dirty tab is asked in turn; Save All is offered while more than
    // one remains.
    const std::size_t dirty = firstDirtyTab();
    if (dirty == static_cast<std::size_t>(-1)) {
      performPendingAction();
      return;
    }
    activateTab(dirty);
    std::size_t remaining = 0;
    for (const std::unique_ptr<EditorTab>& tab : m_tabs) {
      remaining += tab->document.isDirty() ? 1 : 0;
    }
    if (m_confirm) {
      const std::string name = document().displayName().empty()
                                 ? std::string("Untitled")
                                 : document().displayName();
      m_confirm->open("Save changes to " + name + " before closing?",
                      remaining > 1);
    }
    return;
  }
  if (document().isDirty()) {
    if (m_confirm) {
      m_confirm->open("Save changes before continuing?");
    }
    return;
  }
  performPendingAction();
}

bool
EditorScene::closeRequested()
{
  if (m_exitApproved) {
    // Another started module may veto this attempt after the editor accepts;
    // the discards of this negotiation are then void too.
    m_exitApproved = false;
    m_exitDiscarded.clear();
    return true;
  }
  if (m_busy) {
    m_closeAfterBusy = true;
    return false;
  }
  if (firstDirtyTab() == static_cast<std::size_t>(-1)) {
    // Clean documents leave no autosave behind: the slots' free writes
    // must land before the process goes.
    bool pending = false;
    for (const std::unique_ptr<EditorTab>& tab : m_tabs) {
      pending = pending || tab->autosave.live || tab->autosave.inFlight;
    }
    if (pending) {
      m_closeAfterAutosave = true;
      freeAllAutosaveSlots({});
      return false;
    }
    return true;
  }
  // Preserve an existing confirmation on repeated close.
  if (!m_confirm || !m_confirm->isOpen()) {
    requestAction(EditorPendingAction::ExitEditor);
  }
  return false;
}

void
EditorScene::performPendingAction()
{
  const EditorPendingAction action = m_pendingAction;
  m_pendingAction = EditorPendingAction::None;
  if (action == EditorPendingAction::RevertDocument) {
    loadDocument({ document().path(), document().displayName() }, false);
  } else if (action == EditorPendingAction::CloseTab) {
    closeTab(m_activeTab);
  } else if (action == EditorPendingAction::ExitEditor) {
    // The next dirty tab is asked before anything closes.
    if (firstDirtyTab() != static_cast<std::size_t>(-1)) {
      requestAction(EditorPendingAction::ExitEditor);
      return;
    }
    if (ic != nullptr && ic->window != nullptr) {
      // Discarded documents must not come back as recoverable work.
      const std::weak_ptr<bool> alive = m_lifetime;
      freeAllAutosaveSlots([this, alive]() {
        if (alive.expired() || ic == nullptr || ic->window == nullptr) {
          return;
        }
        // A project switch restarts the editor on it instead of closing.
        if (m_switchProjectId != 0) {
          finishProjectSwitch();
          return;
        }
        m_exitApproved = true;
        ic->window->requestClose();
      });
    }
  }
}

static bool
isCreateTool(EditorCommand command)
{
  return command == EditorCommand::CreateEmpty ||
         command == EditorCommand::CreateRect ||
         command == EditorCommand::CreateEllipse ||
         command == EditorCommand::CreateTriangle ||
         command == EditorCommand::CreateCube ||
         command == EditorCommand::CreatePyramid ||
         command == EditorCommand::CreateSphere ||
         command == EditorCommand::CreateWireCube ||
         command == EditorCommand::CreateWireSphere;
}

void
EditorScene::handleCommand(EditorCommand command)
{
  const uint64_t revision = document().revision();
  if (command != EditorCommand::None) {
    // A nudge after any other command starts a new undo step.
    ++m_nudgeSerial;
  }
  dispatchCommand(command);
  if (document().revision() != revision) {
    refreshView();
  }
}

void
EditorScene::dispatchCommand(EditorCommand command)
{
  switch (command) {
    case EditorCommand::None:
      return;
    case EditorCommand::NewDocument:
      // Into its own tab (or the active one while it is still empty), so
      // nothing is lost and nothing needs a confirm.
      tabForOpening();
      newDocument();
      return;
    case EditorCommand::OpenDocument:
      openDocument();
      return;
    case EditorCommand::SaveDocument:
      saveDocument(false);
      return;
    case EditorCommand::SaveDocumentAs:
      saveDocument(true);
      return;
    case EditorCommand::SaveAll:
      saveAll({});
      return;
    case EditorCommand::RevertDocument:
      revertDocument();
      return;
    case EditorCommand::CloseTab:
      requestCloseTab();
      return;
    case EditorCommand::NextTab:
      cycleTab(1);
      return;
    case EditorCommand::PreviousTab:
      cycleTab(-1);
      return;
    case EditorCommand::SaveToProject:
      saveToProject();
      return;
    case EditorCommand::ImportAsset:
      importAsset();
      return;
    case EditorCommand::PackProject:
      packProject();
      return;
    case EditorCommand::OpenProject:
      openProject(false);
      return;
    case EditorCommand::NewProject:
      openProject(true);
      return;
    case EditorCommand::ClearRecentProjects: {
      const std::weak_ptr<bool> alive = m_lifetime;
      IllEdPlatform::current().clearProjects([this, alive](bool) {
        if (!alive.expired()) {
          refreshProjects();
        }
      });
      return;
    }
    case EditorCommand::RecentProject0:
    case EditorCommand::RecentProject1:
    case EditorCommand::RecentProject2:
    case EditorCommand::RecentProject3:
    case EditorCommand::RecentProject4:
    case EditorCommand::RecentProject5:
    case EditorCommand::RecentProject6:
    case EditorCommand::RecentProject7:
    case EditorCommand::RecentProject8:
    case EditorCommand::RecentProject9: {
      const std::size_t index = static_cast<std::size_t>(
        static_cast<int>(command) -
        static_cast<int>(EditorCommand::RecentProject0));
      if (index < m_recentProjects.size()) {
        switchProject(m_recentProjects[index].id);
      }
      return;
    }
    case EditorCommand::CreateLight:
      createLightOrCamera(true);
      return;
    case EditorCommand::CreateCamera:
      createLightOrCamera(false);
      return;
    case EditorCommand::ExitEditor:
      requestAction(EditorPendingAction::ExitEditor);
      return;
    case EditorCommand::Undo:
      undo();
      return;
    case EditorCommand::Redo:
      redo();
      return;
    case EditorCommand::Cut:
      copySelection(true);
      return;
    case EditorCommand::Copy:
      copySelection(false);
      return;
    case EditorCommand::Paste:
      pasteClipboard();
      return;
    case EditorCommand::ToggleVisible:
      toggleSelectionFlag(true);
      return;
    case EditorCommand::ToggleEnabled:
      toggleSelectionFlag(false);
      return;
    case EditorCommand::CreateChild:
      createChildOfPrimary();
      return;
    case EditorCommand::Duplicate:
      duplicateSelection();
      return;
    case EditorCommand::SelectAll:
      selectAll();
      return;
    case EditorCommand::DeselectAll:
      selection().clear();
      if (m_activeTool != EditorCommand::SelectTool) {
        m_activeTool = EditorCommand::SelectTool;
      }
      return;
    case EditorCommand::Rename:
      if (m_inspector && selection().size() == 1) {
        m_inspector->activateField("name", &document(), &selection());
      }
      return;
    case EditorCommand::DeleteNode:
      deleteSelection();
      return;
    case EditorCommand::UnparentNode:
      unparentSelection();
      return;
    case EditorCommand::SelectTool:
      m_activeTool = EditorCommand::SelectTool;
      return;
    case EditorCommand::TranslateMode:
    case EditorCommand::RotateMode:
    case EditorCommand::ScaleMode:
      m_activeTool = EditorCommand::SelectTool;
      m_gizmoMode = command == EditorCommand::TranslateMode
                      ? GizmoMode::Translate
                    : command == EditorCommand::RotateMode ? GizmoMode::Rotate
                                                           : GizmoMode::Scale;
      rebuildSelectionOverlay();
      return;
    case EditorCommand::ToggleGizmoSpace:
      m_gizmoSpace = m_gizmoSpace == GizmoSpace::World ? GizmoSpace::Local
                                                       : GizmoSpace::World;
      toast(m_gizmoSpace == GizmoSpace::World ? "Gizmo: world axes"
                                              : "Gizmo: local axes",
            kToastInfo);
      rebuildSelectionOverlay();
      return;
    case EditorCommand::ToggleSnap: {
      SceneEditorState state = document().editorState();
      state.snapEnabled = !state.snapEnabled;
      document().setEditorState(state);
      toast(state.snapEnabled ? "Snapping on" : "Snapping off", kToastInfo);
      return;
    }
    case EditorCommand::SetMode2D:
      if (document().setWorldMode(SceneWorldMode::World2D)) {
        toast("Mode: 2D Orthographic (XY)", kToastGood);
      }
      return;
    case EditorCommand::SetMode3D:
      if (document().setWorldMode(SceneWorldMode::World3D)) {
        toast("Mode: 3D Perspective (XZ)", ColorRgba{ 70, 160, 255, 255 });
      }
      return;
    case EditorCommand::NudgeExtent:
      nudgeSelectedExtent();
      return;
    case EditorCommand::CycleColor:
      cycleSelectedColor();
      return;
    case EditorCommand::ResetCamera:
      if (ic != nullptr && ic->camera != nullptr) {
        ic->camera->Reset();
        activeTab().cameraTargetY = 0.0f;
        SceneEditorState state = document().editorState();
        state.yaw = SceneEditorState{}.yaw;
        state.pitch = SceneEditorState{}.pitch;
        document().setEditorState(state);
        // The origin sits at the centre of the visible viewport.
        placeCamera(Vector3(0.0f), 32.0f);
        toast("Camera reset to origin", kToastInfo);
      }
      return;
    case EditorCommand::FrameSelection:
      frameSelection();
      return;
    case EditorCommand::PlayScene:
      togglePlay();
      return;
    case EditorCommand::GroupSelection:
      groupSelection();
      return;
    case EditorCommand::UngroupSelection:
      ungroupSelection();
      return;
    case EditorCommand::SelectParent:
      selectRelatives(true);
      return;
    case EditorCommand::SelectChildren:
      selectRelatives(false);
      return;
    case EditorCommand::ToggleLock:
      toggleLockSelection();
      return;
    case EditorCommand::ToggleIsolate:
      toggleIsolate();
      return;
    case EditorCommand::DropToFloor:
      dropSelectionToFloor();
      return;
    case EditorCommand::FindInHierarchy:
      // The filter lives in the Hierarchy panel, so it must be shown.
      if (m_dock.mode("hierarchy") == GuiDockMode::Hidden) {
        m_dock.setHidden("hierarchy", false);
        layoutDock();
      }
      if (m_sceneGraphView) {
        m_sceneGraphView->focusFilter();
      }
      return;
    case EditorCommand::TogglePivot:
      m_pivotCenter = !m_pivotCenter;
      toast(m_pivotCenter ? "Pivot: selection center"
                          : "Pivot: primary node origin",
            kToastInfo);
      rebuildSelectionOverlay();
      return;
    default:
      break;
  }
  if (handleSettingCommand(command) || handleArrangeCommand(command) ||
      handlePanelCommand(command)) {
    return;
  }
  if (isCreateTool(command)) {
    m_activeTool = command;
  }
}
