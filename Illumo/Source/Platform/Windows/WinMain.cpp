#define _CRTDBG_MAP_ALLOC
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WINSOCKAPI_

#include <winsock2.h>
#include <ws2tcpip.h>

#include <crtdbg.h>
#include <cstdio>
#include <windows.h>
#endif

#include <Illumo/Engine/Application.h>

#ifdef _WIN32
// Whether a standard handle leads somewhere (a pipe, file or console).
static bool
hasStandardHandle(DWORD which)
{
  const HANDLE handle = GetStdHandle(which);
  return handle != nullptr && handle != INVALID_HANDLE_VALUE &&
         GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

// A Release runtime links as a Windows program, so starting it opens no
// console window. Started from a terminal with its output not redirected, it
// writes --help and the --capture/--bench results into that terminal's
// console instead; redirected output (pipes, files) is left as it is.
static void
attachParentConsole()
{
  if (GetConsoleWindow() != nullptr) {
    return; // A console program, or one already given a console.
  }
  const bool output = hasStandardHandle(STD_OUTPUT_HANDLE);
  const bool errors = hasStandardHandle(STD_ERROR_HANDLE);
  if ((output && errors) || !AttachConsole(ATTACH_PARENT_PROCESS)) {
    return; // Fully redirected, or started from Explorer: no console.
  }
  FILE* stream = nullptr;
  if (!output) {
    freopen_s(&stream, "CONOUT$", "w", stdout);
  }
  if (!errors) {
    freopen_s(&stream, "CONOUT$", "w", stderr);
  }
}
#endif

int
main(int argc, char** argv)
{
#ifdef _WIN32
  attachParentConsole();
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
#endif
  return RunIllumoApplication(argc, argv, CreateIllumoApplication());
}
