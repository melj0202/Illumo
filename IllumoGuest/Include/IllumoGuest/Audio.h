#pragma once

#include <Illumo/Audio/Audio.h>
#include <Illumo/Audio/AudioClip.h>
#include <IllumoGuest/Services.h>
#include <IllumoGuest/Wire.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

// Audio service requests (Audio capability). A guest decodes its own sound
// files and registers the samples under a guest-chosen sound id; plays and
// releases then name that id. A sound larger than one record arrives as a
// Create carrying its first chunk and total size, then Appends in order; it
// can be played once its last sample has landed. The host mixes; nothing is
// returned but the request status, and guests discard even that.
enum class GuestAudioAction : std::uint32_t
{
  Create = 1,
  Destroy = 2,
  Play = 3,
  StopAll = 4,
  SetVolume = 5,
  Append = 6,
  Stop = 7,
  SetSoundVolume = 8
};

// Wire layout (version 2): version, action, sound, volume, pan, pitch, fade,
// flags, channels, sample rate, total samples, sample count, then that many
// f32 samples. Fields an action does not use must be zero, so each action has
// exactly one encoding.
struct GuestAudioRequest
{
  static constexpr std::uint32_t Version = 2;
  static constexpr std::uint32_t MaximumSoundId = 0x7fffffffu;
  // Samples in one Create or Append: 8 MiB, so a chunk and the rest of an
  // update's requests share one exchange.
  static constexpr std::uint32_t MaximumChunkSamples = 2u * 1024u * 1024u;
  // Play flag: loop until stopped.
  static constexpr std::uint32_t LoopFlag = 1u;

  GuestAudioAction action = GuestAudioAction::Play;
  std::uint32_t sound = 0;
  // Play: the voice; SetVolume: the master gain; SetSoundVolume: the sound's
  // playing voices.
  float volume = 0.0f;
  float pan = 0.0f;
  float pitch = 0.0f;
  // Play: fade-in seconds; Stop: fade-out seconds.
  float fade = 0.0f;
  std::uint32_t flags = 0;
  // Create only: the layout and the sound's whole sample count.
  std::uint32_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint32_t total = 0;
  // Create and Append: this chunk.
  std::vector<float> samples;

  void write(GuestWireWriter& output) const { write(output, samples); }
  // The same encoding with `chunk` in place of `samples`, so a caller can
  // send slices of a clip it keeps without copying them into requests.
  void write(GuestWireWriter& output, std::span<const float> chunk) const
  {
    output.u32(Version);
    output.u32(static_cast<std::uint32_t>(action));
    output.u32(sound);
    output.f32(volume);
    output.f32(pan);
    output.f32(pitch);
    output.f32(fade);
    output.u32(flags);
    output.u32(channels);
    output.u32(sampleRate);
    output.u32(total);
    output.u32(static_cast<std::uint32_t>(chunk.size()));
    for (float sample : chunk) {
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
    candidate.fade = reader.f32();
    candidate.flags = reader.u32();
    candidate.channels = reader.u32();
    candidate.sampleRate = reader.u32();
    candidate.total = reader.u32();
    const std::uint32_t count = reader.u32();
    if (!reader.valid() || version != Version || action < 1 || action > 8 ||
        candidate.sound > MaximumSoundId || count > MaximumChunkSamples ||
        reader.remaining() != static_cast<std::size_t>(count) * 4u) {
      return false;
    }
    candidate.action = static_cast<GuestAudioAction>(action);
    const bool named = candidate.sound != 0;
    const bool noLayout = candidate.channels == 0 &&
                          candidate.sampleRate == 0 && candidate.total == 0;
    const bool noSamples = count == 0;
    const bool noVoice = candidate.volume == 0.0f && candidate.pan == 0.0f &&
                         candidate.pitch == 0.0f && candidate.fade == 0.0f &&
                         candidate.flags == 0;
    const bool gain = std::isfinite(candidate.volume) &&
                      candidate.volume >= 0.0f && candidate.volume <= 1.0f;
    const bool fade = std::isfinite(candidate.fade) && candidate.fade >= 0.0f &&
                      candidate.fade <= SoundPlayback::kMaximumFadeSeconds;
    bool valid = false;
    switch (candidate.action) {
      case GuestAudioAction::Create:
        valid = named && noVoice && count != 0 && candidate.channels != 0 &&
                candidate.channels <= AudioClip::kMaximumChannels &&
                candidate.sampleRate >= AudioClip::kMinimumSampleRate &&
                candidate.sampleRate <= AudioClip::kMaximumSampleRate &&
                candidate.total >= count &&
                candidate.total <= AudioClip::kMaximumSamples &&
                candidate.total % candidate.channels == 0;
        break;
      case GuestAudioAction::Append:
        valid = named && noVoice && noLayout && count != 0;
        break;
      case GuestAudioAction::Destroy:
        valid = named && noVoice && noLayout && noSamples;
        break;
      case GuestAudioAction::Play:
        valid = named && noLayout && noSamples && gain && fade &&
                (candidate.flags & ~LoopFlag) == 0 &&
                std::isfinite(candidate.pan) && candidate.pan >= -1.0f &&
                candidate.pan <= 1.0f && std::isfinite(candidate.pitch) &&
                candidate.pitch >= SoundPlayback::kMinimumPitch &&
                candidate.pitch <= SoundPlayback::kMaximumPitch;
        break;
      case GuestAudioAction::StopAll:
        valid = !named && noVoice && noLayout && noSamples;
        break;
      case GuestAudioAction::SetVolume:
        valid = !named && noLayout && noSamples && gain &&
                candidate.pan == 0.0f && candidate.pitch == 0.0f &&
                candidate.fade == 0.0f && candidate.flags == 0;
        break;
      case GuestAudioAction::Stop:
        valid = named && noLayout && noSamples && fade &&
                candidate.volume == 0.0f && candidate.pan == 0.0f &&
                candidate.pitch == 0.0f && candidate.flags == 0;
        break;
      case GuestAudioAction::SetSoundVolume:
        valid = named && noLayout && noSamples && gain &&
                candidate.pan == 0.0f && candidate.pitch == 0.0f &&
                candidate.fade == 0.0f && candidate.flags == 0;
        break;
    }
    if (!valid) {
      return false;
    }
    candidate.samples.resize(count);
    for (float& sample : candidate.samples) {
      sample = reader.f32();
      if (!std::isfinite(sample)) {
        return false;
      }
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
// Requests the service queue cannot take yet (a large sound's later chunks,
// or anything behind them) wait in order and go out as pump() finds room.
// Without the grant it is unavailable and every call is a no-op.
class GuestAudio final : public IAudio
{
public:
  // Requests that may wait for queue room; a sound whose chunks would not
  // fit is refused rather than half sent.
  static constexpr std::size_t kMaximumBacklog = 64;

  explicit GuestAudio(GuestServiceQueue& services);
  ~GuestAudio() override = default;
  GuestAudio(const GuestAudio&) = delete;
  GuestAudio& operator=(const GuestAudio&) = delete;
  GuestAudio(GuestAudio&&) = delete;
  GuestAudio& operator=(GuestAudio&&) = delete;

  // Whether the host granted the Audio capability.
  void setGranted(bool granted) { m_granted = granted; }
  // Moves waiting requests into the service queue while it has room.
  void pump();
  std::size_t backlog() const { return m_backlog.size(); }

  bool available() const override { return m_granted; }
  // Invalid when not granted, for an invalid clip, when the sound table is
  // full or when the backlog has no room for the clip's chunks.
  SoundHandle createSound(const AudioClip& clip) override;
  void destroySound(SoundHandle sound) override;
  // True once the play is queued; the host may still find no output.
  bool play(SoundHandle sound, const SoundPlayback& playback = {}) override;
  void stop(SoundHandle sound, float fadeSeconds = 0.0f) override;
  void setVolume(SoundHandle sound, float volume) override;
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
  bool send(const GuestAudioRequest& request,
            std::span<const float> chunk = {});

  GuestServiceQueue& m_services;
  bool m_granted = false;
  std::array<Slot, IAudio::kMaximumSounds + 1> m_slots{};
  std::uint32_t m_nextWire = 1;
  float m_masterVolume = 1.0f;
  std::deque<std::vector<std::byte>> m_backlog;
};
