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

static GuestAudioRequest
createRequest(std::uint32_t sound, std::size_t frames = 64)
{
  GuestAudioRequest request;
  request.action = GuestAudioAction::Create;
  request.sound = sound;
  request.channels = 1;
  request.sampleRate = 22050;
  request.samples = makeTone(frames, 1, 22050);
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

  std::vector<std::byte> bytes = encode(playRequest(7));
  bytes[0] = std::byte{ 2 };
  testTrue(counters,
           !GuestAudioRequest::read(bytes, decoded),
           "An unknown version is rejected");
  bytes = encode(playRequest(7));
  bytes[4] = std::byte{ 6 };
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

  // The per-guest sample budget: the sixth maximal clip does not fit.
  RecordingAudio third;
  WasmGameServices budgeted(host.bridge, grant, ILLUMO_ENGINE_ASSETS);
  budgeted.setAudio(&third);
  std::uint32_t accepted = 0;
  for (std::uint32_t sound = 1; sound <= 6; ++sound) {
    GuestAudioRequest large = createRequest(sound, 0);
    large.samples.assign(AudioClip::kMaximumSamples, 0.0f);
    if (budgeted.process(batch({ large }, sound), completions) &&
        statuses(completions, { GuestServiceStatus::Complete })) {
      ++accepted;
    }
  }
  testEqInt(counters,
            static_cast<int>(accepted),
            static_cast<int>(WasmGameServices::kMaximumGuestSamples /
                             AudioClip::kMaximumSamples),
            "Registered samples are bounded per guest");
  budgeted.setAudio(nullptr);
  testTrue(counters,
           third.destroyed.size() == accepted && budgeted.audioSounds() == 0,
           "Withdrawing the output releases the guest's sounds");
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
