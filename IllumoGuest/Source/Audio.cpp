#include <Illumo/Services/Logger.h>
#include <IllumoGuest/Audio.h>
#include <algorithm>
#include <string>

// Reported once: a product that keeps creating sounds would repeat it.
static bool soundLimitReported = false;

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
GuestAudio::send(const GuestAudioRequest& request, std::span<const float> chunk)
{
  GuestWireWriter writer;
  request.write(writer, chunk);
  std::vector<std::byte> payload = writer.take();
  // Behind a waiting request, so the host sees requests in the order made.
  if (m_backlog.empty() &&
      m_services.tryEnqueue(GuestService::Audio, payload) != 0) {
    return true;
  }
  if (m_backlog.size() >= kMaximumBacklog) {
    return false;
  }
  m_backlog.push_back(std::move(payload));
  return true;
}

void
GuestAudio::pump()
{
  while (!m_backlog.empty() &&
         m_services.tryEnqueue(GuestService::Audio, m_backlog.front()) != 0) {
    m_backlog.pop_front();
  }
}

SoundHandle
GuestAudio::createSound(const AudioClip& clip)
{
  if (!m_granted || !clip.valid() ||
      m_nextWire > GuestAudioRequest::MaximumSoundId) {
    return {};
  }
  const std::size_t chunkSamples = GuestAudioRequest::MaximumChunkSamples;
  const std::size_t chunks =
    (clip.samples.size() + chunkSamples - 1) / chunkSamples;
  // Every chunk may have to wait; none may be dropped.
  if (m_backlog.size() + chunks > kMaximumBacklog) {
    return {};
  }
  for (std::uint32_t index = 1; index <= IAudio::kMaximumSounds; ++index) {
    Slot& slot = m_slots[index];
    if (slot.used) {
      continue;
    }
    const std::span<const float> samples(clip.samples);
    GuestAudioRequest request;
    request.action = GuestAudioAction::Create;
    request.sound = m_nextWire;
    request.channels = clip.channels;
    request.sampleRate = clip.sampleRate;
    request.total = static_cast<std::uint32_t>(samples.size());
    for (std::size_t offset = 0; offset < samples.size();
         offset += chunkSamples) {
      const std::size_t count = std::min(chunkSamples, samples.size() - offset);
      send(request, samples.subspan(offset, count));
      request = {};
      request.action = GuestAudioAction::Append;
      request.sound = m_nextWire;
    }
    slot.used = true;
    slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
    slot.wire = m_nextWire++;
    return { index, slot.generation };
  }
  if (!soundLimitReported) {
    soundLimitReported = true;
    Logger::LogWarning("Sound refused: all " +
                       std::to_string(IAudio::kMaximumSounds) +
                       " sound slots are in use");
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
  request.fade = clampedOr(
    playback.fadeInSeconds, 0.0f, SoundPlayback::kMaximumFadeSeconds, 0.0f);
  request.flags = playback.loop ? GuestAudioRequest::LoopFlag : 0u;
  return send(request);
}

void
GuestAudio::stop(SoundHandle sound, float fadeSeconds)
{
  const Slot* found = find(sound);
  if (!m_granted || found == nullptr) {
    return;
  }
  GuestAudioRequest request;
  request.action = GuestAudioAction::Stop;
  request.sound = found->wire;
  request.fade =
    clampedOr(fadeSeconds, 0.0f, SoundPlayback::kMaximumFadeSeconds, 0.0f);
  send(request);
}

void
GuestAudio::setVolume(SoundHandle sound, float volume)
{
  const Slot* found = find(sound);
  if (!m_granted || found == nullptr) {
    return;
  }
  GuestAudioRequest request;
  request.action = GuestAudioAction::SetSoundVolume;
  request.sound = found->wire;
  request.volume = clampedOr(volume, 0.0f, 1.0f, 1.0f);
  send(request);
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
