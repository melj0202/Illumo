#include "RuleSetRegistry.h"
#include "DataRuleSet.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <numeric>
#include <sstream>
#include <utility>

namespace {

const unsigned int kMaximumStateCount = 256u;
const unsigned int kMaximumNeighborCount = 8u;
const unsigned int kMaximumExtendedRadius = 16u;
const unsigned int kMaximumExtendedCount = 1089u;
// A dense von Neumann table holds stateCount^5 entries (248,832 at 12).
const unsigned int kMaximumTableStateCount = 12u;
// Bounds the work of expanding table variables and symmetries at load time.
const std::uint64_t kMaximumTableExpansions = 8000000u;
// Sandpile heights 0..7 occupy the first eight states; later states are the
// phases of a pulsed grain source.
const unsigned int kSandpileHeightCount = 8u;
const unsigned int kMaximumSandpileStateCount = 64u;
const unsigned char kBackgroundState = 1u;
const unsigned char kCountedState = 0u;

struct LegacyRuleSetDefinition
{
  std::string id;
  std::string name;
  RuleFamily family = RuleFamily::LifeLike;
  std::string rule;
  unsigned int birthMask = 0u;
  unsigned int surviveMask = 0u;
  unsigned int ruleNumber = 0u;
  unsigned int stateCount = 2u;
  std::vector<std::string> stateNames;
  std::vector<std::array<unsigned char, 3>> stateColors;
  RuleSet::TransitionTable transitionTable{};
  std::array<unsigned char, 8> elementaryTransitions{};
  bool hasCustomPalette = false;
  std::array<unsigned char, 3> aliveColor = { 0, 0, 0 };
  std::array<unsigned char, 3> deadColor = { 255, 255, 255 };
};

bool
readUnsigned(const nlohmann::json& value,
             unsigned int maximum,
             unsigned int& result)
{
  if (value.is_number_unsigned()) {
    const std::uint64_t number = value.get<std::uint64_t>();
    if (number > maximum) {
      return false;
    }
    result = static_cast<unsigned int>(number);
    return true;
  }
  if (value.is_number_integer()) {
    const std::int64_t number = value.get<std::int64_t>();
    if (number < 0 || static_cast<std::uint64_t>(number) > maximum) {
      return false;
    }
    result = static_cast<unsigned int>(number);
    return true;
  }
  return false;
}

bool
readNeighborMask(const nlohmann::json& values, unsigned int& mask)
{
  if (!values.is_array()) {
    return false;
  }
  mask = 0u;
  for (const nlohmann::json& value : values) {
    unsigned int count = 0u;
    if (!readUnsigned(value, kMaximumNeighborCount, count)) {
      return false;
    }
    mask |= 1u << count;
  }
  return true;
}

bool
readColor(const nlohmann::json& values, std::array<unsigned char, 3>& color)
{
  if (!values.is_array() || values.size() != color.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < color.size(); ++index) {
    unsigned int channel = 0u;
    if (!readUnsigned(values[index], 255u, channel)) {
      return false;
    }
    color[index] = static_cast<unsigned char>(channel);
  }
  return true;
}

// Golly numbering uses 0 as the quiescent state; Illumo reserves state 1 for
// the sparse background and state 0 for the counted state.
unsigned char
gollyToIllumoState(unsigned int state)
{
  if (state == 0u) {
    return kBackgroundState;
  }
  if (state == 1u) {
    return kCountedState;
  }
  return static_cast<unsigned char>(state);
}

unsigned int
extendedNeighborhoodSize(unsigned int radius,
                         RuleSet::ExtendedNeighborhoodShape shape,
                         bool includeCenter)
{
  const int range = static_cast<int>(radius);
  unsigned int count = includeCenter ? 1u : 0u;
  for (int y = -range; y <= range; ++y) {
    for (int x = -range; x <= range; ++x) {
      if ((x != 0 || y != 0) &&
          RuleSet::extendedNeighborhoodContains(shape, range, x, y)) {
        count += 1u;
      }
    }
  }
  return count;
}

bool
parseNeighborhoodShape(const std::string& value,
                       RuleSet::ExtendedNeighborhoodShape& shape)
{
  if (value == "square") {
    shape = RuleSet::ExtendedNeighborhoodShape::Square;
  } else if (value == "circular") {
    shape = RuleSet::ExtendedNeighborhoodShape::Circular;
  } else if (value == "diamond") {
    shape = RuleSet::ExtendedNeighborhoodShape::Diamond;
  } else {
    return false;
  }
  return true;
}

const char*
neighborhoodShapeName(RuleSet::ExtendedNeighborhoodShape shape)
{
  if (shape == RuleSet::ExtendedNeighborhoodShape::Circular) {
    return "circular";
  }
  if (shape == RuleSet::ExtendedNeighborhoodShape::Diamond) {
    return "diamond";
  }
  return "square";
}

// Golly's LtL notation: NM Moore square, NC circular, NN von Neumann diamond.
const char*
neighborhoodShapeSuffix(RuleSet::ExtendedNeighborhoodShape shape)
{
  if (shape == RuleSet::ExtendedNeighborhoodShape::Circular) {
    return "NC";
  }
  if (shape == RuleSet::ExtendedNeighborhoodShape::Diamond) {
    return "NN";
  }
  return "NM";
}

bool
isCyclicExtended(const RuleSetDefinition& definition)
{
  return definition.neighborhoodRadius > 1u ||
         definition.extendedNeighborhoodShape !=
           RuleSet::ExtendedNeighborhoodShape::Square;
}

std::string
trimCopy(const std::string& text)
{
  std::size_t first = 0u;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }
  std::size_t last = text.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(text[last - 1u])) != 0) {
    --last;
  }
  return text.substr(first, last - first);
}

bool
isTableVariableName(const std::string& name)
{
  if (name.empty() || name.size() > 32u ||
      std::isalpha(static_cast<unsigned char>(name[0])) == 0) {
    return false;
  }
  for (const char character : name) {
    if (std::isalnum(static_cast<unsigned char>(character)) == 0 &&
        character != '_') {
      return false;
    }
  }
  return true;
}

bool
parseTableNumber(const std::string& token,
                 unsigned int stateCount,
                 unsigned int& value)
{
  if (token.empty() || token.size() > 3u) {
    return false;
  }
  value = 0u;
  for (const char character : token) {
    if (character < '0' || character > '9') {
      return false;
    }
    value = value * 10u + static_cast<unsigned int>(character - '0');
  }
  return value < stateCount;
}

struct TableVariable
{
  std::string name;
  std::vector<unsigned int> values;
};

const TableVariable*
findTableVariable(const std::vector<TableVariable>& variables,
                  const std::string& name)
{
  for (const TableVariable& variable : variables) {
    if (variable.name == name) {
      return &variable;
    }
  }
  return nullptr;
}

bool
tableSymmetryPermutations(const std::string& symmetry,
                          std::vector<std::array<unsigned int, 4>>& output)
{
  // Each permutation lists, for north, east, south and west in turn, which
  // source slot of the written transition supplies that neighbor.
  const std::array<std::array<unsigned int, 4>, 4> rotations = {
    { { 0u, 1u, 2u, 3u },
      { 3u, 0u, 1u, 2u },
      { 2u, 3u, 0u, 1u },
      { 1u, 2u, 3u, 0u } }
  };
  output.clear();
  if (symmetry == "none") {
    output.push_back(rotations[0]);
  } else if (symmetry == "reflect_horizontal") {
    output.push_back(rotations[0]);
    output.push_back({ 0u, 3u, 2u, 1u });
  } else if (symmetry == "rotate4" || symmetry == "rotate4reflect") {
    for (const std::array<unsigned int, 4>& rotation : rotations) {
      output.push_back(rotation);
    }
    if (symmetry == "rotate4reflect") {
      for (const std::array<unsigned int, 4>& rotation : rotations) {
        output.push_back(
          { rotation[0], rotation[3], rotation[2], rotation[1] });
      }
    }
  } else if (symmetry == "permute") {
    std::array<unsigned int, 4> permutation = { 0u, 1u, 2u, 3u };
    do {
      output.push_back(permutation);
    } while (std::next_permutation(permutation.begin(), permutation.end()));
  } else {
    return false;
  }
  return true;
}

// Compiles Golly @TABLE transitions for a von Neumann neighborhood. The first
// written transition that matches a neighborhood wins, variables that repeat
// within one transition bind to the same value, and unmatched neighborhoods
// keep the center state, exactly as Golly's RuleTable algorithm behaves.
bool
compileVonNeumannTable(const std::vector<std::string>& lines,
                       const std::string& symmetry,
                       unsigned int stateCount,
                       std::vector<unsigned char>& table)
{
  const unsigned char kUnset = 0xFFu;
  std::vector<std::array<unsigned int, 4>> permutations;
  if (stateCount < 2u || stateCount > kMaximumTableStateCount ||
      !tableSymmetryPermutations(symmetry, permutations)) {
    return false;
  }
  std::size_t entryCount = 1u;
  for (int slot = 0; slot < 5; ++slot) {
    entryCount *= stateCount;
  }
  table.assign(entryCount, kUnset);

  std::vector<TableVariable> variables;
  std::uint64_t expansions = 0u;
  bool anyTransition = false;
  for (const std::string& rawLine : lines) {
    std::string line = rawLine;
    const std::size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line.resize(comment);
    }
    line = trimCopy(line);
    if (line.empty()) {
      continue;
    }

    if (line.rfind("var", 0u) == 0u && line.size() > 3u &&
        std::isspace(static_cast<unsigned char>(line[3])) != 0) {
      const std::size_t equals = line.find('=');
      const std::size_t open = line.find('{');
      const std::size_t close = line.rfind('}');
      if (equals == std::string::npos || open == std::string::npos ||
          close == std::string::npos || open < equals || close < open ||
          !trimCopy(line.substr(close + 1u)).empty()) {
        return false;
      }
      TableVariable variable;
      variable.name = trimCopy(line.substr(3u, equals - 3u));
      if (!isTableVariableName(variable.name) ||
          findTableVariable(variables, variable.name) != nullptr ||
          !trimCopy(line.substr(equals + 1u, open - equals - 1u)).empty()) {
        return false;
      }
      std::stringstream members(line.substr(open + 1u, close - open - 1u));
      std::string member;
      while (std::getline(members, member, ',')) {
        member = trimCopy(member);
        unsigned int value = 0u;
        const TableVariable* nested = findTableVariable(variables, member);
        if (nested != nullptr) {
          variable.values.insert(variable.values.end(),
                                 nested->values.begin(),
                                 nested->values.end());
        } else if (parseTableNumber(member, stateCount, value)) {
          variable.values.push_back(value);
        } else {
          return false;
        }
      }
      if (variable.values.empty()) {
        return false;
      }
      variables.push_back(std::move(variable));
      continue;
    }

    std::vector<std::string> tokens;
    if (line.find(',') != std::string::npos) {
      std::stringstream fields(line);
      std::string field;
      while (std::getline(fields, field, ',')) {
        tokens.push_back(trimCopy(field));
      }
    } else if (stateCount <= 10u) {
      for (const char character : line) {
        if (std::isspace(static_cast<unsigned char>(character)) == 0) {
          tokens.push_back(std::string(1u, character));
        }
      }
    } else {
      return false;
    }
    if (tokens.size() != 6u) {
      return false;
    }

    // Distinct variables in first-use order; fixed tokens resolve directly.
    std::vector<const TableVariable*> bound;
    std::array<int, 6> slotVariable{};
    std::array<unsigned int, 6> fixedValue{};
    for (std::size_t slot = 0u; slot < tokens.size(); ++slot) {
      const TableVariable* variable =
        findTableVariable(variables, tokens[slot]);
      if (variable == nullptr) {
        if (!parseTableNumber(tokens[slot], stateCount, fixedValue[slot])) {
          return false;
        }
        slotVariable[slot] = -1;
        continue;
      }
      int boundIndex = -1;
      for (std::size_t index = 0u; index < bound.size(); ++index) {
        if (bound[index] == variable) {
          boundIndex = static_cast<int>(index);
        }
      }
      if (boundIndex < 0) {
        boundIndex = static_cast<int>(bound.size());
        bound.push_back(variable);
      }
      slotVariable[slot] = boundIndex;
    }

    std::vector<std::size_t> odometer(bound.size(), 0u);
    bool more = true;
    while (more) {
      expansions += permutations.size();
      if (expansions > kMaximumTableExpansions) {
        return false;
      }
      std::array<unsigned int, 6> values{};
      for (std::size_t slot = 0u; slot < values.size(); ++slot) {
        values[slot] =
          slotVariable[slot] < 0
            ? fixedValue[slot]
            : bound[static_cast<std::size_t>(slotVariable[slot])]->values
                [odometer[static_cast<std::size_t>(slotVariable[slot])]];
      }
      for (const std::array<unsigned int, 4>& permutation : permutations) {
        std::size_t index = gollyToIllumoState(values[0]);
        for (unsigned int direction = 0u; direction < 4u; ++direction) {
          index = index * stateCount +
                  gollyToIllumoState(values[1u + permutation[direction]]);
        }
        if (table[index] == kUnset) {
          table[index] = gollyToIllumoState(values[5]);
        }
      }
      more = false;
      for (std::size_t digit = 0u; digit < odometer.size(); ++digit) {
        odometer[digit] += 1u;
        if (odometer[digit] < bound[digit]->values.size()) {
          more = true;
          break;
        }
        odometer[digit] = 0u;
      }
    }
    anyTransition = true;
  }
  if (!anyTransition) {
    return false;
  }

  const std::size_t centerStride = entryCount / stateCount;
  for (std::size_t index = 0u; index < entryCount; ++index) {
    if (table[index] == kUnset) {
      table[index] = static_cast<unsigned char>(index / centerStride);
    }
  }
  std::size_t quiescent = 0u;
  for (int slot = 0; slot < 5; ++slot) {
    quiescent = quiescent * stateCount + kBackgroundState;
  }
  return table[quiescent] == kBackgroundState;
}

void
setDefaultStates(LegacyRuleSetDefinition& definition)
{
  definition.stateNames.clear();
  definition.stateColors.clear();
  definition.stateNames.reserve(definition.stateCount);
  definition.stateColors.reserve(definition.stateCount);

  for (unsigned int state = 0u; state < definition.stateCount; ++state) {
    definition.stateNames.push_back("State " + std::to_string(state));
    definition.stateColors.push_back({ 180u, 180u, 180u });
  }
  if (definition.stateCount >= 2u) {
    definition.stateNames[0] = "Active";
    definition.stateNames[kBackgroundState] = "Background";
    definition.stateColors[0] = { 0u, 0u, 0u };
    definition.stateColors[kBackgroundState] = { 255u, 255u, 255u };
  }
  if (definition.family == RuleFamily::Generations &&
      definition.stateCount >= 3u) {
    definition.stateNames[0] = "Firing";
    definition.stateNames[2] = "Refractory";
    definition.stateColors[2] = { 0u, 164u, 128u };
  }
  definition.aliveColor = definition.stateColors[0];
  definition.deadColor = definition.stateColors[kBackgroundState];
}

void
setWireworldTransitions(LegacyRuleSetDefinition& definition)
{
  definition.family = RuleFamily::MooreTable;
  definition.stateCount = 4u;
  definition.birthMask = 0u;
  definition.surviveMask = 0u;
  definition.transitionTable.fill(kBackgroundState);
  for (unsigned char neighbors = 0u; neighbors < RuleSet::kNeighborCountCount;
       ++neighbors) {
    definition.transitionTable[RuleSet::transitionIndex(0u, neighbors)] = 2u;
    definition.transitionTable[RuleSet::transitionIndex(1u, neighbors)] = 1u;
    definition.transitionTable[RuleSet::transitionIndex(2u, neighbors)] = 3u;
    definition.transitionTable[RuleSet::transitionIndex(3u, neighbors)] =
      (neighbors == 1u || neighbors == 2u) ? 0u : 3u;
  }
  setDefaultStates(definition);
}

bool
readStates(const nlohmann::json& values, LegacyRuleSetDefinition& definition)
{
  if (!values.is_array() || values.size() != definition.stateCount) {
    return false;
  }
  definition.stateNames.resize(definition.stateCount);
  definition.stateColors.resize(definition.stateCount);
  std::vector<bool> seen(definition.stateCount, false);
  for (const nlohmann::json& value : values) {
    if (!value.is_object()) {
      return false;
    }
    unsigned int state = 0u;
    if (!value.contains("value") ||
        !readUnsigned(value["value"], definition.stateCount - 1u, state) ||
        seen[state] || !value.contains("name") || !value["name"].is_string() ||
        !value.contains("color") ||
        !readColor(value["color"], definition.stateColors[state])) {
      return false;
    }
    definition.stateNames[state] = value["name"].get<std::string>();
    if (definition.stateNames[state].empty()) {
      return false;
    }
    seen[state] = true;
  }
  return std::find(seen.begin(), seen.end(), false) == seen.end();
}

bool
readTransitionRows(const nlohmann::json& values,
                   LegacyRuleSetDefinition& definition)
{
  if (!values.is_array() || values.size() != definition.stateCount) {
    return false;
  }
  definition.transitionTable.fill(kBackgroundState);
  for (unsigned int state = 0u; state < definition.stateCount; ++state) {
    const nlohmann::json& row = values[state];
    if (!row.is_array() || row.size() != RuleSet::kNeighborCountCount) {
      return false;
    }
    for (unsigned int neighbors = 0u; neighbors < RuleSet::kNeighborCountCount;
         ++neighbors) {
      unsigned int nextState = 0u;
      if (!readUnsigned(
            row[neighbors], definition.stateCount - 1u, nextState)) {
        return false;
      }
      definition.transitionTable[RuleSet::transitionIndex(
        static_cast<unsigned char>(state),
        static_cast<unsigned char>(neighbors))] =
        static_cast<unsigned char>(nextState);
    }
  }
  return true;
}

bool
compileDefinition(LegacyRuleSetDefinition& definition)
{
  definition.id = RuleSetRegistry::normalizeId(definition.id);
  bool validId = !definition.id.empty() && definition.id.size() <= 64u;
  for (const unsigned char character : definition.id) {
    if ((character < 'A' || character > 'Z') &&
        (character < '0' || character > '9') && character != '_') {
      validId = false;
    }
  }
  if (!validId || RuleSetRegistry::familyName(definition.family) == nullptr ||
      definition.stateCount < 2u ||
      definition.stateCount > kMaximumStateCount) {
    return false;
  }
  if (definition.name.empty()) {
    definition.name = definition.id;
  }
  if ((definition.family == RuleFamily::LifeLike ||
       definition.family == RuleFamily::Generations) &&
      !definition.rule.empty()) {
    unsigned int parsedBirth = 0u;
    unsigned int parsedSurvive = 0u;
    if (!RuleSetRegistry::parseLifeLikeRuleString(
          definition.rule, parsedBirth, parsedSurvive)) {
      return false;
    }
    definition.birthMask = parsedBirth;
    definition.surviveMask = parsedSurvive;
  }
  if (definition.stateNames.empty() || definition.stateColors.empty()) {
    setDefaultStates(definition);
  }
  if (definition.stateNames.size() != definition.stateCount ||
      definition.stateColors.size() != definition.stateCount) {
    return false;
  }
  for (unsigned int state = 0u; state < definition.stateCount; ++state) {
    if (definition.stateNames[state].empty()) {
      return false;
    }
  }

  if (definition.family == RuleFamily::LifeLike ||
      definition.family == RuleFamily::Generations) {
    if ((definition.birthMask & ~0x1FFu) != 0u ||
        (definition.surviveMask & ~0x1FFu) != 0u ||
        (definition.birthMask & 1u) != 0u) {
      return false;
    }
    if (definition.family == RuleFamily::LifeLike &&
        definition.stateCount != 2u) {
      return false;
    }
    if (definition.family == RuleFamily::Generations &&
        definition.stateCount < 3u) {
      return false;
    }
    definition.transitionTable.fill(kBackgroundState);
    for (unsigned int state = 0u; state < definition.stateCount; ++state) {
      for (unsigned int neighbors = 0u;
           neighbors < RuleSet::kNeighborCountCount;
           ++neighbors) {
        const bool activeNeighbors =
          ((definition.birthMask >> neighbors) & 1u) != 0u;
        const bool survivingNeighbors =
          ((definition.surviveMask >> neighbors) & 1u) != 0u;
        unsigned char next = kBackgroundState;
        if (state == 0u) {
          if (survivingNeighbors) {
            next = 0u;
          } else if (definition.family == RuleFamily::Generations) {
            next = 2u;
          }
        } else if (state == kBackgroundState) {
          next = activeNeighbors ? 0u : kBackgroundState;
        } else if (definition.family == RuleFamily::Generations) {
          next = state + 1u < definition.stateCount
                   ? static_cast<unsigned char>(state + 1u)
                   : kBackgroundState;
        }
        definition.transitionTable[RuleSet::transitionIndex(
          static_cast<unsigned char>(state),
          static_cast<unsigned char>(neighbors))] = next;
      }
    }
    if (definition.family == RuleFamily::LifeLike ||
        definition.family == RuleFamily::Generations) {
      definition.rule = "B";
      for (unsigned int neighbors = 0u; neighbors <= kMaximumNeighborCount;
           ++neighbors) {
        if (((definition.birthMask >> neighbors) & 1u) != 0u) {
          definition.rule += std::to_string(neighbors);
        }
      }
      definition.rule += "/S";
      for (unsigned int neighbors = 0u; neighbors <= kMaximumNeighborCount;
           ++neighbors) {
        if (((definition.surviveMask >> neighbors) & 1u) != 0u) {
          definition.rule += std::to_string(neighbors);
        }
      }
    }
  } else if (definition.family == RuleFamily::MooreTable) {
    if (definition.stateCount > kMaximumStateCount) {
      return false;
    }
    for (unsigned int state = 0u; state < definition.stateCount; ++state) {
      for (unsigned int neighbors = 0u;
           neighbors < RuleSet::kNeighborCountCount;
           ++neighbors) {
        const unsigned char next =
          definition.transitionTable[RuleSet::transitionIndex(
            static_cast<unsigned char>(state),
            static_cast<unsigned char>(neighbors))];
        if (static_cast<unsigned int>(next) >= definition.stateCount) {
          return false;
        }
      }
    }
  } else if (definition.family == RuleFamily::Elementary1D) {
    if (definition.stateCount != 2u || definition.ruleNumber > 255u ||
        (definition.ruleNumber & 1u) != 0u) {
      return false;
    }
    for (unsigned int pattern = 0u; pattern < 8u; ++pattern) {
      definition.elementaryTransitions[pattern] =
        ((definition.ruleNumber >> pattern) & 1u) != 0u ? 0u : 1u;
    }
    definition.transitionTable.fill(kBackgroundState);
  } else {
    return false;
  }

  if (definition.family == RuleFamily::Elementary1D) {
    if (definition.elementaryTransitions[0] != kBackgroundState) {
      return false;
    }
  } else if (definition.transitionTable[RuleSet::transitionIndex(
               kBackgroundState, 0u)] != kBackgroundState) {
    return false;
  }

  definition.aliveColor = definition.stateColors[0];
  definition.deadColor = definition.stateColors[kBackgroundState];
  return true;
}

bool
parseLegacyDefinition(const nlohmann::json& item,
                      LegacyRuleSetDefinition& definition)
{
  if (!item.is_object()) {
    return false;
  }
  definition.id = RuleSetRegistry::normalizeId(item.value("id", ""));
  if (definition.id.empty()) {
    return false;
  }
  definition.name = item.value("name", definition.id);
  const std::string family = item.value("family", "life_like");
  const bool isLegacyWireworld = family == "wireworld";
  if (isLegacyWireworld) {
    definition.family = RuleFamily::MooreTable;
  } else if (!RuleSetRegistry::parseFamily(family, definition.family)) {
    return false;
  }
  definition.rule = item.value("rule", "");
  definition.stateCount = 2u;

  if (definition.family == RuleFamily::LifeLike) {
    if (!definition.rule.empty() &&
        !RuleSetRegistry::parseLifeLikeRuleString(
          definition.rule, definition.birthMask, definition.surviveMask)) {
      return false;
    }
    if (item.contains("birth") &&
        !readNeighborMask(item["birth"], definition.birthMask)) {
      return false;
    }
    if (item.contains("survive") &&
        !readNeighborMask(item["survive"], definition.surviveMask)) {
      return false;
    }
    if (item.contains("birth") || item.contains("survive")) {
      definition.rule.clear();
    }
  } else if (definition.family == RuleFamily::Elementary1D) {
    if (!item.contains("rule_number") ||
        !readUnsigned(item["rule_number"], 255u, definition.ruleNumber)) {
      return false;
    }
  } else if (definition.family == RuleFamily::Generations) {
    definition.stateCount = 3u;
    definition.birthMask = 1u << 2u;
    definition.surviveMask = 0u;
  } else if (isLegacyWireworld) {
    setWireworldTransitions(definition);
  } else {
    return false;
  }

  setDefaultStates(definition);
  if (isLegacyWireworld) {
    definition.stateNames = { "Head", "Empty", "Tail", "Conductor" };
    definition.stateColors = { { 0u, 80u, 255u },
                               { 255u, 255u, 255u },
                               { 255u, 40u, 40u },
                               { 255u, 200u, 40u } };
  }
  if (item.contains("palette")) {
    const nlohmann::json& palette = item["palette"];
    if (!palette.is_object()) {
      return false;
    }
    if (palette.contains("alive")) {
      if (!readColor(palette["alive"], definition.stateColors[0])) {
        return false;
      }
      definition.hasCustomPalette = true;
    }
    if (palette.contains("dead")) {
      if (!readColor(palette["dead"],
                     definition.stateColors[kBackgroundState])) {
        return false;
      }
      definition.hasCustomPalette = true;
    }
  }
  return compileDefinition(definition);
}

bool
parseVersionTwoDefinition(const nlohmann::json& item,
                          LegacyRuleSetDefinition& definition)
{
  if (!item.is_object()) {
    return false;
  }
  definition.id = RuleSetRegistry::normalizeId(item.value("id", ""));
  if (definition.id.empty() || !item.contains("family") ||
      !item["family"].is_string() || !item.contains("state_count") ||
      !readUnsigned(
        item["state_count"], kMaximumStateCount, definition.stateCount) ||
      definition.stateCount < 2u) {
    return false;
  }
  definition.name = item.value("name", definition.id);
  if (!RuleSetRegistry::parseFamily(item["family"].get<std::string>(),
                                    definition.family)) {
    return false;
  }

  if (definition.family == RuleFamily::LifeLike ||
      definition.family == RuleFamily::Generations) {
    if (item.contains("rule") && !item["rule"].is_string()) {
      return false;
    }
    definition.rule = item.value("rule", "");
    if (!definition.rule.empty() &&
        !RuleSetRegistry::parseLifeLikeRuleString(
          definition.rule, definition.birthMask, definition.surviveMask)) {
      return false;
    }
    if (item.contains("birth")) {
      unsigned int parsedBirth = 0u;
      if (!readNeighborMask(item["birth"], parsedBirth) ||
          (!definition.rule.empty() && parsedBirth != definition.birthMask)) {
        return false;
      }
      definition.birthMask = parsedBirth;
    }
    if (item.contains("survive")) {
      unsigned int parsedSurvive = 0u;
      if (!readNeighborMask(item["survive"], parsedSurvive) ||
          (!definition.rule.empty() &&
           parsedSurvive != definition.surviveMask)) {
        return false;
      }
      definition.surviveMask = parsedSurvive;
    }
  } else if (definition.family == RuleFamily::MooreTable) {
    if (!item.contains("transition_table") ||
        !readTransitionRows(item["transition_table"], definition)) {
      return false;
    }
  } else if (definition.family == RuleFamily::Elementary1D) {
    if (!item.contains("rule_number") ||
        !readUnsigned(item["rule_number"], 255u, definition.ruleNumber)) {
      return false;
    }
  } else {
    return false;
  }

  if (!item.contains("states") || !readStates(item["states"], definition)) {
    return false;
  }
  return compileDefinition(definition);
}

nlohmann::json
neighborCountsToJson(unsigned int mask)
{
  nlohmann::json counts = nlohmann::json::array();
  for (unsigned int count = 0u; count <= kMaximumNeighborCount; ++count) {
    if (((mask >> count) & 1u) != 0u) {
      counts.push_back(count);
    }
  }
  return counts;
}

bool
validId(std::string& id)
{
  id = RuleSetRegistry::normalizeId(std::move(id));
  if (id.empty() || id.size() > 64u) {
    return false;
  }
  for (const unsigned char character : id) {
    if ((character < 'A' || character > 'Z') &&
        (character < '0' || character > '9') && character != '_') {
      return false;
    }
  }
  return true;
}

bool
validateFamily(RuleFamilyDefinition& definition)
{
  if (!validId(definition.id) ||
      RuleSetRegistry::familyName(definition.kind) == nullptr ||
      definition.stateCount < 2u ||
      definition.stateCount > kMaximumStateCount ||
      definition.stateNames.size() != definition.stateCount ||
      definition.stateColors.size() != definition.stateCount) {
    return false;
  }
  if (definition.name.empty()) {
    definition.name = definition.id;
  }
  if ((definition.kind == RuleFamily::LifeLike &&
       definition.stateCount != 2u) ||
      (definition.kind == RuleFamily::Generations &&
       definition.stateCount < 3u) ||
      (definition.kind == RuleFamily::SpeciesLife &&
       definition.stateCount != 3u && definition.stateCount != 5u) ||
      (definition.kind == RuleFamily::VonNeumannTable &&
       definition.stateCount > kMaximumTableStateCount) ||
      (definition.kind == RuleFamily::Sandpile &&
       (definition.stateCount <= kSandpileHeightCount ||
        definition.stateCount > kMaximumSandpileStateCount)) ||
      (definition.kind == RuleFamily::Hodgepodge &&
       definition.stateCount < 3u) ||
      (definition.kind == RuleFamily::Turmite &&
       (definition.stateCount < 10u || definition.stateCount % 5u != 0u)) ||
      (definition.kind == RuleFamily::LatticeGas &&
       definition.stateCount != 16u) ||
      (definition.kind == RuleFamily::Dominance &&
       definition.stateCount < 4u) ||
      (definition.kind == RuleFamily::Elementary1D &&
       definition.stateCount != 2u)) {
    return false;
  }
  for (unsigned int state = 0u; state < definition.stateCount; ++state) {
    if (definition.stateNames[state].empty()) {
      return false;
    }
  }
  return true;
}

bool
sameFamilySchema(const RuleFamilyDefinition& left,
                 const RuleFamilyDefinition& right)
{
  return left.kind == right.kind && left.stateCount == right.stateCount &&
         left.stateNames == right.stateNames &&
         left.stateColors == right.stateColors;
}

std::string
legacyFamilyId(const RuleFamilyDefinition& family)
{
  std::uint64_t hash = 14695981039346656037ULL;
  const auto mix = [&hash](unsigned char value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  mix(static_cast<unsigned char>(family.kind));
  for (unsigned int state = 0u; state < family.stateCount; ++state) {
    for (const unsigned char character : family.stateNames[state]) {
      mix(character);
    }
    mix(0u);
    for (const unsigned char channel : family.stateColors[state]) {
      mix(channel);
    }
  }
  std::ostringstream id;
  id << "LEGACY_" << RuleSetRegistry::familyName(family.kind) << '_'
     << std::uppercase << std::hex << std::setw(16) << std::setfill('0')
     << hash;
  std::string result = id.str();
  std::replace(result.begin(), result.end(), '-', '_');
  std::replace(result.begin(), result.end(), '.', '_');
  return result;
}

RuleFamilyDefinition
familyFromLegacy(const LegacyRuleSetDefinition& legacy)
{
  RuleFamilyDefinition family;
  family.name = RuleSetRegistry::familyName(legacy.family);
  family.kind = legacy.family;
  family.stateCount = legacy.stateCount;
  family.stateNames = legacy.stateNames;
  family.stateColors = legacy.stateColors;
  family.id = legacyFamilyId(family);
  return family;
}

bool
compileRule(RuleSetDefinition& definition, const RuleFamilyDefinition& family)
{
  if (!validId(definition.id)) {
    return false;
  }
  definition.familyId = RuleSetRegistry::normalizeId(definition.familyId);
  if (definition.familyId != family.id) {
    return false;
  }
  if (definition.name.empty()) {
    definition.name = definition.id;
  }
  const unsigned int stateCount = family.stateCount;
  if (family.kind == RuleFamily::LifeLike ||
      family.kind == RuleFamily::Generations ||
      family.kind == RuleFamily::SpeciesLife) {
    if (!definition.rule.empty()) {
      unsigned int parsedBirth = 0u;
      unsigned int parsedSurvive = 0u;
      if (!RuleSetRegistry::parseLifeLikeRuleString(
            definition.rule, parsedBirth, parsedSurvive)) {
        return false;
      }
      definition.birthMask = parsedBirth;
      definition.surviveMask = parsedSurvive;
    }
    if ((definition.birthMask & ~0x1FFu) != 0u ||
        (definition.surviveMask & ~0x1FFu) != 0u ||
        (definition.birthMask & 1u) != 0u ||
        (family.kind == RuleFamily::LifeLike && stateCount != 2u) ||
        (family.kind == RuleFamily::Generations && stateCount < 3u)) {
      return false;
    }
    if (family.kind == RuleFamily::SpeciesLife && stateCount != 3u &&
        stateCount != 5u) {
      return false;
    }
    definition.transitionTable.fill(kBackgroundState);
    if (family.kind != RuleFamily::SpeciesLife) {
      for (unsigned int state = 0u; state < stateCount; ++state) {
        for (unsigned int neighbors = 0u;
             neighbors < RuleSet::kNeighborCountCount;
             ++neighbors) {
          const bool activeNeighbors =
            ((definition.birthMask >> neighbors) & 1u) != 0u;
          const bool survivingNeighbors =
            ((definition.surviveMask >> neighbors) & 1u) != 0u;
          unsigned char next = kBackgroundState;
          if (state == 0u) {
            if (survivingNeighbors) {
              next = 0u;
            } else if (family.kind == RuleFamily::Generations) {
              next = 2u;
            }
          } else if (state == kBackgroundState) {
            next = activeNeighbors ? 0u : kBackgroundState;
          } else if (family.kind == RuleFamily::Generations) {
            next = state + 1u < stateCount
                     ? static_cast<unsigned char>(state + 1u)
                     : kBackgroundState;
          }
          definition.transitionTable[RuleSet::transitionIndex(
            static_cast<unsigned char>(state),
            static_cast<unsigned char>(neighbors))] = next;
        }
      }
    }
    definition.rule = "B";
    for (unsigned int neighbors = 0u; neighbors <= kMaximumNeighborCount;
         ++neighbors) {
      if (((definition.birthMask >> neighbors) & 1u) != 0u) {
        definition.rule += std::to_string(neighbors);
      }
    }
    definition.rule += "/S";
    for (unsigned int neighbors = 0u; neighbors <= kMaximumNeighborCount;
         ++neighbors) {
      if (((definition.surviveMask >> neighbors) & 1u) != 0u) {
        definition.rule += std::to_string(neighbors);
      }
    }
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::LargerThanLife) {
    // More than two states adds Golly's C-state decay trail: a dying cell
    // walks states 2..C-1 before returning to background and never counts.
    if (stateCount < 2u || definition.neighborhoodRadius < 1u ||
        definition.neighborhoodRadius > kMaximumExtendedRadius ||
        definition.birthMinimum == 0u ||
        definition.birthMinimum > definition.birthMaximum ||
        definition.survivalMinimum > definition.survivalMaximum) {
      return false;
    }
    const unsigned int maximumCount =
      extendedNeighborhoodSize(definition.neighborhoodRadius,
                               definition.extendedNeighborhoodShape,
                               definition.includeCenter);
    if (definition.birthMaximum > maximumCount ||
        definition.survivalMaximum > maximumCount) {
      return false;
    }
    definition.rule =
      "R" + std::to_string(definition.neighborhoodRadius) + ",C" +
      std::to_string(stateCount) + ",M" +
      (definition.includeCenter ? "1" : "0") + ",S" +
      std::to_string(definition.survivalMinimum) + ".." +
      std::to_string(definition.survivalMaximum) + ",B" +
      std::to_string(definition.birthMinimum) + ".." +
      std::to_string(definition.birthMaximum) + "," +
      neighborhoodShapeSuffix(definition.extendedNeighborhoodShape);
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::MooreTable) {
    if (!definition.hasTransitionTable ||
        definition.transitionTableStateCount != stateCount) {
      return false;
    }
    for (unsigned int state = 0u; state < stateCount; ++state) {
      for (unsigned int neighbors = 0u;
           neighbors < RuleSet::kNeighborCountCount;
           ++neighbors) {
        if (static_cast<unsigned int>(
              definition.transitionTable[RuleSet::transitionIndex(
                static_cast<unsigned char>(state),
                static_cast<unsigned char>(neighbors))]) >= stateCount) {
          return false;
        }
      }
    }
  } else if (family.kind == RuleFamily::Cyclic) {
    // Radius one with the square shape keeps the Moore histogram path; any
    // other range or shape is Griffeath's long-range cyclic automaton.
    const bool extended = isCyclicExtended(definition);
    if (definition.neighborhoodRadius < 1u ||
        definition.neighborhoodRadius > kMaximumExtendedRadius) {
      return false;
    }
    const unsigned int maximumThreshold =
      extended ? extendedNeighborhoodSize(definition.neighborhoodRadius,
                                          definition.extendedNeighborhoodShape,
                                          false)
               : kMaximumNeighborCount;
    // An inert background leaves the other states to form the cycle.
    const unsigned int cycleLength =
      definition.inertBackground ? stateCount - 1u : stateCount;
    if (cycleLength < 2u || definition.cyclicThreshold < 1u ||
        definition.cyclicThreshold > maximumThreshold ||
        definition.cyclicStep < 1u || definition.cyclicStep >= cycleLength ||
        std::gcd(definition.cyclicStep, cycleLength) != 1u) {
      return false;
    }
    // The center never holds its own successor, so it never counts.
    definition.includeCenter = false;
    if (extended) {
      definition.rule =
        "R" + std::to_string(definition.neighborhoodRadius) + "/T" +
        std::to_string(definition.cyclicThreshold) + "/C" +
        std::to_string(cycleLength) + "/" +
        neighborhoodShapeSuffix(definition.extendedNeighborhoodShape);
    }
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::Hodgepodge) {
    if (definition.infectionDivisor < 1u || definition.illDivisor < 1u ||
        definition.infectionIncrement < 1u ||
        definition.infectionIncrement >= stateCount) {
      return false;
    }
    definition.rule = "HODGE/C" + std::to_string(stateCount) + "/K" +
                      std::to_string(definition.infectionDivisor) + ',' +
                      std::to_string(definition.illDivisor) + "/G" +
                      std::to_string(definition.infectionIncrement);
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::Turmite) {
    const unsigned int tapeColorCount = stateCount / 5u;
    if (definition.turnSequence.size() != tapeColorCount) {
      return false;
    }
    for (char turn : definition.turnSequence) {
      if (turn != 'L' && turn != 'R') {
        return false;
      }
    }
    definition.rule = "TURMITE/" + definition.turnSequence;
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::LatticeGas) {
    if (stateCount != 16u) {
      return false;
    }
    definition.rule = "HPP";
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::Dominance) {
    const unsigned int speciesCount = stateCount - 1u;
    if (definition.dominanceThreshold < 1u ||
        definition.dominanceThreshold > kMaximumNeighborCount ||
        definition.dominancePreyOffsets.empty()) {
      return false;
    }
    std::vector<bool> seen(speciesCount, false);
    for (unsigned int offset : definition.dominancePreyOffsets) {
      if (offset == 0u || offset >= speciesCount || seen[offset]) {
        return false;
      }
      seen[offset] = true;
    }
    definition.rule =
      "DOMINANCE/T" + std::to_string(definition.dominanceThreshold);
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::VonNeumannTable) {
    if (!compileVonNeumannTable(definition.ruleTable,
                                definition.tableSymmetry,
                                stateCount,
                                definition.vonNeumannTransitions)) {
      return false;
    }
    definition.rule = "TABLE/VN/" + definition.tableSymmetry;
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::Sandpile) {
    if (stateCount <= kSandpileHeightCount ||
        stateCount > kMaximumSandpileStateCount) {
      return false;
    }
    definition.rule =
      "SANDPILE/P" + std::to_string(stateCount - kSandpileHeightCount);
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else if (family.kind == RuleFamily::Elementary1D) {
    if (stateCount != 2u || definition.ruleNumber > 255u ||
        (definition.ruleNumber & 1u) != 0u) {
      return false;
    }
    for (unsigned int pattern = 0u; pattern < 8u; ++pattern) {
      definition.elementaryTransitions[pattern] =
        ((definition.ruleNumber >> pattern) & 1u) != 0u ? 0u : 1u;
    }
    definition.transitionTable.fill(kBackgroundState);
    definition.hasTransitionTable = false;
  } else {
    return false;
  }
  definition.transitionTableStateCount = stateCount;
  if (family.kind != RuleFamily::VonNeumannTable) {
    definition.vonNeumannTransitions.clear();
  }
  if (definition.seedPattern == RuleSeedPattern::Rle) {
    std::vector<RuleSeedCell> seedCells;
    if (!RuleSetRegistry::decodeSeedRle(
          definition.seedRle, stateCount, seedCells) ||
        seedCells.empty()) {
      return false;
    }
  } else {
    definition.seedRle.clear();
  }
  if (family.kind == RuleFamily::Elementary1D) {
    if (definition.elementaryTransitions[0] != kBackgroundState) {
      return false;
    }
  } else if (family.kind != RuleFamily::Cyclic &&
             definition.transitionTable[RuleSet::transitionIndex(
               kBackgroundState, 0u)] != kBackgroundState) {
    return false;
  }
  return true;
}

bool
readFamilyDefinition(const nlohmann::json& item,
                     RuleFamilyDefinition& definition)
{
  if (!item.is_object() || !item.contains("id") || !item["id"].is_string() ||
      !item.contains("model") || !item["model"].is_string() ||
      !item.contains("state_count") ||
      !readUnsigned(
        item["state_count"], kMaximumStateCount, definition.stateCount) ||
      !item.contains("states") || !item["states"].is_array()) {
    return false;
  }
  definition.id = item["id"].get<std::string>();
  definition.name = item.value("name", definition.id);
  if (!RuleSetRegistry::parseFamily(item["model"].get<std::string>(),
                                    definition.kind) ||
      item["states"].size() != definition.stateCount) {
    return false;
  }
  definition.stateNames.resize(definition.stateCount);
  definition.stateColors.resize(definition.stateCount);
  std::vector<bool> seen(definition.stateCount, false);
  for (const nlohmann::json& stateItem : item["states"]) {
    if (!stateItem.is_object() || !stateItem.contains("value")) {
      return false;
    }
    unsigned int state = 0u;
    if (!readUnsigned(stateItem["value"], definition.stateCount - 1u, state) ||
        seen[state] || !stateItem.contains("name") ||
        !stateItem["name"].is_string() || !stateItem.contains("color") ||
        !readColor(stateItem["color"], definition.stateColors[state])) {
      return false;
    }
    definition.stateNames[state] = stateItem["name"].get<std::string>();
    if (definition.stateNames[state].empty()) {
      return false;
    }
    seen[state] = true;
  }
  return std::find(seen.begin(), seen.end(), false) == seen.end() &&
         validateFamily(definition);
}

bool
parseModernRule(const nlohmann::json& item,
                const RuleFamilyDefinition& family,
                RuleSetDefinition& definition)
{
  if (!item.is_object() || !item.contains("id") || !item["id"].is_string() ||
      !item.contains("family_id") || !item["family_id"].is_string()) {
    return false;
  }
  definition.id = item["id"].get<std::string>();
  definition.name = item.value("name", definition.id);
  definition.familyId = item["family_id"].get<std::string>();
  if (family.kind == RuleFamily::LifeLike ||
      family.kind == RuleFamily::Generations ||
      family.kind == RuleFamily::SpeciesLife) {
    if (item.contains("rule") && !item["rule"].is_string()) {
      return false;
    }
    definition.rule = item.value("rule", "");
    if (!definition.rule.empty() &&
        !RuleSetRegistry::parseLifeLikeRuleString(
          definition.rule, definition.birthMask, definition.surviveMask)) {
      return false;
    }
    if (item.contains("birth")) {
      unsigned int birth = 0u;
      if (!readNeighborMask(item["birth"], birth) ||
          (!definition.rule.empty() && birth != definition.birthMask)) {
        return false;
      }
      definition.birthMask = birth;
    }
    if (item.contains("survive")) {
      unsigned int survive = 0u;
      if (!readNeighborMask(item["survive"], survive) ||
          (!definition.rule.empty() && survive != definition.surviveMask)) {
        return false;
      }
      definition.surviveMask = survive;
    }
  } else if (family.kind == RuleFamily::LargerThanLife) {
    if (!item.contains("radius") || !item.contains("include_center") ||
        !item["include_center"].is_boolean() || !item.contains("birth_min") ||
        !item.contains("birth_max") || !item.contains("survive_min") ||
        !item.contains("survive_max") || !item.contains("neighborhood") ||
        !item["neighborhood"].is_string() ||
        !readUnsigned(item["radius"],
                      kMaximumExtendedRadius,
                      definition.neighborhoodRadius) ||
        !readUnsigned(
          item["birth_min"], kMaximumExtendedCount, definition.birthMinimum) ||
        !readUnsigned(
          item["birth_max"], kMaximumExtendedCount, definition.birthMaximum) ||
        !readUnsigned(item["survive_min"],
                      kMaximumExtendedCount,
                      definition.survivalMinimum) ||
        !readUnsigned(item["survive_max"],
                      kMaximumExtendedCount,
                      definition.survivalMaximum)) {
      return false;
    }
    definition.includeCenter = item["include_center"].get<bool>();
    if (!parseNeighborhoodShape(item["neighborhood"].get<std::string>(),
                                definition.extendedNeighborhoodShape)) {
      return false;
    }
  } else if (family.kind == RuleFamily::MooreTable) {
    LegacyRuleSetDefinition legacy;
    legacy.stateCount = family.stateCount;
    if (!item.contains("transition_table") ||
        !readTransitionRows(item["transition_table"], legacy)) {
      return false;
    }
    definition.transitionTable = legacy.transitionTable;
    definition.hasTransitionTable = true;
    definition.transitionTableStateCount = family.stateCount;
  } else if (family.kind == RuleFamily::Cyclic) {
    if (!item.contains("threshold") || !item.contains("step") ||
        !readUnsigned(item["threshold"],
                      kMaximumExtendedCount,
                      definition.cyclicThreshold) ||
        !readUnsigned(
          item["step"], family.stateCount - 1u, definition.cyclicStep)) {
      return false;
    }
    // Optional Griffeath range and shape; compileRule bounds the threshold.
    if (item.contains("radius") &&
        !readUnsigned(item["radius"],
                      kMaximumExtendedRadius,
                      definition.neighborhoodRadius)) {
      return false;
    }
    if (item.contains("neighborhood") &&
        (!item["neighborhood"].is_string() ||
         !parseNeighborhoodShape(item["neighborhood"].get<std::string>(),
                                 definition.extendedNeighborhoodShape))) {
      return false;
    }
    if (item.contains("inert_background")) {
      if (!item["inert_background"].is_boolean()) {
        return false;
      }
      definition.inertBackground = item["inert_background"].get<bool>();
    }
  } else if (family.kind == RuleFamily::Hodgepodge) {
    if (!item.contains("infection_divisor") || !item.contains("ill_divisor") ||
        !item.contains("increment") ||
        !readUnsigned(
          item["infection_divisor"], 255u, definition.infectionDivisor) ||
        !readUnsigned(item["ill_divisor"], 255u, definition.illDivisor) ||
        !readUnsigned(item["increment"],
                      family.stateCount - 1u,
                      definition.infectionIncrement)) {
      return false;
    }
  } else if (family.kind == RuleFamily::Turmite) {
    if (!item.contains("turn_sequence") || !item["turn_sequence"].is_string()) {
      return false;
    }
    definition.turnSequence = item["turn_sequence"].get<std::string>();
  } else if (family.kind == RuleFamily::LatticeGas) {
    // HPP has no rule parameters beyond its fixed 16-state family.
  } else if (family.kind == RuleFamily::Dominance) {
    if (!item.contains("threshold") || !item.contains("prey_offsets") ||
        !item["prey_offsets"].is_array() ||
        !readUnsigned(item["threshold"],
                      kMaximumNeighborCount,
                      definition.dominanceThreshold)) {
      return false;
    }
    definition.dominancePreyOffsets.clear();
    for (const nlohmann::json& offsetValue : item["prey_offsets"]) {
      unsigned int offset = 0u;
      if (!readUnsigned(offsetValue, family.stateCount - 2u, offset)) {
        return false;
      }
      definition.dominancePreyOffsets.push_back(offset);
    }
  } else if (family.kind == RuleFamily::VonNeumannTable) {
    if (!item.contains("rule_table") || !item["rule_table"].is_array() ||
        (item.contains("symmetries") && !item["symmetries"].is_string())) {
      return false;
    }
    definition.tableSymmetry = item.value("symmetries", "none");
    definition.ruleTable.clear();
    for (const nlohmann::json& line : item["rule_table"]) {
      if (!line.is_string()) {
        return false;
      }
      definition.ruleTable.push_back(line.get<std::string>());
    }
  } else if (family.kind == RuleFamily::Sandpile) {
    // The sandpile is fixed by its family: heights plus source phases.
  } else if (family.kind == RuleFamily::Elementary1D) {
    if (!item.contains("rule_number") ||
        !readUnsigned(item["rule_number"], 255u, definition.ruleNumber)) {
      return false;
    }
  } else {
    return false;
  }

  if (item.contains("seed")) {
    const nlohmann::json& seed = item["seed"];
    if (!seed.is_object() || !seed.contains("pattern") ||
        !seed["pattern"].is_string() ||
        !RuleSetRegistry::parseSeedPattern(seed["pattern"].get<std::string>(),
                                           definition.seedPattern)) {
      return false;
    }
    if (seed.contains("radius") &&
        (!readUnsigned(seed["radius"], 128u, definition.seedRadius) ||
         definition.seedRadius < 1u)) {
      return false;
    }
    if (seed.contains("density") &&
        (!readUnsigned(seed["density"], 100u, definition.seedDensity) ||
         definition.seedDensity < 1u)) {
      return false;
    }
    if (definition.seedPattern == RuleSeedPattern::Rle) {
      if (!seed.contains("rle") || !seed["rle"].is_string()) {
        return false;
      }
      definition.seedRle = seed["rle"].get<std::string>();
    }
  }
  return compileRule(definition, family);
}

nlohmann::json
familyToJson(const RuleFamilyDefinition& definition)
{
  nlohmann::json item;
  item["id"] = definition.id;
  item["name"] = definition.name;
  item["model"] = RuleSetRegistry::familyName(definition.kind);
  item["state_count"] = definition.stateCount;
  item["states"] = nlohmann::json::array();
  for (unsigned int state = 0u; state < definition.stateCount; ++state) {
    item["states"].push_back({ { "value", state },
                               { "name", definition.stateNames[state] },
                               { "color", definition.stateColors[state] } });
  }
  return item;
}

nlohmann::json
ruleToJson(const RuleSetDefinition& definition,
           const RuleFamilyDefinition& family)
{
  nlohmann::json item;
  item["id"] = definition.id;
  item["name"] = definition.name;
  item["family_id"] = definition.familyId;
  if (family.kind == RuleFamily::LifeLike ||
      family.kind == RuleFamily::Generations ||
      family.kind == RuleFamily::SpeciesLife) {
    item["birth"] = neighborCountsToJson(definition.birthMask);
    item["survive"] = neighborCountsToJson(definition.surviveMask);
  } else if (family.kind == RuleFamily::LargerThanLife) {
    item["radius"] = definition.neighborhoodRadius;
    item["include_center"] = definition.includeCenter;
    item["birth_min"] = definition.birthMinimum;
    item["birth_max"] = definition.birthMaximum;
    item["survive_min"] = definition.survivalMinimum;
    item["survive_max"] = definition.survivalMaximum;
    item["neighborhood"] =
      neighborhoodShapeName(definition.extendedNeighborhoodShape);
  } else if (family.kind == RuleFamily::MooreTable) {
    item["transition_table"] = nlohmann::json::array();
    for (unsigned int state = 0u; state < family.stateCount; ++state) {
      nlohmann::json row = nlohmann::json::array();
      for (unsigned int neighbors = 0u;
           neighbors < RuleSet::kNeighborCountCount;
           ++neighbors) {
        row.push_back(definition.transitionTable[RuleSet::transitionIndex(
          static_cast<unsigned char>(state),
          static_cast<unsigned char>(neighbors))]);
      }
      item["transition_table"].push_back(row);
    }
  } else if (family.kind == RuleFamily::Cyclic) {
    item["threshold"] = definition.cyclicThreshold;
    item["step"] = definition.cyclicStep;
    if (isCyclicExtended(definition)) {
      item["radius"] = definition.neighborhoodRadius;
      item["neighborhood"] =
        neighborhoodShapeName(definition.extendedNeighborhoodShape);
    }
    if (definition.inertBackground) {
      item["inert_background"] = true;
    }
  } else if (family.kind == RuleFamily::Hodgepodge) {
    item["infection_divisor"] = definition.infectionDivisor;
    item["ill_divisor"] = definition.illDivisor;
    item["increment"] = definition.infectionIncrement;
  } else if (family.kind == RuleFamily::Turmite) {
    item["turn_sequence"] = definition.turnSequence;
  } else if (family.kind == RuleFamily::Dominance) {
    item["threshold"] = definition.dominanceThreshold;
    item["prey_offsets"] = definition.dominancePreyOffsets;
  } else if (family.kind == RuleFamily::VonNeumannTable) {
    item["symmetries"] = definition.tableSymmetry;
    item["rule_table"] = definition.ruleTable;
  } else if (family.kind == RuleFamily::Elementary1D) {
    item["rule_number"] = definition.ruleNumber;
  }
  if (definition.seedPattern != RuleSeedPattern::Automatic) {
    item["seed"] = {
      { "pattern", RuleSetRegistry::seedPatternName(definition.seedPattern) },
      { "radius", definition.seedRadius },
      { "density", definition.seedDensity }
    };
    if (definition.seedPattern == RuleSeedPattern::Rle) {
      item["seed"]["rle"] = definition.seedRle;
    }
  }
  return item;
}

} // namespace

RuleSetRegistry&
RuleSetRegistry::instance()
{
  static RuleSetRegistry s_instance;
  return s_instance;
}

std::string
RuleSetRegistry::normalizeId(std::string id)
{
  for (std::size_t i = 0; i < id.size(); ++i) {
    id[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(id[i])));
  }
  return id;
}

bool
RuleSetRegistry::parseFamily(const std::string& value, RuleFamily& family)
{
  if (value == "life_like") {
    family = RuleFamily::LifeLike;
  } else if (value == "generations") {
    family = RuleFamily::Generations;
  } else if (value == "moore_table") {
    family = RuleFamily::MooreTable;
  } else if (value == "cyclic") {
    family = RuleFamily::Cyclic;
  } else if (value == "species_life") {
    family = RuleFamily::SpeciesLife;
  } else if (value == "larger_than_life") {
    family = RuleFamily::LargerThanLife;
  } else if (value == "hodgepodge") {
    family = RuleFamily::Hodgepodge;
  } else if (value == "turmite") {
    family = RuleFamily::Turmite;
  } else if (value == "lattice_gas") {
    family = RuleFamily::LatticeGas;
  } else if (value == "dominance") {
    family = RuleFamily::Dominance;
  } else if (value == "elementary_1d") {
    family = RuleFamily::Elementary1D;
  } else if (value == "von_neumann_table") {
    family = RuleFamily::VonNeumannTable;
  } else if (value == "sandpile") {
    family = RuleFamily::Sandpile;
  } else {
    return false;
  }
  return true;
}

const char*
RuleSetRegistry::familyName(RuleFamily family)
{
  switch (family) {
    case RuleFamily::LifeLike:
      return "life_like";
    case RuleFamily::Generations:
      return "generations";
    case RuleFamily::MooreTable:
      return "moore_table";
    case RuleFamily::Cyclic:
      return "cyclic";
    case RuleFamily::SpeciesLife:
      return "species_life";
    case RuleFamily::LargerThanLife:
      return "larger_than_life";
    case RuleFamily::Hodgepodge:
      return "hodgepodge";
    case RuleFamily::Turmite:
      return "turmite";
    case RuleFamily::LatticeGas:
      return "lattice_gas";
    case RuleFamily::Dominance:
      return "dominance";
    case RuleFamily::Elementary1D:
      return "elementary_1d";
    case RuleFamily::VonNeumannTable:
      return "von_neumann_table";
    case RuleFamily::Sandpile:
      return "sandpile";
    default:
      return nullptr;
  }
}

bool
RuleSetRegistry::parseSeedPattern(const std::string& value,
                                  RuleSeedPattern& pattern)
{
  if (value == "automatic") {
    pattern = RuleSeedPattern::Automatic;
  } else if (value == "glider") {
    pattern = RuleSeedPattern::Glider;
  } else if (value == "single_cell") {
    pattern = RuleSeedPattern::SingleCell;
  } else if (value == "wire") {
    pattern = RuleSeedPattern::Wire;
  } else if (value == "active_soup") {
    pattern = RuleSeedPattern::ActiveSoup;
  } else if (value == "phase_soup") {
    pattern = RuleSeedPattern::PhaseSoup;
  } else if (value == "species_soup") {
    pattern = RuleSeedPattern::SpeciesSoup;
  } else if (value == "excitable_break") {
    pattern = RuleSeedPattern::ExcitableBreak;
  } else if (value == "turmite_swarm") {
    pattern = RuleSeedPattern::TurmiteSwarm;
  } else if (value == "particle_cloud") {
    pattern = RuleSeedPattern::ParticleCloud;
  } else if (value == "rle") {
    pattern = RuleSeedPattern::Rle;
  } else {
    return false;
  }
  return true;
}

const char*
RuleSetRegistry::seedPatternName(RuleSeedPattern pattern)
{
  switch (pattern) {
    case RuleSeedPattern::Automatic:
      return "automatic";
    case RuleSeedPattern::Glider:
      return "glider";
    case RuleSeedPattern::SingleCell:
      return "single_cell";
    case RuleSeedPattern::Wire:
      return "wire";
    case RuleSeedPattern::ActiveSoup:
      return "active_soup";
    case RuleSeedPattern::PhaseSoup:
      return "phase_soup";
    case RuleSeedPattern::SpeciesSoup:
      return "species_soup";
    case RuleSeedPattern::ExcitableBreak:
      return "excitable_break";
    case RuleSeedPattern::TurmiteSwarm:
      return "turmite_swarm";
    case RuleSeedPattern::ParticleCloud:
      return "particle_cloud";
    case RuleSeedPattern::Rle:
      return "rle";
    default:
      return nullptr;
  }
}

bool
RuleSetRegistry::decodeSeedRle(const std::string& text,
                               unsigned int stateCount,
                               std::vector<RuleSeedCell>& cells)
{
  // Starter patterns are small curated stamps; bound them like CellPattern.
  const int kMaximumExtent = 256;
  cells.clear();
  int x = 0;
  int y = 0;
  unsigned int run = 0u;
  bool terminated = false;
  for (std::size_t index = 0u; index < text.size() && !terminated; ++index) {
    const char character = text[index];
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    if (character >= '0' && character <= '9') {
      run = run * 10u + static_cast<unsigned int>(character - '0');
      if (run > static_cast<unsigned int>(kMaximumExtent)) {
        return false;
      }
      continue;
    }
    const int count = run == 0u ? 1 : static_cast<int>(run);
    run = 0u;
    if (character == '!') {
      terminated = true;
      continue;
    }
    if (character == '$') {
      y += count;
      x = 0;
      if (y >= kMaximumExtent) {
        return false;
      }
      continue;
    }
    unsigned int state = 0u;
    if (character == '.' || character == 'b') {
      state = 0u;
    } else if (character == 'o') {
      state = 1u;
    } else if (character >= 'A' && character <= 'X') {
      state = static_cast<unsigned int>(character - 'A') + 1u;
    } else {
      return false;
    }
    if (state >= stateCount || x + count > kMaximumExtent) {
      return false;
    }
    if (state != 0u) {
      for (int step = 0; step < count; ++step) {
        cells.push_back(RuleSeedCell{ x + step, y, gollyToIllumoState(state) });
      }
    }
    x += count;
  }
  return terminated;
}

bool
RuleSetRegistry::parseLifeLikeRuleString(const std::string& ruleStr,
                                         unsigned int& outBirth,
                                         unsigned int& outSurvive)
{
  outBirth = 0u;
  outSurvive = 0u;
  if (ruleStr.empty()) {
    return false;
  }

  std::string normalized;
  for (const char character : ruleStr) {
    if (std::isspace(static_cast<unsigned char>(character)) == 0) {
      normalized.push_back(
        static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
  }
  const std::size_t slash = normalized.find('/');
  if (slash == std::string::npos ||
      normalized.find('/', slash + 1u) != std::string::npos) {
    return false;
  }
  const std::string first = normalized.substr(0u, slash);
  const std::string second = normalized.substr(slash + 1u);
  const bool labeled = !first.empty() && (first[0] == 'B' || first[0] == 'S');
  if (labeled && (second.empty() ||
                  (first[0] == 'B' ? second[0] != 'S' : second[0] != 'B'))) {
    return false;
  }
  unsigned int birth = 0u;
  unsigned int survive = 0u;
  for (int section = 0; section < 2; ++section) {
    const std::string& part = section == 0 ? first : second;
    unsigned int mask = 0u;
    const std::size_t firstDigit = labeled ? 1u : 0u;
    for (std::size_t index = firstDigit; index < part.size(); ++index) {
      if (part[index] < '0' || part[index] > '8') {
        return false;
      }
      mask |= 1u << static_cast<unsigned int>(part[index] - '0');
    }
    const bool isBirth = labeled ? part[0] == 'B' : section == 1;
    if (isBirth) {
      birth = mask;
    } else {
      survive = mask;
    }
  }
  outBirth = birth;
  outSurvive = survive;
  return true;
}

RuleSetRegistry::RuleSetRegistry() = default;

void
RuleSetRegistry::clear()
{
  families.clear();
  rules.clear();
}

bool
RuleSetRegistry::registerFamily(const RuleFamilyDefinition& definition)
{
  RuleFamilyDefinition compiled = definition;
  if (!validateFamily(compiled)) {
    return false;
  }
  RuleSetRegistry staged = *this;
  bool replaced = false;
  for (RuleFamilyDefinition& existing : staged.families) {
    if (existing.id == compiled.id) {
      existing = compiled;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    staged.families.push_back(compiled);
  }
  for (RuleSetDefinition& rule : staged.rules) {
    if (rule.familyId == compiled.id && !compileRule(rule, compiled)) {
      return false;
    }
  }
  *this = std::move(staged);
  return true;
}

bool
RuleSetRegistry::registerRule(const RuleSetDefinition& definition)
{
  RuleSetDefinition compiled = definition;
  const RuleFamilyDefinition* family = getFamilyDefinition(compiled.familyId);
  if (family == nullptr || !compileRule(compiled, *family)) {
    return false;
  }
  for (RuleSetDefinition& existing : rules) {
    if (existing.id == compiled.id) {
      existing = compiled;
      return true;
    }
  }
  rules.push_back(compiled);
  return true;
}

bool
RuleSetRegistry::loadFromText(const std::string& text)
{
  try {
    const nlohmann::json root = nlohmann::json::parse(text);
    unsigned int schemaVersion = 1u;
    const nlohmann::json* definitions = &root;
    if (!root.is_array()) {
      if (!root.is_object() || !root.contains("schema_version") ||
          !root.contains("rules") ||
          !readUnsigned(root["schema_version"], 3u, schemaVersion) ||
          (schemaVersion != 2u && schemaVersion != 3u) ||
          !root["rules"].is_array()) {
        return false;
      }
      definitions = &root["rules"];
    }

    RuleSetRegistry pending = *this;
    for (const nlohmann::json& item : *definitions) {
      if (schemaVersion < 3u) {
        LegacyRuleSetDefinition legacy;
        const bool parsed = schemaVersion == 1u
                              ? parseLegacyDefinition(item, legacy)
                              : parseVersionTwoDefinition(item, legacy);
        if (!parsed) {
          return false;
        }
        RuleFamilyDefinition family = familyFromLegacy(legacy);
        const RuleFamilyDefinition* matchingFamily = nullptr;
        for (const RuleFamilyDefinition& existing : pending.families) {
          if (sameFamilySchema(existing, family)) {
            matchingFamily = &existing;
            break;
          }
        }
        if (matchingFamily != nullptr) {
          family.id = matchingFamily->id;
        } else if (!pending.registerFamily(family)) {
          return false;
        }
        RuleSetDefinition definition;
        definition.id = legacy.id;
        definition.name = legacy.name;
        definition.familyId = family.id;
        definition.rule = legacy.rule;
        definition.birthMask = legacy.birthMask;
        definition.surviveMask = legacy.surviveMask;
        definition.ruleNumber = legacy.ruleNumber;
        definition.transitionTable = legacy.transitionTable;
        definition.hasTransitionTable = family.kind == RuleFamily::MooreTable;
        definition.transitionTableStateCount = family.stateCount;
        if (!pending.registerRule(definition)) {
          return false;
        }
      } else {
        if (!item.is_object() || !item.contains("family_id") ||
            !item["family_id"].is_string()) {
          return false;
        }
        const RuleFamilyDefinition* family =
          pending.getFamilyDefinition(item["family_id"].get<std::string>());
        RuleSetDefinition definition;
        if (family == nullptr || !parseModernRule(item, *family, definition) ||
            !pending.registerRule(definition)) {
          return false;
        }
      }
    }
    *this = std::move(pending);
    return true;
  } catch (...) {
    return false;
  }
}

bool
RuleSetRegistry::loadFamiliesFromText(const std::string& text)
{
  try {
    const nlohmann::json root = nlohmann::json::parse(text);
    unsigned int schemaVersion = 0u;
    if (!root.is_object() || !root.contains("schema_version") ||
        !readUnsigned(root["schema_version"], 1u, schemaVersion) ||
        schemaVersion != 1u || !root.contains("families") ||
        !root["families"].is_array()) {
      return false;
    }
    RuleSetRegistry staged = *this;
    for (const nlohmann::json& item : root["families"]) {
      RuleFamilyDefinition definition;
      if (!readFamilyDefinition(item, definition) ||
          !staged.registerFamily(definition)) {
        return false;
      }
    }
    *this = std::move(staged);
    return true;
  } catch (...) {
    return false;
  }
}

bool
RuleSetRegistry::loadFromCatalogTexts(const std::string& familiesText,
                                      const std::string& rulesText)
{
  RuleSetRegistry staged;
  if (!familiesText.empty() && !staged.loadFamiliesFromText(familiesText)) {
    return false;
  }
  if (rulesText.empty() || !staged.loadFromText(rulesText)) {
    return false;
  }
  for (RuleFamilyDefinition& family : staged.families) {
    family.builtIn = true;
  }
  for (RuleSetDefinition& rule : staged.rules) {
    rule.builtIn = true;
  }
  *this = std::move(staged);
  return true;
}

bool
RuleSetRegistry::loadRulePackage(const std::string& text)
{
  try {
    const nlohmann::json root = nlohmann::json::parse(text);
    if (root.is_array() || !root.is_object() ||
        !root.contains("schema_version")) {
      return loadFromText(text);
    }
    unsigned int schemaVersion = 0u;
    if (!readUnsigned(root["schema_version"], 1u, schemaVersion) ||
        schemaVersion != 1u || !root.contains("families") ||
        !root["families"].is_array() || !root.contains("rules") ||
        !root["rules"].is_array()) {
      return false;
    }
    nlohmann::json familyCatalog = { { "schema_version", 1u },
                                     { "families", root["families"] } };
    nlohmann::json ruleCatalog = { { "schema_version", 3u },
                                   { "rules", root["rules"] } };
    RuleSetRegistry staged = *this;
    if (!staged.loadFamiliesFromText(familyCatalog.dump()) ||
        !staged.loadFromText(ruleCatalog.dump())) {
      return false;
    }
    *this = std::move(staged);
    return true;
  } catch (...) {
    return false;
  }
}

bool
RuleSetRegistry::isKnownFamily(const std::string& id) const
{
  return getFamilyDefinition(id) != nullptr;
}

bool
RuleSetRegistry::isKnownRule(const std::string& id) const
{
  for (const RuleSetDefinition& rule : rules) {
    if (rule.id == id) {
      return true;
    }
  }
  return false;
}

std::vector<std::string>
RuleSetRegistry::getKnownFamilies() const
{
  std::vector<std::string> result;
  result.reserve(families.size());
  for (const RuleFamilyDefinition& family : families) {
    result.push_back(family.id);
  }
  return result;
}

std::vector<std::string>
RuleSetRegistry::getKnownRules() const
{
  std::vector<std::string> result;
  result.reserve(rules.size());
  for (const RuleSetDefinition& rule : rules) {
    result.push_back(rule.id);
  }
  return result;
}

std::vector<std::string>
RuleSetRegistry::getKnownRules(const std::string& familyId) const
{
  const std::string normalized = normalizeId(familyId);
  std::vector<std::string> result;
  for (const RuleSetDefinition& rule : rules) {
    if (rule.familyId == normalized) {
      result.push_back(rule.id);
    }
  }
  return result;
}

const RuleFamilyDefinition*
RuleSetRegistry::getFamilyDefinition(const std::string& id) const
{
  const std::string normalized = normalizeId(id);
  for (const RuleFamilyDefinition& family : families) {
    if (family.id == normalized) {
      return &family;
    }
  }
  return nullptr;
}

const RuleSetDefinition*
RuleSetRegistry::getRuleSetDefinition(const std::string& id) const
{
  const std::string normalized = normalizeId(id);
  for (const RuleSetDefinition& rule : rules) {
    if (rule.id == normalized) {
      return &rule;
    }
  }
  return nullptr;
}

std::string
RuleSetRegistry::serializeFamilies(
  const std::vector<RuleFamilyDefinition>& definitions)
{
  nlohmann::json root;
  root["schema_version"] = 1u;
  root["families"] = nlohmann::json::array();
  for (const RuleFamilyDefinition& definition : definitions) {
    root["families"].push_back(familyToJson(definition));
  }
  return root.dump(2);
}

std::string
RuleSetRegistry::serializeCatalog(
  const std::vector<RuleFamilyDefinition>& familyDefinitions,
  const std::vector<RuleSetDefinition>& definitions)
{
  nlohmann::json root;
  root["schema_version"] = 3u;
  root["rules"] = nlohmann::json::array();
  for (const RuleSetDefinition& definition : definitions) {
    const RuleFamilyDefinition* family = nullptr;
    for (const RuleFamilyDefinition& candidate : familyDefinitions) {
      if (candidate.id == definition.familyId) {
        family = &candidate;
        break;
      }
    }
    if (family != nullptr) {
      root["rules"].push_back(ruleToJson(definition, *family));
    }
  }
  return root.dump(2);
}

std::string
RuleSetRegistry::serializeRulePackage(const RuleFamilyDefinition& family,
                                      const RuleSetDefinition& definition)
{
  nlohmann::json root;
  root["schema_version"] = 1u;
  root["families"] = nlohmann::json::array({ familyToJson(family) });
  root["rules"] = nlohmann::json::array({ ruleToJson(definition, family) });
  return root.dump(2);
}

std::unique_ptr<RuleSet>
RuleSetRegistry::createRuleSet(const std::string& id) const
{
  const RuleSetDefinition* definition = getRuleSetDefinition(id);
  const RuleFamilyDefinition* family =
    definition == nullptr ? nullptr : getFamilyDefinition(definition->familyId);
  if (definition == nullptr || family == nullptr) {
    return nullptr;
  }
  return std::make_unique<DataRuleSet>(*definition, *family);
}
