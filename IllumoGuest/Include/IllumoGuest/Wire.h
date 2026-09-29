#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
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
  void u32(std::uint32_t value) { u32s(&value, 1u); }
  // Little-endian words, appended in one step (a copy on little-endian
  // hosts, which wasm32 and x86 both are).
  void u32s(const std::uint32_t* values, std::size_t count)
  {
    if (count > m_maximum / 4u || !reserve(count * 4u)) {
      fail("Guest packet exceeds byte quota");
      return;
    }
    const std::size_t start = m_bytes.size();
    m_bytes.resize(start + count * 4u);
    std::byte* output = m_bytes.data() + start;
    if constexpr (std::endian::native == std::endian::little) {
      if (count != 0) {
        std::memcpy(output, values, count * 4u);
      }
    } else {
      for (std::size_t word = 0; word < count; ++word) {
        for (unsigned int index = 0; index < 4u; ++index) {
          output[word * 4u + index] =
            static_cast<std::byte>(values[word] >> (index * 8u));
        }
      }
    }
  }
  void f32s(const float* values, std::size_t count)
  {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    if constexpr (std::endian::native == std::endian::little) {
      if (count > m_maximum / 4u || !reserve(count * 4u)) {
        fail("Guest packet exceeds byte quota");
        return;
      }
      const std::size_t start = m_bytes.size();
      m_bytes.resize(start + count * 4u);
      if (count != 0) {
        std::memcpy(m_bytes.data() + start, values, count * 4u);
      }
    } else {
      for (std::size_t index = 0; index < count; ++index) {
        u32(std::bit_cast<std::uint32_t>(values[index]));
      }
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
    if (!reserve(value.size())) {
      return;
    }
    m_bytes.insert(m_bytes.end(), value.begin(), value.end());
  }
  void text(const std::string& value)
  {
    if (value.size() > UINT32_MAX) {
      fail("Guest string exceeds wire range");
      return;
    }
    u32(static_cast<std::uint32_t>(value.size()));
    bytes(std::as_bytes(std::span(value.data(), value.size())));
  }
  // Overwrites a word written earlier (a length known only afterwards).
  void patchU32(std::size_t offset, std::uint32_t value)
  {
    if (m_failed || offset > m_bytes.size() || m_bytes.size() - offset < 4u) {
      fail("Guest wire patch outside the packet");
      return;
    }
    for (unsigned int index = 0; index < 4u; ++index) {
      m_bytes[offset + index] = static_cast<std::byte>(value >> (index * 8u));
    }
  }
  // A write went past the byte quota (or a string past the wire range, or a
  // patch outside the packet). The failure is sticky: later writes are
  // ignored and the packet must not be sent. The writer never throws.
  bool failed() const { return m_failed; }
  // Why the writer failed ("" while it has not).
  const char* failure() const { return m_failed ? m_failure : ""; }
  // Marks the packet unusable; the first reason is kept.
  void fail(const char* reason)
  {
    if (!m_failed) {
      m_failure = reason;
    }
    m_failed = true;
  }
  const std::vector<std::byte>& data() const { return m_bytes; }
  std::vector<std::byte> take() { return std::move(m_bytes); }
  void clear()
  {
    m_bytes.clear();
    m_failed = false;
  }

private:
  // False, marking the writer failed, when count more bytes do not fit.
  bool reserve(std::size_t count)
  {
    if (m_failed || m_bytes.size() > m_maximum ||
        count > m_maximum - m_bytes.size()) {
      fail("Guest packet exceeds byte quota");
      return false;
    }
    return true;
  }
  std::vector<std::byte> m_bytes;
  std::size_t m_maximum;
  bool m_failed = false;
  const char* m_failure = "";
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
  // Little-endian words read in one step; false (and invalid) when fewer
  // than `count` remain, leaving `values` unspecified.
  bool u32s(std::uint32_t* values, std::size_t count)
  {
    if (!m_valid || count > remaining() / 4u) {
      m_valid = false;
      return false;
    }
    const std::span<const std::byte> input = bytes(count * 4u);
    if constexpr (std::endian::native == std::endian::little) {
      if (count != 0) {
        std::memcpy(values, input.data(), count * 4u);
      }
    } else {
      for (std::size_t word = 0; word < count; ++word) {
        std::uint32_t result = 0;
        for (unsigned int index = 0; index < 4u; ++index) {
          result |= std::to_integer<std::uint32_t>(input[word * 4u + index])
                    << (index * 8u);
        }
        values[word] = result;
      }
    }
    return true;
  }
  bool f32s(float* values, std::size_t count)
  {
    if (!m_valid || count > remaining() / 4u) {
      m_valid = false;
      return false;
    }
    if constexpr (std::endian::native == std::endian::little) {
      const std::span<const std::byte> input = bytes(count * 4u);
      if (count != 0) {
        std::memcpy(values, input.data(), count * 4u);
      }
    } else {
      for (std::size_t index = 0; index < count; ++index) {
        values[index] = f32();
      }
    }
    return true;
  }
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
