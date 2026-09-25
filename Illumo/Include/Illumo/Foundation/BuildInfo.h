#pragma once

// Build version vYY.MM_B. The values are defined in a source file that
// cmake/IllumoVersion.cmake regenerates on every build from VERSION.txt and
// Git, so only that file recompiles when the version changes.
class BuildInfo
{
public:
  // Release month, "26.09": the repository's VERSION.txt.
  static const char* const Release;
  // First-parent commits since VERSION.txt last changed; 0 without Git
  // history.
  static const unsigned BuildNumber;
  // Abbreviated commit, "1f709073", or empty without Git.
  static const char* const Commit;
  // Tracked files differed from Commit when this was built.
  static const bool Dirty;
  // What products show: "v26.09_12".
  static const char* const VersionNumber;
  // What logs and --version show: "v26.09_12 (1f709073, dirty)" or
  // "v26.09_0 (unknown)".
  static const char* const FullVersion;
};
