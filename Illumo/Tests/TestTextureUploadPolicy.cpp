#include "Rendering/OpenGL/TextureUploadPolicy.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <array>
#include <limits>

static int
testTextureUploadBounds()
{
  TestCounters counters;
  testTrue(counters,
           TextureUploadPolicy::validLayout(256, 256, 0, 0, 256, 256, 4, 0),
           "full texture at origin is valid");
  testTrue(counters,
           TextureUploadPolicy::validLayout(256, 256, 1, 1, 255, 255, 3, 300),
           "edge-fitting rectangle supports padded source rows");
  const std::array<int, 6> invalid[] = {
    { 0, 1, 256, 256, 4, 256 },
    { -1, 0, 1, 1, 4, 0 },
    { 256, 0, 1, 1, 4, 0 },
    { 0, 0, 0, 1, 4, 0 },
    { 0, 0, 2, 2, 4, 1 },
    { 0, 0, 2, 2, 4, -1 },
    { 0, 0, 2, 2, -1, 0 },
    { 0, 0, 2, 2, 2, 0 },
    { 0, 0, 2, 2, 5, 0 },
    { 1, 0, std::numeric_limits<int>::max(), 2, 4, 0 }
  };
  for (const std::array<int, 6>& rect : invalid) {
    testTrue(counters,
             !TextureUploadPolicy::validLayout(
               256, 256, rect[0], rect[1], rect[2], rect[3], rect[4], rect[5]),
             "invalid layout is rejected before staging");
  }
  const int maximum = std::numeric_limits<int>::max();
  testTrue(counters,
           !TextureUploadPolicy::validLayout(
             maximum, maximum, 0, 0, maximum, maximum, 4, 0),
           "unrepresentable staging size is rejected");
  return counters.failures;
}

static int
testTextureUploadPolicy()
{
  TestCounters counters;
  testSection("Texture upload: direct cutoff and non-waiting fallback");
  testTrue(counters,
           TextureUploadPolicy::useDirectUpload(64u * 1024u),
           "64 KiB uploads use the direct path");
  testTrue(counters,
           !TextureUploadPolicy::useDirectUpload(64u * 1024u + 1u),
           "larger uploads are eligible for PBO staging");

  std::array<TextureUploadSlotState, TextureUploadPolicy::kPboCount> states = {
    TextureUploadSlotState::Busy,
    TextureUploadSlotState::Busy,
    TextureUploadSlotState::Busy
  };
  testEqInt(counters,
            TextureUploadPolicy::selectAvailableSlot(0, states),
            -1,
            "all busy PBO slots select direct fallback without waiting");
  states[2] = TextureUploadSlotState::Signaled;
  testEqInt(counters,
            TextureUploadPolicy::selectAvailableSlot(0, states),
            2,
            "a signaled PBO slot is selected in ring order");
  states[1] = TextureUploadSlotState::Unused;
  testEqInt(counters,
            TextureUploadPolicy::selectAvailableSlot(0, states),
            1,
            "an unused PBO slot takes priority in ring order");
  return counters.failures;
}

void
registerTextureUploadPolicyTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Rendering.TextureUploadBounds",
               []() { return testTextureUploadBounds(); });
  registry.add("Illumo.Rendering.TextureUploadPolicy",
               []() { return testTextureUploadPolicy(); });
}
