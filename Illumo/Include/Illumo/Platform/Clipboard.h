#pragma once

#include <string>

class Clipboard
{
public:
  static std::string GetText();
  static bool SetText(const std::string& text);
};
