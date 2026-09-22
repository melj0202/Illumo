#include <Illumo/Platform/AtomicFile.h>
#include <Illumo/Wasm/WasmFileServices.h>
#include <IllumoGuest/FileProtocol.h>
#include <IllumoGuest/Protocol.h>
#include <array>
#include <condition_variable>
#include <fstream>
#include <list>
#include <map>
#include <mutex>
#include <thread>

class WasmFileServices::State
{
public:
  struct File
  {
    std::filesystem::path destination;
    std::filesystem::path staging;
    std::fstream stream;
    std::uint64_t size = 0;
    std::uint64_t written = 0;
    bool writing = false;
    bool selected = false;
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File()
    {
      if (stream.is_open()) {
        stream.close();
      }
      if (!staging.empty()) {
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
      }
    }
  };
  struct Slot
  {
    std::uint32_t generation = 0;
    std::unique_ptr<File> file;
  };
  struct Request
  {
    std::uint64_t id;
    GuestFileRequest value;
  };

  State(std::uint64_t identity,
        std::uint32_t capabilities,
        std::filesystem::path package,
        std::filesystem::path storage,
        WasmFileLimits policy)
    : owner(identity)
    , grants(capabilities)
    , limits(policy)
  {
    if (owner == 0 || !package.is_absolute() || !storage.is_absolute() ||
        limits.openFiles == 0 || limits.openFiles > 64 ||
        limits.fileBytes > 1024ull * 1024ull * 1024ull) {
      throw std::invalid_argument("Invalid file service policy");
    }
    packageRoot = std::filesystem::canonical(package);
    storageRoot = std::filesystem::canonical(storage);
    if (!std::filesystem::is_directory(packageRoot) ||
        !std::filesystem::is_directory(storageRoot)) {
      throw std::invalid_argument("File service roots must be directories");
    }
    files.resize(limits.openFiles);
    worker = std::thread([this]() { work(); });
  }
  ~State()
  {
    cancel();
    if (worker.joinable()) {
      worker.join();
    }
  }
  void cancel()
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
    requests.clear();
    completions.records.clear();
    outstanding = 0;
    condition.notify_all();
  }
  static bool relativeName(const std::string& path)
  {
    if (path.empty() || path.front() == '/' ||
        path.find_first_of("\\:*?\"<>|") != std::string::npos) {
      return false;
    }
    for (const unsigned char character : path) {
      if (character < 32u || character == 127u) {
        return false;
      }
    }
    std::size_t start = 0;
    while (start < path.size()) {
      const std::size_t separator = path.find('/', start);
      const std::string part = path.substr(
        start, separator == std::string::npos ? separator : separator - start);
      if (part.empty() || part == "." || part == ".." || part.back() == '.' ||
          part.back() == ' ') {
        return false;
      }
      std::string upper = part;
      for (char& character : upper) {
        if (character >= 'a' && character <= 'z') {
          character = static_cast<char>(character - 'a' + 'A');
        }
      }
      if (upper.starts_with(".ILLUMO-")) {
        return false;
      }
      const std::string stem = upper.substr(0, upper.find('.'));
      if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
          stem == "CONIN$" || stem == "CONOUT$" || stem == "CLOCK$" ||
          (stem.size() == 4 &&
           (stem.starts_with("COM") || stem.starts_with("LPT")) &&
           stem[3] >= '0' && stem[3] <= '9')) {
        return false;
      }
      if (separator == std::string::npos) {
        return true;
      }
      start = separator + 1;
    }
    return false;
  }
  std::filesystem::path resolve(const GuestFileRequest& request) const
  {
    if (!relativeName(request.path)) {
      return {};
    }
    const std::filesystem::path& root =
      request.area == GuestFileArea::Package ? packageRoot : storageRoot;
    const std::u8string name(
      reinterpret_cast<const char8_t*>(request.path.data()),
      request.path.size());
    const std::filesystem::path path =
      std::filesystem::weakly_canonical(root / std::filesystem::path(name));
    std::filesystem::path::const_iterator candidate = path.begin();
    for (const std::filesystem::path& component : root) {
      if (candidate == path.end() || *candidate != component) {
        return {};
      }
      ++candidate;
    }
    return candidate != path.end() ? path : std::filesystem::path{};
  }
  File* find(const GuestResourceId& id)
  {
    if (id.owner != owner || id.kind != GuestResourceKind::File ||
        id.slot == 0 || id.slot > files.size()) {
      return nullptr;
    }
    Slot& slot = files[id.slot - 1];
    return slot.generation == id.generation ? slot.file.get() : nullptr;
  }
  void close(const GuestResourceId& id)
  {
    File* file = find(id);
    if (file && file->writing) {
      reserved -= file->size * 2;
    }
    if (file) {
      files[id.slot - 1].file.reset();
    }
  }
  bool storageFits(const File& replacing)
  {
    std::uint64_t bytes = replacing.size;
    std::size_t count = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(storageRoot)) {
      if (++count > 4096 || entry.is_symlink()) {
        return false;
      }
      if (!entry.is_regular_file() || entry.path() == replacing.destination) {
        continue;
      }
      bool staging = false;
      for (const Slot& slot : files) {
        staging = staging || (slot.file && slot.file->staging == entry.path());
      }
      if (staging) {
        continue;
      }
      const std::uint64_t size = entry.file_size();
      if (bytes > limits.storageBytes || size > limits.storageBytes - bytes) {
        return false;
      }
      bytes += size;
    }
    return bytes <= limits.storageBytes;
  }
  // Readable grants need an existing bounded file; writable grants need its
  // directory. An empty fixed name allocates the next "sel-N".
  bool grant(const std::filesystem::path& path,
             bool readable,
             bool writable,
             const std::string& fixedName,
             std::string& name,
             std::uint64_t& size)
  {
    std::error_code status;
    if (!path.is_absolute()) {
      return false;
    }
    const std::filesystem::path absolute =
      std::filesystem::weakly_canonical(path, status);
    if (status || absolute.empty()) {
      return false;
    }
    std::uint64_t bytes = 0;
    if (readable) {
      if (!std::filesystem::is_regular_file(absolute)) {
        return false;
      }
      bytes = std::filesystem::file_size(absolute, status);
      if (status || bytes > limits.fileBytes) {
        return false;
      }
    }
    if (writable && !std::filesystem::is_directory(absolute.parent_path())) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping || selected.size() >= 16 ||
        (!fixedName.empty() && selected.contains(fixedName))) {
      return false;
    }
    name =
      fixedName.empty() ? "sel-" + std::to_string(++nextSelected) : fixedName;
    selected[name] = SelectedGrant{ absolute, writable };
    size = bytes;
    return true;
  }
  GuestFileOutcome execute(const GuestFileRequest& request,
                           GuestWireWriter& response)
  {
    if (request.action == GuestFileAction::Open) {
      const GuestCapability capability =
        request.area == GuestFileArea::Package
          ? GuestCapability::Assets
          : (request.area == GuestFileArea::Selected
               ? GuestCapability::SelectedFiles
               : GuestCapability::Storage);
      if ((grants & static_cast<std::uint32_t>(capability)) == 0 ||
          (request.writing && request.area == GuestFileArea::Package)) {
        return GuestFileOutcome::Denied;
      }
      std::filesystem::path path;
      bool selectedGrant = false;
      if (request.area == GuestFileArea::Selected) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::map<std::string, SelectedGrant>::const_iterator found =
          selected.find(request.path);
        if (found == selected.end() ||
            (request.writing && !found->second.writable)) {
          return GuestFileOutcome::Denied;
        }
        path = found->second.path;
        selectedGrant = true;
      } else {
        path = resolve(request);
      }
      if (path.empty()) {
        return GuestFileOutcome::Denied;
      }
      std::size_t index = 0;
      while (index < files.size() &&
             (files[index].file || files[index].generation == UINT32_MAX)) {
        ++index;
      }
      if (index == files.size()) {
        return GuestFileOutcome::Denied;
      }
      std::unique_ptr<File> file = std::make_unique<File>();
      file->writing = request.writing;
      file->selected = selectedGrant;
      file->destination = path;
      if (request.writing) {
        if (request.size > limits.fileBytes || reserved > limits.stagedBytes ||
            request.size > (limits.stagedBytes - reserved) / 2) {
          return GuestFileOutcome::Denied;
        }
        file->size = request.size;
        for (unsigned int attempt = 0; attempt < 32; ++attempt) {
          file->staging =
            path.parent_path() / (".illumo-wasm-" + std::to_string(owner) +
                                  "-" + std::to_string(++nextStage));
          file->stream.clear();
          file->stream.open(file->staging,
                            std::ios::binary | std::ios::out |
                              std::ios::noreplace);
          if (file->stream.is_open()) {
            break;
          }
          // A collision is not ours to delete during cleanup.
          file->staging.clear();
        }
        if (!file->stream.is_open()) {
          return GuestFileOutcome::IoError;
        }
      } else {
        if (!std::filesystem::exists(path)) {
          return GuestFileOutcome::NotFound;
        }
        if (!std::filesystem::is_regular_file(path)) {
          return GuestFileOutcome::Denied;
        }
        file->size = std::filesystem::file_size(path);
        if (file->size > limits.fileBytes) {
          return GuestFileOutcome::Denied;
        }
        file->stream.open(path, std::ios::binary | std::ios::in);
        if (!file->stream.is_open()) {
          return GuestFileOutcome::IoError;
        }
      }
      Slot& slot = files[index];
      GuestResourceId{ owner,
                       GuestResourceKind::File,
                       static_cast<std::uint32_t>(index + 1),
                       slot.generation + 1 }
        .write(response);
      response.u64(file->size);
      // AtomicFile's commit writer needs a second sibling staging file.
      if (file->writing) {
        reserved += file->size * 2;
      }
      slot.file = std::move(file);
      ++slot.generation;
      return GuestFileOutcome::Success;
    }
    File* file = find(request.file);
    if (!file) {
      return GuestFileOutcome::Denied;
    }
    if (request.action == GuestFileAction::Close) {
      close(request.file);
      return GuestFileOutcome::Success;
    }
    if (request.action == GuestFileAction::Read) {
      if (file->writing || request.offset > file->size ||
          request.size > file->size - request.offset) {
        return GuestFileOutcome::Denied;
      }
      std::vector<std::byte> bytes(static_cast<std::size_t>(request.size));
      file->stream.clear();
      file->stream.seekg(static_cast<std::streamoff>(request.offset));
      file->stream.read(reinterpret_cast<char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
      if (!file->stream ||
          file->stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return GuestFileOutcome::IoError;
      }
      response.bytes(bytes);
      return GuestFileOutcome::Success;
    }
    if (!file->writing) {
      return GuestFileOutcome::Denied;
    }
    if (request.action == GuestFileAction::Write) {
      if (request.offset != file->written ||
          request.data.size() > file->size - file->written) {
        return GuestFileOutcome::Denied;
      }
      file->stream.write(reinterpret_cast<const char*>(request.data.data()),
                         static_cast<std::streamsize>(request.data.size()));
      if (!file->stream) {
        return GuestFileOutcome::IoError;
      }
      file->written += request.data.size();
      response.u64(file->written);
      return GuestFileOutcome::Success;
    }
    if (request.action != GuestFileAction::Commit ||
        file->written != file->size ||
        (!file->selected && !storageFits(*file))) {
      return GuestFileOutcome::Denied;
    }
    file->stream.flush();
    if (!file->stream) {
      return GuestFileOutcome::IoError;
    }
    file->stream.close();
    if (file->stream.fail()) {
      return GuestFileOutcome::IoError;
    }
    const bool committed = AtomicFile::write(
      file->destination, [&file](std::ostream& destination, std::string*) {
        std::ifstream source(file->staging, std::ios::binary);
        std::array<char, GuestFileRequest::MaximumBlock> block{};
        std::uint64_t copied = 0;
        while (copied < file->size && source && destination) {
          const std::streamsize size = static_cast<std::streamsize>(
            std::min<std::uint64_t>(block.size(), file->size - copied));
          source.read(block.data(), size);
          if (source.gcount() != size) {
            return false;
          }
          destination.write(block.data(), size);
          copied += static_cast<std::uint64_t>(size);
        }
        return copied == file->size && destination.good();
      });
    close(request.file);
    return committed ? GuestFileOutcome::Success : GuestFileOutcome::IoError;
  }
  void work()
  try {
    for (;;) {
      Request request;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock,
                       [this]() { return stopping || !requests.empty(); });
        if (stopping) {
          break;
        }
        request = std::move(requests.front());
        requests.pop_front();
      }
      GuestServiceRecord completion{
        request.id, GuestService::File, GuestServiceStatus::Complete, {}
      };
      GuestWireWriter payload;
      GuestFileOutcome outcome = GuestFileOutcome::IoError;
      try {
        outcome = execute(request.value, payload);
      } catch (const std::exception&) {
      }
      GuestWireWriter response;
      response.u32(static_cast<std::uint32_t>(outcome));
      if (outcome == GuestFileOutcome::Success) {
        response.bytes(payload.data());
      }
      completion.payload = response.take();
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!stopping) {
          completions.records.push_back(std::move(completion));
        }
      }
    }
    files.clear();
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex);
    failed = true;
    stopping = true;
  }
  struct SelectedGrant
  {
    std::filesystem::path path;
    bool writable = false;
  };
  const std::uint64_t owner;
  const std::uint32_t grants;
  const WasmFileLimits limits;
  std::filesystem::path packageRoot, storageRoot;
  std::vector<Slot> files;
  std::map<std::string, SelectedGrant> selected;
  std::uint64_t reserved = 0, nextStage = 0, lastRequest = 0, nextSelected = 0;
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::list<Request> requests;
  GuestServices completions;
  std::size_t outstanding = 0;
  bool stopping = false;
  bool failed = false;
  std::thread worker;
  std::string error;
};

WasmFileServices::WasmFileServices(std::uint64_t owner,
                                   std::uint32_t grants,
                                   std::filesystem::path packageRoot,
                                   std::filesystem::path storageRoot,
                                   WasmFileLimits limits)
  : m_state(std::make_unique<State>(owner,
                                    grants,
                                    std::move(packageRoot),
                                    std::move(storageRoot),
                                    limits))
{
}
WasmFileServices::~WasmFileServices() = default;
bool
WasmFileServices::grantSelected(const std::filesystem::path& path,
                                bool writable,
                                std::string& name,
                                std::uint64_t& size)
{
  return m_state->grant(path, !writable, writable, {}, name, size);
}
bool
WasmFileServices::grantEditable(const std::filesystem::path& path,
                                std::string& name,
                                std::uint64_t& size)
{
  return m_state->grant(path, true, true, {}, name, size);
}
bool
WasmFileServices::grantLaunch(const std::filesystem::path& path,
                              bool editable,
                              std::uint64_t& size)
{
  std::string name;
  return m_state->grant(path, true, editable, "launch", name, size);
}
bool
WasmFileServices::submit(const GuestServices& incoming)
{
  State& state = *m_state;
  if (incoming.records.size() > GuestServices::MaximumRecords) {
    state.error = "File batch exceeds record quota";
    return false;
  }
  std::list<State::Request> validated;
  std::uint64_t last = state.lastRequest;
  for (const GuestServiceRecord& record : incoming.records) {
    GuestFileRequest request;
    if (record.operation != GuestService::File ||
        record.status != GuestServiceStatus::Request ||
        record.request <= last ||
        !GuestFileRequest::read(record.payload, request)) {
      state.error = "Invalid file request batch";
      return false;
    }
    validated.push_back({ record.request, std::move(request) });
    last = record.request;
  }
  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.stopping ||
      validated.size() > GuestServices::MaximumRecords - state.outstanding) {
    state.error = "File request queue exhausted";
    return false;
  }
  state.outstanding += validated.size();
  state.requests.splice(state.requests.end(), validated);
  state.lastRequest = last;
  state.condition.notify_one();
  return true;
}
GuestServices
WasmFileServices::poll(std::size_t byteBudget)
{
  State& state = *m_state;
  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.failed) {
    throw std::runtime_error("Native file worker failed");
  }
  GuestServices result;
  result.records.reserve(state.completions.records.size());
  std::size_t bytes = 12;
  while (!state.completions.records.empty()) {
    const std::size_t size =
      state.completions.records.front().payload.size() + 20;
    if (bytes > byteBudget || size > byteBudget - bytes) {
      break;
    }
    result.records.push_back(std::move(state.completions.records.front()));
    state.completions.records.erase(state.completions.records.begin());
    bytes += size;
  }
  state.outstanding -= result.records.size();
  return result;
}
std::size_t
WasmFileServices::pendingRequests() const
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  return m_state->outstanding;
}
void
WasmFileServices::cancel()
{
  m_state->cancel();
}
const std::string&
WasmFileServices::error() const
{
  return m_state->error;
}
