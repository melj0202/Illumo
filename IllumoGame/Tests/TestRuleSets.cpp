// Headless cellular-automaton rules tests (no OpenGL window).

#include "Game/SparseCellGrid.h"
#include "Rulesets/BriansBrainRuleSet.h"
#include "Rulesets/DayAndNightRuleSet.h"
#include "Rulesets/GameOfLifeRuleSet.h"
#include "Rulesets/HighlifeRuleSet.h"
#include "Rulesets/LifeLikeRuleSet.h"
#include "Rulesets/LifeWithoutDeathRuleSet.h"
#include "Rulesets/Rule184RuleSet.h"
#include "Rulesets/Rule90RuleSet.h"
#include "Rulesets/SeedsRuleSet.h"
#include "Rulesets/WireworldRuleSet.h"
#include "TestHarness.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>

static TestCounters g;

class CountingRuleSet : public RuleSet
{
public:
  CountingRuleSet()
    : RuleSet(nullptr)
  {
  }

  unsigned char nextState(unsigned char cell,
                          unsigned char aliveNeighbors) const override
  {
    callCount += 1u;
    return static_cast<unsigned char>((cell + aliveNeighbors) & 0x03u);
  }

  mutable std::size_t callCount = 0u;
};

static void
testGameOfLifeBlockStillLife()
{
  testSection("GoL: 2x2 block is still life");
  HeadlessCanvasFixture f(8, 8);
  f.clearDead();
  // Block
  f.setAlive(3, 3);
  f.setAlive(4, 3);
  f.setAlive(3, 4);
  f.setAlive(4, 4);

  GameOfLifeRuleSet rules(f.canvas);
  rules.calcGeneration(0, 0, 8, 8);

  testTrue(g,
           f.isAlive(3, 3) && f.isAlive(4, 3) && f.isAlive(3, 4) &&
             f.isAlive(4, 4),
           "block cells remain alive");
  // Neighbors of block should stay dead
  testTrue(g, !f.isAlive(2, 3) && !f.isAlive(5, 3), "outside block still dead");
}

static void
testGameOfLifeBlinker()
{
  testSection("GoL: blinker period-2 oscillator");
  HeadlessCanvasFixture f(8, 8);
  f.clearDead();
  // Horizontal blinker at row 4
  f.setAlive(3, 4);
  f.setAlive(4, 4);
  f.setAlive(5, 4);

  GameOfLifeRuleSet rules(f.canvas);
  rules.calcGeneration(0, 0, 8, 8);

  // Expect vertical
  testTrue(g,
           f.isAlive(4, 3) && f.isAlive(4, 4) && f.isAlive(4, 5),
           "blinker becomes vertical after 1 gen");
  testTrue(
    g, !f.isAlive(3, 4) && !f.isAlive(5, 4), "horizontal ends of blinker die");

  rules.calcGeneration(0, 0, 8, 8);
  testTrue(g,
           f.isAlive(3, 4) && f.isAlive(4, 4) && f.isAlive(5, 4),
           "blinker returns to horizontal after 2 gens");
  testTrue(
    g, !f.isAlive(4, 3) && !f.isAlive(4, 5), "vertical ends die on return");
}

static void
testGameOfLifeEmptyStaysEmpty()
{
  testSection("GoL: empty grid stays empty");
  HeadlessCanvasFixture f(6, 6);
  f.clearDead();
  GameOfLifeRuleSet rules(f.canvas);
  rules.calcGeneration(0, 0, 6, 6);
  int alive = 0;
  for (int y = 0; y < 6; ++y) {
    for (int x = 0; x < 6; ++x) {
      if (f.isAlive(x, y)) {
        ++alive;
      }
    }
  }
  testEqInt(g, alive, 0, "no spontaneous births on empty grid");
}

static void
testGameOfLifeEvalCellColors()
{
  testSection("GoL: evalCell colors");
  HeadlessCanvasFixture f(4, 4);
  GameOfLifeRuleSet rules(f.canvas);
  unsigned char rgb[3] = { 1, 2, 3 };
  rules.evalCell(HeadlessCanvasFixture::Dead, rgb);
  testTrue(g, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "dead is white");
  rules.evalCell(HeadlessCanvasFixture::Alive, rgb);
  testTrue(g, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "alive is black");
}

static void
testSeedsBirthOnly()
{
  testSection("Seeds: birth on 2 neighbors, no survival");
  HeadlessCanvasFixture f(8, 8);
  f.clearDead();
  // Two adjacent alive cells create seeds pattern around them
  f.setAlive(3, 3);
  f.setAlive(4, 3);

  SeedsRuleSet rules(f.canvas);
  rules.calcGeneration(0, 0, 8, 8);

  // Original cells should die (Seeds has no survival)
  testTrue(g, !f.isAlive(3, 3) && !f.isAlive(4, 3), "seeds parents die");
  // Cells with exactly 2 live neighbors are born — e.g. (3,2) sees (3,3) and
  // (4,3)
  testTrue(g,
           f.isAlive(3, 2) || f.isAlive(3, 4) || f.isAlive(4, 2) ||
             f.isAlive(4, 4),
           "at least one birth from 2-neighbor rule");
}

static void
testBriansBrainAliveBecomesDying()
{
  testSection("Brian's Brain: alive -> dying -> dead");
  HeadlessCanvasFixture f(6, 6);
  f.clearDead();
  f.setAlive(2, 2);

  BriansBrainRuleSet rules(f.canvas);
  rules.calcGeneration(0, 0, 6, 6);
  // Isolated alive becomes dying (2)
  testEqUChar(g, f.at(2, 2), 2, "alive becomes dying");

  rules.calcGeneration(0, 0, 6, 6);
  testEqUChar(g, f.at(2, 2), HeadlessCanvasFixture::Dead, "dying becomes dead");
}

static void
testHighlifeRuleTag()
{
  testSection("Highlife: rule tag");
  HeadlessCanvasFixture f(4, 4);
  HighlifeRuleSet rules(f.canvas);
  testTrue(g, rules.getRuleTag() == "HIGHLIFE", "Highlife rule tag");
}

static void
testWireworldHeadTailConductorCycle()
{
  testSection("Wireworld: head -> tail -> conductor");
  HeadlessCanvasFixture f(8, 8);
  f.clearDead(); // all empty (1)
  // Isolated head on empty background (no conductors).
  f.canvas->setCanvasPixel(3, 3, WireworldRuleSet::CELL_HEAD);

  WireworldRuleSet rules(f.canvas);
  rules.calcGeneration(0, 0, 8, 8);
  testEqUChar(g, f.at(3, 3), WireworldRuleSet::CELL_TAIL, "head becomes tail");

  rules.calcGeneration(0, 0, 8, 8);
  testEqUChar(
    g, f.at(3, 3), WireworldRuleSet::CELL_CONDUCTOR, "tail becomes conductor");

  // Isolated conductor stays conductor (0 head neighbors).
  rules.calcGeneration(0, 0, 8, 8);
  testEqUChar(g,
              f.at(3, 3),
              WireworldRuleSet::CELL_CONDUCTOR,
              "lonely conductor stays copper");
}

static void
testWireworldElectronOnWire()
{
  testSection("Wireworld: electron advances along a wire");
  HeadlessCanvasFixture f(10, 6);
  f.clearDead();
  // Horizontal conductor wire on row 2, columns 1..6
  for (int x = 1; x <= 6; ++x) {
    f.canvas->setCanvasPixel(x, 2, WireworldRuleSet::CELL_CONDUCTOR);
  }
  // Electron: head at x=1, tail immediately behind would be empty past wire
  // start; place head at (1,2) so next conductor (2,2) sees exactly one head
  // neighbor.
  f.canvas->setCanvasPixel(1, 2, WireworldRuleSet::CELL_HEAD);

  WireworldRuleSet rules(f.canvas);
  rules.calcGeneration(0, 0, 10, 6);

  // Old head → tail; (2,2) had one head neighbor → becomes head.
  testEqUChar(g, f.at(1, 2), WireworldRuleSet::CELL_TAIL, "old head is tail");
  testEqUChar(
    g, f.at(2, 2), WireworldRuleSet::CELL_HEAD, "signal moves one cell right");
  testEqUChar(g,
              f.at(3, 2),
              WireworldRuleSet::CELL_CONDUCTOR,
              "farther wire still copper");

  rules.calcGeneration(0, 0, 10, 6);
  testEqUChar(
    g, f.at(1, 2), WireworldRuleSet::CELL_CONDUCTOR, "tail becomes copper");
  testEqUChar(
    g, f.at(2, 2), WireworldRuleSet::CELL_TAIL, "head advances to tail");
  testEqUChar(g, f.at(3, 2), WireworldRuleSet::CELL_HEAD, "signal at x=3");
}

static void
testWireworldEmptyStaysEmpty()
{
  testSection("Wireworld: empty stays empty");
  HeadlessCanvasFixture f(5, 5);
  f.clearDead();
  WireworldRuleSet rules(f.canvas);
  rules.calcGeneration(0, 0, 5, 5);
  int nonEmpty = 0;
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      if (f.at(x, y) != WireworldRuleSet::CELL_EMPTY) {
        ++nonEmpty;
      }
    }
  }
  testEqInt(g, nonEmpty, 0, "no spontaneous signal on empty grid");
}

static void
testWireworldEvalCellColors()
{
  testSection("Wireworld: evalCell colors");
  HeadlessCanvasFixture f(2, 2);
  WireworldRuleSet rules(f.canvas);
  unsigned char rgb[3] = { 0, 0, 0 };
  rules.evalCell(WireworldRuleSet::CELL_EMPTY, rgb);
  testTrue(
    g, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "empty is white");
  rules.evalCell(WireworldRuleSet::CELL_HEAD, rgb);
  testTrue(g, rgb[2] > rgb[0], "head is bluish");
  rules.evalCell(WireworldRuleSet::CELL_TAIL, rgb);
  testTrue(g, rgb[0] > rgb[2], "tail is reddish");
  rules.evalCell(WireworldRuleSet::CELL_CONDUCTOR, rgb);
  testTrue(g, rgb[0] > 200 && rgb[1] > 150, "conductor is golden");
  testTrue(g, rules.getRuleTag() == "WIREWORLD", "Wireworld rule tag");
}

static void
testDayAndNightTruthTable()
{
  testSection("Day & Night: B3678/S34678 truth table and colors");
  HeadlessCanvasFixture f(2, 2);
  LifeLikeRuleSet rules(f.canvas,
                        "DAY_AND_NIGHT",
                        (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8),
                        (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) |
                          (1u << 8));
  bool deadTransitionsMatch = true;
  bool aliveTransitionsMatch = true;
  for (unsigned char neighbors = 0; neighbors <= 8; ++neighbors) {
    const bool shouldBirth =
      neighbors == 3 || neighbors == 6 || neighbors == 7 || neighbors == 8;
    const bool shouldSurvive = neighbors == 3 || neighbors == 4 ||
                               neighbors == 6 || neighbors == 7 ||
                               neighbors == 8;
    deadTransitionsMatch =
      deadTransitionsMatch &&
      rules.nextState(HeadlessCanvasFixture::Dead, neighbors) ==
        (shouldBirth ? HeadlessCanvasFixture::Alive
                     : HeadlessCanvasFixture::Dead);
    aliveTransitionsMatch =
      aliveTransitionsMatch &&
      rules.nextState(HeadlessCanvasFixture::Alive, neighbors) ==
        (shouldSurvive ? HeadlessCanvasFixture::Alive
                       : HeadlessCanvasFixture::Dead);
  }
  testTrue(g, deadTransitionsMatch, "dead cells follow B3678");
  testTrue(g, aliveTransitionsMatch, "alive cells follow S34678");
  unsigned char rgb[3] = { 127, 127, 127 };
  rules.evalCell(HeadlessCanvasFixture::Dead, rgb);
  testTrue(
    g, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "dead cell is white");
  rules.evalCell(HeadlessCanvasFixture::Alive, rgb);
  testTrue(g, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "alive cell is black");
  testTrue(g, rules.getRuleTag() == "DAY_AND_NIGHT", "Day & Night rule tag");
}

static void
testHighlifeTruthTable()
{
  testSection("Highlife: B36/S23 truth table and colors");
  HeadlessCanvasFixture f(2, 2);
  LifeLikeRuleSet rules(
    f.canvas, "HIGHLIFE", (1u << 3) | (1u << 6), (1u << 2) | (1u << 3));
  bool deadTransitionsMatch = true;
  bool aliveTransitionsMatch = true;
  for (unsigned char neighbors = 0; neighbors <= 8; ++neighbors) {
    const bool shouldBirth = neighbors == 3 || neighbors == 6;
    const bool shouldSurvive = neighbors == 2 || neighbors == 3;
    deadTransitionsMatch =
      deadTransitionsMatch &&
      rules.nextState(HeadlessCanvasFixture::Dead, neighbors) ==
        (shouldBirth ? HeadlessCanvasFixture::Alive
                     : HeadlessCanvasFixture::Dead);
    aliveTransitionsMatch =
      aliveTransitionsMatch &&
      rules.nextState(HeadlessCanvasFixture::Alive, neighbors) ==
        (shouldSurvive ? HeadlessCanvasFixture::Alive
                       : HeadlessCanvasFixture::Dead);
  }
  testTrue(g, deadTransitionsMatch, "dead cells follow B36");
  testTrue(g, aliveTransitionsMatch, "alive cells follow S23");
  unsigned char rgb[3] = { 127, 127, 127 };
  rules.evalCell(HeadlessCanvasFixture::Dead, rgb);
  testTrue(
    g, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "dead cell is white");
  rules.evalCell(HeadlessCanvasFixture::Alive, rgb);
  testTrue(g, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "alive cell is black");
  testTrue(g, rules.getRuleTag() == "HIGHLIFE", "Highlife rule tag");
}

static void
testLifeWithoutDeathTruthTable()
{
  testSection("Life Without Death: B3/Sall truth table and colors");
  HeadlessCanvasFixture f(2, 2);
  LifeLikeRuleSet rules(
    f.canvas, "LIFE_WITHOUT_DEATH", 1u << 3, (1u << 9) - 1u);
  bool deadTransitionsMatch = true;
  bool aliveTransitionsMatch = true;
  for (unsigned char neighbors = 0; neighbors <= 8; ++neighbors) {
    deadTransitionsMatch =
      deadTransitionsMatch &&
      rules.nextState(HeadlessCanvasFixture::Dead, neighbors) ==
        (neighbors == 3 ? HeadlessCanvasFixture::Alive
                        : HeadlessCanvasFixture::Dead);
    aliveTransitionsMatch =
      aliveTransitionsMatch &&
      rules.nextState(HeadlessCanvasFixture::Alive, neighbors) ==
        HeadlessCanvasFixture::Alive;
  }
  testTrue(
    g, deadTransitionsMatch, "dead cells are born only with three neighbors");
  testTrue(g, aliveTransitionsMatch, "alive cells never die");
  unsigned char rgb[3] = { 127, 127, 127 };
  rules.evalCell(HeadlessCanvasFixture::Dead, rgb);
  testTrue(
    g, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "dead cell is white");
  rules.evalCell(HeadlessCanvasFixture::Alive, rgb);
  testTrue(g, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "alive cell is black");
  testTrue(g,
           rules.getRuleTag() == "LIFE_WITHOUT_DEATH",
           "Life Without Death rule tag");
}

static void
testSeedsTruthTableAndColors()
{
  testSection("Seeds: B2/Snone truth table and colors");
  HeadlessCanvasFixture f(2, 2);
  LifeLikeRuleSet rules(f.canvas, "SEEDS", 1u << 2, 0u);
  bool transitionsMatch = true;
  for (unsigned char neighbors = 0; neighbors <= 8; ++neighbors) {
    const unsigned char birth = neighbors == 2 ? HeadlessCanvasFixture::Alive
                                               : HeadlessCanvasFixture::Dead;
    transitionsMatch =
      transitionsMatch &&
      rules.nextState(HeadlessCanvasFixture::Dead, neighbors) == birth &&
      rules.nextState(HeadlessCanvasFixture::Alive, neighbors) ==
        HeadlessCanvasFixture::Dead;
  }
  testTrue(g, transitionsMatch, "all cells follow B2 with no survival");
  unsigned char rgb[3] = { 127, 127, 127 };
  rules.evalCell(HeadlessCanvasFixture::Dead, rgb);
  testTrue(
    g, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "dead cell is white");
  rules.evalCell(HeadlessCanvasFixture::Alive, rgb);
  testTrue(g, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "alive cell is black");
  testTrue(g, rules.getRuleTag() == "SEEDS", "Seeds rule tag");
}

static void
testBriansBrainStateMachineAndColors()
{
  testSection("Brian's Brain: state machine and colors");
  HeadlessCanvasFixture f(2, 2);
  BriansBrainRuleSet rules(f.canvas);
  testEqUChar(
    g, rules.nextState(1, 2), 0, "dead with two neighbors becomes alive");
  testEqUChar(
    g, rules.nextState(1, 1), 1, "dead without two neighbors stays dead");
  testEqUChar(g, rules.nextState(0, 8), 2, "alive becomes dying");
  testEqUChar(g, rules.nextState(2, 0), 1, "dying becomes dead");
  testEqUChar(g, rules.nextState(9, 0), 1, "unknown state falls back to dead");
  unsigned char rgb[3] = { 127, 127, 127 };
  rules.evalCell(1, rgb);
  testTrue(
    g, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "dead cell is white");
  rules.evalCell(0, rgb);
  testTrue(g, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "alive cell is black");
  rules.evalCell(2, rgb);
  testTrue(g,
           rgb[0] == 0 && rgb[1] == 164 && rgb[2] == 128,
           "dying cell uses teal color");
  testTrue(g, rules.getRuleTag() == "BRIANS_BRAIN", "Brian's Brain rule tag");
}

static void
testWireworldConductorNeighborCounts()
{
  testSection("Wireworld: conductor births with one or two heads only");
  HeadlessCanvasFixture f(2, 2);
  WireworldRuleSet rules(f.canvas);
  testEqUChar(g,
              rules.nextState(WireworldRuleSet::CELL_CONDUCTOR, 0),
              WireworldRuleSet::CELL_CONDUCTOR,
              "zero heads keeps conductor");
  testEqUChar(g,
              rules.nextState(WireworldRuleSet::CELL_CONDUCTOR, 1),
              WireworldRuleSet::CELL_HEAD,
              "one head creates electron head");
  testEqUChar(g,
              rules.nextState(WireworldRuleSet::CELL_CONDUCTOR, 2),
              WireworldRuleSet::CELL_HEAD,
              "two heads create electron head");
  testEqUChar(g,
              rules.nextState(WireworldRuleSet::CELL_CONDUCTOR, 3),
              WireworldRuleSet::CELL_CONDUCTOR,
              "three heads keep conductor");
}

static void
testTransitionTableCacheAndEquivalence()
{
  testSection("Rules: cached 256 by 9 transition table");
  CountingRuleSet counting;
  const RuleSet::TransitionTable& first = counting.getTransitionTable();
  const std::size_t expectedCalls =
    RuleSet::kCellStateCount * RuleSet::kNeighborCountCount;
  testEqInt(g,
            static_cast<int>(counting.callCount),
            static_cast<int>(expectedCalls),
            "first access evaluates every state and neighbor pair once");
  const RuleSet::TransitionTable& second = counting.getTransitionTable();
  testTrue(g, &first == &second, "subsequent access returns the same table");
  testEqInt(g,
            static_cast<int>(counting.callCount),
            static_cast<int>(expectedCalls),
            "subsequent access performs no transition calls");

  GameOfLifeRuleSet gameOfLife(nullptr);
  SeedsRuleSet seeds(nullptr);
  BriansBrainRuleSet briansBrain(nullptr);
  HighlifeRuleSet highlife(nullptr);
  DayAndNightRuleSet dayAndNight(nullptr);
  LifeWithoutDeathRuleSet lifeWithoutDeath(nullptr);
  WireworldRuleSet wireworld(nullptr);
  Rule90RuleSet rule90(nullptr);
  Rule184RuleSet rule184(nullptr);
  const RuleSet* rules[] = { &gameOfLife, &seeds,       &briansBrain,
                             &highlife,   &dayAndNight, &lifeWithoutDeath,
                             &wireworld,  &rule90,      &rule184 };
  bool equivalent = true;
  for (const RuleSet* rule : rules) {
    const RuleSet::TransitionTable& table = rule->getTransitionTable();
    for (std::size_t state = 0u; state < RuleSet::kCellStateCount && equivalent;
         ++state) {
      for (std::size_t neighbors = 0u; neighbors < RuleSet::kNeighborCountCount;
           ++neighbors) {
        const unsigned char cell = static_cast<unsigned char>(state);
        const unsigned char aliveNeighbors =
          static_cast<unsigned char>(neighbors);
        if (table[RuleSet::transitionIndex(cell, aliveNeighbors)] !=
            rule->nextState(cell, aliveNeighbors)) {
          equivalent = false;
          break;
        }
      }
    }
  }
  testTrue(g,
           equivalent,
           "all shipped rules match direct transitions for all 2304 inputs");
}

static void
testElementarySpaceTime()
{
  testSection("Rules: Rule 90/184 serial space-time path");
  Rule90RuleSet rule90(nullptr);
  testTrue(g,
           rule90.getNeighborhoodKind() ==
             RuleSet::NeighborhoodKind::Elementary1D,
           "Rule 90 is elementary");
  testEqUChar(g, rule90.nextElementary(0, 1, 0), 1, "Rule 90 0_0 -> dead");
  testEqUChar(g, rule90.nextElementary(0, 1, 1), 0, "Rule 90 0_1 -> alive");
  testEqUChar(g, rule90.nextElementary(1, 1, 0), 0, "Rule 90 1_0 -> alive");

  SparseCellGrid grid;
  grid.setCell(CellAddress{ 0, 0 }, 0);
  testTrue(g, grid.advance(rule90), "Rule 90 advances");
  testEqUChar(g, grid.getCell(CellAddress{ 0, 0 }), 0, "source row stays");
  testEqUChar(g, grid.getCell(CellAddress{ -1, 1 }), 0, "left child");
  testEqUChar(g, grid.getCell(CellAddress{ 1, 1 }), 0, "right child");
  testEqUChar(g, grid.getCell(CellAddress{ 0, 1 }), 1, "center child empty");

  Rule184RuleSet rule184(nullptr);
  SparseCellGrid traffic;
  traffic.setCell(CellAddress{ 0, 0 }, 0);
  traffic.setCell(CellAddress{ 1, 0 }, 1);
  testTrue(g, traffic.advance(rule184), "Rule 184 advances");
  testEqUChar(g, traffic.getCell(CellAddress{ 1, 1 }), 0, "car moved right");
}

static int
runRuleSetCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerRuleSetTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Rules.TransitionTable", []() {
    return runRuleSetCase(testTransitionTableCacheAndEquivalence);
  });
  registry.add("IllumoGame.Rules.ElementarySpaceTime",
               []() { return runRuleSetCase(testElementarySpaceTime); });
  registry.add("IllumoGame.Rules.GameOfLifeBlock",
               []() { return runRuleSetCase(testGameOfLifeBlockStillLife); });
  registry.add("IllumoGame.Rules.GameOfLifeBlinker",
               []() { return runRuleSetCase(testGameOfLifeBlinker); });
  registry.add("IllumoGame.Rules.GameOfLifeEmpty",
               []() { return runRuleSetCase(testGameOfLifeEmptyStaysEmpty); });
  registry.add("IllumoGame.Rules.GameOfLifeColors",
               []() { return runRuleSetCase(testGameOfLifeEvalCellColors); });
  registry.add("IllumoGame.Rules.SeedsBirthOnly",
               []() { return runRuleSetCase(testSeedsBirthOnly); });
  registry.add("IllumoGame.Rules.BriansBrainTransition", []() {
    return runRuleSetCase(testBriansBrainAliveBecomesDying);
  });
  registry.add("IllumoGame.Rules.HighlifeTag",
               []() { return runRuleSetCase(testHighlifeRuleTag); });
  registry.add("IllumoGame.Rules.WireworldCycle", []() {
    return runRuleSetCase(testWireworldHeadTailConductorCycle);
  });
  registry.add("IllumoGame.Rules.WireworldElectron",
               []() { return runRuleSetCase(testWireworldElectronOnWire); });
  registry.add("IllumoGame.Rules.WireworldEmpty",
               []() { return runRuleSetCase(testWireworldEmptyStaysEmpty); });
  registry.add("IllumoGame.Rules.WireworldColors",
               []() { return runRuleSetCase(testWireworldEvalCellColors); });
  registry.add("IllumoGame.Rules.DayAndNightTruthTable",
               []() { return runRuleSetCase(testDayAndNightTruthTable); });
  registry.add("IllumoGame.Rules.HighlifeTruthTable",
               []() { return runRuleSetCase(testHighlifeTruthTable); });
  registry.add("IllumoGame.Rules.LifeWithoutDeathTruthTable",
               []() { return runRuleSetCase(testLifeWithoutDeathTruthTable); });
  registry.add("IllumoGame.Rules.SeedsTruthTable",
               []() { return runRuleSetCase(testSeedsTruthTableAndColors); });
  registry.add("IllumoGame.Rules.BriansBrainStateMachine", []() {
    return runRuleSetCase(testBriansBrainStateMachineAndColors);
  });
  registry.add("IllumoGame.Rules.WireworldConductorNeighbors", []() {
    return runRuleSetCase(testWireworldConductorNeighborCounts);
  });
}
