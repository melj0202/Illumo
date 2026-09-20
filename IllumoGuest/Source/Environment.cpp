#include <IllumoGuest/Environment.h>

GuestEnvironment::GuestEnvironment(GuestFiles& files, std::string path)
  : m_files(files)
  , m_path(std::move(path))
{
  if (m_path.empty() || m_path.size() > 1024 ||
      m_path.find('\0') != std::string::npos) {
    throw std::invalid_argument("Invalid settings path");
  }
}

GuestEnvironment::~GuestEnvironment()
{
  m_files.cancel(m_request);
}

void
GuestEnvironment::load()
{
  if (!idle()) {
    throw std::logic_error("Settings reload while persistence is pending");
  }
  m_loaded = false;
  m_writable = false;
  m_error.clear();
  m_reading = true;
  m_request = m_files.read(GuestFileArea::Storage, m_path, 1024u * 1024u);
}

void
GuestEnvironment::save()
{
  if (!m_writable) {
    m_error = "Settings cannot be saved before a successful load";
    return;
  }
  // Coalesce requests, but capture the latest values only when the preceding
  // atomic write completes. Completion of an older write cannot lose edits.
  m_savePending = true;
}

void
GuestEnvironment::pump()
{
  if (m_reading && m_request == 0) {
    m_request = m_files.read(GuestFileArea::Storage, m_path, 1024u * 1024u);
    return;
  }
  if (m_request != 0) {
    GuestFileResult result;
    if (!m_files.take(m_request, result)) {
      return;
    }
    m_request = 0;
    if (m_reading) {
      m_reading = false;
      m_loaded = true;
      m_writable = result.outcome == GuestFileOutcome::NotFound;
      if (result.outcome == GuestFileOutcome::Success) {
        const std::string_view text(
          reinterpret_cast<const char*>(result.bytes.data()),
          result.bytes.size());
        m_writable = loadText(text);
      }
      if (!m_writable) {
        m_error = "Settings are invalid or unreadable; original file preserved";
      } else {
        m_error.clear();
      }
    } else if (result.outcome != GuestFileOutcome::Success) {
      m_error = "Settings save failed";
    } else {
      m_error.clear();
    }
  }
  if (m_savePending) {
    const std::string text = saveText();
    if (text.size() > 1024u * 1024u) {
      m_savePending = false;
      m_error = "Settings exceed the one MiB limit";
      return;
    }
    const std::span<const std::byte> bytes =
      std::as_bytes(std::span(text.data(), text.size()));
    m_request = m_files.write(m_path, { bytes.begin(), bytes.end() });
    m_savePending = m_request == 0;
  }
}
