#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <wtsapi32.h>

#include "Platform/SessionState.h"

bool
PlatformSessionLocked()
{
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
    return false;
  }
  LPWSTR buffer = nullptr;
  DWORD bytes = 0;
  if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
                                   session,
                                   WTSSessionInfoEx,
                                   &buffer,
                                   &bytes) ||
      buffer == nullptr) {
    return false;
  }
  bool locked = false;
  const WTSINFOEXW* info = reinterpret_cast<const WTSINFOEXW*>(buffer);
  if (bytes >= sizeof(WTSINFOEXW) && info->Level == 1) {
    locked = info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
  }
  WTSFreeMemory(buffer);
  return locked;
}
