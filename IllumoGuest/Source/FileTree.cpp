#include <Illumo/Services/Logger.h>
#include <IllumoGuest/FileTree.h>
#include <string>

bool
GuestFileTree::start(Operation operation)
{
  if (operation.task == 0) {
    return false;
  }
  m_operations.push_back(std::move(operation));
  return true;
}

bool
GuestFileTree::list(std::string path, Listed done)
{
  Operation operation;
  operation.kind = Kind::List;
  operation.task = m_files.list(path, 0);
  operation.path = std::move(path);
  operation.listed = std::move(done);
  return start(std::move(operation));
}

bool
GuestFileTree::stat(std::string path, Stated done)
{
  Operation operation;
  operation.kind = Kind::Stat;
  operation.task = m_files.stat(std::move(path));
  operation.stated = std::move(done);
  return start(std::move(operation));
}

bool
GuestFileTree::read(std::string path, Loaded done, std::size_t maximum)
{
  Operation operation;
  operation.kind = Kind::Read;
  operation.task =
    m_files.read(GuestFileArea::Mounted, std::move(path), maximum);
  operation.loaded = std::move(done);
  return start(std::move(operation));
}

bool
GuestFileTree::write(std::string path, std::vector<std::byte> bytes, Done done)
{
  Operation operation;
  operation.kind = Kind::Write;
  operation.task =
    m_files.write(GuestFileArea::Mounted, std::move(path), std::move(bytes));
  operation.done = std::move(done);
  return start(std::move(operation));
}

bool
GuestFileTree::importFile(std::string grant, std::string target, Done done)
{
  Operation operation;
  operation.kind = Kind::Import;
  operation.task = m_files.importFile(std::move(grant), std::move(target));
  operation.done = std::move(done);
  return start(std::move(operation));
}

bool
GuestFileTree::pack(std::string grant, std::string source, Done done)
{
  Operation operation;
  operation.kind = Kind::Pack;
  operation.task = m_files.pack(std::move(grant), std::move(source));
  operation.done = std::move(done);
  return start(std::move(operation));
}

void
GuestFileTree::pump()
{
  // Finished operations are moved out first, so callbacks may start new
  // operations without disturbing this walk.
  std::vector<std::pair<Operation, GuestFileResult>> finished;
  for (std::size_t index = 0; index < m_operations.size();) {
    Operation& operation = m_operations[index];
    GuestFileResult result;
    if (!m_files.take(operation.task, result)) {
      ++index;
      continue;
    }
    if (operation.kind == Kind::List &&
        result.outcome == GuestFileOutcome::Success) {
      GuestFileListing page;
      if (!GuestFileListing::read(result.bytes, page)) {
        Logger::LogWarning("The host sent a malformed listing of " +
                           operation.path);
        result.outcome = GuestFileOutcome::IoError;
      } else {
        for (GuestFileEntry& entry : page.entries) {
          operation.entries.push_back(std::move(entry));
        }
        operation.cursor += static_cast<std::uint32_t>(page.entries.size());
        const bool more = !page.entries.empty() &&
                          operation.cursor < page.total &&
                          operation.entries.size() < MaximumListing;
        if (more) {
          operation.task = m_files.list(operation.path, operation.cursor);
          if (operation.task != 0) {
            ++index;
            continue;
          }
          Logger::LogWarning("Listing of " + operation.path +
                             " stopped early: too many file operations in "
                             "flight");
          result.outcome = GuestFileOutcome::IoError;
        } else if (operation.entries.size() >= MaximumListing &&
                   operation.cursor < page.total) {
          Logger::LogWarning("Listing of " + operation.path + " truncated at " +
                             std::to_string(operation.entries.size()) + " of " +
                             std::to_string(page.total) + " entries");
        }
      }
    }
    finished.push_back({ std::move(operation), std::move(result) });
    m_operations.erase(m_operations.begin() +
                       static_cast<std::ptrdiff_t>(index));
  }
  for (std::pair<Operation, GuestFileResult>& item : finished) {
    Operation& operation = item.first;
    GuestFileResult& result = item.second;
    switch (operation.kind) {
      case Kind::List:
        if (operation.listed) {
          operation.listed(result.outcome, std::move(operation.entries));
        }
        break;
      case Kind::Stat: {
        GuestFileStatus status;
        if (result.outcome == GuestFileOutcome::Success &&
            !GuestFileStatus::read(result.bytes, status)) {
          result.outcome = GuestFileOutcome::IoError;
        }
        if (operation.stated) {
          operation.stated(result.outcome, std::move(status));
        }
        break;
      }
      case Kind::Read:
        if (operation.loaded) {
          operation.loaded(result.outcome, std::move(result.bytes));
        }
        break;
      default:
        if (operation.done) {
          operation.done(result.outcome);
        }
        break;
    }
  }
}

void
GuestFileTree::cancel()
{
  for (const Operation& operation : m_operations) {
    m_files.cancel(operation.task);
  }
  m_operations.clear();
}
