#include <IllumoGuest/Audio.h>
#include <algorithm>

static float
clampedOr(float value, float minimum, float maximum, float fallback)
{
  return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

GuestAudio::GuestAudio(GuestServiceQueue& services)
  : m_services(services)
{
}

const GuestAudio::Slot*
GuestAudio::find(SoundHandle sound) const
{
  if (sound.slot == 0 || sound.slot > IAudio::kMaximumSounds) {
    return nullptr;
  }
  const Slot& slot = m_slots[sound.slot];
  return slot.used && slot.generation == sound.generation ? &slot : nullptr;
}

bool
GuestAudio::send(const GuestAudioRequest& request)
{
  GuestWireWriter payload;
  request.write(payload);
  return m_services.enqueue(GuestService::Audio, payload.take()) != 0;
}

SoundHandle
GuestAudio::createSound(const AudioClip& clip)
{
  if (!m_granted || !clip.valid() ||
      m_nextWire > GuestAudioRequest::MaximumSoundId) {
    return {};
  }
  for (std::uint32_t index = 1; index <= IAudio::kMaximumSounds; ++index) {
    Slot& slot = m_slots[index];
    if (slot.used) {
      continue;
    }
    GuestAudioRequest request;
    request.action = GuestAudioAction::Create;
    request.sound = m_nextWire;
    request.channels = clip.channels;
    request.sampleRate = clip.sampleRate;
    request.samples = clip.samples;
    if (!send(request)) {
      return {};
    }
    slot.used = true;
    slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
    slot.wire = m_nextWire++;
    return { index, slot.generation };
  }
  return {};
}

void
GuestAudio::destroySound(SoundHandle sound)
{
  const Slot* found = find(sound);
  if (found == nullptr) {
    return;
  }
  GuestAudioRequest request;
  request.action = GuestAudioAction::Destroy;
  request.sound = found->wire;
  // Forgotten here even if the queue is full; the host then releases the
  // sound with the rest of this guest's sounds when the guest retires.
  send(request);
  m_slots[sound.slot].used = false;
}

bool
GuestAudio::play(SoundHandle sound, const SoundPlayback& playback)
{
  const Slot* found = find(sound);
  if (!m_granted || found == nullptr) {
    return false;
  }
  GuestAudioRequest request;
  request.action = GuestAudioAction::Play;
  request.sound = found->wire;
  request.volume = clampedOr(playback.volume, 0.0f, 1.0f, 1.0f);
  request.pan = clampedOr(playback.pan, -1.0f, 1.0f, 0.0f);
  request.pitch = clampedOr(playback.pitch,
                            SoundPlayback::kMinimumPitch,
                            SoundPlayback::kMaximumPitch,
                            1.0f);
  return send(request);
}

void
GuestAudio::stopAll()
{
  if (!m_granted) {
    return;
  }
  GuestAudioRequest request;
  request.action = GuestAudioAction::StopAll;
  send(request);
}

void
GuestAudio::setMasterVolume(float volume)
{
  m_masterVolume = clampedOr(volume, 0.0f, 1.0f, 1.0f);
  if (!m_granted) {
    return;
  }
  GuestAudioRequest request;
  request.action = GuestAudioAction::SetVolume;
  request.volume = m_masterVolume;
  send(request);
}
