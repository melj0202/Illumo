#pragma once

#include <memory>
#include <string>
#include <vector>

// A copy of this program started with other arguments (UTF-8) and owned by
// this one: it ends when stop() is called or this object is destroyed, and on
// Windows also when this process exits, however it exits.
class ChildProcess
{
public:
  ChildProcess();
  ~ChildProcess();
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;
  ChildProcess(ChildProcess&&) = delete;
  ChildProcess& operator=(ChildProcess&&) = delete;

  // Starts this program with `arguments` (not including the program itself),
  // ending any child it already runs. False with a reason on failure.
  bool start(const std::vector<std::string>& arguments, std::string* error);
  bool running();
  void stop();

private:
  struct State;
  std::unique_ptr<State> m_state;
};
