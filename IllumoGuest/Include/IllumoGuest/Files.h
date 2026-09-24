#pragma once
#include <IllumoGuest/FileProtocol.h>
#include <IllumoGuest/Services.h>

struct GuestFileResult
{
  GuestFileOutcome outcome = GuestFileOutcome::IoError;
  std::vector<std::byte> bytes;
};

// Guest-local asynchronous transfers. Task IDs and all continuation state stay
// in this store. Cancellation drains late opens and closes their capabilities.
// Owned by the control application until store retirement; modules borrow it
// and cancel their task IDs on exit while the application continues pumping.
class GuestFiles
{
public:
  // Tasks and in-flight requests held at once.
  static constexpr std::size_t MaximumTasks = 16;

  explicit GuestFiles(GuestServiceQueue& services)
    : m_services(services)
  {
  }
  ~GuestFiles() = default;
  GuestFiles(const GuestFiles&) = delete;
  GuestFiles& operator=(const GuestFiles&) = delete;
  GuestFiles(GuestFiles&&) = delete;
  GuestFiles& operator=(GuestFiles&&) = delete;
  std::uint64_t read(GuestFileArea area,
                     std::string path,
                     std::size_t maximum = 64u * 1024u * 1024u);
  std::uint64_t write(std::string path, std::vector<std::byte> bytes);
  std::uint64_t write(GuestFileArea area,
                      std::string path,
                      std::vector<std::byte> bytes);
  // Single-request queries (file protocol v2). A successful result's bytes
  // hold a GuestFileListing (list) or GuestFileStatus (stat) payload; import
  // and pack succeed with no bytes.
  std::uint64_t list(std::string path,
                     std::uint32_t cursor,
                     std::uint32_t limit = GuestFileRequest::MaximumListPage);
  std::uint64_t stat(std::string path);
  // Copies a Selected grant into the project at target.
  std::uint64_t importFile(std::string grant, std::string target);
  // Packs the project directory source into a writable Selected grant.
  std::uint64_t pack(std::string grant, std::string source);
  void pump();
  bool take(std::uint64_t task, GuestFileResult& result);
  void cancel(std::uint64_t task);
  bool idle() const { return m_tasks.empty(); }

private:
  enum class Stage
  {
    Open,
    Transfer,
    Commit,
    Close,
    Done
  };
  struct Pending
  {
    std::uint64_t request = 0;
    GuestFileAction action = GuestFileAction::Open;
    std::uint64_t offset = 0;
    std::uint32_t count = 0;
  };
  struct Task
  {
    GuestFileArea area = GuestFileArea::Package;
    std::string path;
    std::size_t maximum = 0;
    std::uint64_t size = 0, submitted = 0, completed = 0;
    GuestResourceId file;
    std::vector<std::byte> bytes;
    std::vector<Pending> pending;
    Stage stage = Stage::Open;
    GuestFileOutcome outcome = GuestFileOutcome::Success;
    bool writing = false, cancelled = false;
    // A single-request query (List, Stat, Import, Pack).
    bool query = false;
    GuestFileRequest request;
  };
  std::uint64_t addQuery(GuestFileRequest request);
  bool enqueue(Task& task, GuestFileRequest request, std::uint32_t count = 0);
  void complete(Task& task,
                const Pending& pending,
                const GuestServiceRecord& result);
  GuestServiceQueue& m_services;
  std::map<std::uint64_t, Task> m_tasks;
  std::uint64_t m_next = 1;
};
