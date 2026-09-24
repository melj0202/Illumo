#pragma once
#include <IllumoGuest/Files.h>
#include <functional>

// Browses and edits the host's virtual file tree (file protocol v2) with
// completion callbacks: /app, /engine, /packages/<id> and, when the host
// mounted one, the writable /project. Pump it after GuestFiles::pump on
// every update; callbacks run from pump and may start new operations.
class GuestFileTree
{
public:
  using Listed =
    std::function<void(GuestFileOutcome, std::vector<GuestFileEntry>)>;
  using Stated = std::function<void(GuestFileOutcome, GuestFileStatus)>;
  using Loaded = std::function<void(GuestFileOutcome, std::vector<std::byte>)>;
  using Done = std::function<void(GuestFileOutcome)>;

  // Largest directory a listing collects before it stops paging.
  static constexpr std::size_t MaximumListing = 65536;

  explicit GuestFileTree(GuestFiles& files)
    : m_files(files)
  {
  }
  ~GuestFileTree() { cancel(); }
  GuestFileTree(const GuestFileTree&) = delete;
  GuestFileTree& operator=(const GuestFileTree&) = delete;

  // Each returns false (and never calls back) when the request cannot be
  // queued.
  bool list(std::string path, Listed done);
  bool stat(std::string path, Stated done);
  bool read(std::string path,
            Loaded done,
            std::size_t maximum = 256u * 1024u * 1024u);
  // Project files only (needs the ProjectFiles capability).
  bool write(std::string path, std::vector<std::byte> bytes, Done done);
  bool importFile(std::string grant, std::string target, Done done);
  bool pack(std::string grant, std::string source, Done done);

  void pump();
  // Drops every operation without calling back.
  void cancel();
  bool idle() const { return m_operations.empty(); }

private:
  enum class Kind
  {
    List,
    Stat,
    Read,
    Write,
    Import,
    Pack
  };
  struct Operation
  {
    Kind kind = Kind::Read;
    std::uint64_t task = 0;
    std::string path;
    std::uint32_t cursor = 0;
    std::vector<GuestFileEntry> entries;
    Listed listed;
    Stated stated;
    Loaded loaded;
    Done done;
  };
  bool start(Operation operation);

  GuestFiles& m_files;
  std::vector<Operation> m_operations;
};
