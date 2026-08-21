#include <Illumo/Platform/Clipboard.h>

std::string
Clipboard::GetText()
{
  return std::string();
}

bool
Clipboard::SetText(const std::string& text)
{
  (void)text;
  return false;
}
