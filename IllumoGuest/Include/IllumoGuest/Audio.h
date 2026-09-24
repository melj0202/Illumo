#pragma once

#include <Illumo/Audio/Audio.h>
#include <Illumo/Audio/AudioClip.h>
#include <IllumoGuest/Services.h>
#include <IllumoGuest/Wire.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Audio service requests (Audio capability). A guest decodes its own sound
// files and registers the samples under a guest-chosen sound id; plays and
// releases then name that id. The host mixes; nothing is returned but the
// request status, and guests discard even that.
enum class GuestAudioAction : std::uint32_t
{
  Create = 1,
  Destroy = 2,
  Play = 3,
  StopAll = 4,
  SetVolume = 5
};

// Wire layout (version 1): version, action, sound, volume, pan, pitch,
// channels, sample rate, sample count, then that many f32 samples. Fields an
// action does not use must be zero, so each action has exactly one encoding.
struct GuestAudioRequest
{
  static constexpr std::uint32_t Version = 1;
  static constexpr std::uint32_t MaximumSoundId = 0x7fffffffu;

  GuestAudioAction action = GuestAudioAction::Play;
  std::uint32_t sound = 0;
  // Play: the voice; SetVolume: the master gain.
  float volume = 0.0f;
  float pan = 0.0f;
  float pitch = 0.0f;
  // Create only.
  std::uint32_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::vector<float> samples;

  void write(GuestWireWriter& output) const
  {
    output.u32(Version);
    output.u32(static_cast<std::uint32_t>(action));
    output.u32(sound);
    output.f32(volume);
    output.f32(pan);
    output.f32(pitch);
    output.u32(channels);
    output.u32(sampleRate);
    output.u32(static_cast<std::uint32_t>(samples.size()));
    for (float sample : samples) {
      output.f32(sample);
    }
  }
  static bool read(std::span<const std::byte> bytes, GuestAudioRequest& output)
  {
    GuestWireReader reader(bytes);
    GuestAudioRequest candidate;
    const std::uint32_t version = reader.u32();
    const std::uint32_t action = reader.u32();
    candidate.sound = reader.u32();
    candidate.volume = reader.f32();
    candidate.pan = reader.f32();
    candidate.pitch = reader.f32();
    candidate.channels = reader.u32();
    candidate.sampleRate = reader.u32();
    const std::uint32_t count = reader.u32();
    if (!reader.valid() || version != Version || action < 1 || action > 5 ||
        candidate.sound > MaximumSoundId ||
        count > AudioClip::kMaximumSamples ||
        reader.remaining() != static_cast<std::size_t>(count) * 4u) {
      return false;
    }
    candidate.action = static_cast<GuestAudioAction>(action);
    const bool named = candidate.sound != 0;
    const bool noLayout =
      candidate.channels == 0 && candidate.sampleRate == 0 && count == 0;
    const bool noVoice = candidate.volume == 0.0f && candidate.pan == 0.0f &&
                         candidate.pitch == 0.0f;
    switch (candidate.action) {
      case GuestAudioAction::Create: {
        if (!named || !noVoice || count == 0) {
          return false;
        }
        candidate.samples.resize(count);
        for (float& sample : candidate.samples) {
          sample = reader.f32();
        }
        AudioClip clip;
        clip.channels = candidate.channels;
        clip.sampleRate = candidate.sampleRate;
        clip.samples = std::move(candidate.samples);
        if (!clip.valid()) {
          return false;
        }
        candidate.samples = std::move(clip.samples);
        break;
      }
      case GuestAudioAction::Destroy:
        if (!named || !noVoice || !noLayout) {
          return false;
        }
        break;
      case GuestAudioAction::Play:
        if (!named || !noLayout || !std::isfinite(candidate.volume) ||
            candidate.volume < 0.0f || candidate.volume > 1.0f ||
            !std::isfinite(candidate.pan) || candidate.pan < -1.0f ||
            candidate.pan > 1.0f || !std::isfinite(candidate.pitch) ||
            candidate.pitch < SoundPlayback::kMinimumPitch ||
            candidate.pitch > SoundPlayback::kMaximumPitch) {
          return false;
        }
        break;
      case GuestAudioAction::StopAll:
        if (named || !noVoice || !noLayout) {
          return false;
        }
        break;
      case GuestAudioAction::SetVolume:
        if (named || !noLayout || !std::isfinite(candidate.volume) ||
            candidate.volume < 0.0f || candidate.volume > 1.0f ||
            candidate.pan != 0.0f || candidate.pitch != 0.0f) {
          return false;
        }
        break;
    }
    if (!reader.finished()) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

// IAudio inside a guest (Audio capability): sounds, plays and releases travel
// as Audio service requests, sent with the next exchange. Handles are
// guest-local; each registered sound gets a fresh wire id that is never
// reused, so a late request can only ever name the sound it was made for.
// Without the grant it is unavailable and every call is a no-op.
class GuestAudio final : public IAudio
{
public:
  explicit GuestAudio(GuestServiceQueue& services);
  ~GuestAudio() override = default;
  GuestAudio(const GuestAudio&) = delete;
  GuestAudio& operator=(const GuestAudio&) = delete;
  GuestAudio(GuestAudio&&) = delete;
  GuestAudio& operator=(GuestAudio&&) = delete;

  // Whether the host granted the Audio capability.
  void setGranted(bool granted) { m_granted = granted; }

  bool available() const override { return m_granted; }
  // Invalid when not granted, for an invalid clip, when the sound table is
  // full or when this update's service queue has no room for the samples.
  SoundHandle createSound(const AudioClip& clip) override;
  void destroySound(SoundHandle sound) override;
  // True once the play is queued; the host may still find no output.
  bool play(SoundHandle sound, const SoundPlayback& playback = {}) override;
  void stopAll() override;
  void setMasterVolume(float volume) override;
  float masterVolume() const override { return m_masterVolume; }

private:
  struct Slot
  {
    bool used = false;
    std::uint32_t generation = 0;
    std::uint32_t wire = 0;
  };
  const Slot* find(SoundHandle sound) const;
  bool send(const GuestAudioRequest& request);

  GuestServiceQueue& m_services;
  bool m_granted = false;
  std::array<Slot, IAudio::kMaximumSounds + 1> m_slots{};
  std::uint32_t m_nextWire = 1;
  float m_masterVolume = 1.0f;
};
