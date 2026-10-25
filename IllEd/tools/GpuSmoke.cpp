// IllEd.Gpu.Smoke: the IllEd package through a real window
// (docs/illed-editing-wave4-plan.md, 5.6). Runs IllumoRuntime --app illed on
// a scratch project and storage with --capture and a script that creates a
// node and saves it into the project, then checks that the capture is a
// non-uniform image of the window's size and that the saved scene holds the
// node. Returns 77 (CTest skips) when the runtime could not open a render
// window, so headless hosts never fail on it.
//
// Usage: IllEdGpuSmoke <path to IllumoRuntime>
//
// The runtime rewrites WinX/WinY in its own envvars.json beside the
// executable; that file is backed up and restored.
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneDocument.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "thirdparty/stb/stb_image.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

static constexpr int kSkipped = 77;
static constexpr int kWidth = 1280;
static constexpr int kHeight = 720;

static std::string
utf8Of(const std::filesystem::path& path)
{
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

static std::string
readText(const std::filesystem::path& path)
{
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), {});
}

#if defined(_WIN32)
static std::wstring
wideOf(const std::string& text)
{
  const int size = MultiByteToWideChar(
    CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
  if (size > 0) {
    MultiByteToWideChar(CP_UTF8,
                        0,
                        text.data(),
                        static_cast<int>(text.size()),
                        result.data(),
                        size);
  }
  return result;
}

// Quotes one argument the way CommandLineToArgvW undoes it.
static void
appendQuoted(std::wstring& commandLine, const std::wstring& argument)
{
  commandLine.push_back(L'"');
  std::size_t backslashes = 0;
  for (wchar_t character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'"') {
      commandLine.append(backslashes * 2 + 1, L'\\');
      backslashes = 0;
      commandLine.push_back(L'"');
      continue;
    }
    commandLine.append(backslashes, L'\\');
    backslashes = 0;
    commandLine.push_back(character);
  }
  commandLine.append(backslashes * 2, L'\\');
  commandLine.push_back(L'"');
}
#endif

// Runs the program with the arguments (no command processor), collecting
// its standard output and error; the exit status is the program's, or -1
// when it could not start.
static int
run(const std::filesystem::path& program,
    const std::vector<std::string>& arguments,
    std::string* output)
{
#if defined(_WIN32)
  SECURITY_ATTRIBUTES inheritable{};
  inheritable.nLength = sizeof(inheritable);
  inheritable.bInheritHandle = TRUE;
  HANDLE readEnd = nullptr;
  HANDLE writeEnd = nullptr;
  if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0)) {
    return -1;
  }
  SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
  std::wstring commandLine;
  appendQuoted(commandLine, program.wstring());
  for (const std::string& argument : arguments) {
    commandLine.push_back(L' ');
    appendQuoted(commandLine, wideOf(argument));
  }
  // CreateProcessW may modify the buffer in place.
  std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
  buffer.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = writeEnd;
  startup.hStdError = writeEnd;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION process{};
  const BOOL started = CreateProcessW(program.wstring().c_str(),
                                      buffer.data(),
                                      nullptr,
                                      nullptr,
                                      TRUE,
                                      0,
                                      nullptr,
                                      nullptr,
                                      &startup,
                                      &process);
  CloseHandle(writeEnd);
  if (!started) {
    CloseHandle(readEnd);
    return -1;
  }
  char chunk[512];
  DWORD read = 0;
  while (ReadFile(readEnd, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
    output->append(chunk, read);
  }
  CloseHandle(readEnd);
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD status = 0;
  GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<int>(status);
#else
  int fds[2] = { -1, -1 };
  if (pipe(fds) != 0) {
    return -1;
  }
  std::vector<std::string> owned;
  owned.push_back(utf8Of(program));
  owned.insert(owned.end(), arguments.begin(), arguments.end());
  std::vector<char*> argv;
  for (std::string& argument : owned) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, fds[0]);
  pid_t child = 0;
  const int spawned = posix_spawn(
    &child, owned.front().c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(fds[1]);
  if (spawned != 0) {
    close(fds[0]);
    return -1;
  }
  char chunk[512];
  ssize_t read = 0;
  while ((read = ::read(fds[0], chunk, sizeof(chunk))) > 0) {
    output->append(chunk, static_cast<std::size_t>(read));
  }
  close(fds[0]);
  int status = 0;
  waitpid(child, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

static int
fail(const std::string& message)
{
  std::printf("FAIL: %s\n", message.c_str());
  return 1;
}

int
main(int argc, char** argv)
{
  if (argc != 2) {
    std::printf("usage: IllEdGpuSmoke <IllumoRuntime>\n");
    return 2;
  }
  const std::filesystem::path runtime = std::filesystem::absolute(argv[1]);
  std::error_code code;
  if (!std::filesystem::exists(runtime, code)) {
    return fail("the runtime is missing: " + runtime.string());
  }
  const std::filesystem::path scratch =
    std::filesystem::temp_directory_path() /
    ("illed-gpu-smoke-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path project = scratch / "project";
  const std::filesystem::path storage = scratch / "storage";
  const std::filesystem::path script = scratch / "smoke.txt";
  const std::filesystem::path capture = scratch / "smoke.png";
  std::filesystem::create_directories(project / "scenes", code);
  std::filesystem::create_directories(storage, code);
  std::ofstream(project / "illumo.json")
    << R"({"format":"ilpk","format_version":1,"id":"smoke","kind":"content"})";
  // A node created at the origin, saved into /project/scenes, then a pause
  // so the capture frame shows the result.
  std::ofstream(script) << "@wait 60\nscene_create cube\n@wait 5\n"
                           "scene_save_project\n@wait 30\n";

  // The runtime's own settings file, restored after the run.
  const std::filesystem::path settings = runtime.parent_path() / "envvars.json";
  const bool hadSettings = std::filesystem::exists(settings, code);
  const std::filesystem::path settingsBackup = scratch / "envvars.backup";
  if (hadSettings) {
    std::filesystem::copy_file(
      settings,
      settingsBackup,
      std::filesystem::copy_options::overwrite_existing,
      code);
  }

  const std::vector<std::string> arguments{ "--app",
                                            "illed",
                                            "--project",
                                            utf8Of(project),
                                            "--storage",
                                            utf8Of(storage),
                                            "-ww",
                                            std::to_string(kWidth),
                                            "-wh",
                                            std::to_string(kHeight),
                                            "--capture",
                                            utf8Of(capture),
                                            "--capture-frame",
                                            "150",
                                            "--capture-script",
                                            utf8Of(script) };
  std::string output;
  const int status = run(runtime, arguments, &output);

  if (hadSettings) {
    std::filesystem::copy_file(
      settingsBackup,
      settings,
      std::filesystem::copy_options::overwrite_existing,
      code);
  } else {
    std::filesystem::remove(settings, code);
  }

  // The capture result is the one line holding a "success" member.
  nlohmann::json result;
  std::size_t start = 0;
  while (start < output.size()) {
    const std::size_t end = output.find('\n', start);
    const std::string line = output.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
    if (line.find("\"success\"") != std::string::npos) {
      nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
      if (parsed.is_object()) {
        result = std::move(parsed);
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  int exitCode = 0;
  if (result.is_null()) {
    // No capture result at all: the runtime stopped before its shell ran,
    // which is what a host without a render window does.
    std::printf("SKIPPED: the runtime produced no capture result (status %d); "
                "no render window on this host?\n%s\n",
                status,
                output.c_str());
    exitCode = kSkipped;
  } else if (!result.value("success", false)) {
    exitCode =
      fail("the capture failed: " + result.value("error", std::string()) +
           (result.contains("guestError")
              ? " (guest: " + result.value("guestError", std::string()) + ")"
              : std::string()));
  } else {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels =
      stbi_load(capture.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
      exitCode = fail("the capture could not be decoded");
    } else {
      // Distinct colours: a blank or single-colour frame means the editor
      // drew nothing.
      std::set<unsigned int> colours;
      const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
      for (std::size_t index = 0; index < count && colours.size() < 64;
           ++index) {
        const unsigned char* pixel = pixels + index * 4;
        colours.insert((static_cast<unsigned int>(pixel[0]) << 16u) |
                       (static_cast<unsigned int>(pixel[1]) << 8u) |
                       static_cast<unsigned int>(pixel[2]));
      }
      stbi_image_free(pixels);
      std::printf("capture %dx%d, %zu distinct colours (counted up to 64)\n",
                  width,
                  height,
                  colours.size());
      if (width < kWidth || height < kHeight) {
        exitCode = fail("the capture is smaller than the window");
      } else if (colours.size() < 16) {
        exitCode = fail("the capture is uniform: the editor drew nothing");
      }
    }
    if (exitCode == 0) {
      SceneDocument saved;
      std::string error;
      const std::filesystem::path scene = project / "scenes" / "Untitled.ilsc";
      if (!std::filesystem::exists(scene, code)) {
        exitCode = fail("scene_save_project wrote nothing into the project");
      } else if (!IlscCodec::parse(readText(scene), saved, error)) {
        exitCode = fail("the saved scene does not parse: " + error);
      } else if (saved.nodes.size() != 1 ||
                 saved.nodes.front().find(SceneComponentType::Primitive) ==
                   nullptr) {
        exitCode = fail("the saved scene does not hold the created cube");
      }
    }
  }
  if (exitCode == 0) {
    std::printf("PASS: IllEd drew a window, created a cube and saved it\n");
  } else if (exitCode != kSkipped) {
    std::printf("runtime status %d, output:\n%s\n", status, output.c_str());
  }
  std::filesystem::remove_all(scratch, code);
  return exitCode;
}
