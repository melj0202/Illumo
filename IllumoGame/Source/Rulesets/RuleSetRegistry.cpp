#include "RuleSetRegistry.h"
#include "BriansBrainRuleSet.h"
#include "Elementary1DRuleSet.h"
#include "LifeLikeRuleSet.h"
#include "WireworldRuleSet.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <nlohmann/json.hpp>

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
      normalized.find('/', slash + 1) != std::string::npos) {
    return false;
  }
  const std::string first = normalized.substr(0, slash);
  const std::string second = normalized.substr(slash + 1);
  unsigned int birth = 0, survive = 0;
  const bool labeled = (!first.empty() && (first[0] == 'B' || first[0] == 'S'));
  if (labeled && (second.empty() ||
                  (first[0] == 'B' ? second[0] != 'S' : second[0] != 'B'))) {
    return false;
  }
  for (int section = 0; section < 2; ++section) {
    const std::string& part = section == 0 ? first : second;
    unsigned int mask = 0;
    for (std::size_t i = labeled ? 1u : 0u; i < part.size(); ++i) {
      if (part[i] < '0' || part[i] > '8') {
        return false;
      }
      mask |= 1u << static_cast<unsigned int>(part[i] - '0');
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

RuleSetRegistry::RuleSetRegistry()
{
  loadBuiltinDefaults();
}

void
RuleSetRegistry::clear()
{
  rules.clear();
}

static bool
insertRule(std::vector<RuleDefinition>& rules, const RuleDefinition& def)
{
  const std::string norm = RuleSetRegistry::normalizeId(def.id);
  if (norm.empty()) {
    return false;
  }
  if (def.family == "life_like") {
    unsigned int birth = 0, survive = 0;
    if ((def.birthMask & ~0x1FEu) != 0u || (def.surviveMask & ~0x1FFu) != 0u ||
        (!def.rule.empty() &&
         (!RuleSetRegistry::parseLifeLikeRuleString(def.rule, birth, survive) ||
          (birth & 1u) != 0u))) {
      return false;
    }
  } else if (def.family == "elementary_1d") {
    if (def.ruleNumber > 255u || (def.ruleNumber & 1u) != 0u) {
      return false;
    }
  } else if (def.family != "generations" && def.family != "wireworld") {
    return false;
  }
  for (RuleDefinition& existing : rules) {
    if (existing.id == norm) {
      existing = def;
      existing.id = norm;
      return true;
    }
  }
  RuleDefinition copy = def;
  copy.id = norm;
  rules.push_back(copy);
  return true;
}

bool
RuleSetRegistry::registerRule(const RuleDefinition& def)
{
  return insertRule(rules, def);
}

void
RuleSetRegistry::loadBuiltinDefaults()
{
  rules.clear();

  // 1. Conway's Game of Life
  RuleDefinition gol;
  gol.id = "GAME_OF_LIFE";
  gol.name = "Conway's Game of Life";
  gol.family = "life_like";
  gol.rule = "B3/S23";
  gol.birthMask = 1u << 3;
  gol.surviveMask = (1u << 2) | (1u << 3);
  registerRule(gol);

  // 2. Brian's Brain
  RuleDefinition bb;
  bb.id = "BRIANS_BRAIN";
  bb.name = "Brian's Brain";
  bb.family = "generations";
  registerRule(bb);

  // 3. Day & Night
  RuleDefinition dn;
  dn.id = "DAY_AND_NIGHT";
  dn.name = "Day & Night";
  dn.family = "life_like";
  dn.rule = "B3678/S34678";
  dn.birthMask = (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8);
  dn.surviveMask = (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 8);
  registerRule(dn);

  // 4. HighLife
  RuleDefinition hl;
  hl.id = "HIGHLIFE";
  hl.name = "HighLife";
  hl.family = "life_like";
  hl.rule = "B36/S23";
  hl.birthMask = (1u << 3) | (1u << 6);
  hl.surviveMask = (1u << 2) | (1u << 3);
  registerRule(hl);

  // 5. Life Without Death
  RuleDefinition lwd;
  lwd.id = "LIFE_WITHOUT_DEATH";
  lwd.name = "Life Without Death";
  lwd.family = "life_like";
  lwd.rule = "B3/S012345678";
  lwd.birthMask = 1u << 3;
  lwd.surviveMask = (1u << 9) - 1u;
  registerRule(lwd);

  // 6. Seeds
  RuleDefinition seeds;
  seeds.id = "SEEDS";
  seeds.name = "Seeds";
  seeds.family = "life_like";
  seeds.rule = "B2/S";
  seeds.birthMask = 1u << 2;
  seeds.surviveMask = 0u;
  registerRule(seeds);

  // 7. Wireworld
  RuleDefinition ww;
  ww.id = "WIREWORLD";
  ww.name = "Wireworld";
  ww.family = "wireworld";
  registerRule(ww);

  // 8. Wolfram Rule 90
  RuleDefinition r90;
  r90.id = "RULE_90";
  r90.name = "Wolfram Rule 90";
  r90.family = "elementary_1d";
  r90.ruleNumber = 90u;
  registerRule(r90);

  // 9. Wolfram Rule 184
  RuleDefinition r184;
  r184.id = "RULE_184";
  r184.name = "Wolfram Rule 184";
  r184.family = "elementary_1d";
  r184.ruleNumber = 184u;
  registerRule(r184);
}

static bool
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

static bool
readNeighborMask(const nlohmann::json& values, unsigned int& mask)
{
  if (!values.is_array()) {
    return false;
  }
  mask = 0;
  for (const nlohmann::json& value : values) {
    unsigned int count = 0;
    if (!readUnsigned(value, 8u, count)) {
      return false;
    }
    mask |= 1u << count;
  }
  return true;
}

static bool
readPaletteColor(const nlohmann::json& values,
                 std::array<unsigned char, 3>& color)
{
  if (!values.is_array() || values.size() != 3u) {
    return false;
  }
  for (std::size_t i = 0; i < color.size(); ++i) {
    unsigned int channel = 0;
    if (!readUnsigned(values[i], 255u, channel)) {
      return false;
    }
    color[i] = static_cast<unsigned char>(channel);
  }
  return true;
}

bool
RuleSetRegistry::loadFromText(const std::string& text)
{
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
    if (!root.is_array()) {
      return false;
    }

    std::vector<RuleDefinition> pending = rules;
    for (const nlohmann::json& item : root) {
      if (!item.is_object()) {
        return false;
      }
      RuleDefinition def;
      def.id = normalizeId(item.value("id", ""));
      if (def.id.empty()) {
        return false;
      }
      def.name = item.value("name", def.id);
      def.family = item.value("family", "life_like");
      def.rule = item.value("rule", "");

      if (def.family == "life_like") {
        if (!def.rule.empty() && !parseLifeLikeRuleString(
                                   def.rule, def.birthMask, def.surviveMask)) {
          return false;
        }
        if (item.contains("birth") &&
            !readNeighborMask(item["birth"], def.birthMask)) {
          return false;
        }
        if (item.contains("survive") &&
            !readNeighborMask(item["survive"], def.surviveMask)) {
          return false;
        }
      } else if (def.family == "elementary_1d") {
        if (item.contains("rule_number") &&
            !readUnsigned(item["rule_number"], 255u, def.ruleNumber)) {
          return false;
        }
      }

      if (item.contains("palette")) {
        const nlohmann::json& pal = item["palette"];
        if (!pal.is_object()) {
          return false;
        }
        if (pal.contains("alive")) {
          if (!readPaletteColor(pal["alive"], def.aliveColor)) {
            return false;
          }
          def.hasCustomPalette = true;
        }
        if (pal.contains("dead")) {
          if (!readPaletteColor(pal["dead"], def.deadColor)) {
            return false;
          }
          def.hasCustomPalette = true;
        }
      }

      if (!insertRule(pending, def)) {
        return false;
      }
    }
    rules.swap(pending);
    return true;
  } catch (...) {
    return false;
  }
}

bool
RuleSetRegistry::isKnownRule(const std::string& id) const
{
  for (const RuleDefinition& r : rules) {
    if (r.id == id) {
      return true;
    }
  }
  return false;
}

std::vector<std::string>
RuleSetRegistry::getKnownRules() const
{
  std::vector<std::string> result;
  result.reserve(rules.size());
  for (const RuleDefinition& r : rules) {
    result.push_back(r.id);
  }
  return result;
}

const RuleDefinition*
RuleSetRegistry::getRuleDefinition(const std::string& id) const
{
  const std::string norm = normalizeId(id);
  for (const RuleDefinition& r : rules) {
    if (r.id == norm) {
      return &r;
    }
  }
  return nullptr;
}

std::unique_ptr<RuleSet>
RuleSetRegistry::createRuleSet(const std::string& id, CellGrid* canvas) const
{
  const RuleDefinition* def = getRuleDefinition(id);
  if (def == nullptr) {
    return nullptr;
  }

  if (def->family == "life_like") {
    return std::make_unique<LifeLikeRuleSet>(canvas,
                                             def->id,
                                             def->birthMask,
                                             def->surviveMask,
                                             def->aliveColor,
                                             def->deadColor);
  }

  if (def->family == "elementary_1d") {
    return std::make_unique<Elementary1DRuleSet>(
      canvas, def->id, def->ruleNumber, def->aliveColor, def->deadColor);
  }

  if (def->family == "generations" || def->id == "BRIANS_BRAIN") {
    return std::make_unique<BriansBrainRuleSet>(canvas);
  }

  if (def->family == "wireworld" || def->id == "WIREWORLD") {
    return std::make_unique<WireworldRuleSet>(canvas);
  }

  return nullptr;
}
