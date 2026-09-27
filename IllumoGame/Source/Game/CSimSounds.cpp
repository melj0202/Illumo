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

constexpr std::size_t kTrackCount = static_cast<std::size_t>(CSimMusic::Count);

// Music sits under the cues so menu sounds stay clear over it.
constexpr std::array<Cue, kTrackCount> kTracks = { {
  { "Music/music_main_menu.mp3", 0.45f },
} };

struct Bank
{
  IAudio* audio = nullptr;
  IEnvVars* settings = nullptr;
  std::array<SoundHandle, kCueCount> sounds{};
  std::array<SoundHandle, kTrackCount> music{};
  std::array<std::uint64_t, kCueCount> counts{};
  // The track asked for and not stopped; kTrackCount for none.
  std::size_t track = kTrackCount;
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

std::size_t
indexOf(CSimMusic track)
{
  return std::min(static_cast<std::size_t>(track), kTrackCount - 1);
}

SoundHandle
load(IAudio& audio,
     const CSimSounds::ReadFile& read,
     const std::string& path,
     std::vector<std::string>& problems)
{
  std::vector<std::byte> bytes;
  if (!read || !read(path, bytes)) {
    problems.push_back(path + " is missing");
    return {};
  }
  AudioClip clip;
  std::string error;
  if (!AudioDecoder::decode(bytes, clip, error)) {
    problems.push_back(path + ": " + error);
    return {};
  }
  const SoundHandle sound = audio.createSound(clip);
  if (!sound.isValid()) {
    problems.push_back(path + " could not be registered");
  }
  return sound;
}

float
musicVolume(const Bank& state, int volumePercent)
{
  return kTracks[state.track].level *
         static_cast<float>(std::clamp(volumePercent, 0, 100)) / 100.0f;
}

int
percentSetting(IEnvVars* settings, const char* name)
{
  if (settings == nullptr) {
    return CSimSounds::kDefaultVolume;
  }
  const std::string text = settings->getVar(name).value;
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result result =
    std::from_chars(text.data(), end, value);
  if (text.empty() || result.ec != std::errc() || result.ptr != end) {
    return CSimSounds::kDefaultVolume;
  }
  return std::clamp(value, 0, 100);
}
} // namespace

const char*
CSimSounds::fileName(CSimSound cue)
{
  return kCues[indexOf(cue)].file;
}

const char*
CSimSounds::fileName(CSimMusic track)
{
  return kTracks[indexOf(track)].file;
}

std::vector<std::string>
CSimSounds::fileNames()
{
  std::vector<std::string> names;
  for (const Cue& cue : kCues) {
    names.emplace_back(cue.file);
  }
  for (const Cue& track : kTracks) {
    names.emplace_back(track.file);
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
    state.sounds[index] = load(*audio, read, kCues[index].file, problems);
  }
  for (std::size_t index = 0; index < kTrackCount; ++index) {
    state.music[index] = load(*audio, read, kTracks[index].file, problems);
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
    for (SoundHandle& sound : state.music) {
      state.audio->destroySound(sound);
      sound = {};
    }
  }
  state.audio = nullptr;
  state.settings = nullptr;
  state.track = kTrackCount;
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

void
CSimSounds::playMusic(CSimMusic track)
{
  Bank& state = bank();
  const std::size_t index = indexOf(track);
  if (state.track == index) {
    return;
  }
  stopMusic();
  state.track = index;
  if (state.audio == nullptr || !state.music[index].isValid()) {
    return;
  }
  // Started even at volume 0, so raising musicVolume later brings it in.
  SoundPlayback playback;
  playback.volume = musicVolume(state, musicVolumeSetting(state.settings));
  playback.loop = true;
  playback.fadeInSeconds = kMusicFadeInSeconds;
  state.audio->play(state.music[index], playback);
}

void
CSimSounds::stopMusic()
{
  Bank& state = bank();
  if (state.track == kTrackCount) {
    return;
  }
  if (state.audio != nullptr) {
    state.audio->stop(state.music[state.track], kMusicFadeOutSeconds);
  }
  state.track = kTrackCount;
}

void
CSimSounds::refreshMusicVolume()
{
  previewMusicVolume(musicVolumeSetting(bank().settings));
}

void
CSimSounds::previewMusicVolume(int volumePercent)
{
  const Bank& state = bank();
  if (state.track != kTrackCount && state.audio != nullptr) {
    state.audio->setVolume(state.music[state.track],
                           musicVolume(state, volumePercent));
  }
}

bool
CSimSounds::musicPlaying(CSimMusic track)
{
  return bank().track == indexOf(track);
}

int
CSimSounds::volumeSetting(IEnvVars* settings)
{
  return percentSetting(settings, "soundVolume");
}

int
CSimSounds::musicVolumeSetting(IEnvVars* settings)
{
  return percentSetting(settings, "musicVolume");
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
