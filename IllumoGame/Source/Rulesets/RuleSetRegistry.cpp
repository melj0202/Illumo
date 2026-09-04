#include "RuleSetRegistry.h"
#include "BriansBrainRuleSet.h"
#include "Elementary1DRuleSet.h"
#include "LifeLikeRuleSet.h"
#include "WireworldRuleSet.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

  bool hasLetter = false;
  for (char c : ruleStr) {
    if (c == 'b' || c == 'B' || c == 's' || c == 'S') {
      hasLetter = true;
      break;
    }
  }

  if (hasLetter) {
    char currentSection = '\0';
    for (char c : ruleStr) {
      if (c == 'b' || c == 'B') {
        currentSection = 'B';
      } else if (c == 's' || c == 'S') {
        currentSection = 'S';
      } else if (c >= '0' && c <= '8') {
        const unsigned int digit = static_cast<unsigned int>(c - '0');
        if (currentSection == 'B') {
          outBirth |= (1u << digit);
        } else if (currentSection == 'S') {
          outSurvive |= (1u << digit);
        }
      }
    }
    return true;
  }

  // Fallback: S/B format (e.g. "23/3")
  const std::size_t slashPos = ruleStr.find('/');
  if (slashPos != std::string::npos) {
    for (std::size_t i = 0; i < slashPos; ++i) {
      if (ruleStr[i] >= '0' && ruleStr[i] <= '8') {
        outSurvive |= (1u << static_cast<unsigned int>(ruleStr[i] - '0'));
      }
    }
    for (std::size_t i = slashPos + 1; i < ruleStr.size(); ++i) {
      if (ruleStr[i] >= '0' && ruleStr[i] <= '8') {
        outBirth |= (1u << static_cast<unsigned int>(ruleStr[i] - '0'));
      }
    }
    return true;
  }

  return false;
}

RuleSetRegistry::RuleSetRegistry()
{
  loadBuiltinDefaults();
  loadFromDefaultLocations();
}

void
RuleSetRegistry::clear()
{
  rules.clear();
}

void
RuleSetRegistry::registerRule(const RuleDefinition& def)
{
  const std::string norm = normalizeId(def.id);
  for (RuleDefinition& existing : rules) {
    if (existing.id == norm) {
      existing = def;
      existing.id = norm;
      return;
    }
  }
  RuleDefinition copy = def;
  copy.id = norm;
  rules.push_back(copy);
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

bool
RuleSetRegistry::loadFromFile(const std::string& filePath)
{
  std::ifstream file(filePath);
  if (!file.is_open()) {
    return false;
  }

  nlohmann::json root;
  try {
    file >> root;
    if (!root.is_array()) {
      return false;
    }

    for (const auto& item : root) {
      if (!item.is_object()) {
        continue;
      }
      RuleDefinition def;
      def.id = normalizeId(item.value("id", ""));
      if (def.id.empty()) {
        continue;
      }
      def.name = item.value("name", def.id);
      def.family = item.value("family", "life_like");
      def.rule = item.value("rule", "");

      if (def.family == "life_like") {
        if (!def.rule.empty()) {
          parseLifeLikeRuleString(def.rule, def.birthMask, def.surviveMask);
        }
        if (item.contains("birth") && item["birth"].is_array()) {
          def.birthMask = 0u;
          for (const auto& b : item["birth"]) {
            if (b.is_number_unsigned()) {
              def.birthMask |= (1u << b.get<unsigned int>());
            }
          }
        }
        if (item.contains("survive") && item["survive"].is_array()) {
          def.surviveMask = 0u;
          for (const auto& s : item["survive"]) {
            if (s.is_number_unsigned()) {
              def.surviveMask |= (1u << s.get<unsigned int>());
            }
          }
        }
      } else if (def.family == "elementary_1d") {
        def.ruleNumber = item.value("rule_number", 0u);
      }

      if (item.contains("palette") && item["palette"].is_object()) {
        const auto& pal = item["palette"];
        if (pal.contains("alive") && pal["alive"].is_array() &&
            pal["alive"].size() == 3) {
          def.aliveColor[0] = pal["alive"][0].get<unsigned char>();
          def.aliveColor[1] = pal["alive"][1].get<unsigned char>();
          def.aliveColor[2] = pal["alive"][2].get<unsigned char>();
          def.hasCustomPalette = true;
        }
        if (pal.contains("dead") && pal["dead"].is_array() &&
            pal["dead"].size() == 3) {
          def.deadColor[0] = pal["dead"][0].get<unsigned char>();
          def.deadColor[1] = pal["dead"][1].get<unsigned char>();
          def.deadColor[2] = pal["dead"][2].get<unsigned char>();
          def.hasCustomPalette = true;
        }
      }

      registerRule(def);
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool
RuleSetRegistry::loadFromDefaultLocations()
{
#ifdef _WIN32
  std::string executablePath(MAX_PATH, '\0');
  const DWORD pathLength =
    GetModuleFileNameA(nullptr,
                       executablePath.data(),
                       static_cast<unsigned long>(executablePath.size()));
  if (pathLength > 0 &&
      static_cast<std::size_t>(pathLength) < executablePath.size()) {
    executablePath.resize(pathLength);
    const std::filesystem::path exeDir =
      std::filesystem::path(executablePath).parent_path();
    const std::filesystem::path candidate = exeDir / "rulesets.json";
    if (std::filesystem::exists(candidate)) {
      if (loadFromFile(candidate.string())) {
        return true;
      }
    }
  }
#endif

  const std::filesystem::path curCandidate =
    std::filesystem::current_path() / "rulesets.json";
  if (std::filesystem::exists(curCandidate)) {
    if (loadFromFile(curCandidate.string())) {
      return true;
    }
  }

  const std::filesystem::path subCandidate =
    std::filesystem::current_path() / "IllumoGame" / "rulesets.json";
  if (std::filesystem::exists(subCandidate)) {
    if (loadFromFile(subCandidate.string())) {
      return true;
    }
  }

  return false;
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
