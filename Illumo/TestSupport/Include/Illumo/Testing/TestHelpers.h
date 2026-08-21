#pragma once
// Tiny shared assertions for Illumo headless tests (no third-party test
// framework).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

struct TestCounters
{
  int failures = 0;
};

inline void
testTrue(TestCounters& c, bool cond, const char* msg)
{
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    ++c.failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

inline void
testEqSize(TestCounters& c, size_t a, size_t b, const char* msg)
{
  if (a != b) {
    std::printf("FAIL: %s (got %zu, expected %zu)\n", msg, a, b);
    ++c.failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

inline void
testEqInt(TestCounters& c, int a, int b, const char* msg)
{
  if (a != b) {
    std::printf("FAIL: %s (got %d, expected %d)\n", msg, a, b);
    ++c.failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

inline void
testEqUChar(TestCounters& c, unsigned char a, unsigned char b, const char* msg)
{
  if (a != b) {
    std::printf("FAIL: %s (got %u, expected %u)\n",
                msg,
                static_cast<unsigned>(a),
                static_cast<unsigned>(b));
    ++c.failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

inline void
testEqI64(TestCounters& c, std::int64_t a, std::int64_t b, const char* msg)
{
  if (a != b) {
    std::printf("FAIL: %s (got %lld, expected %lld)\n",
                msg,
                static_cast<long long>(a),
                static_cast<long long>(b));
    ++c.failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

inline void
testEqStr(TestCounters& c,
          const std::string& a,
          const std::string& b,
          const char* msg)
{
  if (a != b) {
    std::printf(
      "FAIL: %s (got '%s', expected '%s')\n", msg, a.c_str(), b.c_str());
    ++c.failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

inline void
testSection(const char* title)
{
  std::printf("\n--- %s ---\n", title);
}
