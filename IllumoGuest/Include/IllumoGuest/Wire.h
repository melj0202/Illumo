#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The wire format never uses C++ struct layout, native pointers or native
// size_t. This header is shared by the native decoder and wasm32 encoder.
class GuestWireWriter
{
public:
  explicit GuestWireWriter(std::size_t maximum = 64u * 1024u * 1024u)
    : m_maximum(maximum)
  {
  }
  ~GuestWireWriter() = default;
  GuestWireWriter(const GuestWireWriter&) = delete;
  GuestWireWriter& operator=(const GuestWireWriter&) = delete;
  GuestWireWriter(GuestWireWriter&&) noexcept = default;
  GuestWireWriter& operator=(GuestWireWriter&&) noexcept = default;
  void u32(std::uint32_t value)
  {
    reserve(4u);
    for (unsigned int index = 0; index < 4u; ++index) {
      m_bytes.push_back(static_cast<std::byte>(value >> (index * 8u)));
    }
  }
  void u64(std::uint64_t value)
  {
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32u));
  }
  void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
  void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
  void bytes(std::span<const std::byte> value)
  {
    reserve(value.size());
    m_bytes.insert(m_bytes.end(), value.begin(), value.end());
  }
  void text(const std::string& value)
  {
    if (value.size() > UINT32_MAX) {
      throw std::length_error("Guest string exceeds wire range");
    }
    u32(static_cast<std::uint32_t>(value.size()));
    bytes(std::as_bytes(std::span(value.data(), value.size())));
  }
  const std::vector<std::byte>& data() const { return m_bytes; }
  std::vector<std::byte> take() { return std::move(m_bytes); }
  void clear() { m_bytes.clear(); }

private:
  void reserve(std::size_t count)
  {
    if (m_bytes.size() > m_maximum || count > m_maximum - m_bytes.size()) {
      throw std::length_error("Guest packet exceeds byte quota");
    }
  }
  std::vector<std::byte> m_bytes;
  std::size_t m_maximum;
};

class GuestWireReader
{
public:
  explicit GuestWireReader(std::span<const std::byte> bytes)
    : m_bytes(bytes)
  {
  }
  std::uint32_t u32()
  {
    const std::span<const std::byte> value = bytes(4u);
    if (!m_valid) {
      return 0;
    }
    std::uint32_t result = 0;
    for (unsigned int index = 0; index < 4u; ++index) {
      result |= std::to_integer<std::uint32_t>(value[index]) << (index * 8u);
    }
    return result;
  }
  std::uint64_t u64()
  {
    const std::uint64_t low = u32();
    const std::uint64_t high = u32();
    return low | (high << 32u);
  }
  float f32() { return std::bit_cast<float>(u32()); }
  double f64() { return std::bit_cast<double>(u64()); }
  std::span<const std::byte> bytes(std::size_t count)
  {
    if (!m_valid || count > m_bytes.size() - m_cursor) {
      m_valid = false;
      return {};
    }
    const std::span<const std::byte> result = m_bytes.subspan(m_cursor, count);
    m_cursor += count;
    return result;
  }
  std::string text(std::size_t maximum = 1024u * 1024u)
  {
    const std::uint32_t count = u32();
    if (count > maximum) {
      m_valid = false;
      return {};
    }
    const std::span<const std::byte> value = bytes(count);
    if (value.empty()) {
      return {};
    }
    return { reinterpret_cast<const char*>(value.data()), value.size() };
  }
  bool valid() const { return m_valid; }
  bool finished() const { return m_valid && m_cursor == m_bytes.size(); }
  std::size_t remaining() const
  {
    return m_valid ? m_bytes.size() - m_cursor : 0u;
  }

private:
  std::span<const std::byte> m_bytes;
  std::size_t m_cursor = 0u;
  bool m_valid = true;
};

inline bool
guestUtf8(std::string_view text)
{
  std::size_t index = 0;
  while (index < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::size_t extra = 0;
    std::uint32_t code = 0;
    if (lead < 0x80u) {
      extra = 0;
      code = lead;
    } else if (lead >= 0xc2u && lead <= 0xdfu) {
      extra = 1;
      code = lead & 0x1fu;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
      extra = 2;
      code = lead & 0x0fu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
      extra = 3;
      code = lead & 0x07u;
    } else {
      return false;
    }
    if (index + extra >= text.size()) {
      return false;
    }
    for (std::size_t follow = 1; follow <= extra; ++follow) {
      const unsigned char next =
        static_cast<unsigned char>(text[index + follow]);
      if (next < 0x80u || next > 0xbfu) {
        return false;
      }
      code = (code << 6u) | (next & 0x3fu);
    }
    const unsigned char second =
      extra == 0 ? 0 : static_cast<unsigned char>(text[index + 1]);
    if ((extra == 1 && code < 0x80u) || (extra == 2 && code < 0x800u) ||
        (extra == 3 && code < 0x10000u) || code > 0x10ffffu ||
        (code >= 0xd800u && code <= 0xdfffu) ||
        (lead == 0xe0u && second < 0xa0u) ||
        (lead == 0xedu && second > 0x9fu) ||
        (lead == 0xf0u && second < 0x90u) ||
        (lead == 0xf4u && second > 0x8fu)) {
      return false;
    }
    index += extra + 1;
  }
  return true;
}
