#pragma once

#include "CellPattern.h"
#include <string>
#include <vector>

class BuiltinPatterns
{
public:
  static std::vector<std::string> names();
  static bool find(const std::string& name, CellPattern* pattern);
};
