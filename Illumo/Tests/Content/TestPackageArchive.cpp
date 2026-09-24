#include <Illumo/Content/PackageArchive.h>
#include <Illumo/Content/PackageManifest.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static std::vector<uint8_t>
bytesOf(const std::string& text)
{
  return std::vector<uint8_t>(text.begin(), text.end());
}

static std::string
textOf(const std::vector<uint8_t>& bytes)
{
  return std::string(bytes.begin(), bytes.end());
}

static std::unique_ptr<PackageArchive>
openBytes(std::vector<uint8_t> bytes,
          std::string& error,
          const PackageArchiveLimits& limits = {})
{
  return PackageArchive::open(
    std::make_shared<MemoryPackageSource>(std::move(bytes)), error, limits);
}

static std::string
repeated(const std::string& line, int count)
{
  std::string text;
  for (int index = 0; index < count; ++index) {
    text += line + std::to_string(index) + "\n";
  }
  return text;
}

// A hand-assembled ZIP, so malformed shapes the writer refuses to produce
// can reach the reader.
struct RawEntry
{
  std::string name;
  std::vector<uint8_t> data;
  uint16_t method = 0;
  uint16_t flags = 0;
  uint16_t madeBy = 20;
  uint32_t external = 0;
  // Declared inflated size and CRC; defaults describe data itself.
  int64_t size = -1;
  int64_t crc = -1;
  // Name written in the local header when different.
  std::string localName;
  // Reuse the previous entry's local header offset.
  bool shareOffset = false;
};

struct RawOptions
{
  uint16_t disk = 0;
  bool zip64Sizes = false;
};

static void
raw16(std::vector<uint8_t>& out, uint32_t value)
{
  out.push_back(static_cast<uint8_t>(value & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

static void
raw32(std::vector<uint8_t>& out, uint32_t value)
{
  raw16(out, value & 0xFFFF);
  raw16(out, value >> 16);
}

static std::vector<uint8_t>
rawZip(const std::vector<RawEntry>& entries, const RawOptions& options = {})
{
  std::vector<uint8_t> out;
  std::vector<uint8_t> directory;
  uint32_t previousOffset = 0;
  for (const RawEntry& entry : entries) {
    const uint32_t crc = entry.crc >= 0
                           ? static_cast<uint32_t>(entry.crc)
                           : packageCrc32(entry.data.data(), entry.data.size());
    const uint32_t size = entry.size >= 0
                            ? static_cast<uint32_t>(entry.size)
                            : static_cast<uint32_t>(entry.data.size());
    const uint32_t packed = options.zip64Sizes
                              ? 0xFFFFFFFFu
                              : static_cast<uint32_t>(entry.data.size());
    uint32_t offset = static_cast<uint32_t>(out.size());
    if (entry.shareOffset) {
      offset = previousOffset;
    } else {
      const std::string& localName =
        entry.localName.empty() ? entry.name : entry.localName;
      raw32(out, 0x04034b50u);
      raw16(out, 20);
      raw16(out, entry.flags);
      raw16(out, entry.method);
      raw16(out, 0);
      raw16(out, 0x21);
      raw32(out, crc);
      raw32(out, packed);
      raw32(out, size);
      raw16(out, static_cast<uint32_t>(localName.size()));
      raw16(out, 0);
      out.insert(out.end(), localName.begin(), localName.end());
      out.insert(out.end(), entry.data.begin(), entry.data.end());
    }
    previousOffset = offset;
    raw32(directory, 0x02014b50u);
    raw16(directory, entry.madeBy);
    raw16(directory, 20);
    raw16(directory, entry.flags);
    raw16(directory, entry.method);
    raw16(directory, 0);
    raw16(directory, 0x21);
    raw32(directory, crc);
    raw32(directory, packed);
    raw32(directory, size);
    raw16(directory, static_cast<uint32_t>(entry.name.size()));
    raw16(directory, 0);
    raw16(directory, 0);
    raw16(directory, options.disk);
    raw16(directory, 0);
    raw32(directory, entry.external);
    raw32(directory, offset);
    directory.insert(directory.end(), entry.name.begin(), entry.name.end());
  }
  const uint32_t directoryOffset = static_cast<uint32_t>(out.size());
  out.insert(out.end(), directory.begin(), directory.end());
  raw32(out, 0x06054b50u);
  raw16(out, options.disk);
  raw16(out, options.disk);
  raw16(out, static_cast<uint32_t>(entries.size()));
  raw16(out, static_cast<uint32_t>(entries.size()));
  raw32(out, static_cast<uint32_t>(directory.size()));
  raw32(out, directoryOffset);
  raw16(out, 0);
  return out;
}

static RawEntry
stored(const std::string& name, const std::string& text = "data")
{
  RawEntry entry;
  entry.name = name;
  entry.data = bytesOf(text);
  return entry;
}

static int
testCrc32KnownValue()
{
  TestCounters counters;
  const char* check = "123456789";
  testTrue(counters,
           packageCrc32(check, 9) == 0xCBF43926u,
           "CRC-32 of the standard check string");
  testTrue(counters,
           packageCrc32(check + 4, 5, packageCrc32(check, 4)) == 0xCBF43926u,
           "a running CRC continues across buffers");
  return counters.failures;
}

static int
testArchiveRoundTrip()
{
  TestCounters counters;
  std::string error;
  PackageArchiveWriter writer;
  const std::string text = repeated("the quick brown fox ", 400);
  std::vector<uint8_t> binary(3000);
  uint32_t seed = 12345u;
  for (uint8_t& value : binary) {
    seed = seed * 1103515245u + 12345u;
    value = static_cast<uint8_t>(seed >> 24);
  }
  testTrue(counters,
           writer.add("scenes/main.ilsc", bytesOf(text), error),
           "adds a text file");
  testTrue(counters,
           writer.add("textures/grass.png", bytesOf(text), error),
           "adds a png-named file");
  testTrue(counters, writer.add("noise.bin", binary, error), "adds noise");
  testTrue(counters, writer.add("empty.txt", {}, error), "adds an empty file");
  testTrue(counters,
           writer.add("textures/deep/a/b.txt", bytesOf("b"), error),
           "adds a nested file");
  testTrue(counters,
           !writer.add("scenes/main.ilsc", bytesOf("x"), error),
           "a duplicate name is refused");
  testTrue(counters,
           !writer.add("textures/grass.png/x", bytesOf("x"), error) &&
             !writer.add("textures", bytesOf("x"), error),
           "file/directory collisions are refused");
  testTrue(counters,
           !writer.add("../escape", bytesOf("x"), error) &&
             !writer.add("/abs", bytesOf("x"), error) &&
             !writer.add("a\\b", bytesOf("x"), error),
           "unsafe names are refused");

  std::vector<uint8_t> first;
  std::vector<uint8_t> second;
  testTrue(counters, writer.finish(first, error), "the writer finishes");
  writer.finish(second, error);
  testTrue(counters, first == second, "output is deterministic");

  std::unique_ptr<PackageArchive> archive = openBytes(first, error);
  testTrue(
    counters, archive != nullptr, "the reader opens the writer's output");
  if (archive == nullptr) {
    return counters.failures + 1;
  }
  testEqSize(counters, archive->entries().size(), 5, "five files");
  const PackageArchiveEntry* scene = archive->find("scenes/main.ilsc");
  const PackageArchiveEntry* png = archive->find("textures/grass.png");
  const PackageArchiveEntry* noise = archive->find("noise.bin");
  testTrue(counters,
           scene != nullptr && scene->method == PackageArchiveMethod::Deflate &&
             scene->compressedSize < scene->size / 4,
           "text is deflated");
  testTrue(counters,
           png != nullptr && png->method == PackageArchiveMethod::Stored,
           "an already-compressed format is stored");
  testTrue(counters,
           noise != nullptr && noise->method == PackageArchiveMethod::Stored,
           "incompressible data is stored");
  std::vector<uint8_t> read;
  testTrue(counters,
           scene != nullptr && archive->read(*scene, read, error) &&
             textOf(read) == text,
           "a deflated entry reads back exactly");
  testTrue(counters,
           noise != nullptr && archive->read(*noise, read, error) &&
             read == binary,
           "a stored entry reads back exactly");
  const PackageArchiveEntry* empty = archive->find("empty.txt");
  testTrue(counters,
           empty != nullptr && archive->read(*empty, read, error) &&
             read.empty(),
           "an empty entry reads back empty");

  testTrue(counters,
           archive->isDirectory("") && archive->isDirectory("textures") &&
             archive->isDirectory("textures/deep/a") &&
             !archive->isDirectory("noise.bin") &&
             !archive->isDirectory("missing"),
           "directories are implied by file names");
  const std::vector<PackageArchiveChild> root = archive->list("");
  testTrue(counters,
           root.size() == 4 && root[0].name == "scenes" && root[0].directory &&
             root[1].name == "textures" && root[2].name == "empty.txt" &&
             !root[2].directory,
           "the root lists directories then files, sorted");
  const std::vector<PackageArchiveChild> textures = archive->list("textures");
  testTrue(counters,
           textures.size() == 2 && textures[0].name == "deep" &&
             textures[1].name == "grass.png",
           "a directory lists only its immediate children");
  testTrue(counters, archive->list("missing").empty(), "unknown lists empty");

  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "illumo-archive-roundtrip.ilpk";
  std::error_code code;
  std::filesystem::remove(path, code);
  testTrue(counters, writer.write(path, error), "the writer writes a file");
  std::unique_ptr<PackageArchive> fromFile =
    PackageArchive::openFile(path, error);
  testTrue(counters,
           fromFile != nullptr && fromFile->find("noise.bin") != nullptr &&
             fromFile->read(*fromFile->find("noise.bin"), read, error) &&
             read == binary,
           "a written file opens and reads");
  fromFile.reset();
  std::filesystem::remove(path, code);
  return counters.failures;
}

static int
testArchiveReadsCMakeZip()
{
  TestCounters counters;
  std::string error;
  std::unique_ptr<PackageArchive> archive =
    PackageArchive::openFile(ILLUMO_TEST_ARCHIVE_FIXTURE, error);
  testTrue(counters, archive != nullptr, "a zip made by cmake -E tar opens");
  if (archive == nullptr) {
    testEqStr(counters, error, "", "open error");
    return counters.failures;
  }
  std::vector<uint8_t> bytes;
  const PackageArchiveEntry* manifest = archive->find("illumo.json");
  testTrue(counters,
           manifest != nullptr && archive->read(*manifest, bytes, error),
           "its manifest reads with a valid CRC");
  PackageManifest decoded;
  testTrue(
    counters,
    decodePackageManifest(textOf(bytes), PackageCeilings{}, decoded, error) &&
      decoded.id == "archive-fixture",
    "the manifest decodes");
  const PackageArchiveEntry* notes = archive->find("docs/notes.txt");
  testTrue(counters,
           notes != nullptr && notes->method == PackageArchiveMethod::Deflate,
           "libarchive deflated the large text file");
  testTrue(counters,
           notes != nullptr && archive->read(*notes, bytes, error) &&
             textOf(bytes).find("Line 200 of the archive fixture") !=
               std::string::npos,
           "the deflated entry inflates completely");
  testTrue(counters,
           archive->isDirectory("scenes") && archive->find("scenes/main.ilsc"),
           "explicit directory entries and nested files are listed");
  return counters.failures;
}

static int
testArchiveInflateBounded()
{
  TestCounters counters;
  std::string error;
  const std::string text = repeated("aaaaaaaaaaaaaaaa", 600);
  PackageArchiveWriter writer;
  writer.add("big.txt", bytesOf(text), error);
  std::vector<uint8_t> bytes;
  writer.finish(bytes, error);
  std::unique_ptr<PackageArchive> archive = openBytes(bytes, error);
  const PackageArchiveEntry* entry =
    archive ? archive->find("big.txt") : nullptr;
  testTrue(counters, entry != nullptr, "a compressible entry packs");
  if (entry == nullptr) {
    return counters.failures;
  }

  // The same deflate stream declared smaller than it inflates.
  RawEntry lying;
  lying.name = "big.txt";
  lying.method = 8;
  lying.data.assign(
    bytes.begin() + static_cast<std::ptrdiff_t>(entry->dataOffset),
    bytes.begin() +
      static_cast<std::ptrdiff_t>(entry->dataOffset + entry->compressedSize));
  lying.size = static_cast<int64_t>(text.size() / 2);
  lying.crc = entry->crc32;
  std::unique_ptr<PackageArchive> liar = openBytes(rawZip({ lying }), error);
  std::vector<uint8_t> read;
  testTrue(counters,
           liar != nullptr && !liar->read(liar->entries().front(), read, error),
           "a stream that inflates past its declared size fails");

  PackageArchiveLimits strict;
  strict.maximumRatio = 4;
  testTrue(counters,
           openBytes(bytes, error, strict) == nullptr &&
             error.find("ratio") != std::string::npos,
           "an extreme compression ratio is refused at open");
  strict = PackageArchiveLimits{};
  strict.maximumEntryBytes = 1000;
  testTrue(counters,
           openBytes(bytes, error, strict) == nullptr,
           "an entry over the size bound is refused");
  strict = PackageArchiveLimits{};
  strict.maximumTotalBytes = 1000;
  testTrue(counters,
           openBytes(bytes, error, strict) == nullptr,
           "contents over the total bound are refused");
  strict = PackageArchiveLimits{};
  strict.maximumEntries = 0;
  testTrue(counters,
           openBytes(bytes, error, strict) == nullptr,
           "too many entries are refused");
  return counters.failures;
}

static bool
rejects(const std::vector<RawEntry>& entries,
        const std::string& fragment,
        const RawOptions& options = {})
{
  std::string error;
  std::unique_ptr<PackageArchive> archive =
    openBytes(rawZip(entries, options), error);
  return archive == nullptr && error.find(fragment) != std::string::npos;
}

static int
testArchiveRejectsMalformed()
{
  TestCounters counters;
  std::string error;
  testTrue(counters,
           openBytes(rawZip({ stored("ok.txt") }), error) != nullptr,
           "the raw builder makes a valid archive");
  for (const char* name : { "../up.txt",
                            "/abs.txt",
                            "a\\b.txt",
                            "C:/x.txt",
                            "a//b.txt",
                            "a/./b.txt",
                            "CON",
                            "dir/nul.txt",
                            ".illumo-staging",
                            "trail. " }) {
    testTrue(counters,
             rejects({ stored(name) }, "Unsafe entry name"),
             (std::string("rejects the name ") + name).c_str());
  }
  testTrue(counters,
           rejects({ stored("a.txt"), stored("a.txt") }, "Duplicate"),
           "rejects duplicate names");
  testTrue(counters,
           rejects({ stored("a"), stored("a/b.txt") }, "both a file"),
           "rejects a file that is also a directory");
  RawEntry encrypted = stored("secret.txt");
  encrypted.flags = 1;
  testTrue(
    counters, rejects({ encrypted }, "Encrypted"), "rejects encrypted entries");
  RawEntry bzip = stored("b.txt");
  bzip.method = 12;
  testTrue(counters,
           rejects({ bzip }, "Unsupported compression"),
           "rejects other methods");
  RawEntry link = stored("link");
  link.madeBy = 0x0314;
  link.external = 0120777u << 16;
  testTrue(counters, rejects({ link }, "Symlink"), "rejects symlinks");
  RawEntry renamed = stored("real.txt");
  renamed.localName = "fake.txt";
  testTrue(counters,
           rejects({ renamed }, "Local header disagrees"),
           "rejects a local name that differs from the directory");
  RawEntry shared = stored("second.txt");
  shared.shareOffset = true;
  testTrue(counters,
           rejects({ stored("first.txt"), shared }, "disagrees") ||
             rejects({ stored("first.txt"), shared }, "Overlapping"),
           "rejects entries that share bytes");
  RawOptions zip64;
  zip64.zip64Sizes = true;
  testTrue(counters,
           rejects({ stored("big.txt") }, "Zip64", zip64),
           "rejects zip64 sizes");
  RawOptions multi;
  multi.disk = 1;
  testTrue(counters,
           rejects({ stored("a.txt") }, "Multi-disk", multi),
           "rejects multi-disk archives");
  RawEntry sizes = stored("a.txt", "abcdef");
  sizes.size = 3;
  testTrue(counters,
           rejects({ sizes }, "Stored entry sizes"),
           "rejects stored entries whose sizes disagree");

  RawEntry badCrc = stored("crc.txt", "payload");
  badCrc.crc = 1234;
  std::unique_ptr<PackageArchive> archive =
    openBytes(rawZip({ badCrc }), error);
  std::vector<uint8_t> read;
  testTrue(counters,
           archive != nullptr &&
             !archive->read(archive->entries().front(), read, error) &&
             error.find("Checksum") != std::string::npos,
           "a CRC mismatch fails the read");

  std::vector<uint8_t> truncated = rawZip({ stored("a.txt") });
  truncated.resize(truncated.size() - 5);
  testTrue(counters,
           openBytes(truncated, error) == nullptr,
           "a truncated end record is refused");
  testTrue(counters,
           openBytes(bytesOf("PK"), error) == nullptr,
           "a tiny file is refused");
  return counters.failures;
}

static int
testArchiveFuzzSmoke()
{
  TestCounters counters;
  std::string error;
  PackageArchiveWriter writer;
  writer.add("scenes/a.ilsc", bytesOf(repeated("{\"node\":", 80)), error);
  writer.add("textures/b.png", bytesOf("not really a png"), error);
  writer.add("c.txt", bytesOf(repeated("text ", 50)), error);
  std::vector<uint8_t> valid;
  writer.finish(valid, error);
  uint32_t seed = 0xC0FFEEu;
  int opened = 0;
  int refused = 0;
  for (int iteration = 0; iteration < 3000; ++iteration) {
    std::vector<uint8_t> bytes = valid;
    seed = seed * 1664525u + 1013904223u;
    const int mutations = 1 + static_cast<int>((seed >> 28) & 3u);
    for (int mutation = 0; mutation < mutations; ++mutation) {
      seed = seed * 1664525u + 1013904223u;
      const std::size_t at = (seed >> 8) % bytes.size();
      seed = seed * 1664525u + 1013904223u;
      bytes[at] = static_cast<uint8_t>(seed >> 24);
    }
    if ((iteration % 17) == 0) {
      bytes.resize((seed >> 4) % bytes.size());
    }
    std::unique_ptr<PackageArchive> archive = openBytes(bytes, error);
    if (archive == nullptr) {
      ++refused;
      continue;
    }
    ++opened;
    std::vector<uint8_t> read;
    for (const PackageArchiveEntry& entry : archive->entries()) {
      archive->read(entry, read, error);
    }
  }
  testTrue(counters,
           opened > 0 && refused > 0,
           "mutated archives are either read or refused, never crash");
  return counters.failures;
}

void
registerPackageArchiveTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.ArchiveCrc32",
               []() { return testCrc32KnownValue(); });
  registry.add("Illumo.Content.ArchiveRoundTrip",
               []() { return testArchiveRoundTrip(); });
  registry.add("Illumo.Content.ArchiveReadsCMakeZip",
               []() { return testArchiveReadsCMakeZip(); });
  registry.add("Illumo.Content.ArchiveInflateBounded",
               []() { return testArchiveInflateBounded(); });
  registry.add("Illumo.Content.ArchiveRejectsMalformed",
               []() { return testArchiveRejectsMalformed(); });
  registry.add("Illumo.Content.ArchiveFuzzSmoke",
               []() { return testArchiveFuzzSmoke(); });
}
