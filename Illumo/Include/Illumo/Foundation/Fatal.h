#pragma once
#include <cstdio>
#include <cstdlib>

// Ends the process for a failure the program cannot continue from: a
// programming error, or running out of memory or threads. Illumo is built
// without exceptions, so there is nothing to unwind to. The message goes to
// stderr (the log file may be what failed); in a WASM guest the abort traps
// and the host reports the guest as failed.
[[noreturn]] inline void
illumoFatal(const char* message)
{
  std::fputs("Illumo fatal error: ", stderr);
  std::fputs(message != nullptr ? message : "(no message)", stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
  std::abort();
}
