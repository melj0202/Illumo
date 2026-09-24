#include <Illumo/Content/PackageArchive.h>

#include <Illumo/Content/VirtualPath.h>
#include <Illumo/Platform/AtomicFile.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <set>
#include <system_error>

// The engine links both stb implementations (AssetManager.cpp and
// FrameCaptureImage.cpp); only these two zlib entry points are used here.
extern "C" int
stbi_zlib_decode_noheader_buffer(char* obuffer,
                                 int olen,
                                 const char* ibuffer,
                                 int ilen);
extern "C" unsigned char*
stbi_zlib_compress(unsigned char* data,
                   int data_len,
                   int* out_len,
                   int quality);

static constexpr uint32_t kLocalSignature = 0x04034b50u;
static constexpr uint32_t kCentralSignature = 0x02014b50u;
static constexpr uint32_t kEndSignature = 0x06054b50u;
static constexpr uint32_t kZip64LocatorSignature = 0x07064b50u;
static constexpr std::size_t kLocalHeaderBytes = 30;
static constexpr std::size_t kCentralHeaderBytes = 46;
static constexpr std::size_t kEndBytes = 22;
static constexpr uint16_t kFlagEncrypted = 0x0001u;
static constexpr uint16_t kFlagStrongEncryption = 0x0040u;
static constexpr uint16_t kFlagUtf8 = 0x0800u;
// 1980-01-01 00:00, the earliest DOS timestamp.
static constexpr uint16_t kDosDate = 0x0021u;

static const std::array<uint32_t, 256>&
crcTable()
{
  static const std::array<uint32_t, 256> table = []() {
    std::array<uint32_t, 256> values{};
    for (uint32_t index = 0; index < 256; ++index) {
      uint32_t value = index;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
      }
      values[index] = value;
    }
    return values;
  }();
  return table;
}

uint32_t
packageCrc32(const void* data, std::size_t bytes, uint32_t running)
{
  const std::array<uint32_t, 256>& table = crcTable();
  const uint8_t* cursor = static_cast<const uint8_t*>(data);
  uint32_t crc = ~running;
  for (std::size_t index = 0; index < bytes; ++index) {
    crc = table[(crc ^ cursor[index]) & 0xFFu] ^ (crc >> 8);
  }
  return ~crc;
}

MemoryPackageSource::MemoryPackageSource(std::vector<uint8_t> bytes)
  : m_bytes(std::move(bytes))
{
}

bool
MemoryPackageSource::readAt(uint64_t offset,
                            void* output,
                            std::size_t bytes) const
{
  if (offset > m_bytes.size() || bytes > m_bytes.size() - offset) {
    return false;
  }
  if (bytes > 0) {
    std::memcpy(output, m_bytes.data() + offset, bytes);
  }
  return true;
}

std::shared_ptr<FilePackageSource>
FilePackageSource::open(const std::filesystem::path& path, std::string& error)
{
  std::error_code code;
  const uint64_t size = std::filesystem::file_size(path, code);
  if (code) {
    error = "Cannot read package size: " + code.message();
    return nullptr;
  }
  std::shared_ptr<FilePackageSource> source =
    std::make_shared<FilePackageSource>();
  source->m_stream.open(path, std::ios::binary);
  if (!source->m_stream) {
    error = "Cannot open package";
    return nullptr;
  }
  source->m_size = size;
  return source;
}

bool
FilePackageSource::readAt(uint64_t offset,
                          void* output,
                          std::size_t bytes) const
{
  if (offset > m_size || bytes > m_size - offset) {
    return false;
  }
  if (bytes == 0) {
    return true;
  }
  const std::lock_guard<std::mutex> lock(m_mutex);
  m_stream.clear();
  m_stream.seekg(static_cast<std::streamoff>(offset));
  m_stream.read(static_cast<char*>(output),
                static_cast<std::streamsize>(bytes));
  return static_cast<std::size_t>(m_stream.gcount()) == bytes;
}

static uint16_t
read16(const uint8_t* bytes)
{
  return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

static uint32_t
read32(const uint8_t* bytes)
{
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

static void
put16(std::vector<uint8_t>& output, uint32_t value)
{
  output.push_back(static_cast<uint8_t>(value & 0xFFu));
  output.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

static void
put32(std::vector<uint8_t>& output, uint32_t value)
{
  put16(output, value & 0xFFFFu);
  put16(output, value >> 16);
}

// Every directory above a relative name ("a/b/c" -> "a", "a/b").
static void
addParents(const std::string& name, std::set<std::string>& directories)
{
  std::size_t slash = name.find('/');
  while (slash != std::string::npos) {
    directories.insert(name.substr(0, slash));
    slash = name.find('/', slash + 1);
  }
}

struct CentralRecord
{
  PackageArchiveEntry entry;
  uint64_t localOffset = 0;
  bool directory = false;
};

std::unique_ptr<PackageArchive>
PackageArchive::openFile(const std::filesystem::path& path,
                         std::string& error,
                         const PackageArchiveLimits& limits)
{
  std::shared_ptr<FilePackageSource> source =
    FilePackageSource::open(path, error);
  if (!source) {
    return nullptr;
  }
  return open(source, error, limits);
}

std::unique_ptr<PackageArchive>
PackageArchive::open(std::shared_ptr<const IPackageByteSource> source,
                     std::string& error,
                     const PackageArchiveLimits& requested)
{
  if (!source) {
    error = "No package bytes";
    return nullptr;
  }
  PackageArchiveLimits limits = requested;
  limits.maximumEntryBytes =
    std::min<uint64_t>(limits.maximumEntryBytes, 1024ull * 1024ull * 1024ull);
  const uint64_t size = source->size();
  if (size < kEndBytes) {
    error = "Not a package archive (too small)";
    return nullptr;
  }

  // The end record sits in the last 22 + 65535 bytes; its comment must run
  // exactly to the end of the file.
  const uint64_t window = std::min<uint64_t>(size, kEndBytes + 65535u);
  std::vector<uint8_t> tail(static_cast<std::size_t>(window));
  if (!source->readAt(size - window, tail.data(), tail.size())) {
    error = "Cannot read the package";
    return nullptr;
  }
  std::size_t endAt = std::string::npos;
  for (std::size_t index = tail.size() - kEndBytes + 1; index-- > 0;) {
    if (read32(&tail[index]) == kEndSignature &&
        index + kEndBytes + read16(&tail[index + 20]) == tail.size()) {
      endAt = index;
      break;
    }
  }
  if (endAt == std::string::npos) {
    error = "Not a package archive (no end of central directory)";
    return nullptr;
  }
  const uint8_t* end = &tail[endAt];
  const uint64_t endOffset = size - window + endAt;
  const uint16_t disk = read16(end + 4);
  const uint16_t directoryDisk = read16(end + 6);
  const uint16_t diskEntries = read16(end + 8);
  const uint16_t totalEntries = read16(end + 10);
  const uint32_t directoryBytes = read32(end + 12);
  const uint32_t directoryOffset = read32(end + 16);
  if (disk != 0 || directoryDisk != 0 || diskEntries != totalEntries) {
    error = "Multi-disk archives are not supported";
    return nullptr;
  }
  if (totalEntries == 0xFFFFu || directoryBytes == 0xFFFFFFFFu ||
      directoryOffset == 0xFFFFFFFFu ||
      (endAt >= 20 && read32(&tail[endAt - 20]) == kZip64LocatorSignature)) {
    error = "Zip64 archives are not supported";
    return nullptr;
  }
  if (totalEntries > limits.maximumEntries) {
    error = "Package has too many entries";
    return nullptr;
  }
  if (directoryBytes > limits.maximumDirectoryBytes) {
    error = "Package directory is too large";
    return nullptr;
  }
  if (static_cast<uint64_t>(directoryOffset) + directoryBytes > endOffset) {
    error = "Package directory lies outside the file";
    return nullptr;
  }
  std::vector<uint8_t> directory(directoryBytes);
  if (!source->readAt(directoryOffset, directory.data(), directory.size())) {
    error = "Cannot read the package directory";
    return nullptr;
  }

  std::vector<CentralRecord> records;
  records.reserve(totalEntries);
  std::set<std::string> directories;
  std::size_t cursor = 0;
  uint64_t total = 0;
  for (uint16_t index = 0; index < totalEntries; ++index) {
    if (cursor + kCentralHeaderBytes > directory.size() ||
        read32(&directory[cursor]) != kCentralSignature) {
      error = "Package directory is corrupt";
      return nullptr;
    }
    const uint8_t* header = &directory[cursor];
    const uint16_t madeBy = read16(header + 4);
    const uint16_t flags = read16(header + 8);
    const uint16_t method = read16(header + 10);
    const uint32_t crc = read32(header + 16);
    const uint32_t compressed = read32(header + 20);
    const uint32_t inflated = read32(header + 24);
    const uint16_t nameBytes = read16(header + 28);
    const uint16_t extraBytes = read16(header + 30);
    const uint16_t commentBytes = read16(header + 32);
    const uint16_t startDisk = read16(header + 34);
    const uint32_t external = read32(header + 38);
    const uint32_t localOffset = read32(header + 42);
    const std::size_t recordBytes =
      kCentralHeaderBytes + nameBytes + extraBytes + commentBytes;
    if (cursor + recordBytes > directory.size()) {
      error = "Package directory is corrupt";
      return nullptr;
    }
    std::string name(
      reinterpret_cast<const char*>(header) + kCentralHeaderBytes, nameBytes);
    cursor += recordBytes;
    if ((flags & (kFlagEncrypted | kFlagStrongEncryption)) != 0) {
      error = "Encrypted entry: " + name;
      return nullptr;
    }
    if (startDisk != 0) {
      error = "Multi-disk archives are not supported";
      return nullptr;
    }
    if (compressed == 0xFFFFFFFFu || inflated == 0xFFFFFFFFu ||
        localOffset == 0xFFFFFFFFu) {
      error = "Zip64 archives are not supported";
      return nullptr;
    }
    // Unix hosts keep the file type in the high attribute bits; only regular
    // files and directories are accepted (never symlinks or devices).
    const uint32_t unixType = (external >> 16) & 0170000u;
    if ((madeBy >> 8) == 3 && unixType != 0 && unixType != 0100000u &&
        unixType != 0040000u) {
      error = "Symlink or special file entry: " + name;
      return nullptr;
    }
    if ((madeBy >> 8) == 0 && (external & 0x400u) != 0) {
      error = "Reparse point entry: " + name;
      return nullptr;
    }
    CentralRecord record;
    record.directory = !name.empty() && name.back() == '/';
    if (record.directory) {
      name.pop_back();
    }
    if (!VirtualPath::validRelative(name)) {
      error = "Unsafe entry name: " + name;
      return nullptr;
    }
    if (record.directory) {
      if (inflated != 0) {
        error = "Directory entry has data: " + name;
        return nullptr;
      }
      directories.insert(name);
      addParents(name, directories);
      continue;
    }
    if (method != 0 && method != 8) {
      error = "Unsupported compression method in " + name;
      return nullptr;
    }
    if (method == 0 && compressed != inflated) {
      error = "Stored entry sizes disagree: " + name;
      return nullptr;
    }
    if (inflated > limits.maximumEntryBytes) {
      error = "Entry is too large: " + name;
      return nullptr;
    }
    if (method == 8 && inflated > 0 &&
        static_cast<uint64_t>(inflated) >
          std::max<uint64_t>(compressed, 1) * limits.maximumRatio) {
      error = "Entry compression ratio is too high: " + name;
      return nullptr;
    }
    total += inflated;
    if (total > limits.maximumTotalBytes) {
      error = "Package contents are too large";
      return nullptr;
    }
    record.entry.name = name;
    record.entry.method = method == 8 ? PackageArchiveMethod::Deflate
                                      : PackageArchiveMethod::Stored;
    record.entry.crc32 = crc;
    record.entry.compressedSize = compressed;
    record.entry.size = inflated;
    record.localOffset = localOffset;
    addParents(name, directories);
    records.push_back(std::move(record));
  }

  // Check each local header against the directory and place the data.
  for (CentralRecord& record : records) {
    std::array<uint8_t, kLocalHeaderBytes> local{};
    if (!source->readAt(record.localOffset, local.data(), local.size()) ||
        read32(local.data()) != kLocalSignature) {
      error = "Missing local header: " + record.entry.name;
      return nullptr;
    }
    const uint16_t localFlags = read16(local.data() + 6);
    const uint16_t localMethod = read16(local.data() + 8);
    const uint16_t nameBytes = read16(local.data() + 26);
    const uint16_t extraBytes = read16(local.data() + 28);
    const uint16_t expectedMethod =
      record.entry.method == PackageArchiveMethod::Deflate ? 8 : 0;
    if ((localFlags & (kFlagEncrypted | kFlagStrongEncryption)) != 0 ||
        localMethod != expectedMethod ||
        nameBytes != record.entry.name.size()) {
      error = "Local header disagrees with the directory: " + record.entry.name;
      return nullptr;
    }
    std::string localName(nameBytes, '\0');
    if (!source->readAt(record.localOffset + kLocalHeaderBytes,
                        localName.data(),
                        localName.size()) ||
        localName != record.entry.name) {
      error = "Local header disagrees with the directory: " + record.entry.name;
      return nullptr;
    }
    record.entry.dataOffset =
      record.localOffset + kLocalHeaderBytes + nameBytes + extraBytes;
    if (record.entry.dataOffset + record.entry.compressedSize >
        directoryOffset) {
      error = "Entry data overlaps the directory: " + record.entry.name;
      return nullptr;
    }
  }

  // Entries may not share or overlap bytes (a classic zip bomb shape).
  std::vector<const CentralRecord*> byOffset;
  byOffset.reserve(records.size());
  for (const CentralRecord& record : records) {
    byOffset.push_back(&record);
  }
  std::sort(byOffset.begin(),
            byOffset.end(),
            [](const CentralRecord* a, const CentralRecord* b) {
              return a->localOffset < b->localOffset;
            });
  for (std::size_t index = 1; index < byOffset.size(); ++index) {
    const CentralRecord& previous = *byOffset[index - 1];
    if (previous.entry.dataOffset + previous.entry.compressedSize >
        byOffset[index]->localOffset) {
      error = "Overlapping entries: " + byOffset[index]->entry.name;
      return nullptr;
    }
  }

  std::unique_ptr<PackageArchive> archive(new PackageArchive());
  archive->m_source = std::move(source);
  archive->m_totalBytes = total;
  archive->m_entries.reserve(records.size());
  for (CentralRecord& record : records) {
    archive->m_entries.push_back(std::move(record.entry));
  }
  std::sort(archive->m_entries.begin(),
            archive->m_entries.end(),
            [](const PackageArchiveEntry& a, const PackageArchiveEntry& b) {
              return a.name < b.name;
            });
  for (std::size_t index = 1; index < archive->m_entries.size(); ++index) {
    if (archive->m_entries[index - 1].name == archive->m_entries[index].name) {
      error = "Duplicate entry: " + archive->m_entries[index].name;
      return nullptr;
    }
  }
  archive->m_directories.assign(directories.begin(), directories.end());
  for (const std::string& name : archive->m_directories) {
    if (archive->find(name) != nullptr) {
      error = "Entry is both a file and a directory: " + name;
      return nullptr;
    }
  }
  return archive;
}

const PackageArchiveEntry*
PackageArchive::find(std::string_view name) const
{
  std::vector<PackageArchiveEntry>::const_iterator found =
    std::lower_bound(m_entries.begin(),
                     m_entries.end(),
                     name,
                     [](const PackageArchiveEntry& entry,
                        std::string_view key) { return entry.name < key; });
  return found != m_entries.end() && found->name == name ? &*found : nullptr;
}

bool
PackageArchive::isDirectory(std::string_view name) const
{
  return name.empty() ||
         std::binary_search(
           m_directories.begin(),
           m_directories.end(),
           name,
           [](std::string_view a, std::string_view b) { return a < b; });
}

static bool
directChild(std::string_view name,
            std::string_view directory,
            std::string_view* child)
{
  if (!directory.empty()) {
    if (name.size() <= directory.size() + 1 ||
        name.substr(0, directory.size()) != directory ||
        name[directory.size()] != '/') {
      return false;
    }
    name.remove_prefix(directory.size() + 1);
  }
  if (name.find('/') != std::string_view::npos) {
    return false;
  }
  *child = name;
  return true;
}

std::vector<PackageArchiveChild>
PackageArchive::list(std::string_view directory) const
{
  std::vector<PackageArchiveChild> children;
  if (!isDirectory(directory)) {
    return children;
  }
  std::string_view child;
  for (const std::string& name : m_directories) {
    if (directChild(name, directory, &child)) {
      children.push_back({ std::string(child), true });
    }
  }
  for (const PackageArchiveEntry& entry : m_entries) {
    if (directChild(entry.name, directory, &child)) {
      children.push_back({ std::string(child), false });
    }
  }
  return children;
}

bool
PackageArchive::read(const PackageArchiveEntry& entry,
                     std::vector<uint8_t>& output,
                     std::string& error) const
{
  std::vector<uint8_t> packed(static_cast<std::size_t>(entry.compressedSize));
  if (!m_source->readAt(entry.dataOffset, packed.data(), packed.size())) {
    error = "Cannot read entry: " + entry.name;
    return false;
  }
  std::vector<uint8_t> result;
  if (entry.method == PackageArchiveMethod::Stored) {
    result = std::move(packed);
  } else {
    result.resize(static_cast<std::size_t>(entry.size));
    if (entry.size > 0) {
      // The output buffer is exactly the declared size, so a stream that
      // inflates to more fails instead of growing.
      const int inflated = stbi_zlib_decode_noheader_buffer(
        reinterpret_cast<char*>(result.data()),
        static_cast<int>(result.size()),
        reinterpret_cast<const char*>(packed.data()),
        static_cast<int>(std::min<std::size_t>(packed.size(), INT_MAX)));
      if (inflated < 0 || static_cast<uint64_t>(inflated) != entry.size) {
        error = "Corrupt compressed data: " + entry.name;
        return false;
      }
    }
  }
  if (packageCrc32(result.data(), result.size()) != entry.crc32) {
    error = "Checksum mismatch: " + entry.name;
    return false;
  }
  output = std::move(result);
  return true;
}

bool
PackageArchive::readRange(const PackageArchiveEntry& entry,
                          uint64_t offset,
                          std::size_t bytes,
                          std::vector<uint8_t>& output,
                          std::string& error) const
{
  const uint64_t start = std::min(offset, entry.size);
  const std::size_t count =
    static_cast<std::size_t>(std::min<uint64_t>(bytes, entry.size - start));
  if (entry.method == PackageArchiveMethod::Stored) {
    std::vector<uint8_t> result(count);
    if (!m_source->readAt(entry.dataOffset + start, result.data(), count)) {
      error = "Cannot read entry: " + entry.name;
      return false;
    }
    output = std::move(result);
    return true;
  }
  std::vector<uint8_t> whole;
  if (!read(entry, whole, error)) {
    return false;
  }
  output.assign(whole.begin() + static_cast<std::ptrdiff_t>(start),
                whole.begin() + static_cast<std::ptrdiff_t>(start + count));
  return true;
}

bool
PackageArchiveWriter::storesUncompressed(std::string_view name)
{
  const std::size_t dot = name.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  std::string extension(name.substr(dot + 1));
  std::transform(
    extension.begin(), extension.end(), extension.begin(), [](char c) {
      return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
  return extension == "png" || extension == "jpg" || extension == "jpeg" ||
         extension == "ilpk" || extension == "zip" || extension == "ogg" ||
         extension == "mp3" || extension == "webp";
}

bool
PackageArchiveWriter::add(std::string_view name,
                          std::vector<uint8_t> bytes,
                          std::string& error)
{
  const std::string key(name);
  if (!VirtualPath::validRelative(key)) {
    error = "Invalid package name: " + key;
    return false;
  }
  if (bytes.size() > kMaximumEntryBytes) {
    error = "File is too large for a package: " + key;
    return false;
  }
  if (m_files.size() >= kMaximumEntries) {
    error = "Too many files for a package";
    return false;
  }
  for (const File& file : m_files) {
    const bool below = file.name.size() > key.size() &&
                       file.name.compare(0, key.size(), key) == 0 &&
                       file.name[key.size()] == '/';
    const bool above = key.size() > file.name.size() &&
                       key.compare(0, file.name.size(), file.name) == 0 &&
                       key[file.name.size()] == '/';
    if (file.name == key || below || above) {
      error = "Package name collides with another entry: " + key;
      return false;
    }
  }
  m_files.push_back({ key, std::move(bytes) });
  return true;
}

bool
PackageArchiveWriter::addDirectory(const std::filesystem::path& root,
                                   std::string& error)
{
  std::error_code code;
  std::filesystem::recursive_directory_iterator walk(
    root, std::filesystem::directory_options::none, code);
  if (code) {
    error = "Cannot list " + root.generic_string() + ": " + code.message();
    return false;
  }
  const std::filesystem::recursive_directory_iterator done;
  for (; walk != done; walk.increment(code)) {
    if (code) {
      error = "Cannot list " + root.generic_string() + ": " + code.message();
      return false;
    }
    const std::filesystem::directory_entry& item = *walk;
    if (item.is_symlink(code)) {
      error = "Symlinks cannot be packed: " + item.path().generic_string();
      return false;
    }
    if (item.is_directory(code)) {
      continue;
    }
    if (!item.is_regular_file(code)) {
      error = "Not a regular file: " + item.path().generic_string();
      return false;
    }
    // Package names are UTF-8 whatever the host code page.
    const std::u8string utf8 =
      std::filesystem::relative(item.path(), root, code).generic_u8string();
    const std::string relative(utf8.begin(), utf8.end());
    std::ifstream stream(item.path(), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    if (!stream.good() && !stream.eof()) {
      error = "Cannot read " + relative;
      return false;
    }
    if (!add(relative, std::move(bytes), error)) {
      return false;
    }
  }
  return true;
}

bool
PackageArchiveWriter::finish(std::vector<uint8_t>& output,
                             std::string& error) const
{
  std::vector<const File*> files;
  files.reserve(m_files.size());
  for (const File& file : m_files) {
    files.push_back(&file);
  }
  std::sort(files.begin(), files.end(), [](const File* a, const File* b) {
    return a->name < b->name;
  });

  std::vector<uint8_t> archive;
  std::vector<uint8_t> directory;
  for (const File* file : files) {
    const uint32_t crc = packageCrc32(file->bytes.data(), file->bytes.size());
    const uint8_t* data = file->bytes.data();
    std::size_t dataBytes = file->bytes.size();
    uint16_t method = 0;
    unsigned char* compressed = nullptr;
    if (dataBytes > 0 && !storesUncompressed(file->name)) {
      int zlibBytes = 0;
      compressed =
        stbi_zlib_compress(const_cast<unsigned char*>(file->bytes.data()),
                           static_cast<int>(dataBytes),
                           &zlibBytes,
                           8);
      // A zlib stream is a 2-byte header, raw deflate and a 4-byte Adler-32;
      // ZIP stores the raw deflate alone.
      if (compressed != nullptr && zlibBytes > 6 &&
          static_cast<std::size_t>(zlibBytes - 6) < dataBytes) {
        data = compressed + 2;
        dataBytes = static_cast<std::size_t>(zlibBytes - 6);
        method = 8;
      }
    }
    const uint64_t localOffset = archive.size();
    if (localOffset + kLocalHeaderBytes + file->name.size() + dataBytes >=
        0xFFFFFFFFull) {
      std::free(compressed);
      error = "Package would exceed 4 GiB";
      return false;
    }
    put32(archive, kLocalSignature);
    put16(archive, 20);
    put16(archive, kFlagUtf8);
    put16(archive, method);
    put16(archive, 0);
    put16(archive, kDosDate);
    put32(archive, crc);
    put32(archive, static_cast<uint32_t>(dataBytes));
    put32(archive, static_cast<uint32_t>(file->bytes.size()));
    put16(archive, static_cast<uint32_t>(file->name.size()));
    put16(archive, 0);
    archive.insert(archive.end(), file->name.begin(), file->name.end());
    archive.insert(archive.end(), data, data + dataBytes);
    std::free(compressed);

    put32(directory, kCentralSignature);
    put16(directory, 20);
    put16(directory, 20);
    put16(directory, kFlagUtf8);
    put16(directory, method);
    put16(directory, 0);
    put16(directory, kDosDate);
    put32(directory, crc);
    put32(directory, static_cast<uint32_t>(dataBytes));
    put32(directory, static_cast<uint32_t>(file->bytes.size()));
    put16(directory, static_cast<uint32_t>(file->name.size()));
    put16(directory, 0);
    put16(directory, 0);
    put16(directory, 0);
    put16(directory, 0);
    put32(directory, 0);
    put32(directory, static_cast<uint32_t>(localOffset));
    directory.insert(directory.end(), file->name.begin(), file->name.end());
  }
  const uint64_t directoryOffset = archive.size();
  if (directoryOffset + directory.size() + kEndBytes >= 0xFFFFFFFFull) {
    error = "Package would exceed 4 GiB";
    return false;
  }
  archive.insert(archive.end(), directory.begin(), directory.end());
  put32(archive, kEndSignature);
  put16(archive, 0);
  put16(archive, 0);
  put16(archive, static_cast<uint32_t>(files.size()));
  put16(archive, static_cast<uint32_t>(files.size()));
  put32(archive, static_cast<uint32_t>(directory.size()));
  put32(archive, static_cast<uint32_t>(directoryOffset));
  put16(archive, 0);
  output = std::move(archive);
  return true;
}

bool
PackageArchiveWriter::write(const std::filesystem::path& destination,
                            std::string& error) const
{
  std::vector<uint8_t> bytes;
  if (!finish(bytes, error)) {
    return false;
  }
  return AtomicFile::write(
    destination,
    [&bytes](std::ostream& stream, std::string* writeError) {
      stream.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
      if (!stream && writeError != nullptr) {
        *writeError = "Cannot write the package";
      }
      return static_cast<bool>(stream);
    },
    &error);
}
