#include <Illumo/Wasm/RuntimeAppLauncher.h>
#include <IllumoGuest/Launcher.h>
#include <algorithm>
#include <fstream>

RuntimeAppLauncher::RuntimeAppLauncher(
  std::vector<std::string> applications,
  std::filesystem::path playDirectory,
  std::vector<std::string> packageArguments)
  : m_applications(std::move(applications))
  , m_playDirectory(std::move(playDirectory))
  , m_packageArguments(std::move(packageArguments))
{
}

RuntimeAppLauncher::~RuntimeAppLauncher()
{
  // The child reads its document at startup; once it is gone so is the
  // directory.
  m_child.stop();
  std::error_code ignored;
  std::filesystem::remove_all(m_playDirectory, ignored);
}

bool
RuntimeAppLauncher::launch(const std::string& application,
                           const std::string& name,
                           std::span<const std::byte> document,
                           std::string& error)
{
  if (std::find(m_applications.begin(), m_applications.end(), application) ==
      m_applications.end()) {
    error = "no installed application is named '" + application + "'";
    return false;
  }
  if (!GuestLaunchRequest::validName(name) || document.empty()) {
    error = "the document name or contents are invalid";
    return false;
  }
  // The running copy, if any, may still read its document; end it first.
  m_child.stop();
  std::error_code status;
  std::filesystem::create_directories(m_playDirectory, status);
  const std::filesystem::path path = m_playDirectory / name;
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(document.data()),
                 static_cast<std::streamsize>(document.size()));
    if (status || !output) {
      error = "the document could not be saved for the launch";
      return false;
    }
  }
  const std::u8string text = path.u8string();
  std::vector<std::string> arguments{
    "--app",
    application,
    "--open",
    std::string(reinterpret_cast<const char*>(text.data()), text.size())
  };
  arguments.insert(
    arguments.end(), m_packageArguments.begin(), m_packageArguments.end());
  return m_child.start(arguments, &error);
}
