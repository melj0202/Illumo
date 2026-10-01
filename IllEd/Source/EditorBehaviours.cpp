#include "EditorBehaviours.h"

#include "IllEdPlatform.h"
#include <Illumo/Services/Logger.h>
#include <algorithm>

EditorBehaviours::EditorBehaviours()
  : m_alive(std::make_shared<bool>(true))
{
}

void
EditorBehaviours::discover()
{
  ++m_round;
  m_pending = 0;
  m_sources.clear();
  rebuild();
  listAndRead("/apps", true);
  listAndRead("/packages", false);
  if (IllEdPlatform::current().hasProject()) {
    read(std::string("/project/") + BehaviourSchema::kFileName, {}, 2);
  }
}

void
EditorBehaviours::listAndRead(const std::string& directory, bool applications)
{
  const std::weak_ptr<bool> alive = m_alive;
  const std::uint64_t round = m_round;
  ++m_pending;
  IllEdPlatform::current().listDirectory(
    directory,
    [this, alive, round, directory, applications](
      bool success, std::vector<IllEdPlatform::FileEntry> entries) {
      if (alive.expired() || round != m_round) {
        return;
      }
      --m_pending;
      // A tree without the directory simply has nothing there.
      if (success) {
        for (const IllEdPlatform::FileEntry& entry : entries) {
          if (entry.directory) {
            read(directory + "/" + entry.name + "/" +
                   BehaviourSchema::kFileName,
                 applications ? entry.name : std::string(),
                 applications ? 0 : 1);
          }
        }
      }
      rebuild();
    });
}

void
EditorBehaviours::read(const std::string& path,
                       const std::string& application,
                       int rank)
{
  const std::weak_ptr<bool> alive = m_alive;
  const std::uint64_t round = m_round;
  ++m_pending;
  IllEdPlatform::current().read(
    std::string(IllEdPlatform::kTreePrefix) + path,
    [this, alive, round, path, application, rank](
      bool success, const std::string& text, const std::string& error) {
      (void)error;
      if (alive.expired() || round != m_round) {
        return;
      }
      --m_pending;
      // Most packages ship no behaviours.json; a missing file is not news.
      if (success) {
        accept(path, application, rank, text);
      }
      rebuild();
    });
}

bool
EditorBehaviours::accept(const std::string& origin,
                         const std::string& application,
                         int rank,
                         const std::string& text)
{
  Source source;
  source.origin = origin;
  source.application = application;
  source.rank = rank;
  std::string error;
  if (text.size() > BehaviourSchema::kMaximumFileBytes ||
      !source.schema.parse(text, error)) {
    Logger::LogWarning("Ignoring " + origin + ": " +
                       (error.empty() ? std::string("too large") : error));
    return false;
  }
  m_sources.push_back(std::move(source));
  return true;
}

bool
EditorBehaviours::addSource(const std::string& origin,
                            const std::string& application,
                            const std::string& text)
{
  const bool added =
    accept(origin, application, application.empty() ? 1 : 0, text);
  rebuild();
  return added;
}

std::string
EditorBehaviours::gameFor(const std::string& type) const
{
  for (const Source& source : m_sources) {
    if (!source.application.empty() && source.schema.find(type) != nullptr) {
      return source.application;
    }
  }
  return {};
}

void
EditorBehaviours::rebuild()
{
  // Completions arrive in any order; merging in rank, then origin, order
  // keeps the result the same however they arrived.
  std::stable_sort(
    m_sources.begin(), m_sources.end(), [](const Source& a, const Source& b) {
      return a.rank != b.rank ? a.rank < b.rank : a.origin < b.origin;
    });
  m_schema.clear();
  m_games.clear();
  std::vector<std::string> conflicts;
  for (const Source& source : m_sources) {
    m_schema.merge(source.schema, &conflicts);
    if (!source.application.empty()) {
      m_games.push_back(source.application);
    }
  }
  if (m_pending == 0) {
    for (const std::string& conflict : conflicts) {
      Logger::LogWarning("Behaviour described twice: " + conflict);
    }
  }
  ++m_revision;
}
