#include "Game/CSimSounds.h"
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/AudioFixtures.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <map>
#include <string>

static bool
near(float a, float b)
{
  return std::abs(a - b) < 1e-5f;
}

static int
testSoundBank()
{
  TestCounters counters;
  CSimSounds::uninstall();
  CSimSounds::resetCounts();
  std::map<std::string, std::vector<std::byte>> files;
  for (const std::string& name : CSimSounds::fileNames()) {
    files[name] = makeWav(makeTone(441, 2, 44100), 2, 44100);
  }
  files.erase(CSimSounds::fileName(CSimSound::CanvasExit));
  files[CSimSounds::fileName(CSimSound::MenuError)] =
    std::vector<std::byte>(64, std::byte{ 1 });
  const CSimSounds::ReadFile read = [&files](const std::string& path,
                                             std::vector<std::byte>& bytes) {
    const std::map<std::string, std::vector<std::byte>>::const_iterator found =
      files.find(path);
    if (found == files.end()) {
      return false;
    }
    bytes = found->second;
    return true;
  };

  CSimSounds::play(CSimSound::MenuHover);
  testTrue(counters,
           !CSimSounds::installed() &&
             CSimSounds::playCount(CSimSound::MenuHover) == 1,
           "cues without an output are silent but counted");

  RecordingAudio audio;
  EnvVars settings;
  std::vector<std::string> problems;
  CSimSounds::install(&audio, &settings, read, problems);
  testTrue(counters,
           CSimSounds::installed() && audio.clips.size() == 5 &&
             problems.size() == 2,
           "each decodable file becomes a sound; the others are reported");

  CSimSounds::play(CSimSound::MenuSelect);
  testTrue(counters,
           audio.plays.size() == 1 &&
             near(audio.plays[0].playback.volume, 0.7f * 0.8f),
           "an unset volume setting plays at the default 80%");
  settings.setVar("soundVolume", "50");
  CSimSounds::play(CSimSound::MenuHover);
  testTrue(counters,
           audio.plays.size() == 2 &&
             near(audio.plays[1].playback.volume, 0.35f * 0.5f),
           "hover sits below the other cues and follows soundVolume");
  CSimSounds::play(CSimSound::CanvasExit);
  CSimSounds::play(CSimSound::MenuError);
  testTrue(counters,
           audio.plays.size() == 2 &&
             CSimSounds::playCount(CSimSound::CanvasExit) == 1,
           "cues whose file failed stay silent");
  settings.setVar("soundVolume", "0");
  CSimSounds::play(CSimSound::MenuBack);
  CSimSounds::playAt(CSimSound::MenuBack, 100);
  testTrue(counters,
           audio.plays.size() == 3 &&
             near(audio.plays[2].playback.volume, 0.7f),
           "volume 0 mutes, while a preview plays at its own level");

  settings.setVar("soundVolume", "150");
  testEqInt(counters,
            CSimSounds::volumeSetting(&settings),
            100,
            "volume settings clamp to 100");
  settings.setVar("soundVolume", "-4");
  testEqInt(counters, CSimSounds::volumeSetting(&settings), 0, "and to 0");
  settings.setVar("soundVolume", "loud");
  testEqInt(counters,
            CSimSounds::volumeSetting(&settings),
            CSimSounds::kDefaultVolume,
            "a malformed setting falls back to the default");

  CSimSounds::uninstall();
  testTrue(counters,
           !CSimSounds::installed() && audio.destroyed.size() == 5,
           "uninstall releases every registered sound");
  CSimSounds::resetCounts();
  return counters.failures;
}

void
registerCSimSoundsTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Sounds.Bank", []() { return testSoundBank(); });
}
