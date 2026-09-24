#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Decoded sound: interleaved 32-bit float samples at the source's own rate.
// Clips are bounded so one always fits a single guest service record.
struct AudioClip
{
  static constexpr std::uint32_t kMaximumChannels = 2;
  static constexpr std::uint32_t kMinimumSampleRate = 8000;
  static constexpr std::uint32_t kMaximumSampleRate = 192000;
  // 12 MiB of samples: about 34 s of 44.1 kHz stereo.
  static constexpr std::size_t kMaximumSamples = 3u * 1024u * 1024u;

  std::uint32_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::vector<float> samples;

  std::size_t frames() const
  {
    return channels == 0 ? 0 : samples.size() / channels;
  }
  // One or two channels, a supported rate, whole frames within the bound and
  // only finite samples.
  bool valid() const;
};

// Encoded sound files: WAV (integer PCM or float), FLAC and MP3. Decoding is
// pure computation with no device or file access, so it also runs inside
// WASM guests, which decode their own package files and send only samples.
class AudioDecoder
{
public:
  // Decodes to float samples at the source rate. Sources with more than two
  // channels are mixed down to stereo. False, with a reason, for malformed,
  // unsupported or oversized input; `clip` is then unchanged.
  static bool decode(std::span<const std::byte> encoded,
                     AudioClip& clip,
                     std::string& error);
};
