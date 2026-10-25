#include "EditorScene.h"

#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Services/Logger.h>
#include <utility>

// Tabs (docs/illed-editing-wave4-plan.md, 5.4). Every open document is an
// EditorTab the scene owns; the active one is bound to the render world,
// drawn, edited and the target of every busy file operation. Switching is
// refused while one is in flight, so a completion always finds the tab it
// started on still active.

static const ColorRgba kTabToastInfo{ 66, 214, 210, 255 };
static const ColorRgba kTabToastBad{ 245, 100, 110, 255 };

static constexpr std::size_t kNoTab = static_cast<std::size_t>(-1);

EditorTab&
EditorScene::openTab()
{
  std::unique_ptr<EditorTab> tab = std::make_unique<EditorTab>();
  tab->serial = m_nextTabSerial++;
  tab->document.setBehaviourSchema(&m_behaviours.schema());
  if (ic != nullptr) {
    bindTab(*tab, false);
    tab->document.rebase(documentRoot({}));
  }
  tab->autosave.seenUid = tab->document.history().currentUid();
  m_tabs.push_back(std::move(tab));
  return *m_tabs.back();
}

void
EditorScene::bindTab(EditorTab& tab, bool active)
{
  if (ic == nullptr) {
    return;
  }
  tab.document.setAssetManager(ic->assetManager);
  tab.document.setRenderer(ic->renderer);
  // Only the active document draws through the shared world; a bound
  // inactive one would still draw and set the sun and sky.
  tab.document.setRenderWorld(active ? world() : nullptr);
}

bool
EditorScene::activateTab(std::size_t index)
{
  if (index >= m_tabs.size()) {
    return false;
  }
  if (index == m_activeTab) {
    return true;
  }
  if (m_busy) {
    toast("Wait for the current file operation to finish", kTabToastBad);
    return false;
  }
  // What the old tab leaves behind: a typed value lands, the camera and the
  // hierarchy's view state are kept for its return.
  if (m_inspector && m_inspector->editing()) {
    m_inspector->finishEdit(document(), selection());
  }
  storeCameraState();
  EditorTab& previous = activeTab();
  if (m_sceneGraphView) {
    previous.hierarchy = m_sceneGraphView->takeState();
  }
  bindTab(previous, false);
  m_dragging = false;
  m_activeGizmoPart = GizmoPart::None;
  m_boxSelecting = false;
  m_dropPreviewShown = false;

  m_activeTab = index;
  EditorTab& next = activeTab();
  bindTab(next, true);
  if (m_sceneGraphView) {
    m_sceneGraphView->applyState(next.hierarchy);
  }
  restoreCameraState();
  // The grid follows the document's world mode and the overlay its
  // selection.
  m_gridBuilt = false;
  refreshView();
  updateStatus();
  return true;
}

void
EditorScene::closeTab(std::size_t index)
{
  if (index >= m_tabs.size()) {
    return;
  }
  EditorTab& closing = *m_tabs[index];
  if (closing.fetchTicket != 0) {
    IllEdPlatform::current().releaseAssets(closing.fetchTicket);
    closing.fetchTicket = 0;
  }
  // The slot goes with the tab; its free write may still land after.
  if (closing.autosave.live || closing.autosave.inFlight) {
    freeAutosaveSlot(closing, {});
  }
  if (closing.autosave.slot >= 0) {
    m_slotTaken[static_cast<std::size_t>(closing.autosave.slot)] = false;
  }
  const std::string name = closing.document.displayName().empty()
                             ? std::string("Untitled")
                             : closing.document.displayName();
  if (m_tabs.size() == 1) {
    // The last tab becomes an empty document rather than nothing.
    closing.document.clear();
    closing.document.rebase(documentRoot({}));
    closing.selection.clear();
    closing.autosave = EditorTab::Autosave{};
    closing.hierarchy = EditorHierarchyState{};
    closing.isolated = false;
    closing.hoverId.clear();
    if (m_sceneGraphView) {
      m_sceneGraphView->applyState(closing.hierarchy);
    }
    refreshView();
    updateStatus();
    Logger::LogInfo("Closed " + name);
    return;
  }
  const bool wasActive = index == m_activeTab;
  // Deactivate first so the neighbour takes the world and the camera.
  if (wasActive) {
    activateTab(index + 1 < m_tabs.size() ? index + 1 : index - 1);
  }
  const std::size_t activeSerialIndex = m_activeTab;
  m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
  m_activeTab =
    activeSerialIndex > index ? activeSerialIndex - 1 : activeSerialIndex;
  Logger::LogInfo("Closed " + name);
  updateStatus();
}

EditorTab*
EditorScene::tabBySerial(uint64_t serial)
{
  for (std::unique_ptr<EditorTab>& tab : m_tabs) {
    if (tab->serial == serial) {
      return tab.get();
    }
  }
  return nullptr;
}

std::size_t
EditorScene::tabIndex(uint64_t serial) const
{
  for (std::size_t index = 0; index < m_tabs.size(); ++index) {
    if (m_tabs[index]->serial == serial) {
      return index;
    }
  }
  return kNoTab;
}

bool
EditorScene::pristine(const EditorTab& tab)
{
  return !tab.document.isDirty() && tab.document.path().empty() &&
         tab.document.displayName().empty() && tab.document.nodeCount() == 0 &&
         tab.document.history().currentUid() == 0;
}

EditorTab&
EditorScene::tabForOpening()
{
  if (pristine(activeTab())) {
    return activeTab();
  }
  EditorTab& opened = openTab();
  activateTab(m_tabs.size() - 1);
  return opened;
}

void
EditorScene::requestCloseTab()
{
  if (m_busy) {
    return;
  }
  if (!document().isDirty()) {
    closeTab(m_activeTab);
    return;
  }
  m_pendingAction = EditorPendingAction::CloseTab;
  if (m_confirm) {
    const std::string name = document().displayName().empty()
                               ? std::string("Untitled")
                               : document().displayName();
    m_confirm->open("Save changes to " + name + " before closing it?");
  }
}

void
EditorScene::cycleTab(int direction)
{
  if (m_tabs.size() < 2) {
    return;
  }
  const std::size_t count = m_tabs.size();
  const std::size_t next = direction > 0 ? (m_activeTab + 1) % count
                                         : (m_activeTab + count - 1) % count;
  activateTab(next);
}

std::size_t
EditorScene::firstDirtyTab() const
{
  for (std::size_t index = 0; index < m_tabs.size(); ++index) {
    const EditorTab& tab = *m_tabs[index];
    bool discarded = false;
    for (uint64_t serial : m_exitDiscarded) {
      discarded = discarded || serial == tab.serial;
    }
    if (tab.document.isDirty() && !discarded) {
      return index;
    }
  }
  return kNoTab;
}

void
EditorScene::syncTabStrip()
{
  if (!m_toolbar) {
    return;
  }
  std::vector<EditorTabEntry> entries;
  entries.reserve(m_tabs.size());
  for (std::size_t index = 0; index < m_tabs.size(); ++index) {
    const EditorDocument& entry = m_tabs[index]->document;
    EditorTabEntry tab;
    tab.label = entry.displayName().empty() ? std::string("Untitled")
                                            : entry.displayName();
    tab.dirty = entry.isDirty();
    tab.active = index == m_activeTab;
    entries.push_back(std::move(tab));
  }
  m_toolbar->setTabs(entries);
}

void
EditorScene::queueOpen(const IllEdLocation& location)
{
  PendingOpen pending;
  pending.location = location;
  m_openQueue.push_back(std::move(pending));
}

void
EditorScene::drainOpenQueue()
{
  if (m_openQueue.empty() || m_busy) {
    return;
  }
  PendingOpen next = std::move(m_openQueue.front());
  m_openQueue.erase(m_openQueue.begin());
  if (next.recovery) {
    tabForOpening();
    recoverInto(next.recovery);
  } else if (!next.location.empty()) {
    tabForOpening();
    loadDocument(next.location, true);
  }
  if (!m_openQueue.empty() ||
      m_openQueueActivateIndex == static_cast<std::size_t>(-1)) {
    return;
  }
  // The remembered tab becomes active once the last load has settled
  // (loads pin the active tab); an empty entry drains after it.
  if (m_busy) {
    m_openQueue.push_back(PendingOpen{});
    return;
  }
  const std::size_t index = m_openQueueActivateIndex;
  m_openQueueActivateIndex = static_cast<std::size_t>(-1);
  activateTab(index);
}
void
EditorScene::saveAll(std::function<void(bool saved)> done)
{
  if (!m_saveAllQueue.empty() || m_busy) {
    if (done) {
      done(false);
    }
    return;
  }
  for (const std::unique_ptr<EditorTab>& tab : m_tabs) {
    if (tab->document.isDirty()) {
      m_saveAllQueue.push_back(tab->serial);
    }
  }
  if (m_saveAllQueue.empty()) {
    if (done) {
      done(true);
    }
    return;
  }
  m_saveAllDone = std::move(done);
  continueSaveAll(true);
}

void
EditorScene::continueSaveAll(bool saved)
{
  if (!saved || m_saveAllQueue.empty()) {
    m_saveAllQueue.clear();
    std::function<void(bool saved)> done = std::move(m_saveAllDone);
    m_saveAllDone = nullptr;
    if (saved) {
      toast("Saved every scene", kTabToastInfo);
    }
    if (done) {
      done(saved);
    }
    return;
  }
  // Each save runs on the active tab, so the tab to save becomes active
  // (a document without a location asks for one there).
  const uint64_t serial = m_saveAllQueue.front();
  m_saveAllQueue.erase(m_saveAllQueue.begin());
  const std::size_t index = tabIndex(serial);
  if (index == kNoTab || !activateTab(index)) {
    continueSaveAll(false);
    return;
  }
  const std::weak_ptr<bool> alive = m_lifetime;
  saveDocument(false, [this, alive](bool ok) {
    if (!alive.expired()) {
      continueSaveAll(ok);
    }
  });
}

// ---------------------------------------------------------------------------
// Projects (docs/illed-editing-wave4-plan.md, 5.5).

void
EditorScene::refreshProjects()
{
  IllEdPlatform& platform = IllEdPlatform::current();
  if (!platform.canSwitchProject()) {
    m_recentProjects.clear();
    m_projectsListed = true;
    if (m_toolbar) {
      m_toolbar->setRecentProjects({}, false);
    }
    return;
  }
  const std::weak_ptr<bool> alive = m_lifetime;
  platform.listProjects(
    [this, alive](bool ok, std::vector<IllEdPlatform::ProjectEntry> entries) {
      if (alive.expired()) {
        return;
      }
      m_recentProjects = ok ? std::move(entries)
                            : std::vector<IllEdPlatform::ProjectEntry>{};
      m_projectsListed = true;
      std::vector<std::string> labels;
      for (const IllEdPlatform::ProjectEntry& entry : m_recentProjects) {
        labels.push_back(entry.label);
      }
      if (m_toolbar) {
        m_toolbar->setRecentProjects(labels, true);
      }
    });
}

void
EditorScene::openProject(bool create)
{
  IllEdPlatform& platform = IllEdPlatform::current();
  if (!platform.canSwitchProject()) {
    toast("This host keeps no projects; start IllEd with --project <dir>",
          kTabToastBad);
    return;
  }
  if (m_busy) {
    return;
  }
  // The folder dialog holds the editor like the file dialogs do.
  m_busy = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  const IllEdPlatform::ProjectPickCallback done =
    [this, alive, create](
      bool ok, bool cancelled, IllEdPlatform::ProjectEntry entry) {
      if (alive.expired()) {
        return;
      }
      m_busy = false;
      finishBusy();
      if (!ok) {
        toast(create ? "Could not create the project"
                     : "Could not open the project",
              kTabToastBad);
        return;
      }
      if (cancelled) {
        return;
      }
      refreshProjects();
      switchProject(entry.id);
    };
  if (create) {
    platform.createProject(done);
  } else {
    platform.chooseProject(done);
  }
}

void
EditorScene::switchProject(std::uint32_t id)
{
  if (id == 0 || m_busy) {
    return;
  }
  // Settled like an exit: every dirty tab is asked, then the restart.
  m_switchProjectId = id;
  requestAction(EditorPendingAction::ExitEditor);
}

void
EditorScene::finishProjectSwitch()
{
  const std::uint32_t id = m_switchProjectId;
  m_switchProjectId = 0;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().openProject(id, [this, alive](bool ok) {
    if (alive.expired()) {
      return;
    }
    if (!ok || ic == nullptr || ic->window == nullptr) {
      toast("Could not switch to the project", kTabToastBad);
      return;
    }
    Logger::LogInfo("Restarting the editor on the chosen project");
    m_exitApproved = true;
    ic->window->requestRestart();
  });
}