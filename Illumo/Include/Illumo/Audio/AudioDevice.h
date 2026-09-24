#pragma once

#include <Illumo/Audio/Audio.h>
#include <memory>
#include <span>
#include <string>

struct AudioDeviceOptions
{
  // No device: nothing is heard and render() pulls the mix instead (tests,
  // tools). Otherwise the system's default output device plays it.
  bool headless = false;
  // Mix format in headless mode; a device picks its own.
  std::uint32_t channels = 2;
  std::uint32_t sampleRate = 48000;
};

// The native IAudio: the platform's output device, mixed by miniaudio (kept
// private to this implementation; no miniaudio type crosses this header).
// Sounds keep their source rate and channel count; each voice is converted
// while it mixes. The mixer runs on the device's own thread; every call here
// is main-thread affine.
class AudioDevice final : public IAudio
{
public:
  // Null, with a reason, when the output cannot be opened (no device,
  // exclusive use, unsupported platform).
  static std::unique_ptr<AudioDevice> create(const AudioDeviceOptions& options,
                                             std::string& error);
  ~AudioDevice() override;
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;
  AudioDevice(AudioDevice&&) = delete;
  AudioDevice& operator=(AudioDevice&&) = delete;

  bool available() const override { return true; }
  SoundHandle createSound(const AudioClip& clip) override;
  void destroySound(SoundHandle sound) override;
  bool play(SoundHandle sound, const SoundPlayback& playback = {}) override;
  void stopAll() override;
  void setMasterVolume(float volume) override;
  float masterVolume() const override;

  std::uint32_t channels() const;
  std::uint32_t sampleRate() const;
  // Voices still playing.
  std::uint32_t activeVoices();
  std::uint32_t soundCount() const;
  // Headless only: mixes the next frames into `output` (interleaved,
  // channels() wide) and returns the frames written; 0 with a device.
  std::size_t render(std::span<float> output);

private:
  struct State;
  explicit AudioDevice(std::unique_ptr<State> state);
  std::unique_ptr<State> m_state;
};
