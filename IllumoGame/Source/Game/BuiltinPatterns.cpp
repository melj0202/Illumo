#include "BuiltinPatterns.h"
#include "PatternCodec.h"
#include <cctype>
#include <cstring>

static std::string
normalizeName(const std::string& name)
{
  std::string normalized;
  for (std::size_t i = 0; i < name.size(); ++i) {
    normalized.push_back(
      static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
  }
  return normalized;
}

std::vector<std::string>
BuiltinPatterns::names()
{
  return { "glider", "lwss", "gosper", "diode" };
}

bool
BuiltinPatterns::find(const std::string& name, CellPattern* pattern)
{
  if (pattern == nullptr) {
    return false;
  }
  const std::string key = normalizeName(name);
  const char* rle = nullptr;
  if (key == "glider") {
    rle = "x = 3, y = 3\nbo$2bo$3o!";
  } else if (key == "lwss") {
    rle = "x = 5, y = 4\nbo2bo$o$o3bo$4o!";
  } else if (key == "gosper") {
    rle = "x = 36, y = 9\n24bo$22bobo$12b2o6b2o12b2o$11bo3bo4b2o12b2o$"
          "2o8bo5bo3b2o$2o8bo3bob2o4bobo$10bo5bo7bo$11bo3bo$12b2o!";
  } else if (key == "diode") {
    rle = "x = 8, y = 1\np0p2p3p3p3p3p3p3!";
  } else {
    return false;
  }
  std::string error;
  return PatternCodec::parseRle(rle, pattern, &error);
}
