#include <Illumo/Platform/ProcessRelaunch.h>

#include <cstring>
#include <spawn.h>
#include <string>
#include <sys/types.h>

extern char** environ;

bool
RelaunchCurrentProcess(int argc, char** argv, std::string* error)
{
  if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
    if (error != nullptr) {
      *error = "the original command line is unavailable";
    }
    return false;
  }
  // /proc/self/exe is this program even when argv[0] is a bare name.
  pid_t child = 0;
  const int result =
    posix_spawn(&child, "/proc/self/exe", nullptr, nullptr, argv, environ);
  if (result != 0) {
    if (error != nullptr) {
      *error = std::string("posix_spawn failed: ") + std::strerror(result);
    }
    return false;
  }
  return true;
}
