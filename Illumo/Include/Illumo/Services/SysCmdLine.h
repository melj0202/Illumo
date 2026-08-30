#pragma once

#include <string>
#include <vector>

class IEnvVars;

enum class SysCmdLineAction
{
  Continue,
  ExitSuccess,
  ExitFailure
};

struct SysCmdLineResult
{
  SysCmdLineAction action{ SysCmdLineAction::Continue };

  bool shouldExit() const { return action != SysCmdLineAction::Continue; }
  int exitCode() const
  {
    return action == SysCmdLineAction::ExitFailure ? 1 : 0;
  }
};

struct SysCmdLineOption
{
  std::string option;
  std::string valueName;
  std::string environmentVariable;
  std::string description;
};

struct SysCmdLineConfig
{
  std::string applicationName{ "Illumo" };
  std::string description;
  std::string usage;
  std::string positionalEnvironmentVariable;
  std::vector<SysCmdLineOption> applicationOptions;
  std::vector<std::string> helpSections;
};

class SysCmdLine
{
public:
  static SysCmdLineResult ParseCommandLine(int argc,
                                           char** argv,
                                           IEnvVars* environment);
  static SysCmdLineResult ParseCommandLine(int argc,
                                           char** argv,
                                           IEnvVars* environment,
                                           const SysCmdLineConfig& config);

  static bool StringIsDigit(const char* text);
  static bool StringisModeString(const char* text);
};
