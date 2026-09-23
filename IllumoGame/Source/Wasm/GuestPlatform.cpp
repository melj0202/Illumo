#include "GuestPlatform.h"
#include "Game/RuleCatalogOverlay.h"
#include <stdexcept>

static GuestCSimPlatform* installedPlatform = nullptr;

static const std::string kSelectedPrefix = "selected:";

static bool
isSelected(const std::string& location)
{
  return location.compare(0, kSelectedPrefix.size(), kSelectedPrefix) == 0;
}

CSimPlatform&
CSimPlatform::current()
{
  if (installedPlatform == nullptr) {
    throw std::logic_error("No guest CSimPlatform is installed");
  }
  return *installedPlatform;
}

GuestSimulationLanes::GuestSimulationLanes(GuestServiceQueue& services)
  : m_services(services)
{
}

void
GuestSimulationLanes::pump()
{
  if (m_grant == Grant::Unknown) {
    // Ask at startup: the host compiles lane stores while menus run and
    // answers once they are ready.
    laneCount();
    return;
  }
  GuestServiceRecord result;
  if (m_grant != Grant::Requested || !m_services.take(m_query, result)) {
    return;
  }
  m_grant = Grant::Known;
  GuestWireReader reader(result.payload);
  const std::uint32_t lanes = reader.u32();
  // A rejected query (no Jobs grant) or an out-of-range answer means none.
  m_lanes = result.status == GuestServiceStatus::Complete &&
                reader.finished() && lanes <= 64u
              ? lanes
              : 0u;
  m_outstanding.assign(m_lanes, 0u);
  m_failed.assign(m_lanes, false);
}

std::uint32_t
GuestSimulationLanes::laneCount()
{
  if (m_grant == Grant::Unknown) {
    m_query = m_services.enqueue(GuestService::JobLanes, {});
    if (m_query != 0) {
      m_grant = Grant::Requested;
    }
  }
  return m_grant == Grant::Known ? m_lanes : 0u;
}

bool
GuestSimulationLanes::laneCountKnown() const
{
  return m_grant == Grant::Known;
}

bool
GuestSimulationLanes::submit(std::uint32_t lane,
                             std::vector<std::byte>&& request)
{
  if (lane >= m_lanes || m_outstanding[lane] != 0 || m_failed[lane] ||
      request.size() > GuestServices::MaximumJobBytes - 4u) {
    return false;
  }
  GuestWireWriter payload;
  payload.u32(lane);
  payload.bytes(request);
  const std::uint64_t id =
    m_services.enqueue(GuestService::LaneJob, payload.take());
  if (id == 0) {
    return false;
  }
  m_outstanding[lane] = id;
  request.clear();
  return true;
}

int
GuestSimulationLanes::poll(std::uint32_t lane, std::vector<std::byte>& reply)
{
  if (lane >= m_lanes) {
    return -1;
  }
  if (m_failed[lane]) {
    return -1;
  }
  GuestServiceRecord result;
  if (m_outstanding[lane] == 0 ||
      !m_services.take(m_outstanding[lane], result)) {
    return 0;
  }
  m_outstanding[lane] = 0;
  if (result.status != GuestServiceStatus::Complete) {
    m_failed[lane] = true;
    return -1;
  }
  reply = std::move(result.payload);
  return 1;
}

bool
GuestSimulationLanes::busy(std::uint32_t lane) const
{
  return lane < m_lanes && m_outstanding[lane] != 0;
}

GuestCSimPlatform::GuestCSimPlatform(GuestServiceQueue& services,
                                     GuestFiles& files)
  : m_files(files)
  , m_lanes(services)
  , m_dialog(services)
  , m_clipboard(services)
{
}

GuestCSimPlatform::~GuestCSimPlatform()
{
  for (const ReadRequest& request : m_reads) {
    m_files.cancel(request.task);
  }
  for (const WriteRequest& request : m_writes) {
    m_files.cancel(request.task);
  }
  if (installedPlatform == this) {
    installedPlatform = nullptr;
  }
}

void
GuestCSimPlatform::install()
{
  installedPlatform = this;
}

std::uint64_t
GuestCSimPlatform::submitRead(const std::string& location)
{
  if (isSelected(location)) {
    return m_files.read(GuestFileArea::Selected,
                        location.substr(kSelectedPrefix.size()));
  }
  return m_files.read(GuestFileArea::Storage, location);
}

std::uint64_t
GuestCSimPlatform::submitWrite(const std::string& location, std::string bytes)
{
  const std::byte* first = reinterpret_cast<const std::byte*>(bytes.data());
  std::vector<std::byte> copied(first, first + bytes.size());
  if (isSelected(location)) {
    return m_files.write(GuestFileArea::Selected,
                         location.substr(kSelectedPrefix.size()),
                         std::move(copied));
  }
  return m_files.write(GuestFileArea::Storage, location, std::move(copied));
}

void
GuestCSimPlatform::chooseLoadLocation(const SaveLoadDialogSpec& specification,
                                      LocationCallback done)
{
  m_dialogs.push_back({ false, specification, std::move(done) });
}

void
GuestCSimPlatform::chooseSaveLocation(const SaveLoadDialogSpec& specification,
                                      LocationCallback done)
{
  m_dialogs.push_back({ true, specification, std::move(done) });
}

void
GuestCSimPlatform::readFirst(std::vector<std::string> locations,
                             ReadCallback done)
{
  ReadRequest request;
  request.locations = std::move(locations);
  request.done = std::move(done);
  m_reads.push_back(std::move(request));
}

void
GuestCSimPlatform::writeFile(const std::string& location,
                             std::string bytes,
                             WriteCallback done)
{
  WriteRequest request;
  request.files.emplace_back(location, std::move(bytes));
  request.done = std::move(done);
  m_writes.push_back(std::move(request));
}

void
GuestCSimPlatform::readClipboard(TextCallback done)
{
  m_clipboardReads.push_back(std::move(done));
}

void
GuestCSimPlatform::writeClipboard(const std::string& text)
{
  m_clipboard.set(text);
}

void
GuestCSimPlatform::saveUserCatalog(std::vector<RuleFamilyDefinition> families,
                                   std::vector<RuleSetDefinition> rules,
                                   WriteCallback done)
{
  // The active registry already holds the shipped catalog plus the overlay
  // loaded at startup and every overlay write since, so no reread is needed.
  RuleSetRegistry staged = RuleSetRegistry::instance();
  std::string error;
  if ((!families.empty() &&
       !RuleCatalogOverlay::stageFamilies(staged, families, &error)) ||
      !RuleCatalogOverlay::stageRules(staged, rules, &error)) {
    WriteRequest failed;
    failed.done = [done, error](bool, const std::string&) {
      done(false, error);
    };
    m_writes.push_back(std::move(failed));
    return;
  }
  WriteRequest request;
  request.files.emplace_back("families.user.json",
                             RuleCatalogOverlay::familiesText(staged));
  request.files.emplace_back("rulesets.user.json",
                             RuleCatalogOverlay::rulesText(staged));
  request.done = std::move(done);
  m_writes.push_back(std::move(request));
}

bool
GuestCSimPlatform::startRead(ReadRequest& request)
{
  if (request.next >= request.locations.size()) {
    return false;
  }
  request.task = submitRead(request.locations[request.next]);
  return request.task != 0;
}

bool
GuestCSimPlatform::startWrite(WriteRequest& request)
{
  if (request.next >= request.files.size()) {
    return false;
  }
  std::pair<std::string, std::string>& file = request.files[request.next];
  request.task = submitWrite(file.first, std::move(file.second));
  return request.task != 0;
}

void
GuestCSimPlatform::pump()
{
  m_lanes.pump();
  // Completions are collected first; callbacks may queue further requests.
  std::vector<std::function<void()>> completions;

  if (m_dialogActive && m_dialog.idle()) {
    m_dialogActive = false;
    DialogRequest finished = std::move(m_dialogs.front());
    m_dialogs.pop_front();
    const GuestDialogResult& result = m_dialog.result();
    const std::string location = result.outcome == GuestFileOutcome::Success
                                   ? kSelectedPrefix + result.name
                                   : std::string();
    completions.push_back([finished, location]() { finished.done(location); });
  }
  if (!m_dialogActive && !m_dialogs.empty()) {
    const DialogRequest& next = m_dialogs.front();
    const std::string defaultName = next.specification.defaultFilename.empty()
                                      ? std::string("untitled")
                                      : next.specification.defaultFilename;
    if (next.save) {
      m_dialog.save(next.specification.fileDescription,
                    defaultName,
                    next.specification.extensionPattern);
    } else {
      m_dialog.load(next.specification.fileDescription,
                    defaultName,
                    next.specification.extensionPattern);
    }
    m_dialogActive = true;
  }
  m_dialog.pump();

  if (m_clipboardActive && m_clipboard.idle()) {
    m_clipboardActive = false;
    TextCallback finished = std::move(m_clipboardReads.front());
    m_clipboardReads.pop_front();
    const bool available = m_clipboard.error().empty();
    const std::string text = m_clipboard.text();
    completions.push_back(
      [finished, available, text]() { finished(available, text); });
  }
  if (!m_clipboardActive && !m_clipboardReads.empty()) {
    m_clipboard.get();
    m_clipboardActive = true;
  }
  m_clipboard.pump();

  for (std::deque<ReadRequest>::iterator it = m_reads.begin();
       it != m_reads.end();) {
    ReadRequest& request = *it;
    CSimReadResult result;
    bool finished = false;
    if (request.task == 0) {
      if (!startRead(request)) {
        result.missing = true;
        result.error = "Failed to open for loading: " +
                       (request.locations.empty() ? std::string()
                                                  : request.locations.back());
        finished = true;
      }
    } else {
      GuestFileResult file;
      if (m_files.take(request.task, file)) {
        request.task = 0;
        const std::string& location = request.locations[request.next];
        if (file.outcome == GuestFileOutcome::NotFound) {
          ++request.next;
        } else {
          result.location = location;
          if (file.outcome == GuestFileOutcome::Success) {
            result.success = true;
            result.bytes.assign(
              reinterpret_cast<const char*>(file.bytes.data()),
              file.bytes.size());
          } else {
            result.error = "Failed to read: " + location;
          }
          finished = true;
        }
      }
    }
    if (finished) {
      ReadCallback done = std::move(request.done);
      completions.push_back([done, result]() { done(result); });
      it = m_reads.erase(it);
    } else {
      ++it;
    }
  }

  for (std::deque<WriteRequest>::iterator it = m_writes.begin();
       it != m_writes.end();) {
    WriteRequest& request = *it;
    bool finished = false;
    bool written = false;
    std::string error;
    if (request.files.empty()) {
      finished = true; // Validation already failed; done reports it.
    } else if (request.task == 0) {
      if (!startWrite(request)) {
        written = request.next >= request.files.size();
        if (!written) {
          error = "Failed to save: " + request.files[request.next].first;
        }
        finished = true;
      }
    } else {
      GuestFileResult file;
      if (m_files.take(request.task, file)) {
        request.task = 0;
        if (file.outcome != GuestFileOutcome::Success) {
          error = "Failed to save: " + request.files[request.next].first;
          finished = true;
        } else {
          ++request.next;
        }
      }
    }
    if (finished) {
      WriteCallback done = std::move(request.done);
      completions.push_back([done, written, error]() { done(written, error); });
      it = m_writes.erase(it);
    } else {
      ++it;
    }
  }

  for (const std::function<void()>& completion : completions) {
    completion();
  }
}
