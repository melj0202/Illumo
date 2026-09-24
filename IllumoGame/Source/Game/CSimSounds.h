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
  Count
};

// Product sound effects over the engine's IAudio. Menus and dialogs without
// an IllumoContext fire cues too, so the loaded bank is installed once for
// the store's lifetime, as CSimPlatform::current() is. With no output
// installed every cue is silent but still counted, which the tests read.
class CSimSounds
{
public:
  static constexpr int kDefaultVolume = 80;
  using ReadFile =
    std::function<bool(const std::string& path, std::vector<std::byte>& bytes)>;

  // Package-relative file of a cue.
  static const char* fileName(CSimSound cue);
  static std::vector<std::string> fileNames();
  // Decodes each cue's file through `read` and registers it with `audio`,
  // replacing any earlier bank. `settings` supplies soundVolume when cues
  // play. A missing or undecodable file leaves that cue silent and adds a
  // line to `problems`.
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

  // soundVolume clamped to 0..100; kDefaultVolume when unset or malformed.
  static int volumeSetting(IEnvVars* settings);
  // Cues requested since the last reset, installed or not (tests).
  static std::uint64_t playCount(CSimSound cue);
  static void resetCounts();
};
