#include <Illumo/Wasm/RuntimeProjectHost.h>

#include <Illumo/Platform/ProcessRelaunch.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/Logger.h>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

// Paths are stored one per line; no path holds a line break.
static constexpr char kSeparator = '\n';

static std::filesystem::path
pathOf(const std::string& utf8)
{
  return std::filesystem::path(
    std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

static std::string
utf8Of(const std::filesystem::path& path)
{
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// The folder's own name, for the guest's label; the drive or root when the
// path has no other component.
static std::string
labelOf(const std::string& utf8)
{
  const std::filesystem::path path = pathOf(utf8);
  std::string name = utf8Of(path.filename());
  if (name.empty()) {
    name = utf8Of(path.parent_path().filename());
  }
  if (name.empty()) {
    name = utf8;
  }
  if (name.size() > 128) {
    name.resize(128);
  }
  return name;
}

std::string
RuntimeProjectHost::settingsKey(const std::string& application)
{
  return "recentProjects." + application;
}

RuntimeProjectHost::RuntimeProjectHost(IEnvVars* settings,
                                       std::string application,
                                       std::vector<std::string> baseArguments,
                                       PickFolder pickFolder)
  : m_settings(settings)
  , m_application(std::move(application))
  , m_baseArguments(std::move(baseArguments))
  , m_pickFolder(std::move(pickFolder))
{
  if (!m_pickFolder) {
    m_pickFolder = [](const std::string& title) {
      return SaveLoad::GetFolderLocation(title);
    };
  }
}

RuntimeProjectHost::~RuntimeProjectHost() = default;

bool
RuntimeProjectHost::available() const
{
  return m_settings != nullptr && !m_application.empty();
}

std::vector<std::string>
RuntimeProjectHost::recent() const
{
  std::vector<std::string> paths;
  if (m_settings == nullptr) {
    return paths;
  }
  const std::string stored =
    m_settings->getVar(settingsKey(m_application)).value;
  std::size_t start = 0;
  while (start <= stored.size()) {
    const std::size_t end = stored.find(kSeparator, start);
    const std::string path = stored.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
    std::error_code ignored;
    if (!path.empty() && std::filesystem::is_directory(pathOf(path), ignored)) {
      bool seen = false;
      for (const std::string& earlier : paths) {
        seen = seen || earlier == path;
      }
      if (!seen && paths.size() < kMaximumRecent) {
        paths.push_back(path);
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return paths;
}

void
RuntimeProjectHost::store(const std::vector<std::string>& paths)
{
  if (m_settings == nullptr) {
    return;
  }
  std::string joined;
  for (const std::string& path : paths) {
    if (!joined.empty()) {
      joined.push_back(kSeparator);
    }
    joined += path;
  }
  m_settings->setVar(settingsKey(m_application), joined);
  m_settings->save();
}

void
RuntimeProjectHost::remember(const std::string& path)
{
  std::vector<std::string> paths = recent();
  for (std::size_t index = 0; index < paths.size();) {
    if (paths[index] == path) {
      paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(index));
    } else {
      ++index;
    }
  }
  paths.insert(paths.begin(), path);
  if (paths.size() > kMaximumRecent) {
    paths.resize(kMaximumRecent);
  }
  store(paths);
}

std::vector<ProjectHostEntry>
RuntimeProjectHost::list()
{
  m_listed = recent();
  std::vector<ProjectHostEntry> entries;
  for (std::size_t index = 0; index < m_listed.size(); ++index) {
    entries.push_back(
      { static_cast<std::uint32_t>(index + 1), labelOf(m_listed[index]) });
  }
  return entries;
}

bool
RuntimeProjectHost::scaffold(const std::string& directory, std::string& error)
{
  const std::filesystem::path root = pathOf(directory);
  std::error_code code;
  if (!std::filesystem::is_directory(root, code)) {
    error = "not a directory";
    return false;
  }
  for (const char* child : { "scenes", "meshes", "textures", "assets" }) {
    std::filesystem::create_directories(root / child, code);
    if (code) {
      error = std::string("cannot create ") + child + ": " + code.message();
      return false;
    }
  }
  const std::filesystem::path manifest = root / "illumo.json";
  if (std::filesystem::exists(manifest, code)) {
    return true;
  }
  // A package id from the folder's name: lower-case letters, digits, dots,
  // dashes and underscores, else "project".
  std::string id;
  for (char character : labelOf(directory)) {
    if (character >= 'A' && character <= 'Z') {
      id.push_back(static_cast<char>(character - 'A' + 'a'));
    } else if ((character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_' ||
               character == '-') {
      id.push_back(character);
    } else if (character == ' ' || character == '.') {
      id.push_back('-');
    }
  }
  while (!id.empty() && (id.front() == '-' || id.front() == '.')) {
    id.erase(id.begin());
  }
  if (id.empty()) {
    id = "project";
  }
  std::string title;
  for (char character : labelOf(directory)) {
    if (character == '"' || character == '\\') {
      title.push_back('\\');
    }
    title.push_back(character);
  }
  std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
  out << "{\n  \"format\": \"ilpk\",\n  \"format_version\": 1,\n  \"id\": \""
      << id << "\",\n  \"title\": \"" << title
      << "\",\n  \"kind\": \"content\"\n}\n";
  if (!out.good()) {
    error = "cannot write illumo.json";
    return false;
  }
  return true;
}

bool
RuntimeProjectHost::pick(const std::string& title,
                         bool scaffoldNew,
                         ProjectHostEntry* entry,
                         bool* cancelled,
                         std::string& error)
{
  *cancelled = false;
  const std::string chosen = m_pickFolder(title);
  if (chosen.empty()) {
    *cancelled = true;
    return false;
  }
  std::error_code code;
  const std::filesystem::path root = pathOf(chosen);
  if (!std::filesystem::is_directory(root, code)) {
    error = "the chosen path is not a directory";
    return false;
  }
  if (scaffoldNew && !scaffold(chosen, error)) {
    return false;
  }
  if (!scaffoldNew && !std::filesystem::exists(root / "illumo.json", code)) {
    Logger::LogWarning("The project folder has no illumo.json: " + chosen);
  }
  remember(chosen);
  list();
  entry->id = 1;
  entry->label = labelOf(chosen);
  return true;
}

bool
RuntimeProjectHost::choose(ProjectHostEntry* chosen,
                           bool* cancelled,
                           std::string& error)
{
  return pick("Open Project", false, chosen, cancelled, error);
}

bool
RuntimeProjectHost::create(ProjectHostEntry* created,
                           bool* cancelled,
                           std::string& error)
{
  return pick("New Project Folder", true, created, cancelled, error);
}

bool
RuntimeProjectHost::open(std::uint32_t id, std::string& error)
{
  if (id == 0 || id > m_listed.size()) {
    error = "no such project in the last listing";
    return false;
  }
  const std::string path = m_listed[id - 1];
  std::error_code code;
  if (!std::filesystem::is_directory(pathOf(path), code)) {
    error = "the project folder is gone";
    return false;
  }
  std::vector<std::string> arguments = m_baseArguments;
  arguments.push_back("--project");
  arguments.push_back(path);
  SetRelaunchArguments(std::move(arguments));
  remember(path);
  Logger::LogInfo("The next restart opens the project " + path);
  return true;
}

void
RuntimeProjectHost::clear()
{
  store({});
  m_listed.clear();
}
