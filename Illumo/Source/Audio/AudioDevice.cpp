#include <Illumo/Audio/AudioClip.h>
#include <Illumo/Audio/AudioDevice.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <miniaudio.h>
#include <vector>

namespace {
float
clampedOr(float value, float minimum, float maximum, float fallback)
{
  return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

struct Sound
{
  std::uint32_t generation = 0;
  bool used = false;
  std::uint32_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::vector<float> samples;
};

// One playing copy of a sound: a cursor over the sound's samples feeding an
// engine sound node. Addresses are stable (the voices never move) because
// the mixer thread reads both while the voice plays.
struct Voice
{
  bool initialized = false;
  std::uint32_t sound = 0;
  std::uint64_t order = 0;
  ma_audio_buffer_ref source{};
  ma_sound node{};
};
} // namespace

struct AudioDevice::State
{
  ma_engine engine{};
  bool engineReady = false;
  bool headless = false;
  // Slot 0 is the invalid handle; sounds live in slots 1..kMaximumSounds.
  std::array<Sound, IAudio::kMaximumSounds + 1> sounds;
  std::uint32_t soundCount = 0;
  std::array<Voice, IAudio::kMaximumVoices> voices;
  std::uint64_t nextOrder = 1;

  State() = default;
  ~State()
  {
    if (engineReady) {
      ma_engine_uninit(&engine);
    }
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;

  Sound* find(SoundHandle handle)
  {
    if (handle.slot == 0 || handle.slot > IAudio::kMaximumSounds) {
      return nullptr;
    }
    Sound& sound = sounds[handle.slot];
    return sound.used && sound.generation == handle.generation ? &sound
                                                               : nullptr;
  }
  static bool finished(const Voice& voice)
  {
    return !voice.initialized || ma_sound_at_end(&voice.node) ||
           !ma_sound_is_playing(&voice.node);
  }
  void release(Voice& voice)
  {
    if (voice.initialized) {
      // Detaches from the node graph; safe while the mixer thread runs.
      ma_sound_uninit(&voice.node);
      ma_audio_buffer_ref_uninit(&voice.source);
      voice.initialized = false;
    }
  }
  // A finished voice, else the oldest one, which is cut off.
  Voice& claimVoice()
  {
    Voice* oldest = &voices.front();
    for (Voice& voice : voices) {
      if (finished(voice)) {
        release(voice);
        return voice;
      }
      if (voice.order < oldest->order) {
        oldest = &voice;
      }
    }
    release(*oldest);
    return *oldest;
  }
};

AudioDevice::AudioDevice(std::unique_ptr<State> state)
  : m_state(std::move(state))
{
}

AudioDevice::~AudioDevice()
{
  if (m_state) {
    for (Voice& voice : m_state->voices) {
      m_state->release(voice);
    }
  }
}

std::unique_ptr<AudioDevice>
AudioDevice::create(const AudioDeviceOptions& options, std::string& error)
{
  error.clear();
  std::unique_ptr<State> state = std::make_unique<State>();
  state->headless = options.headless;
  ma_engine_config config = ma_engine_config_init();
  if (options.headless) {
    if (options.channels == 0 || options.channels > 2 ||
        options.sampleRate < AudioClip::kMinimumSampleRate ||
        options.sampleRate > AudioClip::kMaximumSampleRate) {
      error = "Unsupported headless mix format";
      return nullptr;
    }
    config.noDevice = MA_TRUE;
    config.channels = options.channels;
    config.sampleRate = options.sampleRate;
  }
  const ma_result result = ma_engine_init(&config, &state->engine);
  if (result != MA_SUCCESS) {
    error = std::string("No audio output: ") + ma_result_description(result);
    return nullptr;
  }
  state->engineReady = true;
  return std::unique_ptr<AudioDevice>(new AudioDevice(std::move(state)));
}

SoundHandle
AudioDevice::createSound(const AudioClip& clip)
{
  if (!clip.valid()) {
    return {};
  }
  for (std::uint32_t slot = 1; slot <= IAudio::kMaximumSounds; ++slot) {
    Sound& sound = m_state->sounds[slot];
    if (sound.used) {
      continue;
    }
    sound.samples = clip.samples;
    sound.channels = clip.channels;
    sound.sampleRate = clip.sampleRate;
    sound.used = true;
    sound.generation =
      sound.generation == UINT32_MAX ? 1 : sound.generation + 1;
    ++m_state->soundCount;
    return { slot, sound.generation };
  }
  return {};
}

void
AudioDevice::destroySound(SoundHandle handle)
{
  Sound* sound = m_state->find(handle);
  if (sound == nullptr) {
    return;
  }
  for (Voice& voice : m_state->voices) {
    if (voice.initialized && voice.sound == handle.slot) {
      m_state->release(voice);
    }
  }
  sound->used = false;
  sound->samples.clear();
  sound->samples.shrink_to_fit();
  --m_state->soundCount;
}

bool
AudioDevice::play(SoundHandle handle, const SoundPlayback& playback)
{
  const Sound* sound = m_state->find(handle);
  if (sound == nullptr) {
    return false;
  }
  Voice& voice = m_state->claimVoice();
  if (ma_audio_buffer_ref_init(ma_format_f32,
                               sound->channels,
                               sound->samples.data(),
                               sound->samples.size() / sound->channels,
                               &voice.source) != MA_SUCCESS) {
    return false;
  }
  // The engine resamples each voice from its own rate to the output's.
  voice.source.sampleRate = sound->sampleRate;
  if (ma_sound_init_from_data_source(&m_state->engine,
                                     &voice.source,
                                     MA_SOUND_FLAG_NO_SPATIALIZATION,
                                     nullptr,
                                     &voice.node) != MA_SUCCESS) {
    ma_audio_buffer_ref_uninit(&voice.source);
    return false;
  }
  voice.initialized = true;
  voice.sound = handle.slot;
  voice.order = m_state->nextOrder++;
  ma_sound_set_volume(&voice.node,
                      clampedOr(playback.volume, 0.0f, 1.0f, 1.0f));
  ma_sound_set_pan(&voice.node, clampedOr(playback.pan, -1.0f, 1.0f, 0.0f));
  ma_sound_set_pitch(&voice.node,
                     clampedOr(playback.pitch,
                               SoundPlayback::kMinimumPitch,
                               SoundPlayback::kMaximumPitch,
                               1.0f));
  if (ma_sound_start(&voice.node) != MA_SUCCESS) {
    m_state->release(voice);
    return false;
  }
  return true;
}

void
AudioDevice::stopAll()
{
  for (Voice& voice : m_state->voices) {
    m_state->release(voice);
  }
}

void
AudioDevice::setMasterVolume(float volume)
{
  ma_engine_set_volume(&m_state->engine, clampedOr(volume, 0.0f, 1.0f, 1.0f));
}

float
AudioDevice::masterVolume() const
{
  return ma_engine_get_volume(&m_state->engine);
}

std::uint32_t
AudioDevice::channels() const
{
  return ma_engine_get_channels(&m_state->engine);
}

std::uint32_t
AudioDevice::sampleRate() const
{
  return ma_engine_get_sample_rate(&m_state->engine);
}

std::uint32_t
AudioDevice::activeVoices()
{
  std::uint32_t count = 0;
  for (const Voice& voice : m_state->voices) {
    count += State::finished(voice) ? 0u : 1u;
  }
  return count;
}

std::uint32_t
AudioDevice::soundCount() const
{
  return m_state->soundCount;
}

std::size_t
AudioDevice::render(std::span<float> output)
{
  const std::uint32_t width = channels();
  if (!m_state->headless || width == 0) {
    return 0;
  }
  ma_uint64 read = 0;
  if (ma_engine_read_pcm_frames(
        &m_state->engine, output.data(), output.size() / width, &read) !=
      MA_SUCCESS) {
    return 0;
  }
  return static_cast<std::size_t>(read);
}
