#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using CommandFn = std::function<void(const std::vector<std::string>&)>;

class CommandRegistry
{
  struct Registration
  {
    CommandFn fn;
    std::shared_ptr<const bool> identity = std::make_shared<const bool>(true);
  };
  struct QueuedCommand
  {
    CommandFn fn;
    std::vector<std::string> args;
    std::weak_ptr<const bool> identity;
  };

  std::unordered_map<std::string, Registration> commands;
  std::unordered_map<std::string, std::string> usages;
  std::unordered_map<std::string, std::string> descriptions;
  std::unordered_map<std::string, std::vector<std::string>> completions;
  std::vector<QueuedCommand> queue;
  bool executing = false;
  bool cancelBatch = false;

public:
  CommandRegistry() {}
  ~CommandRegistry() = default;
  CommandRegistry(const CommandRegistry&) = delete;
  CommandRegistry& operator=(const CommandRegistry&) = delete;
  CommandRegistry(CommandRegistry&&) = delete;
  CommandRegistry& operator=(CommandRegistry&&) = delete;

  void RegisterCommand(
    const std::string& name,
    CommandFn fn,
    const std::string& usage = "",
    const std::string& description = "",
    const std::vector<std::string>& completionCandidates = {})
  {
    commands.insert_or_assign(name, Registration{ std::move(fn) });
    usages[name] = usage;
    descriptions[name] = description;
    completions[name] = completionCandidates;
  }
  void UnregisterCommand(const std::string& name)
  {
    commands.erase(name);
    usages.erase(name);
    descriptions.erase(name);
    completions.erase(name);
  }
  bool QueueCommand(const std::string& name,
                    const std::vector<std::string>& args = {})
  {
    std::unordered_map<std::string, Registration>::iterator it =
      commands.find(name);
    if (it != commands.end()) {
      queue.push_back({ it->second.fn, args, it->second.identity });
      return true;
    }
    return false;
  }
  void ClearQueue()
  {
    queue.clear();
    cancelBatch = executing;
  }
  void ExecuteQueue()
  {
    if (executing) {
      return;
    }
    std::vector<QueuedCommand> batch;
    batch.swap(queue);
    executing = true;
    cancelBatch = false;
    try {
      for (const QueuedCommand& command : batch) {
        if (cancelBatch) {
          break;
        }
        if (!command.identity.expired() && command.fn) {
          command.fn(command.args);
        }
      }
    } catch (...) {
      executing = false;
      throw;
    }
    executing = false;
  }
  bool HasCommand(const std::string& name) const
  {
    return commands.count(name) > 0;
  }
  std::string GetCommandUsage(const std::string& name) const
  {
    std::unordered_map<std::string, std::string>::const_iterator it =
      usages.find(name);
    return it != usages.end() ? it->second : "";
  }
  std::string GetCommandDescription(const std::string& name) const
  {
    std::unordered_map<std::string, std::string>::const_iterator it =
      descriptions.find(name);
    return it != descriptions.end() ? it->second : "";
  }
  std::vector<std::string> GetCommandCompletions(const std::string& name) const
  {
    std::unordered_map<std::string, std::vector<std::string>>::const_iterator
      it = completions.find(name);
    return it != completions.end() ? it->second : std::vector<std::string>();
  }
  std::vector<std::string> GetCommandNames() const
  {
    std::vector<std::string> names;
    for (const std::pair<const std::string, Registration>& command : commands) {
      names.push_back(command.first);
    }
    std::sort(names.begin(), names.end());
    return names;
  }
};
