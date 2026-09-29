#pragma once
#include <cctype>
#include <charconv>
#include <cstddef>
#include <string_view>
#include <system_error>

// Non-throwing replacements for std::stol, std::stoll, std::stoul, std::stod
// and std::stof. Like them, parsing skips leading white space and accepts one
// leading '+' (or '-'), stops at the first character that cannot continue the
// number, and fails on an empty number or an out-of-range value. Unlike
// std::strtod they do not read hexadecimal floats. *consumed, when given, gets
// the characters used including the skipped prefix, so a whole-text check is
// `consumed == text.size()`. Floating-point text also accepts inf and nan.

inline std::size_t
illumoNumberStart(std::string_view text, bool* negative)
{
  std::size_t index = 0;
  while (index < text.size() &&
         std::isspace(static_cast<unsigned char>(text[index])) != 0) {
    ++index;
  }
  *negative = false;
  if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
    *negative = text[index] == '-';
    ++index;
  }
  return index;
}

template<typename Integer>
bool
parseInteger(std::string_view text,
             Integer* value,
             std::size_t* consumed = nullptr)
{
  bool negative = false;
  const std::size_t start = illumoNumberStart(text, &negative);
  // from_chars reads '-' itself; step back onto it so the minimum value
  // parses. '+' is skipped here because from_chars rejects it.
  const std::size_t from = negative ? start - 1u : start;
  if (start >= text.size() || text[start] == '+' || text[start] == '-') {
    return false;
  }
  Integer parsed = 0;
  const char* begin = text.data() + from;
  const std::from_chars_result result =
    std::from_chars(begin, text.data() + text.size(), parsed, 10);
  if (result.ec != std::errc{}) {
    return false;
  }
  *value = parsed;
  if (consumed != nullptr) {
    *consumed = static_cast<std::size_t>(result.ptr - text.data());
  }
  return true;
}

template<typename Floating>
bool
parseFloating(std::string_view text,
              Floating* value,
              std::size_t* consumed = nullptr)
{
  bool negative = false;
  const std::size_t start = illumoNumberStart(text, &negative);
  const std::size_t from = negative ? start - 1u : start;
  if (start >= text.size() || text[start] == '+' || text[start] == '-') {
    return false;
  }
  Floating parsed = 0;
  const char* begin = text.data() + from;
  const std::from_chars_result result =
    std::from_chars(begin, text.data() + text.size(), parsed);
  if (result.ec != std::errc{}) {
    return false;
  }
  *value = parsed;
  if (consumed != nullptr) {
    *consumed = static_cast<std::size_t>(result.ptr - text.data());
  }
  return true;
}

// Whole-text forms: the number must use every character of text.
template<typename Integer>
bool
parseWholeInteger(std::string_view text, Integer* value)
{
  std::size_t consumed = 0;
  Integer parsed = 0;
  if (!parseInteger(text, &parsed, &consumed) || consumed != text.size()) {
    return false;
  }
  *value = parsed;
  return true;
}

template<typename Floating>
bool
parseWholeFloating(std::string_view text, Floating* value)
{
  std::size_t consumed = 0;
  Floating parsed = 0;
  if (!parseFloating(text, &parsed, &consumed) || consumed != text.size()) {
    return false;
  }
  *value = parsed;
  return true;
}
