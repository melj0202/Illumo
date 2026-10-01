#include <Illumo/Platform/SystemInfo.h>
#include <thread>

SystemInfo
QuerySystemInfo()
{
  SystemInfo info;
  info.logicalProcessors = std::thread::hardware_concurrency();
  return info;
}
