#include <Illumo/Platform/ChildProcess.h>

#include <cstring>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <vector>

extern char** environ;

struct ChildProcess::State
{
  pid_t process = 0;
};

ChildProcess::ChildProcess()
  : m_state(std::make_unique<State>())
{
}

ChildProcess::~ChildProcess()
{
  stop();
}

bool
ChildProcess::start(const std::vector<std::string>& arguments,
                    std::string* error)
{
  stop();
  // /proc/self/exe is this program even when argv[0] is a bare name.
  std::vector<std::string> storage;
  storage.reserve(arguments.size() + 1u);
  storage.push_back("/proc/self/exe");
  storage.insert(storage.end(), arguments.begin(), arguments.end());
  std::vector<char*> argv;
  for (std::string& argument : storage) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);
  pid_t child = 0;
  const int result = posix_spawn(
    &child, "/proc/self/exe", nullptr, nullptr, argv.data(), environ);
  if (result != 0) {
    if (error != nullptr) {
      *error = std::string("posix_spawn failed: ") + std::strerror(result);
    }
    return false;
  }
  m_state->process = child;
  return true;
}

bool
ChildProcess::running()
{
  if (m_state->process == 0) {
    return false;
  }
  int status = 0;
  if (waitpid(m_state->process, &status, WNOHANG) == 0) {
    return true;
  }
  m_state->process = 0;
  return false;
}

void
ChildProcess::stop()
{
  if (m_state->process == 0) {
    return;
  }
  kill(m_state->process, SIGKILL);
  int status = 0;
  waitpid(m_state->process, &status, 0);
  m_state->process = 0;
}
