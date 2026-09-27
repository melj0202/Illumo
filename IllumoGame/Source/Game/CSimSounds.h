#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class IAudio;
class IEnvVars;

// CSim's sound cues, one file each under the package's Sounds/ directory.
enum class CSimSound
{
  ProgramStart,
  MenuHover,
  MenuSelect,
  MenuBack,
  MenuError,
  CanvasEnter,
  CanvasExit,
  // The canvas switched between EDIT and NORMAL (running).
  CanvasModeSwitch,
  // The paint drawer at the bottom of the canvas opened or closed.
  CanvasPaintMenuExpand,
  CanvasPaintMenuCollapse,
  Count
};

// CSim's looping music, one file each under the package's Music/ directory.
enum class CSimMusic
{
  MainMenu,
  Count
};

// Product sound effects and music over the engine's IAudio. Menus and
// dialogs without an IllumoContext fire cues too, so the loaded bank is
// installed once for the store's lifetime, as CSimPlatform::current() is.
// With no output installed every cue is silent but still counted, and the
// requested music is still tracked, which the tests read.
class CSimSounds
{
public:
  static constexpr int kDefaultVolume = 80;
  static constexpr float kMusicFadeInSeconds = 2.0f;
  static constexpr float kMusicFadeOutSeconds = 0.8f;
  using ReadFile =
    std::function<bool(const std::string& path, std::vector<std::byte>& bytes)>;

  // Package-relative file of a cue or music track.
  static const char* fileName(CSimSound cue);
  static const char* fileName(CSimMusic track);
  // Every file the bank loads: the cues, then the music.
  static std::vector<std::string> fileNames();
  // Decodes each file through `read` and registers it with `audio`,
  // replacing any earlier bank. `settings` supplies soundVolume when cues
  // play and musicVolume when music does. A missing or undecodable file leaves
  // that cue or track silent and adds a line to `problems`.
  static void install(IAudio* audio,
                      IEnvVars* settings,
                      const ReadFile& read,
                      std::vector<std::string>& problems);
  // Releases the bank's sounds and forgets the output.
  static void uninstall();
  static bool installed();

  // Plays a cue at its mix level scaled by the soundVolume setting.
  static void play(CSimSound cue);
  // Plays at an explicit 0..100 volume (the settings preview).
  static void playAt(CSimSound cue, int volumePercent);

  // Loops `track` at its mix level scaled by musicVolume, fading in. Asking
  // for the track already playing changes nothing; another track stops first.
  static void playMusic(CSimMusic track);
  // Fades the playing track out.
  static void stopMusic();
  // Brings the playing track to the current musicVolume.
  static void refreshMusicVolume();
  // Plays the playing track at an explicit 0..100 volume until the next
  // refresh (the settings preview).
  static void previewMusicVolume(int volumePercent);
  // The track last asked for and not stopped, installed or not (tests).
  static bool musicPlaying(CSimMusic track);

  // soundVolume clamped to 0..100; kDefaultVolume when unset or malformed.
  static int volumeSetting(IEnvVars* settings);
  // musicVolume, likewise.
  static int musicVolumeSetting(IEnvVars* settings);
  // Cues requested since the last reset, installed or not (tests).
  static std::uint64_t playCount(CSimSound cue);
  static void resetCounts();
};
