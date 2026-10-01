#pragma once

#include <Illumo/Content/BehaviourSchema.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The scene behaviours the editor can show as typed fields: every
// behaviours.json in the virtual file tree, read from installed applications
// (/apps/<id>), mounted packages (/packages/<id>) and the project. The
// applications that ship one are the games a scene can play with. Reads go
// through IllEdPlatform and may complete on later updates; revision() changes
// whenever what is known changes.
class EditorBehaviours
{
public:
  EditorBehaviours();
  ~EditorBehaviours() = default;
  EditorBehaviours(const EditorBehaviours&) = delete;
  EditorBehaviours& operator=(const EditorBehaviours&) = delete;

  // Forgets what is known and reads every behaviours.json again.
  void discover();
  // Requests still in flight.
  bool pending() const { return m_pending != 0; }
  std::uint64_t revision() const { return m_revision; }

  // Every known type; a type two files describe keeps the first in
  // application, package, project order.
  const BehaviourSchema& schema() const { return m_schema; }
  // Installed applications with a behaviours.json, by id.
  const std::vector<std::string>& games() const { return m_games; }
  // The application whose behaviours.json describes `type`, or empty.
  std::string gameFor(const std::string& type) const;

  // Adds one file's text as discovery would (tests); `application` is empty
  // for a package or the project.
  bool addSource(const std::string& origin,
                 const std::string& application,
                 const std::string& text);

private:
  struct Source
  {
    std::string origin;
    std::string application;
    // Discovery order: applications 0, packages 1, the project 2.
    int rank = 0;
    BehaviourSchema schema;
  };
  void read(const std::string& path, const std::string& application, int rank);
  void listAndRead(const std::string& directory, bool applications);
  bool accept(const std::string& origin,
              const std::string& application,
              int rank,
              const std::string& text);
  void rebuild();

  std::vector<Source> m_sources;
  BehaviourSchema m_schema;
  std::vector<std::string> m_games;
  std::uint64_t m_revision = 0;
  // Bumped by discover(), so completions of an older round are dropped.
  std::uint64_t m_round = 0;
  int m_pending = 0;
  std::shared_ptr<bool> m_alive;
};
