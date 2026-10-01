#pragma once

#include <Illumo/Services/SysCmdLine.h>

#include <chrono>
#include <string>

class IEnvVars;
class Illumo;

using IllumoDefaultsCallback = void (*)(IEnvVars* environment);
// Runs the application on the initialized engine until it closes and returns
// the process exit code. `launched` is when the process started, for the
// startup time it reports.
using IllumoRunCallback =
  int (*)(Illumo& illumo, std::chrono::steady_clock::time_point launched);

struct IllumoApplicationDefinition
{
  std::string applicationName{ "Illumo" };
  SysCmdLineConfig commandLine;
  IllumoDefaultsCallback applyDefaults{ nullptr };
  // The runtime's frame loop (D-E31). Required.
  IllumoRunCallback run{ nullptr };
};

// The consuming product defines this factory. Illumo's platform entry invokes
// it before handing the resulting definition to the generic runtime.
IllumoApplicationDefinition
CreateIllumoApplication();

// The process around the application: logger, settings, command line,
// startup report, engine services and, when asked, the relaunch.
int
RunIllumoApplication(int argc,
                     char** argv,
                     IllumoApplicationDefinition application);
