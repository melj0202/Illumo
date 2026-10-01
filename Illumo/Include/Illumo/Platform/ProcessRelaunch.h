#pragma once

#include <string>

// Starts a new copy of this program with the same command line, environment
// and working directory, for an in-application restart. It does not wait for
// the copy; the caller then exits normally. Returns false with a reason when
// the copy could not be started.
bool
RelaunchCurrentProcess(int argc, char** argv, std::string* error);
