#pragma once
#include <string>
#include <vector>

// Tightly packed RGBA8, top row first. Empty pixels on failure.
struct FrameReadback
{
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
  std::string error;
  bool success() const { return error.empty() && !pixels.empty(); }
};
