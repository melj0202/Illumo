#pragma once

#include <Illumo/Platform/ChildProcess.h>
#include <Illumo/Wasm/AppLauncher.h>
#include <filesystem>
#include <string>
#include <vector>

// The runtime's launcher: each launch saves the document in a private play
// directory and runs this program again as `--app <id> --open <document>`
// with the launching application's package options (its --mount and
// --project), in a child process that ends with this one. Only the installed
// applications it was given can be launched.
class RuntimeAppLauncher final : public IAppLauncher
{
public:
  RuntimeAppLauncher(std::vector<std::string> applications,
                     std::filesystem::path playDirectory,
                     std::vector<std::string> packageArguments);
  ~RuntimeAppLauncher() override;
  RuntimeAppLauncher(const RuntimeAppLauncher&) = delete;
  RuntimeAppLauncher& operator=(const RuntimeAppLauncher&) = delete;
  RuntimeAppLauncher(RuntimeAppLauncher&&) = delete;
  RuntimeAppLauncher& operator=(RuntimeAppLauncher&&) = delete;

  bool available() const override { return true; }
  bool launch(const std::string& application,
              const std::string& name,
              std::span<const std::byte> document,
              std::string& error) override;
  void stop() override { m_child.stop(); }
  bool running() override { return m_child.running(); }

private:
  std::vector<std::string> m_applications;
  std::filesystem::path m_playDirectory;
  std::vector<std::string> m_packageArguments;
  ChildProcess m_child;
};
