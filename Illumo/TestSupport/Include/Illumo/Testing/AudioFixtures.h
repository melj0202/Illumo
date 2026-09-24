#pragma once
// Test-only audio helpers: in-memory WAV files and a recording IAudio.

#include <Illumo/Audio/Audio.h>
#include <Illumo/Audio/AudioClip.h>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Appends `count` little-endian bytes of `value`.
inline void
appendWavValue(std::vector<std::byte>& bytes, std::uint32_t value, int count)
{
  for (int index = 0; index < count; ++index) {
    bytes.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xffu));
  }
}

inline void
appendWavTag(std::vector<std::byte>& bytes, const char* text)
{
  for (int index = 0; index < 4; ++index) {
    bytes.push_back(static_cast<std::byte>(text[index]));
  }
}

// A little-endian RIFF/WAVE file. Format 1 writes 16-bit PCM, format 3
// 32-bit float. `junk` inserts an unknown chunk before "fmt ", as some
// editors do.
inline std::vector<std::byte>
makeWav(const std::vector<float>& samples,
        std::uint32_t channels,
        std::uint32_t sampleRate,
        std::uint16_t format = 1,
        bool junk = false)
{
  std::vector<std::byte> bytes;
  const std::uint32_t sampleBytes = format == 3 ? 4u : 2u;
  const std::uint32_t dataBytes =
    static_cast<std::uint32_t>(samples.size()) * sampleBytes;
  const std::uint32_t junkBytes = junk ? 12u : 0u;
  appendWavTag(bytes, "RIFF");
  appendWavValue(bytes, 4u + junkBytes + 24u + 8u + dataBytes, 4);
  appendWavTag(bytes, "WAVE");
  if (junk) {
    appendWavTag(bytes, "JUNK");
    appendWavValue(bytes, 4, 4);
    appendWavValue(bytes, 0, 4);
  }
  appendWavTag(bytes, "fmt ");
  appendWavValue(bytes, 16, 4);
  appendWavValue(bytes, format, 2);
  appendWavValue(bytes, channels, 2);
  appendWavValue(bytes, sampleRate, 4);
  appendWavValue(bytes, sampleRate * channels * sampleBytes, 4);
  appendWavValue(bytes, channels * sampleBytes, 2);
  appendWavValue(bytes, sampleBytes * 8u, 2);
  appendWavTag(bytes, "data");
  appendWavValue(bytes, dataBytes, 4);
  for (float sample : samples) {
    if (format == 3) {
      appendWavValue(bytes, std::bit_cast<std::uint32_t>(sample), 4);
    } else {
      const float clamped = sample < -1.0f  ? -1.0f
                            : sample > 1.0f ? 1.0f
                                            : sample;
      const std::int16_t value =
        static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
      appendWavValue(bytes, static_cast<std::uint16_t>(value), 2);
    }
  }
  return bytes;
}

// A sine tone, interleaved with the same value on every channel.
inline std::vector<float>
makeTone(std::size_t frames,
         std::uint32_t channels,
         std::uint32_t sampleRate,
         float frequency = 440.0f,
         float amplitude = 0.5f)
{
  std::vector<float> samples;
  samples.reserve(frames * channels);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float value =
      amplitude *
      static_cast<float>(std::sin(6.283185307179586 * frequency *
                                  static_cast<double>(frame) / sampleRate));
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      samples.push_back(value);
    }
  }
  return samples;
}

// Records every call; sounds are numbered from 1 and never reuse a slot.
class RecordingAudio final : public IAudio
{
public:
  struct Play
  {
    SoundHandle sound;
    SoundPlayback playback;
  };

  bool available() const override { return true; }
  SoundHandle createSound(const AudioClip& clip) override
  {
    if (!clip.valid() || live >= kMaximumSounds) {
      return {};
    }
    clips.push_back(clip);
    ++live;
    return { static_cast<std::uint32_t>(clips.size()), 1 };
  }
  void destroySound(SoundHandle sound) override
  {
    if (sound.isValid()) {
      destroyed.push_back(sound);
      --live;
    }
  }
  bool play(SoundHandle sound, const SoundPlayback& playback) override
  {
    plays.push_back({ sound, playback });
    return sound.isValid() && sound.slot <= clips.size();
  }
  void stopAll() override { ++stops; }
  void setMasterVolume(float volume) override { master = volume; }
  float masterVolume() const override { return master; }

  std::vector<AudioClip> clips;
  std::vector<SoundHandle> destroyed;
  std::vector<Play> plays;
  std::uint32_t live = 0;
  int stops = 0;
  float master = 1.0f;
};
