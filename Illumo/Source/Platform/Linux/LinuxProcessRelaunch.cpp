#include <Illumo/Platform/ProcessRelaunch.h>

#include <cstring>
#include <spawn.h>
#include <string>
#include <utility>
#include <sys/types.h>

extern char** environ;

static std::vector<std::string>&
relaunchArguments()
{
  static std::vector<std::string> arguments;
  return arguments;
}

void
SetRelaunchArguments(std::vector<std::string> arguments)
{
  relaunchArguments() = std::move(arguments);
}

const std::vector<std::string>&
RelaunchArguments()
{
  return relaunchArguments();
}

bool
RelaunchCurrentProcess(int argc, char** argv, std::string* error)
{
  if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
    if (error != nullptr) {
      *error = "the original command line is unavailable";
    }
    return false;
  }
  // Replacement arguments keep the program name.
  std::vector<char*> replaced;
  if (!relaunchArguments().empty()) {
    replaced.push_back(argv[0]);
    for (const std::string& argument : relaunchArguments()) {
      replaced.push_back(const_cast<char*>(argument.c_str()));
    }
    replaced.push_back(nullptr);
  }
  // /proc/self/exe is this program even when argv[0] is a bare name.
  pid_t child = 0;
  const int result = posix_spawn(&child,
                                 "/proc/self/exe",
                                 nullptr,
                                 nullptr,
                                 replaced.empty() ? argv : replaced.data(),
                                 environ);
  if (result != 0) {
    if (error != nullptr) {
      *error = std::string("posix_spawn failed: ") + std::strerror(result);
    }
    return false;
  }
  return true;
}
