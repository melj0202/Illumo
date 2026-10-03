#include <Illumo/Foundation/Sha256.h>

#include <algorithm>
#include <cstring>

static constexpr std::array<std::uint32_t, 64> kRoundConstants = {
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
  0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
  0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
  0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
  0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
  0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
  0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
  0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
  0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static std::uint32_t
rotateRight(std::uint32_t value, unsigned int count)
{
  return (value >> count) | (value << (32u - count));
}

Sha256::Sha256()
  : m_state{ 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u }
{
}

void
Sha256::compress(const std::uint8_t* block)
{
  std::array<std::uint32_t, 64> schedule{};
  for (std::size_t index = 0u; index < 16u; ++index) {
    const std::uint8_t* word = block + index * 4u;
    schedule[index] = (static_cast<std::uint32_t>(word[0]) << 24u) |
                      (static_cast<std::uint32_t>(word[1]) << 16u) |
                      (static_cast<std::uint32_t>(word[2]) << 8u) |
                      static_cast<std::uint32_t>(word[3]);
  }
  for (std::size_t index = 16u; index < 64u; ++index) {
    const std::uint32_t early = schedule[index - 15u];
    const std::uint32_t late = schedule[index - 2u];
    const std::uint32_t sigma0 =
      rotateRight(early, 7u) ^ rotateRight(early, 18u) ^ (early >> 3u);
    const std::uint32_t sigma1 =
      rotateRight(late, 17u) ^ rotateRight(late, 19u) ^ (late >> 10u);
    schedule[index] =
      schedule[index - 16u] + sigma0 + schedule[index - 7u] + sigma1;
  }
  std::uint32_t a = m_state[0];
  std::uint32_t b = m_state[1];
  std::uint32_t c = m_state[2];
  std::uint32_t d = m_state[3];
  std::uint32_t e = m_state[4];
  std::uint32_t f = m_state[5];
  std::uint32_t g = m_state[6];
  std::uint32_t h = m_state[7];
  for (std::size_t index = 0u; index < 64u; ++index) {
    const std::uint32_t sum1 =
      rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
    const std::uint32_t choice = (e & f) ^ (~e & g);
    const std::uint32_t first =
      h + sum1 + choice + kRoundConstants[index] + schedule[index];
    const std::uint32_t sum0 =
      rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t second = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + first;
    d = c;
    c = b;
    b = a;
    a = first + second;
  }
  m_state[0] += a;
  m_state[1] += b;
  m_state[2] += c;
  m_state[3] += d;
  m_state[4] += e;
  m_state[5] += f;
  m_state[6] += g;
  m_state[7] += h;
}

void
Sha256::update(std::span<const std::byte> bytes)
{
  const std::uint8_t* data =
    reinterpret_cast<const std::uint8_t*>(bytes.data());
  std::size_t remaining = bytes.size();
  m_length += static_cast<std::uint64_t>(remaining);
  if (m_buffered != 0u) {
    const std::size_t taken = std::min(remaining, 64u - m_buffered);
    std::memcpy(m_buffer.data() + m_buffered, data, taken);
    m_buffered += taken;
    data += taken;
    remaining -= taken;
    if (m_buffered < 64u) {
      return;
    }
    compress(m_buffer.data());
    m_buffered = 0u;
  }
  while (remaining >= 64u) {
    compress(data);
    data += 64;
    remaining -= 64u;
  }
  if (remaining != 0u) {
    std::memcpy(m_buffer.data(), data, remaining);
    m_buffered = remaining;
  }
}

Sha256::Digest
Sha256::finish()
{
  const std::uint64_t bitLength = m_length * 8u;
  // Padding: one bit, zeros to 56 bytes mod 64, then the big-endian length.
  m_buffer[m_buffered++] = 0x80u;
  if (m_buffered > 56u) {
    std::memset(m_buffer.data() + m_buffered, 0, 64u - m_buffered);
    compress(m_buffer.data());
    m_buffered = 0u;
  }
  std::memset(m_buffer.data() + m_buffered, 0, 56u - m_buffered);
  for (unsigned int index = 0u; index < 8u; ++index) {
    m_buffer[56u + index] =
      static_cast<std::uint8_t>(bitLength >> (56u - index * 8u));
  }
  compress(m_buffer.data());
  m_buffered = 0u;
  Digest result{};
  for (std::size_t word = 0u; word < m_state.size(); ++word) {
    for (unsigned int byte = 0u; byte < 4u; ++byte) {
      result[word * 4u + byte] =
        static_cast<std::uint8_t>(m_state[word] >> (24u - byte * 8u));
    }
  }
  return result;
}

Sha256::Digest
Sha256::digest(std::span<const std::byte> bytes)
{
  Sha256 hash;
  hash.update(bytes);
  return hash.finish();
}
