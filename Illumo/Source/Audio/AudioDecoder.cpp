#include <Illumo/Audio/AudioClip.h>
#include <algorithm>
#include <cmath>
#include <memory>
// This unit is miniaudio's one implementation, for engine and guests alike:
// its embedded decoders' memory entry points are declared only there.
#define MINIAUDIO_IMPLEMENTATION
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX // miniaudio includes <windows.h>
#endif
#include <miniaudio.h>

// Encoded files are bounded before any decoder sees them.
static constexpr std::size_t kMaximumEncodedBytes = 64u * 1024u * 1024u;
static constexpr ma_uint64 kReadFrames = 4096u;

bool
AudioClip::valid() const
{
  if (channels == 0 || channels > kMaximumChannels ||
      sampleRate < kMinimumSampleRate || sampleRate > kMaximumSampleRate ||
      samples.empty() || samples.size() > kMaximumSamples ||
      samples.size() % channels != 0) {
    return false;
  }
  return std::all_of(samples.begin(), samples.end(), [](float sample) {
    return std::isfinite(sample);
  });
}

namespace {
// One of miniaudio's embedded decoders (dr_wav, dr_flac, dr_mp3) opened on
// memory. Only their memory entry points are used: ma_decoder would also
// link miniaudio's stdio file loaders, and guests have no file-system
// imports.
class MemorySource
{
public:
  MemorySource() = default;
  ~MemorySource()
  {
    if (m_kind == Kind::Wav) {
      ma_dr_wav_uninit(&m_wav);
    } else if (m_kind == Kind::Flac) {
      ma_dr_flac_close(m_flac);
    } else if (m_kind == Kind::Mp3) {
      ma_dr_mp3_uninit(&m_mp3);
    }
  }
  MemorySource(const MemorySource&) = delete;
  MemorySource& operator=(const MemorySource&) = delete;
  MemorySource(MemorySource&&) = delete;
  MemorySource& operator=(MemorySource&&) = delete;

  bool open(std::span<const std::byte> encoded)
  {
    const void* data = encoded.data();
    const std::size_t size = encoded.size();
    if (ma_dr_wav_init_memory(&m_wav, data, size, nullptr)) {
      m_kind = Kind::Wav;
      m_channels = m_wav.channels;
      m_sampleRate = m_wav.sampleRate;
      return true;
    }
    m_flac = ma_dr_flac_open_memory(data, size, nullptr);
    if (m_flac != nullptr) {
      m_kind = Kind::Flac;
      m_channels = m_flac->channels;
      m_sampleRate = m_flac->sampleRate;
      return true;
    }
    if (ma_dr_mp3_init_memory(&m_mp3, data, size, nullptr)) {
      m_kind = Kind::Mp3;
      m_channels = m_mp3.channels;
      m_sampleRate = m_mp3.sampleRate;
      return true;
    }
    return false;
  }
  // Interleaved frames at the source's own channel count.
  ma_uint64 read(ma_uint64 frames, float* output)
  {
    if (m_kind == Kind::Wav) {
      return ma_dr_wav_read_pcm_frames_f32(&m_wav, frames, output);
    }
    if (m_kind == Kind::Flac) {
      return ma_dr_flac_read_pcm_frames_f32(m_flac, frames, output);
    }
    if (m_kind == Kind::Mp3) {
      return ma_dr_mp3_read_pcm_frames_f32(&m_mp3, frames, output);
    }
    return 0;
  }
  std::uint32_t channels() const { return m_channels; }
  std::uint32_t sampleRate() const { return m_sampleRate; }

private:
  enum class Kind
  {
    None,
    Wav,
    Flac,
    Mp3
  };
  Kind m_kind = Kind::None;
  std::uint32_t m_channels = 0;
  std::uint32_t m_sampleRate = 0;
  ma_dr_wav m_wav{};
  ma_dr_flac* m_flac = nullptr;
  ma_dr_mp3 m_mp3{};
};

// Mixes wider sources down to stereo with miniaudio's standard channel maps.
class StereoMix
{
public:
  StereoMix() = default;
  ~StereoMix()
  {
    if (m_ready) {
      ma_channel_converter_uninit(&m_converter, nullptr);
    }
  }
  StereoMix(const StereoMix&) = delete;
  StereoMix& operator=(const StereoMix&) = delete;
  StereoMix(StereoMix&&) = delete;
  StereoMix& operator=(StereoMix&&) = delete;

  bool open(std::uint32_t inputChannels)
  {
    const ma_channel_converter_config config =
      ma_channel_converter_config_init(ma_format_f32,
                                       inputChannels,
                                       nullptr,
                                       AudioClip::kMaximumChannels,
                                       nullptr,
                                       ma_channel_mix_mode_default);
    m_ready =
      ma_channel_converter_init(&config, nullptr, &m_converter) == MA_SUCCESS;
    return m_ready;
  }
  void mix(const float* input, float* output, ma_uint64 frames)
  {
    ma_channel_converter_process_pcm_frames(
      &m_converter, output, input, frames);
  }

private:
  ma_channel_converter m_converter{};
  bool m_ready = false;
};
} // namespace

bool
AudioDecoder::decode(std::span<const std::byte> encoded,
                     AudioClip& clip,
                     std::string& error)
{
  error.clear();
  if (encoded.empty() || encoded.size() > kMaximumEncodedBytes) {
    error = "The sound file is empty or larger than 64 MiB";
    return false;
  }
  // The decoder states are large (MP3 especially); keep them off the stack.
  const std::unique_ptr<MemorySource> source = std::make_unique<MemorySource>();
  if (!source->open(encoded)) {
    error = "Not a supported sound file (WAV, FLAC or MP3)";
    return false;
  }
  const std::uint32_t sourceChannels = source->channels();
  AudioClip decoded;
  decoded.channels = std::min(sourceChannels, AudioClip::kMaximumChannels);
  decoded.sampleRate = source->sampleRate();
  if (sourceChannels == 0 || sourceChannels > MA_MAX_CHANNELS ||
      decoded.sampleRate < AudioClip::kMinimumSampleRate ||
      decoded.sampleRate > AudioClip::kMaximumSampleRate) {
    error = "The sound's channel count or sample rate is unsupported";
    return false;
  }
  std::unique_ptr<StereoMix> downmix;
  if (sourceChannels > decoded.channels) {
    downmix = std::make_unique<StereoMix>();
    if (!downmix->open(sourceChannels)) {
      error = "The sound's channel layout is unsupported";
      return false;
    }
  }
  // Read in blocks rather than trusting a header's length, so a forged
  // length cannot force a large allocation.
  const std::size_t maximumFrames =
    AudioClip::kMaximumSamples / decoded.channels;
  std::vector<float> block(static_cast<std::size_t>(kReadFrames) *
                           sourceChannels);
  std::vector<float> mixed(
    downmix ? static_cast<std::size_t>(kReadFrames) * decoded.channels : 0u);
  for (;;) {
    const ma_uint64 read = source->read(kReadFrames, block.data());
    if (read == 0) {
      break;
    }
    if (decoded.frames() + read > maximumFrames) {
      error = "The sound is too long (at most " +
              std::to_string(AudioClip::kMaximumSamples) + " samples)";
      return false;
    }
    const float* frames = block.data();
    if (downmix) {
      downmix->mix(block.data(), mixed.data(), read);
      frames = mixed.data();
    }
    decoded.samples.insert(
      decoded.samples.end(),
      frames,
      frames + static_cast<std::ptrdiff_t>(read * decoded.channels));
  }
  if (!decoded.valid()) {
    error = "The sound file is damaged or holds no playable samples";
    return false;
  }
  clip = std::move(decoded);
  return true;
}
