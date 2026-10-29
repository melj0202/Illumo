#include "../../Wasm/WasmCompiler.h"
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

// The Linux counterpart of WinWasmCompiler.cpp. The helper receives the
// shared buffer as this descriptor and limits its own memory before it reads
// the module (LinuxWasmCompilerMain.cpp).
static constexpr int kHelperDescriptor = 3;

class CompilerDescriptor
{
public:
  explicit CompilerDescriptor(int value = -1)
    : m_value(value)
  {
  }
  ~CompilerDescriptor()
  {
    if (m_value >= 0) {
      close(m_value);
    }
  }
  CompilerDescriptor(const CompilerDescriptor&) = delete;
  CompilerDescriptor& operator=(const CompilerDescriptor&) = delete;
  CompilerDescriptor(CompilerDescriptor&&) = delete;
  CompilerDescriptor& operator=(CompilerDescriptor&&) = delete;
  int get() const { return m_value; }

private:
  int m_value;
};

class CompilerSpawnSetup
{
public:
  CompilerSpawnSetup()
  {
    m_actionsReady = posix_spawn_file_actions_init(&m_actions) == 0;
    m_attributesReady = posix_spawnattr_init(&m_attributes) == 0;
  }
  ~CompilerSpawnSetup()
  {
    if (m_actionsReady) {
      posix_spawn_file_actions_destroy(&m_actions);
    }
    if (m_attributesReady) {
      posix_spawnattr_destroy(&m_attributes);
    }
  }
  CompilerSpawnSetup(const CompilerSpawnSetup&) = delete;
  CompilerSpawnSetup& operator=(const CompilerSpawnSetup&) = delete;
  CompilerSpawnSetup(CompilerSpawnSetup&&) = delete;
  CompilerSpawnSetup& operator=(CompilerSpawnSetup&&) = delete;
  bool ready() const { return m_actionsReady && m_attributesReady; }
  posix_spawn_file_actions_t* actions() { return &m_actions; }
  posix_spawnattr_t* attributes() { return &m_attributes; }

private:
  posix_spawn_file_actions_t m_actions{};
  posix_spawnattr_t m_attributes{};
  bool m_actionsReady = false;
  bool m_attributesReady = false;
};

// True once the helper has exited. It is left unreaped, so its process ID,
// which is also its process group ID, cannot be reused yet.
static bool
helperExited(pid_t helper)
{
  siginfo_t info{};
  if (waitid(
        P_PID, static_cast<id_t>(helper), &info, WEXITED | WNOHANG | WNOWAIT) !=
      0) {
    return errno != EINTR;
  }
  return info.si_pid == helper;
}

// Waits for the helper to exit, at most until the deadline. A pidfd wakes
// on exit; a kernel without pidfd_open is polled every two milliseconds.
static bool
waitForHelper(pid_t helper, std::chrono::steady_clock::time_point deadline)
{
  CompilerDescriptor process(
    static_cast<int>(syscall(SYS_pidfd_open, helper, 0)));
  while (!helperExited(helper)) {
    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();
    if (now >= deadline) {
      return false;
    }
    const long long remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
        .count() +
      1;
    if (process.get() >= 0) {
      pollfd ready{ process.get(), POLLIN, 0 };
      poll(
        &ready, 1, static_cast<int>(remaining < INT_MAX ? remaining : INT_MAX));
    } else {
      const timespec pause{ 0, 2000000L };
      nanosleep(&pause, nullptr);
    }
  }
  return true;
}

bool
compileWasmIsolated(std::span<const std::byte> input,
                    std::uint64_t memoryLimit,
                    std::uint32_t timeoutMilliseconds,
                    std::uint32_t engineOptions,
                    std::vector<std::byte>& artifact,
                    std::string& error)
{
  constexpr std::size_t kCapacity = 256u * 1024u * 1024u;
  constexpr std::size_t kHeaderBytes = 16u;
  if (input.empty() || input.size() > kCapacity - kHeaderBytes ||
      memoryLimit < 16u * 1024u * 1024u || timeoutMilliseconds == 0) {
    error = "Invalid isolated compiler limits";
    return false;
  }
  std::string executable(PATH_MAX, '\0');
  const ssize_t length =
    readlink("/proc/self/exe", executable.data(), executable.size());
  if (length <= 0 || static_cast<std::size_t>(length) >= executable.size()) {
    error = "Cannot locate the bundled WASM compiler";
    return false;
  }
  executable.resize(static_cast<std::size_t>(length));
  executable.resize(executable.find_last_of('/') + 1u);
  executable += "IllumoWasmCompiler";

  // The buffer is created close-on-exec and handed to the helper as
  // descriptor 3 only. The parent's copy stays off 3: duplicating a
  // descriptor onto its own number would keep close-on-exec set.
  const int created = memfd_create("IllumoWasmCompiler", MFD_CLOEXEC);
  CompilerDescriptor shared(
    created == kHelperDescriptor
      ? fcntl(created, F_DUPFD_CLOEXEC, kHelperDescriptor + 1)
      : created);
  if (created == kHelperDescriptor) {
    close(created);
  }
  if (shared.get() < 0 ||
      ftruncate(shared.get(), static_cast<off_t>(kCapacity)) != 0) {
    error = "Cannot allocate isolated compiler resources";
    return false;
  }
  void* view = mmap(
    nullptr, kCapacity, PROT_READ | PROT_WRITE, MAP_SHARED, shared.get(), 0);
  if (view == MAP_FAILED) {
    error = "Cannot map compiler input";
    return false;
  }
  std::byte* buffer = static_cast<std::byte*>(view);
  const std::uint64_t inputSize = input.size();
  std::memcpy(buffer, &inputSize, sizeof(inputSize));
  std::memcpy(buffer + kHeaderBytes, input.data(), input.size());
  munmap(view, kCapacity);

  // The helper inherits only the buffer and stderr (for a Wasmtime panic
  // message); stdin and stdout are /dev/null. It leads its own process
  // group, so a kill reaches anything it started.
  CompilerSpawnSetup setup;
  sigset_t noSignals;
  sigemptyset(&noSignals);
  if (!setup.ready() ||
      posix_spawn_file_actions_adddup2(
        setup.actions(), shared.get(), kHelperDescriptor) != 0 ||
      posix_spawn_file_actions_addopen(
        setup.actions(), STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0 ||
      posix_spawn_file_actions_addopen(
        setup.actions(), STDOUT_FILENO, "/dev/null", O_WRONLY, 0) != 0 ||
      posix_spawn_file_actions_addclosefrom_np(setup.actions(),
                                               kHelperDescriptor + 1) != 0 ||
      posix_spawnattr_setflags(
        setup.attributes(),
        static_cast<short>(POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK)) !=
        0 ||
      posix_spawnattr_setpgroup(setup.attributes(), 0) != 0 ||
      posix_spawnattr_setsigmask(setup.attributes(), &noSignals) != 0) {
    error = "Cannot restrict inherited compiler handles";
    return false;
  }
  std::string descriptorText = std::to_string(kHelperDescriptor);
  std::string optionsText = std::to_string(engineOptions);
  std::string memoryText = std::to_string(memoryLimit);
  std::vector<char*> arguments{ executable.data(),
                                descriptorText.data(),
                                optionsText.data(),
                                memoryText.data(),
                                nullptr };
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() +
    std::chrono::milliseconds(timeoutMilliseconds);
  pid_t helper = 0;
  if (posix_spawn(&helper,
                  executable.c_str(),
                  setup.actions(),
                  setup.attributes(),
                  arguments.data(),
                  environ) != 0) {
    error = "Cannot launch bundled IllumoWasmCompiler";
    return false;
  }
  const bool finished = waitForHelper(helper, deadline);
  // The helper is a zombie or still running, so its group ID is still its
  // own: end the group (the helper on a timeout, any descendants always),
  // as closing the Windows job does, then reap the helper.
  kill(-helper, SIGKILL);
  int status = 0;
  while (waitpid(helper, &status, 0) < 0 && errno == EINTR) {
  }
  if (!finished) {
    error = "WASM compiler exceeded its deadline";
    return false;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    // Codes 2-4 are the helper's own verdicts; others are terminations.
    if (WIFEXITED(status) && WEXITSTATUS(status) == 3) {
      error = "WASM module failed to compile";
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 4) {
      error = "Compiled WASM artifact exceeds the compiler buffer";
    } else {
      const std::string cause =
        WIFSIGNALED(status)
          ? "signal " + std::to_string(WTERMSIG(status))
          : "exit code " + std::to_string(WEXITSTATUS(status));
      error = "WASM compiler terminated (" + cause +
              "); it may have exceeded its " +
              std::to_string(memoryLimit / (1024u * 1024u)) + " MiB limit";
    }
    return false;
  }
  view = mmap(nullptr, kCapacity, PROT_READ, MAP_SHARED, shared.get(), 0);
  if (view == MAP_FAILED) {
    error = "Cannot read compiler artifact";
    return false;
  }
  buffer = static_cast<std::byte*>(view);
  std::uint64_t outputSize = 0;
  std::memcpy(&outputSize, buffer + sizeof(inputSize), sizeof(outputSize));
  if (outputSize == 0 || outputSize > kCapacity - kHeaderBytes) {
    munmap(view, kCapacity);
    error = "Invalid compiler artifact size";
    return false;
  }
  artifact.assign(buffer + kHeaderBytes, buffer + kHeaderBytes + outputSize);
  munmap(view, kCapacity);
  return true;
}
