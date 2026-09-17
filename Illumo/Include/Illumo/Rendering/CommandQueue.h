#pragma once
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <vector>

// Growable per-frame command buffer with an explicit safety ceiling. Typical
// frames stay inside the initial reserve; pathological emitters cannot grow
// memory without bound.
class CommandQueue
{
private:
  static const size_t INITIAL_CAPACITY = 2048;
  static const size_t DEFAULT_MAX_COMMANDS = 65536;
  std::vector<RenderCommand> commandQueue;
  size_t maxCommands;
  size_t rejectedThisFrame = 0;
  size_t totalRejected = 0;
  size_t highWaterMark = 0;
  bool rejectionLoggedThisFrame = false;

public:
  explicit CommandQueue(size_t safetyCeiling = DEFAULT_MAX_COMMANDS)
    : maxCommands(safetyCeiling < INITIAL_CAPACITY ? INITIAL_CAPACITY
                                                   : safetyCeiling)
  {
    commandQueue.reserve(INITIAL_CAPACITY);
  }
  ~CommandQueue() = default;

  void Reset()
  {
    commandQueue.clear();
    rejectedThisFrame = 0;
    rejectionLoggedThisFrame = false;
  }

  bool Submit(const RenderCommand& command)
  {
    if (commandQueue.size() < maxCommands) {
      if (commandQueue.size() == commandQueue.capacity()) {
        const size_t doubled = commandQueue.capacity() * 2;
        const size_t nextCapacity =
          std::min(maxCommands, std::max(commandQueue.size() + 1, doubled));
        commandQueue.reserve(nextCapacity);
      }
      commandQueue.push_back(command);
      if (commandQueue.size() > highWaterMark) {
        highWaterMark = commandQueue.size();
      }
      return true;
    }

    rejectedThisFrame++;
    totalRejected++;
    if (!rejectionLoggedThisFrame) {
      rejectionLoggedThisFrame = true;
      Logger::LogError(
        "CommandQueue reached its safety ceiling; rejecting tokens until "
        "Clear/Reset.");
    }
    return false;
  }

  size_t GetCommandCount() const { return commandQueue.size(); }
  size_t GetCapacity() const { return commandQueue.capacity(); }
  size_t GetSafetyCeiling() const { return maxCommands; }
  size_t GetRejectedThisFrame() const { return rejectedThisFrame; }
  size_t GetTotalRejected() const { return totalRejected; }
  size_t GetHighWaterMark() const { return highWaterMark; }
  bool HasRejectedThisFrame() const { return rejectedThisFrame > 0; }
  RenderCommand& GetCommand(size_t index) { return commandQueue[index]; }
};
