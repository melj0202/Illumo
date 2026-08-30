#include <Illumo/Services/SysCmdLine.h>

#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Services/IEnvVars.h>
#include <cctype>
#include <charconv>
#include <cstring>
#include <iostream>

static const SysCmdLineOption g_windowOptions[] = {
  { "-ww", "pixels", "WinX", "Render window width" },
  { "-wh", "pixels", "WinY", "Render window height" },
};

static SysCmdLineResult
parsePositiveIntegerOption(int argc,
                           char** argv,
                           int* index,
                           IEnvVars* environment,
                           const SysCmdLineOption& option)
{
  const int valueIndex = *index + 1;
  int value = 0;
  bool valid = valueIndex < argc && argv[valueIndex] != nullptr;
  if (valid) {
    const char* begin = argv[valueIndex];
    const char* end = begin + std::strlen(begin);
    const std::from_chars_result result = std::from_chars(begin, end, value);
    valid = begin != end && result.ec == std::errc() && result.ptr == end &&
            value > 0;
  }
  if (!valid) {
    std::cout << "ERROR: option '" << option.option
              << "' requires a positive integer.\n";
    return { SysCmdLineAction::ExitFailure };
  }
  environment->setVar(option.environmentVariable, argv[valueIndex]);
  *index = valueIndex;
  return {};
}

static SysCmdLineResult
parseStringOption(int argc,
                  char** argv,
                  int* index,
                  IEnvVars* environment,
                  const SysCmdLineOption& option)
{
  const int valueIndex = *index + 1;
  const bool valid = valueIndex < argc && argv[valueIndex] != nullptr &&
                     argv[valueIndex][0] != '\0';
  if (!valid) {
    std::cout << "ERROR: option '" << option.option
              << "' requires a string argument.\n";
    return { SysCmdLineAction::ExitFailure };
  }
  environment->setVar(option.environmentVariable, argv[valueIndex]);
  *index = valueIndex;
  return {};
}

static const SysCmdLineOption*
findOption(const char* argument, const SysCmdLineConfig& config)
{
  for (const SysCmdLineOption& option : g_windowOptions) {
    if (option.option == argument) {
      return &option;
    }
  }
  for (const SysCmdLineOption& option : config.applicationOptions) {
    if (option.option == argument) {
      return &option;
    }
  }
  return nullptr;
}

static void
printIdentity(const SysCmdLineConfig& config)
{
  std::cout << (config.applicationName.empty() ? "Illumo"
                                               : config.applicationName);
  if (!config.description.empty()) {
    std::cout << ' ' << config.description;
  }
  std::cout << '\n';
}

static void
printHelp(const SysCmdLineConfig& config)
{
  printIdentity(config);
  const std::string applicationName =
    config.applicationName.empty() ? "Illumo" : config.applicationName;
  std::cout << "Usage: "
            << (config.usage.empty() ? applicationName + ".exe [OPTION] ..."
                                     : config.usage)
            << "\n\nOptions:\n";
  for (const SysCmdLineOption& option : g_windowOptions) {
    std::cout << option.option << " <" << option.valueName << ">\t "
              << option.description << '\n';
  }
  for (const SysCmdLineOption& option : config.applicationOptions) {
    std::cout << option.option << " <" << option.valueName << ">\t "
              << option.description << '\n';
  }
  std::cout << "-h, --help\t Print this help message\n"
            << "-v, --version\t Print " << applicationName
            << " version information\n";
  for (const std::string& section : config.helpSections) {
    std::cout << '\n' << section;
    if (section.empty() || section.back() != '\n') {
      std::cout << '\n';
    }
  }
}

SysCmdLineResult
SysCmdLine::ParseCommandLine(int argc, char** argv, IEnvVars* environment)
{
  return ParseCommandLine(argc, argv, environment, SysCmdLineConfig{});
}

SysCmdLineResult
SysCmdLine::ParseCommandLine(int argc,
                             char** argv,
                             IEnvVars* environment,
                             const SysCmdLineConfig& config)
{
  if (argc <= 1 || argv == nullptr || environment == nullptr) {
    return {};
  }

  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }
    if (std::strcmp(argv[i], "-h") == 0 ||
        std::strcmp(argv[i], "--help") == 0) {
      printHelp(config);
      return { SysCmdLineAction::ExitSuccess };
    }
    if (std::strcmp(argv[i], "-v") == 0 ||
        std::strcmp(argv[i], "--version") == 0) {
      printIdentity(config);
      std::cout << "Version: " << BuildInfo::VersionNumber
                << "\nBuild Date: " << BuildInfo::BuildDateShort << ' '
                << BuildInfo::BuildTimestamp << '\n';
      return { SysCmdLineAction::ExitSuccess };
    }

    const SysCmdLineOption* option = findOption(argv[i], config);
    if (option != nullptr) {
      if (option->environmentVariable.empty()) {
        std::cout << "ERROR: option '" << option->option
                  << "' has no environment target.\n";
        return { SysCmdLineAction::ExitFailure };
      }
      if (option->valueName == "path" || option->valueName == "string" ||
          option->valueName == "file") {
        const SysCmdLineResult result =
          parseStringOption(argc, argv, &i, environment, *option);
        if (result.shouldExit()) {
          return result;
        }
      } else {
        const SysCmdLineResult result =
          parsePositiveIntegerOption(argc, argv, &i, environment, *option);
        if (result.shouldExit()) {
          return result;
        }
      }
    } else if (argv[i][0] != '-' &&
               !config.positionalEnvironmentVariable.empty()) {
      if (environment->getVar(config.positionalEnvironmentVariable)
            .value.empty()) {
        environment->setVar(config.positionalEnvironmentVariable, argv[i]);
      }
    }
  }
  return {};
}

bool
SysCmdLine::StringIsDigit(const char* text)
{
  if (text == nullptr) {
    return false;
  }
  const std::size_t size = std::strlen(text);
  for (std::size_t i = 0; i < size; ++i) {
    if (std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
      return false;
    }
  }
  return true;
}

bool
SysCmdLine::StringisModeString(const char* text)
{
  if (text == nullptr) {
    return false;
  }
  const std::size_t size = std::strlen(text);
  for (std::size_t i = 0; i < size; ++i) {
    const unsigned char character = static_cast<unsigned char>(text[i]);
    if (std::isalpha(character) == 0 && character != '_') {
      return false;
    }
  }
  return true;
}
