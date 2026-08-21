#include "PatternCodec.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

void
PatternCodec::setError(std::string* error, const char* message)
{
  if (error != nullptr && message != nullptr) {
    *error = message;
  }
}

bool
PatternCodec::looksLikeRle(const std::string& text)
{
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char character = text[i];
    if (character == '!' || character == '$' || character == 'b' ||
        character == 'o' || character == 'p' || character == 'P') {
      return true;
    }
    if (character == 'x' && text.find("x =", i) == i) {
      return true;
    }
  }
  return false;
}

static int
decodeStateToken(char token)
{
  if (token == 'b' || token == 'B' || token == '.') {
    return 1;
  }
  if (token == 'o' || token == 'O' || token == '*' || token == 'A') {
    return 0;
  }
  if (token >= 'C' && token <= 'Z') {
    return static_cast<int>(token - 'A');
  }
  return -1;
}

bool
PatternCodec::parseRle(const std::string& text,
                       CellPattern* pattern,
                       std::string* error)
{
  if (pattern == nullptr) {
    setError(error, "pattern output is null");
    return false;
  }
  pattern->clear();

  int x = 0;
  int y = 0;
  int run = 0;
  bool inHeader = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char character = text[i];
    if (character == '#') {
      while (i < text.size() && text[i] != '\n') {
        ++i;
      }
      continue;
    }
    if (!inHeader && character == 'x') {
      inHeader = true;
    }
    if (inHeader) {
      if (character == '\n') {
        inHeader = false;
      }
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    if (character >= '0' && character <= '9') {
      run = run * 10 + (character - '0');
      if (run > CellPattern::kMaxWidth * CellPattern::kMaxHeight) {
        setError(error, "RLE run exceeds pattern cap");
        return false;
      }
      continue;
    }
    if (run <= 0) {
      run = 1;
    }
    if (character == '!') {
      break;
    }
    if (character == '$') {
      y += run;
      x = 0;
      run = 0;
      if (y > CellPattern::kMaxHeight) {
        setError(error, "RLE pattern is taller than 256 cells");
        return false;
      }
      continue;
    }
    int state = -1;
    if (character == 'p' || character == 'P') {
      ++i;
      if (i >= text.size() || text[i] < '0' || text[i] > '9') {
        setError(error, "RLE p-state token is incomplete");
        return false;
      }
      state = text[i] - '0';
    } else {
      state = decodeStateToken(character);
    }
    if (state < 0 || state > 255) {
      setError(error, "RLE contains an unknown cell token");
      return false;
    }
    for (int n = 0; n < run; ++n) {
      if (state != 1) {
        if (!pattern->addCell(x, y, static_cast<unsigned char>(state))) {
          setError(error, "RLE pattern exceeds size or occupancy caps");
          return false;
        }
      } else if (x + 1 > pattern->getWidth() || y + 1 > pattern->getHeight()) {
        if (!pattern->setExtent(std::max(pattern->getWidth(), x + 1),
                                std::max(pattern->getHeight(), y + 1))) {
          setError(error, "RLE pattern exceeds size caps");
          return false;
        }
      }
      ++x;
      if (x > CellPattern::kMaxWidth) {
        setError(error, "RLE pattern is wider than 256 cells");
        return false;
      }
    }
    run = 0;
  }
  return true;
}

bool
PatternCodec::parsePlaintext(const std::string& text,
                             CellPattern* pattern,
                             std::string* error)
{
  if (pattern == nullptr) {
    setError(error, "pattern output is null");
    return false;
  }
  pattern->clear();
  int x = 0;
  int y = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char character = text[i];
    if (character == '!') {
      while (i < text.size() && text[i] != '\n') {
        ++i;
      }
      continue;
    }
    if (character == '\r') {
      continue;
    }
    if (character == '\n') {
      ++y;
      x = 0;
      if (y > CellPattern::kMaxHeight) {
        setError(error, "plaintext pattern is taller than 256 cells");
        return false;
      }
      continue;
    }
    int state = decodeStateToken(character);
    if (state < 0) {
      setError(error, "plaintext contains an unknown cell token");
      return false;
    }
    if (state != 1) {
      if (!pattern->addCell(x, y, static_cast<unsigned char>(state))) {
        setError(error, "plaintext pattern exceeds size or occupancy caps");
        return false;
      }
    } else if (!pattern->setExtent(std::max(pattern->getWidth(), x + 1),
                                   std::max(pattern->getHeight(), y + 1))) {
      setError(error, "plaintext pattern exceeds size caps");
      return false;
    }
    ++x;
    if (x > CellPattern::kMaxWidth) {
      setError(error, "plaintext pattern is wider than 256 cells");
      return false;
    }
  }
  return true;
}

bool
PatternCodec::parse(const std::string& text,
                    CellPattern* pattern,
                    std::string* error)
{
  if (looksLikeRle(text)) {
    return parseRle(text, pattern, error);
  }
  return parsePlaintext(text, pattern, error);
}

std::string
PatternCodec::encodeRle(const CellPattern& pattern)
{
  std::ostringstream output;
  output << "x = " << pattern.getWidth() << ", y = " << pattern.getHeight()
         << "\n";
  std::vector<unsigned char> grid(
    static_cast<std::size_t>(std::max(1, pattern.getWidth()) *
                             std::max(1, pattern.getHeight())),
    static_cast<unsigned char>(1));
  const int width = std::max(1, pattern.getWidth());
  const int height = std::max(1, pattern.getHeight());
  for (const CellPatternCell& cell : pattern.getCells()) {
    if (cell.dx < 0 || cell.dy < 0 || cell.dx >= width || cell.dy >= height) {
      continue;
    }
    grid[static_cast<std::size_t>(cell.dy * width + cell.dx)] = cell.state;
  }

  int run = 0;
  unsigned char runState = 1;
  bool started = false;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const unsigned char state = grid[static_cast<std::size_t>(y * width + x)];
      if (!started) {
        runState = state;
        run = 1;
        started = true;
        continue;
      }
      if (state == runState) {
        ++run;
        continue;
      }
      if (run > 1) {
        output << run;
      }
      if (runState == 0) {
        output << 'o';
      } else if (runState == 1) {
        output << 'b';
      } else {
        output << 'p' << static_cast<int>(runState);
      }
      runState = state;
      run = 1;
    }
    if (started) {
      if (run > 1) {
        output << run;
      }
      if (runState == 0) {
        output << 'o';
      } else if (runState == 1) {
        output << 'b';
      } else {
        output << 'p' << static_cast<int>(runState);
      }
    }
    if (y + 1 < height) {
      output << '$';
    }
    started = false;
    run = 0;
  }
  output << '!';
  return output.str();
}

std::string
PatternCodec::encodePlaintext(const CellPattern& pattern)
{
  const int width = std::max(0, pattern.getWidth());
  const int height = std::max(0, pattern.getHeight());
  std::vector<unsigned char> grid(
    static_cast<std::size_t>(std::max(1, width) * std::max(1, height)),
    static_cast<unsigned char>(1));
  for (const CellPatternCell& cell : pattern.getCells()) {
    if (cell.dx < 0 || cell.dy < 0 || cell.dx >= width || cell.dy >= height) {
      continue;
    }
    grid[static_cast<std::size_t>(cell.dy * width + cell.dx)] = cell.state;
  }
  std::string output;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const unsigned char state = grid[static_cast<std::size_t>(y * width + x)];
      if (state == 0) {
        output.push_back('O');
      } else if (state == 1) {
        output.push_back('.');
      } else {
        output.push_back(static_cast<char>('A' + state));
      }
    }
    if (y + 1 < height) {
      output.push_back('\n');
    }
  }
  return output;
}
