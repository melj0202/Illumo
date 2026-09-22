#pragma once

#include <Illumo/Services/SysCmdLine.h>

#include <memory>
#include <string>

class IEnvVars;
class IModule;

using IllumoDefaultsCallback = void (*)(IEnvVars* environment);
using IllumoModuleFactory = std::unique_ptr<IModule> (*)(IEnvVars* environment);
using IllumoExitCodeCallback = int (*)();

struct IllumoApplicationDefinition
{
  std::string applicationName{ "Illumo" };
  SysCmdLineConfig commandLine;
  IllumoDefaultsCallback applyDefaults{ nullptr };
  IllumoModuleFactory createRequiredModule{ nullptr };
  // Consulted after a normal close. Products whose run has an outcome (a
  // capture that failed) report it here; absent means success.
  IllumoExitCodeCallback exitCode{ nullptr };
};

// The consuming product defines this factory. Illumo's platform entry invokes
// it before handing the resulting definition to the generic runtime.
IllumoApplicationDefinition
CreateIllumoApplication();

int
RunIllumoApplication(int argc,
                     char** argv,
                     IllumoApplicationDefinition application);
