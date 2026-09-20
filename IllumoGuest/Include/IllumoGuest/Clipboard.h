#pragma once
#include <IllumoGuest/Services.h>

struct GuestClipboardRequest
{
  static constexpr std::uint32_t MaximumBytes = 4u * 1024u * 1024u;
  bool set = false;
  std::string text;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(1);
    writer.u32(set ? 1 : 0);
    writer.text(text);
  }
  static bool read(std::span<const std::byte> bytes,
                   GuestClipboardRequest& output)
  {
    GuestWireReader reader(bytes);
    const std::uint32_t version = reader.u32();
    const std::uint32_t set = reader.u32();
    GuestClipboardRequest candidate;
    candidate.set = set != 0;
    candidate.text = reader.text(MaximumBytes);
    if (!reader.finished() || version != 1 || set > 1 ||
        candidate.text.size() > MaximumBytes || !guestUtf8(candidate.text)) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

// Last get or set wins while a request is outstanding. Completions carry the
// host clipboard after the accepted mutation, never a native pointer.
class GuestClipboard
{
public:
  explicit GuestClipboard(GuestServiceQueue& services)
    : m_services(services)
  {
  }
  GuestClipboard(const GuestClipboard&) = delete;
  GuestClipboard& operator=(const GuestClipboard&) = delete;
  GuestClipboard(GuestClipboard&&) = delete;
  GuestClipboard& operator=(GuestClipboard&&) = delete;
  void get()
  {
    m_next = GuestClipboardRequest{};
    m_pending = true;
  }
  void set(std::string text)
  {
    GuestClipboardRequest request{ true, std::move(text) };
    GuestWireWriter bytes;
    request.write(bytes);
    GuestClipboardRequest checked;
    if (!GuestClipboardRequest::read(bytes.data(), checked)) {
      throw std::invalid_argument("Invalid clipboard text");
    }
    m_next = std::move(request);
    m_pending = true;
  }
  void pump();
  bool idle() const { return !m_pending && m_request == 0; }
  const std::string& text() const { return m_text; }
  const std::string& error() const { return m_error; }

private:
  GuestServiceQueue& m_services;
  GuestClipboardRequest m_next;
  std::string m_text;
  std::uint64_t m_request = 0;
  bool m_pending = false;
  std::string m_error;
};
