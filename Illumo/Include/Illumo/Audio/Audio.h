#pragma once

#include <cstdint>

struct AudioClip;

// A sound registered with an IAudio. Slot zero is always invalid; a destroyed
// sound's handle goes stale and is ignored from then on.
struct SoundHandle
{
  std::uint32_t slot = 0;
  std::uint32_t generation = 0;

  bool isValid() const { return slot != 0 && generation != 0; }
};

inline bool
operator==(SoundHandle left, SoundHandle right)
{
  return left.slot == right.slot && left.generation == right.generation;
}

inline bool
operator!=(SoundHandle left, SoundHandle right)
{
  return !(left == right);
}

// How one play of a sound is heard.
struct SoundPlayback
{
  static constexpr float kMinimumPitch = 0.25f;
  static constexpr float kMaximumPitch = 4.0f;

  // Linear gain, 0 to 1.
  float volume = 1.0f;
  // -1 is hard left, 1 hard right.
  float pan = 0.0f;
  // Playback-rate multiplier; also shifts the pitch.
  float pitch = 1.0f;
};

// Fire-and-forget sound effects. A product registers decoded clips once and
// then plays them by handle; every play is an independent voice, so a sound
// may overlap itself. When more voices are wanted than the output mixes, the
// oldest voice is replaced. Calls are main-thread affine.
// Published as IllumoContext::audio; absent where nothing can play sound, so
// products must also work silently.
class IAudio
{
public:
  static constexpr std::uint32_t kMaximumSounds = 256;
  static constexpr std::uint32_t kMaximumVoices = 32;

  IAudio() = default;
  virtual ~IAudio() = default;
  IAudio(const IAudio&) = delete;
  IAudio& operator=(const IAudio&) = delete;
  IAudio(IAudio&&) = delete;
  IAudio& operator=(IAudio&&) = delete;

  virtual bool available() const = 0;
  // Copies the clip. Invalid when the clip is not valid() or kMaximumSounds
  // sounds already exist.
  virtual SoundHandle createSound(const AudioClip& clip) = 0;
  // Stops the sound's voices and releases it.
  virtual void destroySound(SoundHandle sound) = 0;
  // Starts a voice; values outside SoundPlayback's ranges are clamped. False
  // for a stale handle or when there is no output.
  virtual bool play(SoundHandle sound, const SoundPlayback& playback = {}) = 0;
  virtual void stopAll() = 0;
  // Linear gain over every voice, 0 to 1.
  virtual void setMasterVolume(float volume) = 0;
  virtual float masterVolume() const = 0;
};
