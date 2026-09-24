#include "CSimSounds.h"
#include <Illumo/Audio/Audio.h>
#include <Illumo/Audio/AudioClip.h>
#include <Illumo/Services/IEnvVars.h>
#include <algorithm>
#include <charconv>

namespace {
constexpr std::size_t kCueCount = static_cast<std::size_t>(CSimSound::Count);

struct Cue
{
  const char* file;
  // Mix level before the soundVolume setting: hover fires often, so it sits
  // well below the one-off cues.
  float level;
};

constexpr std::array<Cue, kCueCount> kCues = { {
  { "Sounds/csim_program_start.wav", 0.8f },
  { "Sounds/ui_menu_hover.wav", 0.35f },
  { "Sounds/ui_menu_select.wav", 0.7f },
  { "Sounds/ui_menu_back.wav", 0.7f },
  { "Sounds/ui_menu_error.wav", 0.7f },
  { "Sounds/canvas_enter.wav", 0.8f },
  { "Sounds/canvas_exit.wav", 0.8f },
  // The E key toggles often while editing, so it sits under the one-offs.
  { "Sounds/canvas_mode_switch.wav", 0.6f },
  { "Sounds/canvas_paintmenu_expand.wav", 0.6f },
  { "Sounds/canvas_paintmenu_collapse.wav", 0.6f },
} };

struct Bank
{
  IAudio* audio = nullptr;
  IEnvVars* settings = nullptr;
  std::array<SoundHandle, kCueCount> sounds{};
  std::array<std::uint64_t, kCueCount> counts{};
};

Bank&
bank()
{
  static Bank state;
  return state;
}

std::size_t
indexOf(CSimSound cue)
{
  return std::min(static_cast<std::size_t>(cue), kCueCount - 1);
}
} // namespace

const char*
CSimSounds::fileName(CSimSound cue)
{
  return kCues[indexOf(cue)].file;
}

std::vector<std::string>
CSimSounds::fileNames()
{
  std::vector<std::string> names;
  for (const Cue& cue : kCues) {
    names.emplace_back(cue.file);
  }
  return names;
}

void
CSimSounds::install(IAudio* audio,
                    IEnvVars* settings,
                    const ReadFile& read,
                    std::vector<std::string>& problems)
{
  uninstall();
  if (audio == nullptr || !audio->available()) {
    return;
  }
  Bank& state = bank();
  state.audio = audio;
  state.settings = settings;
  for (std::size_t index = 0; index < kCueCount; ++index) {
    const std::string path = kCues[index].file;
    std::vector<std::byte> bytes;
    if (!read || !read(path, bytes)) {
      problems.push_back(path + " is missing");
      continue;
    }
    AudioClip clip;
    std::string error;
    if (!AudioDecoder::decode(bytes, clip, error)) {
      problems.push_back(path + ": " + error);
      continue;
    }
    state.sounds[index] = audio->createSound(clip);
    if (!state.sounds[index].isValid()) {
      problems.push_back(path + " could not be registered");
    }
  }
}

void
CSimSounds::uninstall()
{
  Bank& state = bank();
  if (state.audio != nullptr) {
    for (SoundHandle& sound : state.sounds) {
      state.audio->destroySound(sound);
      sound = {};
    }
  }
  state.audio = nullptr;
  state.settings = nullptr;
}

bool
CSimSounds::installed()
{
  return bank().audio != nullptr;
}

void
CSimSounds::play(CSimSound cue)
{
  playAt(cue, volumeSetting(bank().settings));
}

void
CSimSounds::playAt(CSimSound cue, int volumePercent)
{
  Bank& state = bank();
  const std::size_t index = indexOf(cue);
  ++state.counts[index];
  const int volume = std::clamp(volumePercent, 0, 100);
  if (state.audio == nullptr || volume == 0 || !state.sounds[index].isValid()) {
    return;
  }
  SoundPlayback playback;
  playback.volume = kCues[index].level * static_cast<float>(volume) / 100.0f;
  state.audio->play(state.sounds[index], playback);
}

int
CSimSounds::volumeSetting(IEnvVars* settings)
{
  if (settings == nullptr) {
    return kDefaultVolume;
  }
  const std::string text = settings->getVar("soundVolume").value;
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result result =
    std::from_chars(text.data(), end, value);
  if (text.empty() || result.ec != std::errc() || result.ptr != end) {
    return kDefaultVolume;
  }
  return std::clamp(value, 0, 100);
}

std::uint64_t
CSimSounds::playCount(CSimSound cue)
{
  return bank().counts[indexOf(cue)];
}

void
CSimSounds::resetCounts()
{
  bank().counts.fill(0);
}
