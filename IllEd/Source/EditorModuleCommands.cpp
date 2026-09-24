#include "EditorClipboard.h"
#include "EditorModule.h"

#include "EditorAssets.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneAssetRefs.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/CommandLine.h>
#include <utility>

static const ColorRgba kToastGood{ 60, 220, 120, 255 };
static const ColorRgba kToastInfo{ 66, 214, 210, 255 };
static const ColorRgba kToastBad{ 245, 100, 110, 255 };

void
EditorModule::toast(const std::string& message, ColorRgba color)
{
  if (m_toolbar) {
    m_toolbar->showToast(message, color);
  }
}

SaveLoadDialogSpec
EditorModule::dialogSpec() const
{
  SaveLoadDialogSpec specification;
  specification.fileDescription = "Illumo Scene";
  specification.defaultFilename = "Scene.ilsc";
  specification.extensionPattern = "*.ilsc";
  return specification;
}

void
EditorModule::saveDocument(bool saveAs, std::function<void(bool saved)> done)
{
  if (m_busy) {
    if (done) {
      done(false);
    }
    return;
  }
  if (!saveAs && !m_document.path().empty()) {
    writeDocument({ m_document.path(), m_document.displayName() },
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

void
EditorModule::writeDocument(const IllEdLocation& location,
                            std::function<void(bool saved)> done)
{
  m_busy = true;
  storeCameraState();
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().write(
    location.location,
    m_document.encode(),
    [this, alive, location, done](bool saved, const std::string& error) {
      if (alive.expired()) {
        return;
      }
      m_busy = false;
      if (saved) {
        // Input was held while the write was in flight, so nothing changed
        // since the encoded snapshot.
        m_document.markSaved(location.location, location.label);
        toast("Saved scene: " + m_document.displayName(), kToastGood);
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
EditorModule::openDocument()
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
      loadDocument(chosen, false);
    });
}

std::string
EditorModule::documentRoot(const std::string& location) const
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
EditorModule::loadDocument(const IllEdLocation& location, bool initial)
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
      const SceneFetchList fetches = collectSceneFetches(parsed, root);
      IllEdPlatform::current().releaseAssets();
      IllEdPlatform::current().fetchAssets(
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
EditorModule::finishLoad(const IllEdLocation& location,
                         bool initial,
                         const std::string& text,
                         const std::string& packageRoot,
                         const std::vector<std::string>& missing)
{
  m_busy = false;
  std::string error;
  if (!text.empty() && m_document.loadFromText(text, &error, packageRoot)) {
    m_document.setLocation(location.location, location.label);
    m_selection.clear();
    restoreCameraState();
    if (!missing.empty() && ic != nullptr && ic->commandLine != nullptr) {
      for (const std::string& path : missing) {
        ic->commandLine->logError("Scene asset is missing: " + path);
      }
    }
    if (!initial) {
      toast(missing.empty()
              ? "Opened scene: " + m_document.displayName()
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
      m_document.clear();
      m_document.rebase(documentRoot({}));
    } else {
      toast("Failed to load: " + error, kToastBad);
    }
  }
  refreshView();
  updateStatus();
  finishBusy();
}

void
EditorModule::saveToProject()
{
  if (!IllEdPlatform::current().hasProject()) {
    toast("Start IllEd with --project <dir> to save into a project", kToastBad);
    return;
  }
  // Keep a project scene's own path; anything else goes to /project/scenes.
  const std::string path = IllEdPlatform::treePath(m_document.path());
  if (path.rfind("/project/", 0) == 0) {
    saveDocument(false);
    return;
  }
  std::string name = m_document.displayName();
  const std::size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }
  if (name.empty()) {
    name = "Untitled";
  }
  name = IlscCodec::withExtension(name);
  m_document.rebase("/project");
  writeDocument(
    { std::string(IllEdPlatform::kTreePrefix) + "/project/scenes/" + name,
      name },
    {});
}

void
EditorModule::importAsset()
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
        toast("Imported " + path, kToastGood);
        if (m_assetBrowser) {
          m_assetBrowser->refresh();
        }
      } else if (!error.empty()) {
        toast("Import failed: " + error, kToastBad);
      }
      finishBusy();
    });
}

void
EditorModule::packProject()
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
        toast("Packed the project", kToastGood);
      } else if (!error.empty()) {
        toast("Pack failed: " + error, kToastBad);
      }
      finishBusy();
    });
}

void
EditorModule::createLightOrCamera(bool light)
{
  SceneNode node;
  SceneComponent component;
  const bool is3D = m_document.worldMode() == SceneWorldMode::World3D;
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
  const std::string id = m_document.createNode(node, {});
  if (!id.empty()) {
    m_selection.set(id);
    toast("Created " + node.name, kToastGood);
  }
}

void
EditorModule::placeDroppedAsset(const std::string& path,
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
  placeAssetAt(path, m_document.makeEditPlaneTransform(worldX, worldY));
}

void
EditorModule::placeAssetAt(const std::string& path,
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
        toast("Cannot read " + path, kToastBad);
        return;
      }
      const std::string id = m_document.placeAsset(path, transform);
      if (id.empty()) {
        toast("Cannot place " + path, kToastBad);
        return;
      }
      m_selection.set(id);
      toast("Placed " + path, kToastGood);
      refreshView();
    });
}
void
EditorModule::finishBusy()
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
EditorModule::newDocument()
{
  m_document.clear();
  m_document.rebase(documentRoot({}));
  m_selection.clear();
  if (ic != nullptr && ic->camera != nullptr) {
    ic->camera->Reset();
    ic->camera->SetZoom(32.0f);
  }
  m_cameraTargetY = 0.0f;
  toast("Created new scene", kToastInfo);
  refreshView();
  updateStatus();
}

void
EditorModule::undo()
{
  std::string label;
  if (m_document.undo(&label)) {
    m_selection.prune(m_document.scene());
    toast("Undo: " + label, kToastInfo);
  }
}

void
EditorModule::redo()
{
  std::string label;
  if (m_document.redo(&label)) {
    m_selection.prune(m_document.scene());
    toast("Redo: " + label, kToastInfo);
  }
}

void
EditorModule::duplicateSelection()
{
  const std::vector<std::string> roots =
    m_selection.topLevel(m_document.scene());
  if (roots.empty()) {
    return;
  }
  const std::vector<std::string> created = m_document.duplicate(roots);
  if (!created.empty()) {
    m_selection.set(created);
    toast(created.size() == 1
            ? std::string("Duplicated node")
            : "Duplicated " + std::to_string(created.size()) + " nodes",
          kToastGood);
  }
}

void
EditorModule::deleteSelection()
{
  const std::vector<std::string> roots =
    m_selection.topLevel(m_document.scene());
  if (roots.empty()) {
    return;
  }
  m_document.destroyNodes(roots);
  m_selection.clear();
  toast(roots.size() == 1
          ? std::string("Deleted selected node")
          : "Deleted " + std::to_string(roots.size()) + " nodes",
        kToastBad);
}

void
EditorModule::unparentSelection()
{
  bool changed = false;
  for (const std::string& id : m_selection.topLevel(m_document.scene())) {
    changed = m_document.setParent(id, {}) || changed;
  }
  if (changed) {
    toast("Unparented to root", kToastInfo);
  }
}

void
EditorModule::copySelection(bool cut)
{
  const std::vector<std::string> roots =
    m_selection.topLevel(m_document.scene());
  if (roots.empty()) {
    return;
  }
  const std::string text = EditorClipboard::copy(m_document.scene(), roots);
  if (text.empty()) {
    toast("Selection is too large to copy (4 MiB limit)", kToastBad);
    return;
  }
  IllEdPlatform::current().setClipboardText(text);
  const std::string count = roots.size() == 1
                              ? std::string("node")
                              : std::to_string(roots.size()) + " nodes";
  if (cut) {
    m_document.destroyNodes(roots);
    m_selection.clear();
    toast("Cut " + count, kToastInfo);
  } else {
    toast("Copied " + count, kToastInfo);
  }
}

void
EditorModule::pasteClipboard()
{
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().requestClipboardText(
    [this, alive](const std::string& text) {
      if (alive.expired()) {
        return;
      }
      const uint64_t revision = m_document.revision();
      pasteText(text);
      if (m_document.revision() != revision) {
        refreshView();
      }
    });
}

bool
EditorModule::pasteText(const std::string& text)
{
  SceneDocument fragment;
  std::string error;
  if (!EditorClipboard::read(text, fragment, error)) {
    toast(error, kToastBad);
    return false;
  }
  std::string parentId;
  std::string insertBefore;
  const std::string primary = m_selection.primary();
  if (!primary.empty() && m_document.findNode(primary) != nullptr) {
    parentId = m_document.scene().parentOf(primary);
    insertBefore = m_document.scene().nextSiblingId(primary);
  }
  const std::vector<std::string> pasted =
    m_document.paste(fragment, parentId, insertBefore);
  if (pasted.empty()) {
    toast("Could not paste nodes", kToastBad);
    return false;
  }
  m_selection.set(pasted);
  toast(pasted.size() == 1
          ? std::string("Pasted node")
          : "Pasted " + std::to_string(pasted.size()) + " nodes",
        kToastGood);
  return true;
}

void
EditorModule::toggleSelectionFlag(bool visibility)
{
  const std::vector<std::string>& ids = m_selection.ids();
  if (ids.empty()) {
    return;
  }
  bool anyOn = false;
  for (const std::string& id : ids) {
    const SceneNode* node = m_document.findNode(id);
    if (node != nullptr) {
      anyOn = anyOn || (visibility ? node->visible : node->enabled);
    }
  }
  const bool value = !anyOn;
  const std::string label =
    visibility ? (value ? "Show" : "Hide") : (value ? "Enable" : "Disable");
  m_document.editNodes(ids, label, {}, [visibility, value](SceneNode& node) {
    bool& flag = visibility ? node.visible : node.enabled;
    if (flag == value) {
      return false;
    }
    flag = value;
    return true;
  });
}

void
EditorModule::createChildOfPrimary()
{
  const std::string parent = m_selection.primary();
  const std::string id = m_document.createPrimitive(
    true,
    ScenePrimitiveShape::Cube,
    m_document.findNode(parent) != nullptr ? parent : std::string(),
    Transform3D{});
  if (!id.empty()) {
    m_selection.set(id);
  }
}

void
EditorModule::selectAll()
{
  std::vector<std::string> ids;
  const SceneGraph& graph = m_document.graph();
  for (SceneNodeHandle node = graph.firstNode(); !node.isNull();
       node = graph.nextNode(node)) {
    ids.emplace_back(graph.getName(node));
  }
  m_selection.set(ids);
}

void
EditorModule::nudgeSelectedExtent()
{
  const SceneNode* node = m_document.findNode(m_selection.primary());
  const SceneComponent* component =
    node == nullptr ? nullptr : node->find(SceneComponentType::Primitive);
  if (component == nullptr) {
    return;
  }
  const Vector3 extent =
    std::get<ScenePrimitive>(component->value).extent * 1.15f;
  if (m_document.setExtent(node->id, extent)) {
    toast("Nudged node size", kToastInfo);
  }
}

void
EditorModule::cycleSelectedColor()
{
  const SceneNode* node = m_document.findNode(m_selection.primary());
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
  if (m_document.setColor(node->id, next)) {
    toast("Updated node color", next);
  }
}

void
EditorModule::requestAction(EditorPendingAction action)
{
  if (m_document.isDirty()) {
    m_pendingAction = action;
    if (m_confirm) {
      m_confirm->open("Save changes before continuing?");
    }
    return;
  }
  m_pendingAction = action;
  performPendingAction();
}

bool
EditorModule::OnCloseRequested()
{
  if (m_exitApproved) {
    // Another started module may veto this attempt after the editor accepts.
    m_exitApproved = false;
    return true;
  }
  if (m_busy) {
    m_closeAfterBusy = true;
    return false;
  }
  if (!m_document.isDirty()) {
    return true;
  }
  // Preserve an existing confirmation (including New/Open) on repeated close.
  if (!m_confirm || !m_confirm->isOpen()) {
    requestAction(EditorPendingAction::ExitEditor);
  }
  return false;
}

void
EditorModule::performPendingAction()
{
  const EditorPendingAction action = m_pendingAction;
  m_pendingAction = EditorPendingAction::None;
  if (action == EditorPendingAction::NewDocument) {
    newDocument();
  } else if (action == EditorPendingAction::OpenDocument) {
    openDocument();
  } else if (action == EditorPendingAction::OpenLocation) {
    loadDocument(m_pendingLocation, false);
  } else if (action == EditorPendingAction::ExitEditor) {
    if (ic != nullptr && ic->window != nullptr) {
      m_exitApproved = true;
      ic->window->requestClose();
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
EditorModule::handleCommand(EditorCommand command)
{
  const uint64_t revision = m_document.revision();
  dispatchCommand(command);
  if (m_document.revision() != revision) {
    refreshView();
  }
}

void
EditorModule::dispatchCommand(EditorCommand command)
{
  switch (command) {
    case EditorCommand::None:
      return;
    case EditorCommand::NewDocument:
      requestAction(EditorPendingAction::NewDocument);
      return;
    case EditorCommand::OpenDocument:
      requestAction(EditorPendingAction::OpenDocument);
      return;
    case EditorCommand::SaveDocument:
      saveDocument(false);
      return;
    case EditorCommand::SaveDocumentAs:
      saveDocument(true);
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
      m_selection.clear();
      if (m_activeTool != EditorCommand::SelectTool) {
        m_activeTool = EditorCommand::SelectTool;
      }
      return;
    case EditorCommand::Rename:
      if (m_inspector && m_selection.size() == 1) {
        m_inspector->activateField("name", &m_document, &m_selection);
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
      SceneEditorState state = m_document.editorState();
      state.snapEnabled = !state.snapEnabled;
      m_document.setEditorState(state);
      toast(state.snapEnabled ? "Snapping on" : "Snapping off", kToastInfo);
      return;
    }
    case EditorCommand::SetMode2D:
      if (m_document.setWorldMode(SceneWorldMode::World2D)) {
        toast("Mode: 2D Orthographic (XY)", kToastGood);
      }
      return;
    case EditorCommand::SetMode3D:
      if (m_document.setWorldMode(SceneWorldMode::World3D)) {
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
        ic->camera->SetZoom(32.0f);
        m_cameraTargetY = 0.0f;
        SceneEditorState state = m_document.editorState();
        state.yaw = SceneEditorState{}.yaw;
        state.pitch = SceneEditorState{}.pitch;
        m_document.setEditorState(state);
        toast("Camera reset to origin", kToastInfo);
      }
      return;
    case EditorCommand::FrameSelection:
      frameSelection();
      return;
    default:
      break;
  }
  if (handlePanelCommand(command)) {
    return;
  }
  if (isCreateTool(command)) {
    m_activeTool = command;
  }
}
