#include "EditorScene.h"

#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneAssetRefs.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Foundation/ParseNumber.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

// Autosave and recovery (docs/illed-editing-wave4-plan.md, 5.2).
//
// Every dirty tab is written to its own storage slot once its last edit is
// kAutosaveIdleSeconds old, or kAutosaveLatestSeconds after the slot last
// matched it while edits keep coming, and right before Play. Autosave never
// holds input (m_busy) and never changes what a document considers saved.
// A slot is emptied as soon as its document is clean, so a live slot at the
// next start means work was lost. Storage cannot be listed or deleted
// through the file protocol, so the slots are a fixed set of flat names
// that are read one by one.

static const ColorRgba kAutosaveToastInfo{ 66, 214, 210, 255 };
static const ColorRgba kAutosaveToastBad{ 245, 100, 110, 255 };

static constexpr double kAutosaveIdleSeconds = 30.0;
static constexpr double kAutosaveLatestSeconds = 120.0;

// The name after the last slash of a tree path.
static std::string
fileNameOf(const std::string& path)
{
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

void
EditorScene::updateRecoveryScan()
{
  // Reads are asynchronous in the guest (one in flight) and synchronous on
  // the native oracle (the loop then issues every slot in one update).
  while (m_autosaveScanning && !m_autosaveScanRead) {
    if (m_autosaveScanSlot >= EditorAutosaveRecord::kSlots) {
      finishRecoveryScan();
      return;
    }
    const int slot = m_autosaveScanSlot++;
    m_autosaveScanRead = true;
    const std::weak_ptr<bool> alive = m_lifetime;
    IllEdPlatform::current().read(
      EditorAutosaveRecord::slotName(slot),
      [this, alive, slot](
        bool success, const std::string& text, const std::string&) {
        if (alive.expired()) {
          return;
        }
        m_autosaveScanRead = false;
        if (!success || text.empty()) {
          return;
        }
        RecoveryEntry entry;
        entry.slot = slot;
        std::string error;
        if (IlscCodec::parse(text, entry.document, error) &&
            entry.record.read(entry.document)) {
          EditorAutosaveRecord::strip(entry.document);
          m_slotTaken[static_cast<std::size_t>(slot)] = true;
          m_recoverable.push_back(std::move(entry));
        } else {
          Logger::LogWarning("Autosave slot " +
                             EditorAutosaveRecord::slotName(slot) +
                             " is unreadable and will be reused: " + error);
        }
      });
  }
}

void
EditorScene::finishRecoveryScan()
{
  m_autosaveScanning = false;
  // Newest first; the time strings sort as text.
  std::sort(m_recoverable.begin(),
            m_recoverable.end(),
            [](const RecoveryEntry& a, const RecoveryEntry& b) {
              if (a.record.savedAt != b.record.savedAt) {
                return a.record.savedAt > b.record.savedAt;
              }
              return a.slot > b.slot;
            });
  offerRecovery();
}

void
EditorScene::offerRecovery()
{
  if (m_recoverable.empty() || !m_recovery) {
    return;
  }
  std::string message;
  if (m_recoverable.size() == 1) {
    const EditorAutosaveRecord& record = m_recoverable.front().record;
    message =
      "Recover unsaved changes to " +
      (record.label == "-" ? std::string("an untitled scene") : record.label);
    if (!record.savedAt.empty()) {
      message += " from " + record.savedAt;
    }
    message += "?";
  } else {
    message = "Recover " + std::to_string(m_recoverable.size()) +
              " scenes with unsaved changes (" +
              (m_recoverable.front().record.label == "-"
                 ? std::string("untitled")
                 : m_recoverable.front().record.label) +
              " and others)?";
  }
  m_recovery->open(message);
}

void
EditorScene::recoverAll()
{
  if (m_recoverable.empty()) {
    return;
  }
  // Oldest first, so the newest ends up active (the last tab opened).
  for (std::size_t index = m_recoverable.size(); index > 0; --index) {
    PendingOpen pending;
    pending.recovery =
      std::make_shared<RecoveryEntry>(std::move(m_recoverable[index - 1]));
    m_openQueue.push_back(std::move(pending));
  }
  m_recoverable.clear();
}

void
EditorScene::recoverInto(const std::shared_ptr<RecoveryEntry>& entry)
{
  const std::string root = entry->record.root.empty()
                             ? documentRoot(entry->record.location)
                             : entry->record.root;
  EditorTab& tab = activeTab();
  const uint64_t serial = tab.serial;
  m_busy = true;
  const SceneFetchList fetches = collectSceneFetches(entry->document, root);
  if (tab.fetchTicket != 0) {
    IllEdPlatform::current().releaseAssets(tab.fetchTicket);
  }
  const std::weak_ptr<bool> alive = m_lifetime;
  tab.fetchTicket = IllEdPlatform::current().fetchAssets(
    fetches.paths,
    [this, alive, entry, root, serial](std::vector<std::string> missing) {
      if (alive.expired()) {
        return;
      }
      m_busy = false;
      EditorTab* target = tabBySerial(serial);
      std::string error;
      if (target == nullptr) {
        error = "the tab was closed";
      } else if (target->document.loadParsed(entry->document, &error, root)) {
        const std::string label =
          entry->record.label == "-" ? std::string() : entry->record.label;
        if (!entry->record.location.empty()) {
          target->document.setLocation(entry->record.location, label);
        } else {
          target->document.setLocation(
            {},
            (label.empty() ? std::string("Untitled") : label) + " (recovered)");
        }
        target->document.markRecovered();
        // The slot now holds exactly this document: the tab adopts it,
        // rewrites it on the next edit and frees it on save.
        if (target->autosave.slot >= 0) {
          m_slotTaken[static_cast<std::size_t>(target->autosave.slot)] = false;
        }
        target->autosave = EditorTab::Autosave{};
        target->autosave.slot = entry->slot;
        target->autosave.live = true;
        target->autosave.uid = target->document.history().currentUid();
        target->autosave.seenUid = target->autosave.uid;
        target->autosave.anchor = m_autosaveClock;
        target->selection.clear();
        if (target == &activeTab()) {
          restoreCameraState();
        }
        for (const std::string& path : missing) {
          if (ic != nullptr && ic->commandLine != nullptr) {
            ic->commandLine->logError("Scene asset is missing: " + path);
          }
        }
        Logger::LogInfo(
          "Recovered unsaved work: " + target->document.displayName() + " (" +
          std::to_string(target->document.nodeCount()) + " nodes, from " +
          entry->record.savedAt + ")");
        toast("Recovered unsaved work: " + target->document.displayName(),
              kAutosaveToastInfo);
      }
      if (!error.empty()) {
        // The slot stays as it is for another try.
        Logger::LogError("Could not recover unsaved work: " + error);
        toast("Could not recover unsaved work: " + error, kAutosaveToastBad);
      }
      refreshView();
      updateStatus();
      finishBusy();
    });
}

void
EditorScene::discardAll()
{
  const std::weak_ptr<bool> alive = m_lifetime;
  for (const RecoveryEntry& entry : m_recoverable) {
    const int slot = entry.slot;
    m_slotTaken[static_cast<std::size_t>(slot)] = false;
    IllEdPlatform::current().write(
      EditorAutosaveRecord::slotName(slot),
      {},
      [alive, slot](bool freed, const std::string& error) {
        if (alive.expired() || freed) {
          return;
        }
        Logger::LogWarning("Could not discard autosave slot " +
                           EditorAutosaveRecord::slotName(slot) + ": " + error);
      });
  }
  m_recoverable.clear();
}

void
EditorScene::deferRecovery()
{
  if (m_recoverable.empty()) {
    return;
  }
  Logger::LogInfo(std::to_string(m_recoverable.size()) +
                  " unsaved document(s) kept for the next start");
  toast("Unsaved work kept for the next start", kAutosaveToastInfo);
  // Their slots stay taken, so this session never overwrites them.
  m_recoverable.clear();
}

int
EditorScene::takeFreeSlot()
{
  for (std::size_t slot = 0; slot < m_slotTaken.size(); ++slot) {
    if (!m_slotTaken[slot]) {
      m_slotTaken[slot] = true;
      return static_cast<int>(slot);
    }
  }
  return -1;
}

void
EditorScene::updateAutosave(double dt)
{
  m_autosaveClock += dt;
  for (std::unique_ptr<EditorTab>& owner : m_tabs) {
    EditorTab& tab = *owner;
    EditorTab::Autosave& state = tab.autosave;
    const uint64_t uid = tab.document.history().currentUid();
    if (uid != state.seenUid) {
      state.seenUid = uid;
      state.editTime = m_autosaveClock;
    }
    if (!tab.document.isDirty()) {
      if (state.live && !state.inFlight) {
        freeAutosaveSlot(tab, {});
      }
      state.anchor = m_autosaveClock;
      continue;
    }
    if (state.live && uid == state.uid) {
      state.anchor = m_autosaveClock;
      continue;
    }
    if (state.inFlight) {
      continue;
    }
    if (m_autosaveClock - state.editTime >= kAutosaveIdleSeconds ||
        m_autosaveClock - state.anchor >= kAutosaveLatestSeconds) {
      autosaveNow(tab);
    }
  }
}

void
EditorScene::autosaveNow(EditorTab& tab)
{
  EditorTab::Autosave& state = tab.autosave;
  if (state.inFlight || !tab.document.isDirty()) {
    return;
  }
  if (state.slot < 0) {
    state.slot = takeFreeSlot();
    if (state.slot < 0) {
      if (!m_autosaveWarned) {
        m_autosaveWarned = true;
        Logger::LogWarning("Every autosave slot is taken; " +
                           tab.document.displayName() + " is not autosaved");
        toast("Autosave: every slot is taken", kAutosaveToastBad);
      }
      return;
    }
  }
  SceneDocument copy = tab.document.scene().document();
  EditorAutosaveRecord record;
  record.label = tab.document.displayName();
  record.location = IllEdPlatform::isTreeLocation(tab.document.path())
                      ? tab.document.path()
                      : "";
  record.root = tab.document.packageRoot();
  record.savedAt = EditorAutosaveRecord::now();
  record.write(copy);
  const uint64_t uid = tab.document.history().currentUid();
  const uint64_t serial = tab.serial;
  state.inFlight = true;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().write(
    EditorAutosaveRecord::slotName(state.slot),
    IlscCodec::encode(copy, true),
    [this, alive, serial, uid](bool written, const std::string& error) {
      if (alive.expired()) {
        return;
      }
      EditorTab* target = tabBySerial(serial);
      if (target == nullptr) {
        return;
      }
      EditorTab::Autosave& done = target->autosave;
      done.inFlight = false;
      if (written) {
        done.live = true;
        done.uid = uid;
        done.anchor = m_autosaveClock;
        Logger::LogTrace("Autosaved " + target->document.displayName());
      } else if (!m_autosaveWarned) {
        m_autosaveWarned = true;
        Logger::LogWarning("Autosave failed: " + error);
        toast("Autosave failed: " + error, kAutosaveToastBad);
      }
      if (done.freeWanted) {
        done.freeWanted = false;
        freeAutosaveSlot(*target, std::move(done.freeDone));
      }
    });
}

void
EditorScene::freeAutosaveSlot(EditorTab& tab, std::function<void()> done)
{
  EditorTab::Autosave& state = tab.autosave;
  if (state.inFlight) {
    state.freeWanted = true;
    state.freeDone = std::move(done);
    return;
  }
  if (state.slot < 0 || !state.live) {
    if (done) {
      done();
    }
    return;
  }
  state.live = false;
  state.uid = 0;
  state.inFlight = true;
  const uint64_t serial = tab.serial;
  const int slot = state.slot;
  const std::weak_ptr<bool> alive = m_lifetime;
  IllEdPlatform::current().write(
    EditorAutosaveRecord::slotName(slot),
    {},
    [this, alive, serial, done](bool freed, const std::string& error) {
      if (alive.expired()) {
        return;
      }
      if (EditorTab* target = tabBySerial(serial)) {
        target->autosave.inFlight = false;
      }
      if (!freed) {
        Logger::LogWarning("Could not free an autosave slot: " + error);
      }
      if (done) {
        done();
      }
      if (m_closeAfterAutosave && ic != nullptr && ic->window != nullptr) {
        bool pending = false;
        for (const std::unique_ptr<EditorTab>& other : m_tabs) {
          pending = pending || other->autosave.inFlight;
        }
        if (!pending) {
          m_closeAfterAutosave = false;
          m_exitApproved = true;
          ic->window->requestClose();
        }
      }
    });
}

void
EditorScene::freeAllAutosaveSlots(std::function<void()> done)
{
  // Counted down by the per-slot completions; `done` runs after the last.
  const std::shared_ptr<std::size_t> remaining =
    std::make_shared<std::size_t>(m_tabs.size());
  const std::shared_ptr<std::function<void()>> finish =
    std::make_shared<std::function<void()>>(std::move(done));
  for (std::unique_ptr<EditorTab>& tab : m_tabs) {
    freeAutosaveSlot(*tab, [remaining, finish]() {
      if (--*remaining == 0 && *finish) {
        (*finish)();
      }
    });
  }
}

void
EditorScene::recordSession()
{
  if (ic == nullptr || ic->envVars == nullptr) {
    return;
  }
  std::string joined;
  std::string active = "0";
  std::size_t listed = 0;
  for (std::size_t index = 0; index < m_tabs.size(); ++index) {
    const std::string& path = m_tabs[index]->document.path();
    if (!IllEdPlatform::isTreeLocation(path)) {
      continue;
    }
    if (!joined.empty()) {
      joined += ';';
    }
    joined += path;
    if (index == m_activeTab) {
      active = std::to_string(listed);
    }
    ++listed;
  }
  if (ic->envVars->getVar("sessionTabs").value == joined &&
      ic->envVars->getVar("sessionActive").value == active) {
    return;
  }
  ic->envVars->setVar("sessionTabs", joined);
  ic->envVars->setVar("sessionActive", active);
  ic->envVars->save();
}

bool
EditorScene::restoreSession()
{
  if (ic == nullptr || ic->envVars == nullptr ||
      ic->envVars->getVar("restoreSession").value == "0") {
    return false;
  }
  const std::string joined = ic->envVars->getVar("sessionTabs").value;
  if (joined.empty()) {
    return false;
  }
  std::size_t activeIndex = 0;
  long active = 0;
  if (parseWholeInteger(ic->envVars->getVar("sessionActive").value, &active) &&
      active > 0) {
    activeIndex = static_cast<std::size_t>(active);
  }
  std::size_t count = 0;
  std::size_t start = 0;
  while (start <= joined.size()) {
    const std::size_t end = joined.find(';', start);
    const std::string location = joined.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
    if (!location.empty() && IllEdPlatform::isTreeLocation(location)) {
      queueOpen({ location, fileNameOf(IllEdPlatform::treePath(location)) });
      ++count;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  if (count == 0) {
    return false;
  }
  Logger::LogInfo("Reopening the last session's " + std::to_string(count) +
                  " scene(s)");
  // The remembered tab is resolved by position once the queue drains: the
  // first scene reuses the empty first tab, the rest open beside it.
  m_openQueueActivateIndex = std::min(activeIndex, count - 1);
  return true;
}
