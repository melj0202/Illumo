#include "Rule184RuleSet.h"
#include <cstring>

void
Rule184RuleSet::evalCell(const unsigned char& target,
                         unsigned char dest[3]) const
{
  if (target == 1) {
    std::memset(dest, 255, 3);
  } else {
    std::memset(dest, 0, 3);
  }
}
