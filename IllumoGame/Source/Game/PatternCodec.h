#pragma once

#include "CellPattern.h"
#include <string>

class PatternCodec
{
public:
  static bool parse(const std::string& text,
                    CellPattern* pattern,
                    std::string* error);
  static bool parseRle(const std::string& text,
                       CellPattern* pattern,
                       std::string* error);
  static bool parsePlaintext(const std::string& text,
                             CellPattern* pattern,
                             std::string* error);
  static std::string encodeRle(const CellPattern& pattern);
  static std::string encodePlaintext(const CellPattern& pattern);

private:
  static void setError(std::string* error, const char* message);
  static bool looksLikeRle(const std::string& text);
};
