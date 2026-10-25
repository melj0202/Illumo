#pragma once

#include <Illumo/Wasm/ProjectHost.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class IEnvVars;

// The runtime's project host for one application: the recent list lives in
// the runtime's own settings (`recentProjects.<application>`, newest first,
// at most kMaximumRecent host paths, never a launch option, so a stale
// package never replays), the folder dialogs come from SaveLoad (replaceable
// for tests), and open() hands RelaunchCurrentProcess the application's
// base arguments plus `--project <folder>`; the guest then asks to restart.
// Host paths never cross the ABI: the guest sees an entry's position and
// the folder's name. docs/illed-editing-wave4-plan.md, 5.5.
class RuntimeProjectHost final : public IProjectHost
{
public:
  static constexpr std::size_t kMaximumRecent = 10;
  // The settings key for an application's recent list.
  static std::string settingsKey(const std::string& application);
  // A folder dialog: the title in, the chosen directory (UTF-8) out, empty
  // when cancelled.
  using PickFolder = std::function<std::string(const std::string& title)>;

  // `baseArguments` are the runtime options the restart keeps (the app, its
  // mounts, storage, graphics API), without any project.
  RuntimeProjectHost(IEnvVars* settings,
                     std::string application,
                     std::vector<std::string> baseArguments,
                     PickFolder pickFolder = {});
  ~RuntimeProjectHost() override;

  bool available() const override;
  std::vector<ProjectHostEntry> list() override;
  bool choose(ProjectHostEntry* chosen,
              bool* cancelled,
              std::string& error) override;
  bool create(ProjectHostEntry* created,
              bool* cancelled,
              std::string& error) override;
  bool open(std::uint32_t id, std::string& error) override;
  void clear() override;

  // The paths behind the last listing, newest first (tests).
  const std::vector<std::string>& listed() const { return m_listed; }
  // Scaffolds a project folder: illumo.json (when absent) and the scenes,
  // meshes, textures and assets directories. False with `error`.
  static bool scaffold(const std::string& directory, std::string& error);

private:
  // The stored paths, newest first, dropping folders that are gone.
  std::vector<std::string> recent() const;
  void store(const std::vector<std::string>& paths);
  // Puts a path first (dropping an earlier copy and the overflow).
  void remember(const std::string& path);
  bool pick(const std::string& title,
            bool scaffoldNew,
            ProjectHostEntry* entry,
            bool* cancelled,
            std::string& error);

  IEnvVars* m_settings;
  std::string m_application;
  std::vector<std::string> m_baseArguments;
  PickFolder m_pickFolder;
  std::vector<std::string> m_listed;
};
