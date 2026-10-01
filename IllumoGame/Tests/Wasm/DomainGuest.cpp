#include "DomainFixture.h"
#include <cstdint>

static DomainFixture s_fixture;
static std::string s_save;

extern "C" std::uint32_t
domainSave()
{
  s_save = s_fixture.save();
  return reinterpret_cast<std::uint32_t>(s_save.data());
}

extern "C" int
domainSaveSize()
{
  return static_cast<int>(s_save.size());
}

extern "C" int
domainRestore()
{
  return s_fixture.restore(s_save) ? 1 : 0;
}

extern "C" int
domainInitialize(const char* families,
                 int familiesSize,
                 const char* rules,
                 int rulesSize)
{
  if (familiesSize < 0 || rulesSize < 0) {
    return -1;
  }
  return s_fixture.initialize(std::string(families, familiesSize),
                              std::string(rules, rulesSize))
           ? s_fixture.ruleCount()
           : -1;
}

extern "C" int
domainCase(int index, int topology, int workload)
{
  return s_fixture.select(index, topology, workload) ? 1 : 0;
}

extern "C" int
domainAdvance()
{
  return s_fixture.advance() ? 1 : 0;
}

extern "C" int
domainHash(int high)
{
  const std::uint64_t hash = s_fixture.hash();
  return static_cast<int>(high != 0 ? hash >> 32u : hash & UINT32_MAX);
}

extern "C" std::uint32_t
allocate(int size)
{
  return reinterpret_cast<std::uint32_t>(new unsigned char[size]);
}

extern "C" int
release(std::uint32_t pointer)
{
  delete[] reinterpret_cast<unsigned char*>(pointer);
  return 0;
}
