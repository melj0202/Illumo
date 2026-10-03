#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// SHA-256 (FIPS 180-4) for content identity, such as the key of a compiled
// code cache. Feed bytes with update() any number of times, then call
// finish() once; digest() hashes one buffer. A plain value type: no
// allocation and no global state.
class Sha256
{
public:
  using Digest = std::array<std::uint8_t, 32>;

  Sha256();
  void update(std::span<const std::byte> bytes);
  Digest finish();
  static Digest digest(std::span<const std::byte> bytes);

private:
  void compress(const std::uint8_t* block);

  std::array<std::uint32_t, 8> m_state{};
  std::array<std::uint8_t, 64> m_buffer{};
  std::size_t m_buffered = 0u;
  std::uint64_t m_length = 0u;
};
