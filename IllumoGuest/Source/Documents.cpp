#include <IllumoGuest/Documents.h>

static const std::string kSelectedPrefix = "selected:";

static bool
isSelected(const std::string& location)
{
  return location.compare(0, kSelectedPrefix.size(), kSelectedPrefix) == 0;
}

GuestDocuments::GuestDocuments(GuestServiceQueue& services, GuestFiles& files)
  : m_files(files)
  , m_dialog(services)
{
}

GuestDocuments::~GuestDocuments()
{
  for (const Transfer& transfer : m_transfers) {
    m_files.cancel(transfer.task);
  }
}

GuestDocumentLocation
GuestDocuments::launch(const GuestLaunch& launch)
{
  return { kSelectedPrefix + GuestLaunch::Selection, launch.label };
}

void
GuestDocuments::choose(GuestDocumentDialog mode,
                       std::string description,
                       std::string defaultName,
                       std::string pattern,
                       ChooseCallback done)
{
  Choice choice;
  choice.mode = mode;
  choice.description = std::move(description);
  choice.defaultName =
    defaultName.empty() ? std::string("untitled") : std::move(defaultName);
  choice.pattern = std::move(pattern);
  choice.done = std::move(done);
  m_choices.push_back(std::move(choice));
}

void
GuestDocuments::read(const std::string& location, ReadCallback done)
{
  Transfer transfer;
  transfer.location = location;
  transfer.read = std::move(done);
  m_pending.push_back(std::move(transfer));
}

void
GuestDocuments::write(const std::string& location,
                      std::string bytes,
                      WriteCallback done)
{
  Transfer transfer;
  transfer.location = location;
  transfer.bytes = std::move(bytes);
  transfer.written = std::move(done);
  m_pending.push_back(std::move(transfer));
}

std::uint64_t
GuestDocuments::submit(Transfer& transfer)
{
  const bool selected = isSelected(transfer.location);
  const GuestFileArea area =
    selected ? GuestFileArea::Selected : GuestFileArea::Storage;
  const std::string name = selected
                             ? transfer.location.substr(kSelectedPrefix.size())
                             : transfer.location;
  if (!transfer.written) {
    return m_files.read(area, name);
  }
  const std::byte* first =
    reinterpret_cast<const std::byte*>(transfer.bytes.data());
  const std::uint64_t task = m_files.write(
    area, name, std::vector<std::byte>(first, first + transfer.bytes.size()));
  if (task != 0) {
    transfer.bytes.clear();
    transfer.bytes.shrink_to_fit();
  }
  return task;
}

void
GuestDocuments::pump()
{
  // Completions are collected first; callbacks may queue further requests.
  std::vector<std::function<void()>> completions;

  if (m_dialogActive && m_dialog.idle()) {
    m_dialogActive = false;
    const Choice finished = std::move(m_choices.front());
    m_choices.pop_front();
    const GuestDialogResult& result = m_dialog.result();
    GuestDocumentLocation chosen;
    if (result.outcome == GuestFileOutcome::Success) {
      chosen.location = kSelectedPrefix + result.name;
      chosen.label = result.label.empty() ? result.name : result.label;
    }
    completions.push_back([finished, chosen]() { finished.done(chosen); });
  }
  if (!m_dialogActive && !m_choices.empty()) {
    const Choice& next = m_choices.front();
    if (next.mode == GuestDocumentDialog::Save) {
      m_dialog.save(next.description, next.defaultName, next.pattern);
    } else if (next.mode == GuestDocumentDialog::Edit) {
      m_dialog.edit(next.description, next.defaultName, next.pattern);
    } else {
      m_dialog.load(next.description, next.defaultName, next.pattern);
    }
    m_dialogActive = true;
  }
  m_dialog.pump();

  while (!m_pending.empty()) {
    Transfer& next = m_pending.front();
    next.task = submit(next);
    if (next.task == 0) {
      break; // the file queue is full; retry on the next pump
    }
    m_transfers.push_back(std::move(next));
    m_pending.pop_front();
  }

  for (std::deque<Transfer>::iterator it = m_transfers.begin();
       it != m_transfers.end();) {
    GuestFileResult file;
    if (!m_files.take(it->task, file)) {
      ++it;
      continue;
    }
    const bool success = file.outcome == GuestFileOutcome::Success;
    if (it->written) {
      const WriteCallback done = std::move(it->written);
      const std::string error =
        success ? std::string() : "Failed to save " + it->location;
      completions.push_back([done, success, error]() { done(success, error); });
    } else {
      const ReadCallback done = std::move(it->read);
      const std::string bytes(reinterpret_cast<const char*>(file.bytes.data()),
                              file.bytes.size());
      const std::string error =
        success ? std::string() : "Failed to read " + it->location;
      completions.push_back(
        [done, success, bytes, error]() { done(success, bytes, error); });
    }
    it = m_transfers.erase(it);
  }

  for (const std::function<void()>& completion : completions) {
    completion();
  }
}
