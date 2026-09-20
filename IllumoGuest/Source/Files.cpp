#include <IllumoGuest/Files.h>
#include <cstring>

std::uint64_t
GuestFiles::read(GuestFileArea area, std::string path, std::size_t maximum)
{
  if (m_tasks.size() >= 8 || m_next == UINT64_MAX ||
      maximum > 512u * 1024u * 1024u || path.empty() || path.size() > 1024 ||
      path.find('\0') != std::string::npos) {
    return 0;
  }
  Task task;
  task.area = area;
  task.path = std::move(path);
  task.maximum = maximum;
  const std::uint64_t id = m_next++;
  m_tasks.emplace(id, std::move(task));
  return id;
}
std::uint64_t
GuestFiles::write(std::string path, std::vector<std::byte> bytes)
{
  return write(GuestFileArea::Storage, std::move(path), std::move(bytes));
}
std::uint64_t
GuestFiles::write(GuestFileArea area,
                  std::string path,
                  std::vector<std::byte> bytes)
{
  const std::uint64_t id = read(area, std::move(path), bytes.size());
  if (id != 0) {
    Task& task = m_tasks.at(id);
    task.writing = true;
    task.size = bytes.size();
    task.bytes = std::move(bytes);
  }
  return id;
}
bool
GuestFiles::enqueue(Task& task, GuestFileRequest request, std::uint32_t count)
{
  // Reserve local ownership before publishing a request to the queue.
  task.pending.reserve(8);
  GuestWireWriter payload;
  request.write(payload);
  const std::uint64_t id =
    m_services.enqueue(GuestService::File, payload.take());
  if (id == 0) {
    return false;
  }
  task.pending.push_back({ id, request.action, request.offset, count });
  return true;
}
void
GuestFiles::complete(Task& task,
                     const Pending& pending,
                     const GuestServiceRecord& result)
{
  GuestWireReader reader(result.payload);
  const std::uint32_t outcome =
    result.status == GuestServiceStatus::Complete
      ? reader.u32()
      : static_cast<std::uint32_t>(GuestFileOutcome::Denied);
  if (outcome > static_cast<std::uint32_t>(GuestFileOutcome::Cancelled) ||
      (result.status == GuestServiceStatus::Complete && !reader.valid())) {
    throw std::runtime_error("Invalid host file completion");
  }
  if (outcome != 0) {
    if (task.outcome == GuestFileOutcome::Success) {
      task.outcome = static_cast<GuestFileOutcome>(outcome);
    }
    if (pending.action == GuestFileAction::Close) {
      task.file = {};
    }
    task.stage = task.file.owner != 0 ? Stage::Close : Stage::Done;
    return;
  }
  if (pending.action == GuestFileAction::Open) {
    task.file = GuestResourceId::read(reader);
    const std::uint64_t size = reader.u64();
    if (!reader.finished() || task.file.owner == 0 ||
        task.file.kind != GuestResourceKind::File || task.file.slot == 0 ||
        task.file.generation == 0 || (task.writing && size != task.size)) {
      throw std::runtime_error("Invalid host file capability");
    }
    if (task.cancelled) {
      task.stage = Stage::Close;
    } else if (size > task.maximum) {
      task.outcome = GuestFileOutcome::Denied;
      task.stage = Stage::Close;
    } else {
      task.size = size;
      if (!task.writing) {
        task.bytes.resize(static_cast<std::size_t>(size));
      }
      task.stage = Stage::Transfer;
    }
  } else if (pending.action == GuestFileAction::Read) {
    const std::span<const std::byte> bytes = reader.bytes(pending.count);
    if (!reader.finished()) {
      throw std::runtime_error("Invalid host file block");
    }
    std::memcpy(task.bytes.data() + static_cast<std::size_t>(pending.offset),
                bytes.data(),
                bytes.size());
    task.completed += pending.count;
  } else if (pending.action == GuestFileAction::Write) {
    if (reader.u64() != pending.offset + pending.count || !reader.finished()) {
      throw std::runtime_error("Invalid host write acknowledgement");
    }
    task.completed += pending.count;
  } else {
    if (!reader.finished()) {
      throw std::runtime_error("Invalid host close acknowledgement");
    }
    task.file = {};
    task.stage = Stage::Done;
  }
}
void
GuestFiles::pump()
{
  std::size_t issued = 0;
  for (std::pair<const std::uint64_t, Task>& entry : m_tasks) {
    Task& task = entry.second;
    for (std::vector<Pending>::iterator it = task.pending.begin();
         it != task.pending.end();) {
      GuestServiceRecord result;
      if (!m_services.take(it->request, result)) {
        ++it;
        continue;
      }
      complete(task, *it, result);
      it = task.pending.erase(it);
    }
    issued += task.pending.size();
  }
  for (std::map<std::uint64_t, Task>::iterator it = m_tasks.begin();
       it != m_tasks.end();) {
    Task& task = it->second;
    if (task.cancelled && task.pending.empty()) {
      task.stage = task.file.owner != 0 ? Stage::Close : Stage::Done;
    }
    if (task.stage == Stage::Transfer && task.pending.empty() &&
        task.completed == task.size) {
      task.stage = task.writing ? Stage::Commit : Stage::Close;
    }
    if (task.stage == Stage::Done && task.cancelled) {
      it = m_tasks.erase(it);
      continue;
    }
    if (issued >= 8 || task.stage == Stage::Done) {
      ++it;
      continue;
    }
    if (task.stage == Stage::Transfer && !task.cancelled) {
      while (issued < 8 && task.submitted < task.size) {
        GuestFileRequest request;
        request.action =
          task.writing ? GuestFileAction::Write : GuestFileAction::Read;
        request.file = task.file;
        request.offset = task.submitted;
        const std::uint32_t count =
          static_cast<std::uint32_t>(std::min<std::uint64_t>(
            GuestFileRequest::MaximumBlock, task.size - task.submitted));
        if (task.writing) {
          const std::span<const std::byte> bytes =
            std::span(task.bytes)
              .subspan(static_cast<std::size_t>(task.submitted), count);
          request.data.assign(bytes.begin(), bytes.end());
        } else {
          request.size = count;
        }
        if (!enqueue(task, std::move(request), count)) {
          break;
        }
        task.submitted += count;
        ++issued;
      }
    } else if (task.pending.empty()) {
      GuestFileRequest request;
      if (task.stage == Stage::Open) {
        request.area = task.area;
        request.path = task.path;
        request.writing = task.writing;
        request.size = task.writing ? task.size : 0;
      } else {
        request.file = task.file;
        request.action = task.stage == Stage::Commit ? GuestFileAction::Commit
                                                     : GuestFileAction::Close;
      }
      if (enqueue(task, std::move(request))) {
        ++issued;
      }
    }
    ++it;
  }
}
bool
GuestFiles::take(std::uint64_t id, GuestFileResult& result)
{
  const std::map<std::uint64_t, Task>::iterator found = m_tasks.find(id);
  if (found == m_tasks.end() || found->second.stage != Stage::Done ||
      !found->second.pending.empty()) {
    return false;
  }
  result.outcome = found->second.outcome;
  result.bytes = std::move(found->second.bytes);
  m_tasks.erase(found);
  return true;
}
void
GuestFiles::cancel(std::uint64_t id)
{
  const std::map<std::uint64_t, Task>::iterator found = m_tasks.find(id);
  if (found != m_tasks.end()) {
    found->second.cancelled = true;
  }
}
