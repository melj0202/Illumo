#include <Illumo/Audio/AudioClip.h>
#include <Illumo/Audio/AudioDevice.h>
#include <Illumo/Testing/AudioFixtures.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <limits>
#include <memory>

// Mean absolute sample value of `frames` frames pulled from a headless mix.
static double
renderedLevel(AudioDevice& device, std::size_t frames)
{
  std::vector<float> output(frames * device.channels());
  const std::size_t rendered = device.render(output);
  double sum = 0.0;
  for (std::size_t index = 0; index < rendered * device.channels(); ++index) {
    sum += std::abs(output[index]);
  }
  return rendered == 0 ? 0.0 : sum / static_cast<double>(output.size());
}

static std::unique_ptr<AudioDevice>
headlessDevice(TestCounters& counters)
{
  AudioDeviceOptions options;
  options.headless = true;
  std::string error;
  std::unique_ptr<AudioDevice> device = AudioDevice::create(options, error);
  testTrue(counters, device != nullptr, "headless audio device opens");
  if (!error.empty()) {
    std::printf("  error: %s\n", error.c_str());
  }
  return device;
}

static int
testDecodeWav()
{
  TestCounters counters;
  AudioClip clip;
  std::string error;
  const std::vector<float> mono = makeTone(1000, 1, 22050);
  testTrue(counters,
           AudioDecoder::decode(makeWav(mono, 1, 22050), clip, error),
           "16-bit PCM mono WAV decodes");
  testEqInt(counters, static_cast<int>(clip.channels), 1, "mono channels");
  testEqInt(
    counters, static_cast<int>(clip.sampleRate), 22050, "source rate kept");
  testEqSize(counters, clip.frames(), 1000, "every frame decoded");
  testTrue(counters,
           std::abs(clip.samples[100] - mono[100]) < 1.0f / 16000.0f,
           "PCM samples convert to float within one step");

  const std::vector<float> stereo = makeTone(480, 2, 48000, 1000.0f, 0.25f);
  testTrue(counters,
           AudioDecoder::decode(makeWav(stereo, 2, 48000, 3), clip, error),
           "32-bit float stereo WAV decodes");
  testEqInt(counters, static_cast<int>(clip.channels), 2, "stereo channels");
  testTrue(counters,
           clip.samples.size() == stereo.size() &&
             clip.samples[301] == stereo[301],
           "float samples are exact");

  testTrue(
    counters,
    AudioDecoder::decode(makeWav(mono, 1, 44100, 1, true), clip, error) &&
      clip.frames() == 1000,
    "an unknown chunk before fmt is skipped");
  testTrue(counters, clip.valid(), "decoded clips are valid");

  const std::vector<float> quad = makeTone(300, 4, 32000);
  testTrue(counters,
           AudioDecoder::decode(makeWav(quad, 4, 32000), clip, error) &&
             clip.channels == 2 && clip.frames() == 300,
           "a four-channel source is mixed down to stereo");
  double level = 0.0;
  for (float sample : clip.samples) {
    level += std::abs(sample);
  }
  testTrue(counters, level > 1.0, "the stereo mix keeps the signal");
  return counters.failures;
}

static int
testDecodeRejects()
{
  TestCounters counters;
  AudioClip clip;
  clip.channels = 1;
  clip.sampleRate = 8000;
  clip.samples = { 0.5f };
  std::string error;
  testTrue(counters,
           !AudioDecoder::decode({}, clip, error) && !error.empty(),
           "empty input is rejected with a reason");
  std::vector<std::byte> garbage(256, std::byte{ 0x5a });
  testTrue(counters,
           !AudioDecoder::decode(garbage, clip, error),
           "unrecognized bytes are rejected");
  std::vector<std::byte> truncated = makeWav(makeTone(100, 1, 22050), 1, 22050);
  truncated.resize(20);
  testTrue(counters,
           !AudioDecoder::decode(truncated, clip, error),
           "a truncated header is rejected");
  testTrue(counters,
           !AudioDecoder::decode(
             makeWav(std::vector<float>(64, 0.0f), 1, 4000), clip, error),
           "a rate below 8 kHz is rejected");
  std::vector<float> tooLong(AudioClip::kMaximumSamples + 2, 0.0f);
  testTrue(counters,
           !AudioDecoder::decode(makeWav(tooLong, 2, 48000), clip, error),
           "a clip beyond the sample bound is rejected");
  testTrue(counters,
           clip.channels == 1 && clip.samples.size() == 1,
           "a failed decode leaves the clip unchanged");
  return counters.failures;
}

static int
testClipValidity()
{
  TestCounters counters;
  AudioClip clip;
  clip.channels = 2;
  clip.sampleRate = 44100;
  clip.samples = { 0.1f, -0.1f, 0.2f, -0.2f };
  testTrue(counters, clip.valid(), "whole stereo frames are valid");
  clip.samples.push_back(0.3f);
  testTrue(counters, !clip.valid(), "a partial frame is invalid");
  clip.samples.pop_back();
  clip.samples[1] = std::numeric_limits<float>::quiet_NaN();
  testTrue(counters, !clip.valid(), "non-finite samples are invalid");
  clip.samples[1] = 0.0f;
  clip.channels = 3;
  testTrue(counters, !clip.valid(), "more than two channels is invalid");
  clip.channels = 2;
  clip.sampleRate = 200000;
  testTrue(counters, !clip.valid(), "rates above 192 kHz are invalid");
  clip.sampleRate = 44100;
  clip.samples.clear();
  testTrue(counters, !clip.valid(), "an empty clip is invalid");
  return counters.failures;
}

static int
testDeviceMixes()
{
  TestCounters counters;
  std::unique_ptr<AudioDevice> device = headlessDevice(counters);
  if (!device) {
    return counters.failures;
  }
  testEqInt(counters, static_cast<int>(device->channels()), 2, "stereo mix");
  testEqInt(
    counters, static_cast<int>(device->sampleRate()), 48000, "48 kHz mix");
  testTrue(
    counters, renderedLevel(*device, 480) == 0.0, "an idle mix is silent");

  AudioClip clip;
  clip.channels = 1;
  clip.sampleRate = 22050;
  clip.samples = makeTone(22050 / 10, 1, 22050);
  const SoundHandle tone = device->createSound(clip);
  testTrue(counters, tone.isValid(), "a valid clip becomes a sound");
  testEqInt(counters, static_cast<int>(device->soundCount()), 1, "one sound");
  testTrue(counters, device->play(tone), "the sound plays");
  testEqInt(counters,
            static_cast<int>(device->activeVoices()),
            1,
            "one voice is active");
  testTrue(counters,
           renderedLevel(*device, 480) > 0.05,
           "a resampled mono voice is heard in the stereo mix");
  renderedLevel(*device, 48000 / 5);
  testEqInt(counters,
            static_cast<int>(device->activeVoices()),
            0,
            "a voice finishes at the end of its sound");

  device->setMasterVolume(0.0f);
  device->play(tone);
  testTrue(counters,
           renderedLevel(*device, 480) == 0.0,
           "master volume 0 silences every voice");
  device->setMasterVolume(std::numeric_limits<float>::quiet_NaN());
  testTrue(counters,
           device->masterVolume() == 1.0f,
           "a non-finite master volume falls back to full");
  SoundPlayback quiet;
  quiet.volume = 0.0f;
  quiet.pitch = std::numeric_limits<float>::infinity();
  device->stopAll();
  testTrue(counters, device->play(tone, quiet), "odd playback values clamp");
  testTrue(counters,
           renderedLevel(*device, 480) == 0.0,
           "a voice at volume 0 is silent");
  device->stopAll();
  testEqInt(counters,
            static_cast<int>(device->activeVoices()),
            0,
            "stopAll ends every voice");
  return counters.failures;
}

static int
testDeviceLifetimes()
{
  TestCounters counters;
  std::unique_ptr<AudioDevice> device = headlessDevice(counters);
  if (!device) {
    return counters.failures;
  }
  AudioClip clip;
  clip.channels = 2;
  clip.sampleRate = 48000;
  clip.samples = makeTone(48000, 2, 48000);
  const SoundHandle first = device->createSound(clip);
  for (std::uint32_t index = 0; index < IAudio::kMaximumVoices + 8; ++index) {
    device->play(first);
  }
  testEqInt(counters,
            static_cast<int>(device->activeVoices()),
            static_cast<int>(IAudio::kMaximumVoices),
            "extra plays replace the oldest voices");
  device->destroySound(first);
  testEqInt(counters,
            static_cast<int>(device->activeVoices()),
            0,
            "destroying a sound stops its voices");
  testTrue(
    counters, !device->play(first), "a destroyed sound's handle is stale");
  const SoundHandle second = device->createSound(clip);
  testTrue(counters,
           second.slot == first.slot && second != first,
           "a reused slot gets a new generation");
  device->destroySound(first);
  testEqInt(counters,
            static_cast<int>(device->soundCount()),
            1,
            "a stale destroy is ignored");
  testTrue(counters,
           !device->createSound(AudioClip{}).isValid(),
           "an invalid clip is refused");
  clip.samples = { 0.0f, 0.0f };
  std::uint32_t created = device->soundCount();
  while (device->createSound(clip).isValid()) {
    ++created;
  }
  testEqInt(counters,
            static_cast<int>(created),
            static_cast<int>(IAudio::kMaximumSounds),
            "the sound table is bounded");
  AudioDeviceOptions options;
  options.headless = true;
  options.channels = 6;
  std::string error;
  testTrue(counters,
           AudioDevice::create(options, error) == nullptr && !error.empty(),
           "an unsupported headless format is refused");
  return counters.failures;
}

void
registerAudioTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Audio.DecodeWav", []() { return testDecodeWav(); });
  registry.add("Illumo.Audio.DecodeRejects",
               []() { return testDecodeRejects(); });
  registry.add("Illumo.Audio.ClipValidity",
               []() { return testClipValidity(); });
  registry.add("Illumo.Audio.DeviceMixes", []() { return testDeviceMixes(); });
  registry.add("Illumo.Audio.DeviceLifetimes",
               []() { return testDeviceLifetimes(); });
}
