#pragma once

#include <functional>
#include <string>
#include <vector>

struct IllumoTestCase
{
  std::string name;
  std::function<int()> function;
  int timeoutSeconds = 30;
};

class IllumoTestRegistry
{
public:
  void add(const std::string& name,
           const std::function<int()>& function,
           int timeoutSeconds = 30)
  {
    IllumoTestCase testCase;
    testCase.name = name;
    testCase.function = function;
    testCase.timeoutSeconds = timeoutSeconds;
    testCases.push_back(testCase);
  }

  const std::vector<IllumoTestCase>& getTestCases() const { return testCases; }

  const IllumoTestCase* find(const std::string& name) const
  {
    for (const IllumoTestCase& testCase : testCases) {
      if (testCase.name == name) {
        return &testCase;
      }
    }

    return nullptr;
  }

private:
  std::vector<IllumoTestCase> testCases;
};
