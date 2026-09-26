#pragma once
#include <Illumo/Services/KeyCode.h>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

class InputContext
{
public:
  // Transparent hashing: per-frame action queries by literal or view build
  // no temporary std::string.
  struct ActionHash
  {
    using is_transparent = void;
    std::size_t operator()(std::string_view tag) const noexcept
    {
      return std::hash<std::string_view>{}(tag);
    }
  };
  using ActionMap =
    std::unordered_map<std::string, InputEvent, ActionHash, std::equal_to<>>;

private:
  ActionMap actions;

public:
  InputContext() {}
  InputEvent getActionTag(std::string_view actionTag) const
  {
    const ActionMap::const_iterator found = actions.find(actionTag);
    if (found == actions.end()) {
      throw std::out_of_range("Unknown input action");
    }
    return found->second;
  }
  const ActionMap& getActions() const { return actions; }
  // bool containsKeyCode(const std::string& keyCode) const { return
  // actions.count(keyCode); }

  void bindAction(const std::string& actionTag, InputEvent inputEvent)
  {
    actions[actionTag] = inputEvent;
  }
};
