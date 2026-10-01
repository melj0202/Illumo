// Audio capability: the Audio service decoder, host deny/permit behavior and
// the guest SDK's GuestAudio talking to the host through real exchanges.
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/AudioFixtures.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmGameServices.h>
#include <IllumoGuest/Audio.h>
#include <IllumoGuest/Protocol.h>
#include <limits>
#include <string>

static std::vector<std::byte>
encode(const GuestAudioRequest& request)
{
  GuestWireWriter writer;
  request.write(writer);
  return writer.take();
}

static bool
decodes(const GuestAudioRequest& request)
{
  GuestAudioRequest decoded;
  return GuestAudioRequest::read(encode(request), decoded);
}

// A mono sound's Create; `total` 0 means the chunk is the whole sound.
static GuestAudioRequest
createRequest(std::uint32_t sound,
              std::size_t frames = 64,
              std::size_t total = 0)
{
  GuestAudioRequest request;
  request.action = GuestAudioAction::Create;
  request.sound = sound;
  request.channels = 1;
  request.sampleRate = 22050;
  request.samples = makeTone(frames, 1, 22050);
  request.total = static_cast<std::uint32_t>(total == 0 ? frames : total);
  return request;
}

static GuestAudioRequest
appendRequest(std::uint32_t sound, std::size_t frames = 64)
{
  GuestAudioRequest request;
  request.action = GuestAudioAction::Append;
  request.sound = sound;
  request.samples = makeTone(frames, 1, 22050, 220.0f);
  return request;
}

static GuestAudioRequest
playRequest(std::uint32_t sound, float volume = 0.5f)
{
  GuestAudioRequest request;
  request.action = GuestAudioAction::Play;
  request.sound = sound;
  request.volume = volume;
  request.pitch = 1.0f;
  return request;
}

static GuestAudioRequest
simpleRequest(GuestAudioAction action, std::uint32_t sound = 0)
{
  GuestAudioRequest request;
  request.action = action;
  request.sound = sound;
  return request;
}

// One services batch of audio requests with consecutive ids from `first`.
static std::vector<std::byte>
batch(const std::vector<GuestAudioRequest>& requests, std::uint64_t first)
{
  GuestServices services;
  for (const GuestAudioRequest& request : requests) {
    services.records.push_back({ first++,
                                 GuestService::Audio,
                                 GuestServiceStatus::Request,
                                 encode(request) });
  }
  GuestWireWriter writer;
  services.write(writer);
  return writer.take();
}

static bool
statuses(const std::vector<std::byte>& completions,
         const std::vector<GuestServiceStatus>& expected)
{
  GuestServices result;
  if (!GuestServices::read(completions, result, false) ||
      result.records.size() != expected.size()) {
    return false;
  }
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (result.records[index].operation != GuestService::Audio ||
        result.records[index].status != expected[index] ||
        !result.records[index].payload.empty()) {
      return false;
    }
  }
  return true;
}

static bool
testDecoder()
{
  TestCounters counters;
  GuestAudioRequest decoded;
  const GuestAudioRequest create = createRequest(7);
  testTrue(counters,
           GuestAudioRequest::read(encode(create), decoded) &&
             decoded.action == GuestAudioAction::Create && decoded.sound == 7 &&
             decoded.samples == create.samples && decoded.channels == 1 &&
             decoded.sampleRate == 22050,
           "Create round-trips with its samples");
  testTrue(counters,
           decodes(playRequest(7)) &&
             decodes(simpleRequest(GuestAudioAction::Destroy, 7)) &&
             decodes(simpleRequest(GuestAudioAction::StopAll)),
           "Play, Destroy and StopAll round-trip");
  GuestAudioRequest volume = simpleRequest(GuestAudioAction::SetVolume);
  volume.volume = 0.25f;
  testTrue(counters, decodes(volume), "SetVolume round-trips");

  GuestAudioRequest loop = playRequest(7);
  loop.flags = GuestAudioRequest::LoopFlag;
  loop.fade = 1.5f;
  testTrue(counters,
           GuestAudioRequest::read(encode(loop), decoded) &&
             decoded.flags == GuestAudioRequest::LoopFlag &&
             decoded.fade == 1.5f,
           "Play carries its loop flag and fade-in");
  const GuestAudioRequest first = createRequest(7, 64, 192);
  const GuestAudioRequest more = appendRequest(7);
  testTrue(counters,
           GuestAudioRequest::read(encode(first), decoded) &&
             decoded.total == 192 && decoded.samples.size() == 64 &&
             GuestAudioRequest::read(encode(more), decoded) &&
             decoded.action == GuestAudioAction::Append &&
             decoded.samples == more.samples,
           "A chunked Create and its Append round-trip");
  GuestAudioRequest stop = simpleRequest(GuestAudioAction::Stop, 7);
  stop.fade = 0.8f;
  GuestAudioRequest soundVolume =
    simpleRequest(GuestAudioAction::SetSoundVolume, 7);
  soundVolume.volume = 0.3f;
  testTrue(counters,
           decodes(stop) && decodes(soundVolume) &&
             decodes(simpleRequest(GuestAudioAction::Stop, 7)),
           "Stop and SetSoundVolume round-trip");

  std::vector<std::byte> bytes = encode(playRequest(7));
  bytes[0] = std::byte{ 1 };
  testTrue(counters,
           !GuestAudioRequest::read(bytes, decoded),
           "The retired version 1 layout is rejected");
  bytes[0] = std::byte{ 3 };
  testTrue(counters,
           !GuestAudioRequest::read(bytes, decoded),
           "An unknown version is rejected");
  bytes = encode(playRequest(7));
  bytes[4] = std::byte{ 9 };
  testTrue(counters,
           !GuestAudioRequest::read(bytes, decoded),
           "An unknown action is rejected");
  bytes = encode(playRequest(7));
  bytes.push_back(std::byte{ 0 });
  testTrue(counters,
           !GuestAudioRequest::read(bytes, decoded),
           "Trailing bytes are rejected");
  bytes = encode(create);
  bytes.resize(bytes.size() - 4);
  testTrue(counters,
           !GuestAudioRequest::read(bytes, decoded),
           "A sample count beyond the payload is rejected");

  GuestAudioRequest bad = createRequest(0);
  testTrue(counters, !decodes(bad), "Create needs a sound id");
  bad = createRequest(GuestAudioRequest::MaximumSoundId + 1u);
  testTrue(counters, !decodes(bad), "Sound ids stay below 2^31");
  bad = createRequest(7);
  bad.channels = 3;
  bad.samples.resize(63);
  testTrue(counters, !decodes(bad), "Create refuses three channels");
  bad = createRequest(7);
  bad.samples[3] = std::numeric_limits<float>::quiet_NaN();
  testTrue(counters, !decodes(bad), "Create refuses non-finite samples");
  bad = createRequest(7);
  bad.sampleRate = 1000;
  testTrue(counters, !decodes(bad), "Create refuses an unsupported rate");
  bad = createRequest(7);
  bad.volume = 1.0f;
  testTrue(counters, !decodes(bad), "Create carries no voice fields");
  bad = createRequest(7, 0);
  testTrue(counters, !decodes(bad), "Create needs samples");
  bad = createRequest(7, 64, 32);
  testTrue(counters, !decodes(bad), "Create's total covers its chunk");
  bad = createRequest(7, 64, AudioClip::kMaximumSamples + 1u);
  testTrue(counters, !decodes(bad), "Create's total stays within a clip");
  bad = createRequest(7, 64, 129);
  bad.channels = 2;
  testTrue(counters, !decodes(bad), "Create's total is whole frames");
  bad = createRequest(7, GuestAudioRequest::MaximumChunkSamples + 1u);
  testTrue(counters, !decodes(bad), "A chunk stays within its bound");
  bad = appendRequest(7, 0);
  testTrue(counters, !decodes(bad), "Append needs samples");
  bad = appendRequest(7);
  bad.channels = 1;
  testTrue(counters, !decodes(bad), "Append carries no layout");
  bad = appendRequest(0);
  testTrue(counters, !decodes(bad), "Append needs a sound id");
  bad = appendRequest(7);
  bad.samples[5] = std::numeric_limits<float>::infinity();
  testTrue(counters, !decodes(bad), "Append refuses non-finite samples");
  bad = playRequest(7);
  bad.flags = 2;
  testTrue(counters, !decodes(bad), "Play refuses unknown flags");
  bad = playRequest(7);
  bad.fade = SoundPlayback::kMaximumFadeSeconds + 1.0f;
  testTrue(counters, !decodes(bad), "Play fade stays within its bound");
  bad = simpleRequest(GuestAudioAction::Stop, 7);
  bad.fade = -1.0f;
  testTrue(counters, !decodes(bad), "Stop fade is not negative");
  bad = simpleRequest(GuestAudioAction::Stop, 7);
  bad.volume = 0.5f;
  testTrue(counters, !decodes(bad), "Stop carries no gain");
  bad = simpleRequest(GuestAudioAction::Stop);
  testTrue(counters, !decodes(bad), "Stop needs a sound id");
  bad = simpleRequest(GuestAudioAction::SetSoundVolume, 7);
  bad.volume = 1.5f;
  testTrue(counters, !decodes(bad), "SetSoundVolume stays within 0..1");
  bad = simpleRequest(GuestAudioAction::SetSoundVolume);
  bad.volume = 0.5f;
  testTrue(counters, !decodes(bad), "SetSoundVolume needs a sound id");
  bad = simpleRequest(GuestAudioAction::StopAll);
  bad.fade = 1.0f;
  testTrue(counters, !decodes(bad), "StopAll carries no fade");

  bad = playRequest(7, 1.5f);
  testTrue(counters, !decodes(bad), "Play volume stays within 0..1");
  bad = playRequest(7);
  bad.pitch = 0.1f;
  testTrue(counters, !decodes(bad), "Play pitch stays within 0.25..4");
  bad = playRequest(7);
  bad.pan = std::numeric_limits<float>::infinity();
  testTrue(counters, !decodes(bad), "Play pan must be finite");
  bad = playRequest(0);
  testTrue(counters, !decodes(bad), "Play needs a sound id");
  bad = playRequest(7);
  bad.samples = { 0.0f };
  testTrue(counters, !decodes(bad), "Play carries no samples");
  bad = simpleRequest(GuestAudioAction::StopAll, 7);
  testTrue(counters, !decodes(bad), "StopAll names no sound");
  bad = simpleRequest(GuestAudioAction::Destroy, 7);
  bad.pitch = 1.0f;
  testTrue(counters, !decodes(bad), "Destroy carries no voice fields");
  bad = simpleRequest(GuestAudioAction::SetVolume);
  bad.volume = -0.5f;
  testTrue(counters, !decodes(bad), "SetVolume stays within 0..1");
  return counters.failures == 0;
}

struct AudioHost
{
  NullRenderWindow window{ 640, 480 };
  EnvVars env;
  Camera camera{ glm::vec2(0, 0), 1, &env };
  MockBackend mock;
  Renderer renderer{ &window, &env, &camera, &mock, false };
  WasmFrameRenderer bridge{ renderer, 331 };

  AudioHost() { mock.Initialize(); }
};

static bool
testServices()
{
  TestCounters counters;
  AudioHost host;
  RecordingAudio audio;
  const std::uint32_t grant =
    static_cast<std::uint32_t>(GuestCapability::Audio);
  std::vector<std::byte> completions;

  WasmGameServices denied(host.bridge, 0, ILLUMO_ENGINE_ASSETS);
  denied.setAudio(&audio);
  testTrue(counters,
           denied.process(batch({ createRequest(7), playRequest(7) }, 1),
                          completions) &&
             statuses(completions,
                      { GuestServiceStatus::Rejected,
                        GuestServiceStatus::Rejected }) &&
             audio.clips.empty() && audio.plays.empty(),
           "Without the grant audio requests are rejected with no effect");

  WasmGameServices silent(host.bridge, grant, ILLUMO_ENGINE_ASSETS);
  testTrue(counters,
           silent.process(batch({ createRequest(7) }, 1), completions) &&
             statuses(completions, { GuestServiceStatus::Rejected }),
           "A grant without an output rejects audio requests");

  WasmGameServices services(host.bridge, grant, ILLUMO_ENGINE_ASSETS);
  services.setAudio(&audio);
  testTrue(
    counters,
    services.process(batch({ createRequest(7), playRequest(7, 0.25f) }, 1),
                     completions) &&
      statuses(
        completions,
        { GuestServiceStatus::Complete, GuestServiceStatus::Complete }) &&
      audio.clips.size() == 1 && audio.plays.size() == 1 &&
      audio.plays[0].playback.volume == 0.25f && services.audioSounds() == 1,
    "A sound registered and played in one exchange plays");
  testTrue(
    counters,
    services.process(batch({ playRequest(9),
                             createRequest(7),
                             simpleRequest(GuestAudioAction::Destroy, 9) },
                           3),
                     completions) &&
      statuses(completions,
               { GuestServiceStatus::Rejected,
                 GuestServiceStatus::Rejected,
                 GuestServiceStatus::Rejected }) &&
      audio.clips.size() == 1,
    "Unknown and duplicate sound ids are rejected, not fatal");
  GuestAudioRequest volume = simpleRequest(GuestAudioAction::SetVolume);
  volume.volume = 0.5f;
  testTrue(counters,
           services.process(batch({ volume,
                                    simpleRequest(GuestAudioAction::StopAll),
                                    simpleRequest(GuestAudioAction::Destroy, 7),
                                    playRequest(7) },
                                  6),
                            completions) &&
             statuses(completions,
                      { GuestServiceStatus::Complete,
                        GuestServiceStatus::Complete,
                        GuestServiceStatus::Complete,
                        GuestServiceStatus::Rejected }) &&
             audio.master == 0.5f && audio.stops == 1 &&
             audio.destroyed.size() == 1 && services.audioSounds() == 0,
           "Volume, stop and release reach the output; a released id is stale");

  GuestServices malformed;
  malformed.records.push_back({ 10,
                                GuestService::Audio,
                                GuestServiceStatus::Request,
                                encode(createRequest(8)) });
  std::vector<std::byte> broken = encode(playRequest(8));
  broken.push_back(std::byte{ 0 });
  malformed.records.push_back(
    { 11, GuestService::Audio, GuestServiceStatus::Request, broken });
  GuestWireWriter malformedBatch;
  malformed.write(malformedBatch);
  testTrue(counters,
           !services.process(malformedBatch.data(), completions) &&
             audio.clips.size() == 1 &&
             services.error() == "Invalid audio request",
           "A malformed audio request fails the exchange before any effect");

  // A fresh session: registered sounds are released when the guest retires.
  RecordingAudio second;
  WasmGameServices retiring(host.bridge, grant, ILLUMO_ENGINE_ASSETS);
  retiring.setAudio(&second);
  retiring.process(batch({ createRequest(1), createRequest(2), volume }, 1),
                   completions);
  retiring.cancel();
  testTrue(counters,
           second.destroyed.size() == 2 && second.stops == 1 &&
             second.master == 1.0f && retiring.audioSounds() == 0,
           "Cancel releases the guest's sounds and restores the volume");

  // A sound in chunks: registered only when its last sample lands.
  RecordingAudio chunked;
  WasmGameServices uploads(host.bridge, grant, ILLUMO_ENGINE_ASSETS);
  uploads.setAudio(&chunked);
  const GuestAudioRequest head = createRequest(4, 64, 192);
  const GuestAudioRequest middle = appendRequest(4);
  const GuestAudioRequest tail = appendRequest(4);
  testTrue(
    counters,
    uploads.process(batch({ head, playRequest(4), middle }, 1), completions) &&
      statuses(completions,
               { GuestServiceStatus::Complete,
                 GuestServiceStatus::Rejected,
                 GuestServiceStatus::Complete }) &&
      chunked.clips.empty() && uploads.audioUploads() == 1 &&
      uploads.audioSounds() == 0,
    "An arriving sound cannot play until its last chunk lands");
  GuestAudioRequest loop = playRequest(4);
  loop.flags = GuestAudioRequest::LoopFlag;
  loop.fade = 2.0f;
  std::vector<float> whole = head.samples;
  whole.insert(whole.end(), middle.samples.begin(), middle.samples.end());
  whole.insert(whole.end(), tail.samples.begin(), tail.samples.end());
  testTrue(counters,
           uploads.process(batch({ tail, loop }, 4), completions) &&
             statuses(completions,
                      { GuestServiceStatus::Complete,
                        GuestServiceStatus::Complete }) &&
             chunked.clips.size() == 1 && chunked.clips[0].samples == whole &&
             uploads.audioUploads() == 0 && uploads.audioSounds() == 1 &&
             chunked.plays.size() == 1 && chunked.plays[0].playback.loop &&
             chunked.plays[0].playback.fadeInSeconds == 2.0f,
           "The last chunk registers the whole sound, which then loops");
  GuestAudioRequest fadeOut = simpleRequest(GuestAudioAction::Stop, 4);
  fadeOut.fade = 0.8f;
  GuestAudioRequest softer = simpleRequest(GuestAudioAction::SetSoundVolume, 4);
  softer.volume = 0.2f;
  testTrue(counters,
           uploads.process(batch({ softer,
                                   fadeOut,
                                   appendRequest(4),
                                   simpleRequest(GuestAudioAction::Stop, 9) },
                                 6),
                           completions) &&
             statuses(completions,
                      { GuestServiceStatus::Complete,
                        GuestServiceStatus::Complete,
                        GuestServiceStatus::Rejected,
                        GuestServiceStatus::Rejected }) &&
             chunked.volumes.size() == 1 && chunked.volumes[0].volume == 0.2f &&
             chunked.soundStops.size() == 1 &&
             chunked.soundStops[0].fadeSeconds == 0.8f &&
             chunked.clips.size() == 1,
           "Sound volume and fading stops reach the output; a complete sound "
           "takes no more chunks");
  testTrue(counters,
           uploads.process(batch({ createRequest(5, 64, 128),
                                   appendRequest(5, 128),
                                   simpleRequest(GuestAudioAction::Destroy, 5),
                                   appendRequest(5) },
                                 10),
                           completions) &&
             statuses(completions,
                      { GuestServiceStatus::Complete,
                        GuestServiceStatus::Rejected,
                        GuestServiceStatus::Complete,
                        GuestServiceStatus::Rejected }) &&
             uploads.audioUploads() == 0 && chunked.clips.size() == 1,
           "A chunk past the declared total is refused and an arriving sound "
           "can be released");

  // The per-guest sample budget counts arriving sounds at their whole size.
  RecordingAudio third;
  WasmGameServices budgeted(host.bridge, grant, ILLUMO_ENGINE_ASSETS);
  budgeted.setAudio(&third);
  std::uint32_t accepted = 0;
  for (std::uint32_t sound = 1; sound <= 3; ++sound) {
    if (budgeted.process(
          batch({ createRequest(sound, 64, AudioClip::kMaximumSamples) },
                sound),
          completions) &&
        statuses(completions, { GuestServiceStatus::Complete })) {
      ++accepted;
    }
  }
  testEqInt(counters,
            static_cast<int>(accepted),
            static_cast<int>(WasmGameServices::kMaximumGuestSamples /
                             AudioClip::kMaximumSamples),
            "Registered and arriving samples are bounded per guest");
  testTrue(counters,
           budgeted.process(batch({ simpleRequest(GuestAudioAction::Destroy, 1),
                                    createRequest(3) },
                                  4),
                            completions) &&
             statuses(completions,
                      { GuestServiceStatus::Complete,
                        GuestServiceStatus::Complete }) &&
             budgeted.audioSounds() == 1,
           "Releasing an arriving sound returns its budget");
  budgeted.setAudio(nullptr);
  testTrue(counters,
           third.destroyed.size() == 1 && budgeted.audioSounds() == 0 &&
             budgeted.audioUploads() == 0,
           "Withdrawing the output releases the guest's sounds and uploads");
  return counters.failures == 0;
}

// Host completions, then the guest's queued requests.
static bool
exchange(GuestServiceQueue& queue,
         WasmGameServices& services,
         std::vector<std::byte>& completions)
{
  std::vector<std::byte> requests;
  return queue.exchange(completions, requests) &&
         services.process(requests, completions);
}

static bool
testGuestAudio()
{
  TestCounters counters;
  AudioHost host;
  RecordingAudio audio;
  WasmGameServices services(host.bridge,
                            static_cast<std::uint32_t>(GuestCapability::Audio),
                            ILLUMO_ENGINE_ASSETS);
  services.setAudio(&audio);
  GuestServiceQueue queue;
  GuestAudio guest(queue);
  AudioClip clip;
  clip.channels = 2;
  clip.sampleRate = 48000;
  clip.samples = makeTone(256, 2, 48000);

  testTrue(counters,
           !guest.available() && !guest.createSound(clip).isValid() &&
             !queue.hasOutgoing(),
           "Without the grant GuestAudio is unavailable and sends nothing");
  guest.setGranted(true);
  const SoundHandle sound = guest.createSound(clip);
  SoundPlayback loud;
  loud.volume = 7.0f;
  loud.pan = -3.0f;
  testTrue(counters,
           sound.isValid() && guest.play(sound, loud) && queue.hasOutgoing(),
           "A granted sound is registered and played");
  GuestWireWriter empty;
  GuestServices{}.write(empty);
  std::vector<std::byte> completions = empty.take();
  testTrue(counters,
           exchange(queue, services, completions) && audio.clips.size() == 1 &&
             audio.clips[0].samples == clip.samples &&
             audio.plays.size() == 1 &&
             audio.plays[0].playback.volume == 1.0f &&
             audio.plays[0].playback.pan == -1.0f,
           "Samples arrive intact and playback values arrive clamped");
  guest.destroySound(sound);
  testTrue(counters,
           !guest.play(sound) && exchange(queue, services, completions) &&
             audio.destroyed.size() == 1,
           "A destroyed guest handle is stale and the host releases it");
  // Completions are discarded by the queue, so plays never pile up against
  // its outstanding-request bound.
  const SoundHandle again = guest.createSound(clip);
  exchange(queue, services, completions);
  bool queued = true;
  for (int round = 0; round < 4 && queued; ++round) {
    for (int play = 0; play < 16; ++play) {
      queued = queued && guest.play(again);
    }
    queued = queued && exchange(queue, services, completions);
  }
  testTrue(counters,
           queued && audio.plays.size() == 65,
           "Audio completions are drained, never taken");
  guest.setMasterVolume(0.4f);
  exchange(queue, services, completions);
  testTrue(counters,
           guest.masterVolume() == 0.4f && audio.master == 0.4f,
           "Master volume reaches the host");

  // A clip of three chunks: one exchange cannot carry two, so the later
  // chunks and the play made after them wait in order in the backlog.
  AudioClip music;
  music.channels = 1;
  music.sampleRate = 44100;
  music.samples.assign(GuestAudioRequest::MaximumChunkSamples * 2u + 10u, 0.0f);
  for (std::size_t index = 0; index < music.samples.size(); index += 997u) {
    music.samples[index] = static_cast<float>(index % 13u) / 13.0f;
  }
  const SoundHandle track = guest.createSound(music);
  SoundPlayback looping;
  looping.loop = true;
  looping.fadeInSeconds = 99.0f;
  testTrue(counters,
           track.isValid() && guest.play(track, looping) &&
             guest.backlog() == 3,
           "A large clip's later chunks and the play behind them wait");
  const std::size_t clipsBefore = audio.clips.size();
  const std::size_t playsBefore = audio.plays.size();
  bool flowing = true;
  for (int round = 0; round < 4 && flowing; ++round) {
    flowing = exchange(queue, services, completions);
    guest.pump();
  }
  testTrue(counters,
           flowing && guest.backlog() == 0 &&
             audio.clips.size() == clipsBefore + 1u &&
             audio.clips.back().samples == music.samples &&
             audio.plays.size() == playsBefore + 1u &&
             audio.plays.back().playback.loop &&
             audio.plays.back().playback.fadeInSeconds ==
               SoundPlayback::kMaximumFadeSeconds,
           "The chunks arrive over several exchanges and the looping play "
           "follows the whole sound, its fade clamped");
  guest.setVolume(track, 0.25f);
  guest.stop(track, 0.5f);
  exchange(queue, services, completions);
  testTrue(counters,
           audio.volumes.size() == 1 && audio.volumes[0].volume == 0.25f &&
             audio.soundStops.size() == 1 &&
             audio.soundStops[0].fadeSeconds == 0.5f,
           "Sound volume and a fading stop reach the host");
  return counters.failures == 0;
}

bool
runWasmAudioTest(const std::string& name)
{
  if (name == "AudioServiceDecoder") {
    return testDecoder();
  }
  if (name == "AudioServices") {
    return testServices();
  }
  if (name == "GuestAudio") {
    return testGuestAudio();
  }
  return false;
}
