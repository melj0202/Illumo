#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/SysCmdLine.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static TestCounters g;

class StandardOutputCapture
{
public:
  StandardOutputCapture()
    : m_previous(std::cout.rdbuf(m_output.rdbuf()))
  {
  }

  ~StandardOutputCapture() { std::cout.rdbuf(m_previous); }

  StandardOutputCapture(const StandardOutputCapture&) = delete;
  StandardOutputCapture& operator=(const StandardOutputCapture&) = delete;

  std::string text() const { return m_output.str(); }

private:
  std::ostringstream m_output;
  std::streambuf* m_previous;
};

static std::filesystem::path
environmentPath(const char* name)
{
  return std::filesystem::temp_directory_path() /
         (std::string("illumo-syscmdline-") + name + ".json");
}

static void
testArgumentValidators()
{
  testSection("SysCmdLine: token validators");
  char digits[] = "123456";
  char empty[] = "";
  char mixed[] = "12x";
  char mode[] = "GAME_OF_LIFE";
  char lowerMode[] = "seeds";
  char invalidMode[] = "RULE-90";
  testTrue(g, SysCmdLine::StringIsDigit(digits), "decimal digits accepted");
  testTrue(g,
           SysCmdLine::StringIsDigit(empty),
           "empty token preserves existing validator behavior");
  testTrue(
    g, !SysCmdLine::StringIsDigit(mixed), "mixed numeric token rejected");
  testTrue(g, SysCmdLine::StringisModeString(mode), "underscore mode accepted");
  testTrue(g,
           SysCmdLine::StringisModeString(lowerMode),
           "alphabetic lowercase mode accepted");
  testTrue(g,
           !SysCmdLine::StringisModeString(invalidMode),
           "punctuated mode rejected");
  testTrue(g, !SysCmdLine::StringIsDigit(nullptr), "null digit token rejected");
  testTrue(
    g, !SysCmdLine::StringisModeString(nullptr), "null mode token rejected");
}

static void
testWindowDimensionOptions()
{
  testSection("SysCmdLine: engine window dimension options");
  const std::filesystem::path path = environmentPath("window");
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    EnvVars environment(path);
    char executable[] = "Host";
    char ww[] = "-ww";
    char width[] = "1024";
    char wh[] = "-wh";
    char height[] = "768";
    char* arguments[] = { executable, ww, width, wh, height };
    const SysCmdLineResult result =
      SysCmdLine::ParseCommandLine(5, arguments, &environment);
    testTrue(g, !result.shouldExit(), "valid dimensions continue startup");
    testTrue(
      g, environment.getVar("WinX").value == "1024", "window width parsed");
    testTrue(
      g, environment.getVar("WinY").value == "768", "window height parsed");
  }
  std::filesystem::remove(path, error);
}

static void
testInvalidWindowDimensions()
{
  testSection("SysCmdLine: invalid window dimensions fail before startup");
  char executable[] = "Host";
  char ww[] = "-ww";
  char wh[] = "-wh";
  char zero[] = "0";
  char overflow[] = "999999999999999999999999999999999999";
  char* options[] = { ww, wh };
  const char* variables[] = { "WinX", "WinY" };

  for (std::size_t i = 0; i < 2; ++i) {
    const std::filesystem::path zeroPath = environmentPath("zero");
    std::error_code error;
    std::filesystem::remove(zeroPath, error);
    {
      EnvVars zeroEnvironment(zeroPath);
      char* zeroArguments[] = { executable, options[i], zero };
      const SysCmdLineResult zeroResult =
        SysCmdLine::ParseCommandLine(3, zeroArguments, &zeroEnvironment);
      testTrue(g,
               zeroResult.action == SysCmdLineAction::ExitFailure,
               "zero dimension is rejected");
      testTrue(g,
               zeroEnvironment.getVar(variables[i]).value.empty(),
               "rejected zero is not stored");
    }
    std::filesystem::remove(zeroPath, error);

    const std::filesystem::path overflowPath = environmentPath("overflow");
    std::filesystem::remove(overflowPath, error);
    {
      EnvVars overflowEnvironment(overflowPath);
      char* overflowArguments[] = { executable, options[i], overflow };
      const SysCmdLineResult overflowResult = SysCmdLine::ParseCommandLine(
        3, overflowArguments, &overflowEnvironment);
      testTrue(g,
               overflowResult.action == SysCmdLineAction::ExitFailure,
               "overflow dimension is rejected");
      testTrue(g,
               overflowEnvironment.getVar(variables[i]).value.empty(),
               "rejected overflow is not stored");
    }
    std::filesystem::remove(overflowPath, error);
  }
}

static void
testHelpAndVersion()
{
  testSection("SysCmdLine: engine help and build version output");
  const std::filesystem::path path = environmentPath("help-version");
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    EnvVars environment(path);
    SysCmdLineConfig config;
    config.applicationName = "HostProbe";
    config.description = "Runtime";
    config.usage = "HostProbe.exe [OPTION]";
    config.helpSections = { "Extra:\nprobe\n" };

    char executable[] = "HostProbe";
    char help[] = "--help";
    char* helpArguments[] = { executable, help };
    std::string helpText;
    {
      StandardOutputCapture capture;
      const SysCmdLineResult result =
        SysCmdLine::ParseCommandLine(2, helpArguments, &environment, config);
      testTrue(g,
               result.action == SysCmdLineAction::ExitSuccess,
               "help exits successfully");
      helpText = capture.text();
    }
    testTrue(g,
             helpText.find("HostProbe Runtime") != std::string::npos,
             "help uses configured application identity");
    testTrue(g,
             helpText.find("-ww <pixels>") != std::string::npos,
             "help lists engine window options");
    testTrue(g,
             helpText.find("Extra:\nprobe") != std::string::npos,
             "help appends configured sections");

    char version[] = "--version";
    char* versionArguments[] = { executable, version };
    std::string versionText;
    {
      StandardOutputCapture capture;
      const SysCmdLineResult result =
        SysCmdLine::ParseCommandLine(2, versionArguments, &environment, config);
      testTrue(g,
               result.action == SysCmdLineAction::ExitSuccess,
               "version exits successfully");
      versionText = capture.text();
    }
    testTrue(g,
             versionText.find(std::string("Version: ") +
                              BuildInfo::FullVersion + "\n") !=
               std::string::npos,
             "version output shows the full engine version");
  }
  std::filesystem::remove(path, error);
}

static void
testStringOptionsAndPositionalArguments()
{
  testSection("SysCmdLine: string options and positional arguments");
  const std::filesystem::path path = environmentPath("string-positional");
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    EnvVars environment(path);
    SysCmdLineConfig config;
    config.applicationName = "TestViewer";
    config.positionalEnvironmentVariable = "LaunchMesh";
    config.applicationOptions = {
      { "-m", "path", "LaunchMesh", "Mesh file path" },
      { "--name", "string", "SceneName", "Name of scene" },
      { "--mount", "paths", "Mounts", "Repeatable package path" },
    };

    char executable[] = "TestViewer";
    char meshFile[] = "assets/teapot.obj";
    char* positionalArgs[] = { executable, meshFile };
    const SysCmdLineResult positionalResult =
      SysCmdLine::ParseCommandLine(2, positionalArgs, &environment, config);
    testTrue(
      g, !positionalResult.shouldExit(), "positional arg continues startup");
    testTrue(g,
             environment.getVar("LaunchMesh").value == "assets/teapot.obj",
             "positional mesh path parsed");

    char mOption[] = "-m";
    char newMesh[] = "assets/bunny.obj";
    char nameOption[] = "--name";
    char sceneName[] = "StanfordBunny";
    char* optionArgs[] = {
      executable, mOption, newMesh, nameOption, sceneName
    };
    const SysCmdLineResult optionResult =
      SysCmdLine::ParseCommandLine(5, optionArgs, &environment, config);
    testTrue(g, !optionResult.shouldExit(), "string options continue startup");
    testTrue(g,
             environment.getVar("LaunchMesh").value == "assets/bunny.obj",
             "string option -m parsed");
    testTrue(g,
             environment.getVar("SceneName").value == "StanfordBunny",
             "string option --name parsed");

    char mountOption[] = "--mount";
    char firstMount[] = "packages/a";
    char secondMount[] = "b.ilpk";
    char* mountArgs[] = {
      executable, mountOption, firstMount, mountOption, secondMount
    };
    SysCmdLine::ParseCommandLine(5, mountArgs, &environment, config);
    testTrue(g,
             environment.getVar("Mounts").value == "packages/a\nb.ilpk",
             "a paths option repeats, one value per line");
  }
  std::filesystem::remove(path, error);
}

static bool
isDigit(char character)
{
  return character >= '0' && character <= '9';
}

static void
testBuildInfoMetadata()
{
  testSection("BuildInfo: generated vYY.MM_B version");
  const std::string release = BuildInfo::Release;
  testTrue(g,
           release.size() == 5 && isDigit(release[0]) && isDigit(release[1]) &&
             release[2] == '.' && isDigit(release[3]) && isDigit(release[4]),
           "release is YY.MM");

  // The build is stamped from the repository's VERSION.txt file.
  std::ifstream versionFile(std::filesystem::path(ILLUMO_ENGINE_ASSETS) / ".." /
                            ".." / "VERSION.txt");
  std::string fileRelease;
  versionFile >> fileRelease;
  testTrue(g, fileRelease == release, "release matches the VERSION.txt file");

  const std::string shortVersion = BuildInfo::VersionNumber;
  testTrue(g,
           shortVersion ==
             "v" + release + "_" + std::to_string(BuildInfo::BuildNumber),
           "short version is v<release>_<build>");

  const std::string commit = BuildInfo::Commit;
  const std::string identity =
    (commit.empty() ? std::string("unknown") : commit) +
    (BuildInfo::Dirty ? ", dirty" : "");
  testTrue(g,
           std::string(BuildInfo::FullVersion) ==
             shortVersion + " (" + identity + ")",
           "full version adds the commit and dirty state");
  testTrue(g,
           !commit.empty() || BuildInfo::BuildNumber == 0,
           "a build number comes only from Git history");
}

static int
runSysCmdLineCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerSysCmdLineTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.SysCmdLine.Validators",
               []() { return runSysCmdLineCase(testArgumentValidators); });
  registry.add("Illumo.SysCmdLine.WindowDimensionOptions",
               []() { return runSysCmdLineCase(testWindowDimensionOptions); });
  registry.add("Illumo.SysCmdLine.InvalidWindowDimensions",
               []() { return runSysCmdLineCase(testInvalidWindowDimensions); });
  registry.add("Illumo.SysCmdLine.HelpAndVersion",
               []() { return runSysCmdLineCase(testHelpAndVersion); });
  registry.add("Illumo.SysCmdLine.StringOptionsAndPositionalArguments", []() {
    return runSysCmdLineCase(testStringOptionsAndPositionalArguments);
  });
  registry.add("Illumo.BuildInfo.VersionMetadata",
               []() { return runSysCmdLineCase(testBuildInfoMetadata); });
}
