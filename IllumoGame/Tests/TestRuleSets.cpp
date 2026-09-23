#include "DenseRuleEvaluator.h"
// Headless cellular-automaton rules tests (no OpenGL window).

#include "Game/RuleCatalogLoader.h"
#include "Game/SparseCellGrid.h"
#include "Rulesets/BriansBrainRuleSet.h"
#include "Rulesets/Elementary1DRuleSet.h"
#include "Rulesets/LifeLikeRuleSet.h"
#include "Rulesets/RuleSetRegistry.h"
#include "Rulesets/WireworldRuleSet.h"
#include "TestHarness.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>
#include <limits>

static TestCounters g;

class CountingRuleSet : public RuleSet
{
public:
  CountingRuleSet()
    : RuleSet()
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

  LifeLikeRuleSet rules{};
  DenseRuleEvaluator rulesDense(f.canvas, rules);
  rulesDense.calcGeneration(0, 0, 8, 8);

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

  LifeLikeRuleSet rules{};
  DenseRuleEvaluator rulesDense(f.canvas, rules);
  rulesDense.calcGeneration(0, 0, 8, 8);

  // Expect vertical
  testTrue(g,
           f.isAlive(4, 3) && f.isAlive(4, 4) && f.isAlive(4, 5),
           "blinker becomes vertical after 1 gen");
  testTrue(
    g, !f.isAlive(3, 4) && !f.isAlive(5, 4), "horizontal ends of blinker die");

  rulesDense.calcGeneration(0, 0, 8, 8);
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
  LifeLikeRuleSet rules{};
  DenseRuleEvaluator rulesDense(f.canvas, rules);
  rulesDense.calcGeneration(0, 0, 6, 6);
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
  LifeLikeRuleSet rules{};
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

  LifeLikeRuleSet rules("SEEDS", 1u << 2, 0u);
  DenseRuleEvaluator rulesDense(f.canvas, rules);
  rulesDense.calcGeneration(0, 0, 8, 8);

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

  BriansBrainRuleSet rules{};
  DenseRuleEvaluator rulesDense(f.canvas, rules);
  rulesDense.calcGeneration(0, 0, 6, 6);
  // Isolated alive becomes dying (2)
  testEqUChar(g, f.at(2, 2), 2, "alive becomes dying");

  rulesDense.calcGeneration(0, 0, 6, 6);
  testEqUChar(g, f.at(2, 2), HeadlessCanvasFixture::Dead, "dying becomes dead");
}

static void
testHighlifeRuleTag()
{
  testSection("Highlife: rule tag");
  LifeLikeRuleSet rules(
    "HIGHLIFE", (1u << 3) | (1u << 6), (1u << 2) | (1u << 3));
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

  WireworldRuleSet rules{};
  DenseRuleEvaluator rulesDense(f.canvas, rules);
  rulesDense.calcGeneration(0, 0, 8, 8);
  testEqUChar(g, f.at(3, 3), WireworldRuleSet::CELL_TAIL, "head becomes tail");

  rulesDense.calcGeneration(0, 0, 8, 8);
  testEqUChar(
    g, f.at(3, 3), WireworldRuleSet::CELL_CONDUCTOR, "tail becomes conductor");

  // Isolated conductor stays conductor (0 head neighbors).
  rulesDense.calcGeneration(0, 0, 8, 8);
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

  WireworldRuleSet rules{};
  DenseRuleEvaluator rulesDense(f.canvas, rules);
  rulesDense.calcGeneration(0, 0, 10, 6);

  // Old head → tail; (2,2) had one head neighbor → becomes head.
  testEqUChar(g, f.at(1, 2), WireworldRuleSet::CELL_TAIL, "old head is tail");
  testEqUChar(
    g, f.at(2, 2), WireworldRuleSet::CELL_HEAD, "signal moves one cell right");
  testEqUChar(g,
              f.at(3, 2),
              WireworldRuleSet::CELL_CONDUCTOR,
              "farther wire still copper");

  rulesDense.calcGeneration(0, 0, 10, 6);
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
  WireworldRuleSet rules{};
  DenseRuleEvaluator rulesDense(f.canvas, rules);
  rulesDense.calcGeneration(0, 0, 5, 5);
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
  WireworldRuleSet rules{};
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
  LifeLikeRuleSet rules("DAY_AND_NIGHT",
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
  LifeLikeRuleSet rules(
    "HIGHLIFE", (1u << 3) | (1u << 6), (1u << 2) | (1u << 3));
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
  LifeLikeRuleSet rules("LIFE_WITHOUT_DEATH", 1u << 3, (1u << 9) - 1u);
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
  LifeLikeRuleSet rules("SEEDS", 1u << 2, 0u);
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
  BriansBrainRuleSet rules{};
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
  WireworldRuleSet rules{};
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

  LifeLikeRuleSet gameOfLife{};
  LifeLikeRuleSet seeds("SEEDS", 1u << 2, 0u);
  BriansBrainRuleSet briansBrain{};
  LifeLikeRuleSet highlife(
    "HIGHLIFE", (1u << 3) | (1u << 6), (1u << 2) | (1u << 3));
  LifeLikeRuleSet dayAndNight("DAY_AND_NIGHT",
                              (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8),
                              (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) |
                                (1u << 8));
  LifeLikeRuleSet lifeWithoutDeath(
    "LIFE_WITHOUT_DEATH", 1u << 3, (1u << 9) - 1u);
  WireworldRuleSet wireworld{};
  Elementary1DRuleSet rule90("RULE_90", 90u);
  Elementary1DRuleSet rule184("RULE_184", 184u);
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
  Elementary1DRuleSet rule90("RULE_90", 90u);
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

  Elementary1DRuleSet rule184("RULE_184", 184u);
  for (const RuleSet* rule : { static_cast<const RuleSet*>(&rule90),
                               static_cast<const RuleSet*>(&rule184) }) {
    CellGrid dense(16, 16);
    dense.clearCells();
    SparseCellGrid reference(1, 1);
    dense.setCanvasPixel(0, 15, 0);
    dense.setCanvasPixel(5, 0, 0);
    reference.setCell(CellAddress{ 0, 15 }, 0);
    reference.setCell(CellAddress{ 5, 0 }, 0);
    DenseRuleEvaluator evaluator(&dense, *rule);
    evaluator.calcGeneration(0, 0, 16, 16);
    testTrue(
      g, reference.advance(*rule), "finite elementary reference advances");
    bool identical = true;
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        identical = identical && dense.getCanvasPixel(x, y) ==
                                   reference.getCell(CellAddress{ x, y });
      }
    }
    testTrue(g,
             identical,
             "dense elementary matches sparse wrap and retained history");
    dense.clearCells();
    evaluator.calcGeneration(0, 0, 16, 16);
    testEqUChar(
      g, dense.getCanvasPixel(0, 0), 1, "empty elementary world stays empty");
  }
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "shipped elementary catalog loads");
  const std::unique_ptr<RuleSet> compiled90 = registry.createRuleSet("RULE_90");
  if (compiled90) {
    CellGrid legacy(8, 8), compiled(8, 8);
    legacy.clearCells();
    compiled.clearCells();
    legacy.setCanvasPixel(4, 0, 0);
    compiled.setCanvasPixel(4, 0, 0);
    DenseRuleEvaluator(&legacy, rule90).calcGeneration(0, 0, 8, 8);
    DenseRuleEvaluator(&compiled, *compiled90).calcGeneration(0, 0, 8, 8);
    testTrue(g,
             std::memcmp(legacy.lifeCanvas, compiled.lifeCanvas, 64) == 0,
             "data-backed dense elementary agrees with reference rule");
    testEqUChar(
      g, compiled.getCanvasPixel(3, 1), 0, "dense Rule 90 left child");
    testEqUChar(
      g, compiled.getCanvasPixel(5, 1), 0, "dense Rule 90 right child");
    testEqUChar(g, compiled.getCanvasPixel(4, 0), 0, "dense history retained");
  } else {
    testTrue(g, false, "compiled Rule 90 is available");
  }
  SparseCellGrid traffic;
  traffic.setCell(CellAddress{ 0, 0 }, 0);
  traffic.setCell(CellAddress{ 1, 0 }, 1);
  testTrue(g, traffic.advance(rule184), "Rule 184 advances");
  testEqUChar(g, traffic.getCell(CellAddress{ 1, 1 }), 0, "car moved right");
  const std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
  const std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
  for (const std::int64_t endpoint : { minimum, maximum }) {
    SparseCellGrid edge;
    edge.setCell(CellAddress{ endpoint, minimum }, 0);
    testTrue(g, edge.advance(rule90), "Rule 90 advances at signed X endpoint");
    const std::int64_t childX =
      endpoint == minimum ? endpoint + 1 : endpoint - 1;
    testEqUChar(g,
                edge.getCell(CellAddress{ childX, minimum + 1 }),
                0,
                "representable endpoint child is alive");
    testEqUChar(g,
                edge.getCell(CellAddress{ endpoint, minimum + 1 }),
                1,
                "out-of-domain neighbor is background");
  }
  SparseCellGrid lastRow;
  lastRow.setCell(CellAddress{ 0, maximum }, 0);
  const std::uint64_t revision = lastRow.getRevision();
  testTrue(
    g, !lastRow.advance(rule90), "unrepresentable destination row fails");
  testTrue(g,
           lastRow.getRevision() == revision &&
             lastRow.getCell(CellAddress{ 0, maximum }) == 0,
           "failed endpoint advance preserves grid contents and revision");
}

static void
testRuleSetRegistryParsing()
{
  testSection("RuleSetRegistry: B.../S... string parsing");
  unsigned int birth = 0u;
  unsigned int survive = 0u;
  testTrue(g,
           RuleSetRegistry::parseLifeLikeRuleString("B3/S23", birth, survive),
           "parse B3/S23 succeeds");
  testEqInt(g, static_cast<int>(birth), 1 << 3, "B3 birth mask");
  testEqInt(
    g, static_cast<int>(survive), (1 << 2) | (1 << 3), "S23 survive mask");

  testTrue(g,
           RuleSetRegistry::parseLifeLikeRuleString("b36/s23", birth, survive),
           "parse lowercase b36/s23 succeeds");
  testEqInt(g, static_cast<int>(birth), (1 << 3) | (1 << 6), "B36 birth mask");
  testEqInt(
    g, static_cast<int>(survive), (1 << 2) | (1 << 3), "S23 survive mask");

  testTrue(g,
           RuleSetRegistry::parseLifeLikeRuleString("B2/S", birth, survive),
           "parse B2/S succeeds");
  testEqInt(g, static_cast<int>(birth), 1 << 2, "B2 birth mask");
  testEqInt(g, static_cast<int>(survive), 0, "S empty survive mask");

  testTrue(g,
           RuleSetRegistry::parseLifeLikeRuleString("23/3", birth, survive),
           "parse S/B slash notation 23/3 succeeds");
  testEqInt(
    g, static_cast<int>(survive), (1 << 2) | (1 << 3), "23 survive mask");
  testEqInt(g, static_cast<int>(birth), 1 << 3, "3 birth mask");

  testTrue(g,
           !RuleSetRegistry::parseLifeLikeRuleString("", birth, survive),
           "empty string rejected");
}

static void
testRuleSetRegistryFactory()
{
  testSection("RuleSetRegistry: default rule registration and creation");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "shipped rule catalog loads");

  const std::vector<std::string> known = registry.getKnownRules();
  testTrue(g, known.size() >= 13u, "at least 13 default rules registered");
  testTrue(g, registry.isKnownRule("GAME_OF_LIFE"), "GoL known");
  testTrue(g,
           registry.isKnownRule(RuleSetRegistry::normalizeId("game_of_life")),
           "normalized known");
  testTrue(
    g, !registry.isKnownRule("game_of_life"), "exact match requires uppercase");
  testTrue(g, registry.isKnownRule("RULE_90"), "Rule 90 known");
  testTrue(g, registry.isKnownRule("BRIANS_BRAIN"), "Brian's Brain known");
  testTrue(g, registry.isKnownRule("WIREWORLD"), "Wireworld known");
  testTrue(g, registry.isKnownRule("STAR_WARS"), "Star Wars known");
  testTrue(g,
           registry.isKnownRule("EXCITABLE_WAVES_T2"),
           "excitable-media rule known");
  testTrue(g, !registry.isKnownRule("NONEXISTENT_RULE"), "unknown rejected");

  std::unique_ptr<RuleSet> gol = registry.createRuleSet("GAME_OF_LIFE");
  testTrue(g, gol != nullptr, "GoL instance created");
  testTrue(g, gol->getRuleTag() == "GAME_OF_LIFE", "GoL tag matches");
  testTrue(g,
           gol->getNeighborhoodKind() == RuleSet::NeighborhoodKind::MooreCount,
           "GoL is Moore count");

  std::unique_ptr<RuleSet> r90 = registry.createRuleSet("RULE_90");
  testTrue(g, r90 != nullptr, "Rule 90 instance created");
  testTrue(g,
           r90->getNeighborhoodKind() ==
             RuleSet::NeighborhoodKind::Elementary1D,
           "Rule 90 is Elementary1D");

  std::unique_ptr<RuleSet> bb = registry.createRuleSet("BRIANS_BRAIN");
  testTrue(g, bb != nullptr, "Brian's Brain instance created");
  testTrue(g, bb->getRuleTag() == "BRIANS_BRAIN", "Brian's Brain tag matches");

  std::unique_ptr<RuleSet> ww = registry.createRuleSet("WIREWORLD");
  testTrue(g, ww != nullptr, "Wireworld instance created");
  testTrue(g, ww->getRuleTag() == "WIREWORLD", "Wireworld tag matches");
}

static void
testRuleSetRegistryDynamicRegistration()
{
  testSection("RuleSetRegistry: dynamic rule registration");
  RuleSetRegistry registry;

  RuleSetDefinition custom;
  custom.id = "REPLICATOR";
  custom.name = "Replicator";
  RuleFamilyDefinition family;
  family.id = "LIFE_BINARY_TEST";
  family.name = "Binary test";
  family.kind = RuleFamily::LifeLike;
  family.stateCount = 2u;
  family.stateNames = { "Alive", "Background" };
  family.stateColors = { { 0u, 0u, 0u }, { 255u, 255u, 255u } };
  testTrue(g, registry.registerFamily(family), "test family registered");
  custom.familyId = family.id;
  custom.rule = "B1357/S1357";
  RuleSetRegistry::parseLifeLikeRuleString(
    custom.rule, custom.birthMask, custom.surviveMask);
  registry.registerRule(custom);

  testTrue(g, registry.isKnownRule("REPLICATOR"), "custom rule registered");
  std::unique_ptr<RuleSet> replicator = registry.createRuleSet("REPLICATOR");
  testTrue(g, replicator != nullptr, "custom rule created");
  testTrue(g, replicator->getRuleTag() == "REPLICATOR", "custom tag matches");
  // 1 neighbor on dead -> born
  testEqUChar(g, replicator->nextState(1, 1), 0, "replicator birth on 1");
  // 2 neighbors on dead -> dead
  testEqUChar(g, replicator->nextState(1, 2), 1, "replicator no birth on 2");
}

static void
testRuleSetRegistryValidation()
{
  testSection("RuleSetRegistry: validated transactional definitions");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "shipped rule catalog loads");
  const std::vector<std::string> originalNames = registry.getKnownRules();
  const std::string replacement = R"({"id":"GAME_OF_LIFE","rule":"B2/S"})";
  const std::vector<std::string> invalid = {
    R"({"id":"BAD","rule":"B0/S23"})",
    R"({"id":"BAD","rule":"B3/S9"})",
    R"({"id":"BAD","rule":"B3/S23garbage"})",
    R"({"id":"BAD","rule":"B3//S23"})",
    R"({"id":"BAD","family":"unknown"})",
    R"({"id":"BAD","birth":[32]})",
    R"({"id":"BAD","birth":[0]})",
    R"({"id":"BAD","birth":[-1]})",
    R"({"id":"BAD","birth":[3.0]})",
    R"({"id":"BAD","birth":["3"]})",
    R"({"id":"BAD","birth":[true]})",
    R"({"id":"BAD","survive":[9]})",
    R"({"id":"BAD","survive":[18446744073709551615]})",
    R"({"id":"BAD","family":"elementary_1d","rule_number":256})",
    R"({"id":"BAD","family":"elementary_1d","rule_number":1})",
    R"({"id":"BAD","palette":{"alive":[0,0,256]}})",
    R"({"id":"BAD","name":false})",
    "null"
  };
  for (const std::string& item : invalid) {
    testTrue(g,
             !registry.loadFromText('[' + replacement + ',' + item + ']'),
             "invalid later definition rejects batch");
    testTrue(g,
             registry.getKnownRules() == originalNames &&
               registry.getRuleSetDefinition("GAME_OF_LIFE")->birthMask ==
                 (1u << 3),
             "failed batch preserves catalog and earlier replaced definition");
  }
  for (const std::string& tail : { " garbage", " []" }) {
    testTrue(g,
             !registry.loadFromText('[' + replacement + ']' + tail) &&
               registry.getRuleSetDefinition("GAME_OF_LIFE")->birthMask ==
                 (1u << 3),
             "strict JSON rejects trailing input without publishing");
  }
  testTrue(
    g,
    registry.loadFromText(
      R"([{"id":"VALID","birth":[2,8],"survive":[0,8],"palette":{"alive":[0,128,255]}}])"),
    "valid bounded arrays and palette load");
  const RuleSetDefinition* valid = registry.getRuleSetDefinition("VALID");
  std::unique_ptr<RuleSet> validRule = registry.createRuleSet("VALID");
  unsigned char validColor[3] = {};
  if (validRule != nullptr) {
    validRule->evalCell(0u, validColor);
  }
  testTrue(g,
           valid != nullptr && valid->birthMask == ((1u << 2) | (1u << 8)) &&
             valid->surviveMask == ((1u << 0) | (1u << 8)) &&
             validRule != nullptr && validColor[2] == 255,
           "valid legacy definition retains masks and converts its palette to "
           "a family");
  testTrue(
    g,
    registry.loadFromText(
      R"([{"id":"MASK_OVERRIDE","rule":"B3/S23","birth":[2],"survive":[3]}])") &&
      registry.getRuleSetDefinition("MASK_OVERRIDE") != nullptr &&
      registry.getRuleSetDefinition("MASK_OVERRIDE")->birthMask == (1u << 2) &&
      registry.getRuleSetDefinition("MASK_OVERRIDE")->surviveMask == (1u << 3),
    "schema-v1 explicit masks override the compact rule string");
  RuleSetDefinition unsupported;
  unsupported.id = "DIRECT_BAD";
  unsupported.familyId = "LIFE_LIKE_BINARY";
  unsupported.birthMask = 1u;
  testTrue(g,
           !registry.registerRule(unsupported) &&
             !registry.isKnownRule("DIRECT_BAD"),
           "direct registration rejects nonquiescent B0");
  unsupported.birthMask = 1u << 20;
  testTrue(g,
           !registry.registerRule(unsupported),
           "direct registration rejects high mask bits");
  unsigned int birth = 0, survive = 0;
  for (const std::string& malformed :
       { "B9/S23", "B3/S23junk", "B3", "B3/B2", "23/3x" }) {
    testTrue(
      g,
      !RuleSetRegistry::parseLifeLikeRuleString(malformed, birth, survive),
      "malformed rule grammar rejected");
  }
  LifeLikeRuleSet rule("BOUNDS", 1u << 3, (1u << 2) | (1u << 3));
  for (int count = 9; count <= 255; ++count) {
    testTrue(g,
             rule.nextState(0, static_cast<unsigned char>(count)) == 1 &&
               rule.nextState(1, static_cast<unsigned char>(count)) == 1,
             "invalid neighbor count returns background without shifting");
  }
}

static void
testDataCatalogParity()
{
  testSection("RuleSetRegistry: data rules match the former built-in rules");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "shipped schema-v3 catalog loads");
  RuleFamily parsedFamily = RuleFamily::LifeLike;
  testTrue(g,
           RuleSetRegistry::parseFamily("elementary_1d", parsedFamily) &&
             parsedFamily == RuleFamily::Elementary1D &&
             std::string(RuleSetRegistry::familyName(parsedFamily)) ==
               "elementary_1d" &&
             !RuleSetRegistry::parseFamily("wireworld", parsedFamily),
           "typed families map to stable schema names without legacy aliases");
  std::vector<std::unique_ptr<RuleSet>> legacyRules;
  legacyRules.push_back(std::make_unique<LifeLikeRuleSet>(
    "GAME_OF_LIFE", 1u << 3u, (1u << 2u) | (1u << 3u)));
  legacyRules.push_back(std::make_unique<BriansBrainRuleSet>());
  legacyRules.push_back(std::make_unique<LifeLikeRuleSet>(
    "DAY_AND_NIGHT",
    (1u << 3u) | (1u << 6u) | (1u << 7u) | (1u << 8u),
    (1u << 3u) | (1u << 4u) | (1u << 6u) | (1u << 7u) | (1u << 8u)));
  legacyRules.push_back(std::make_unique<LifeLikeRuleSet>(
    "HIGHLIFE", (1u << 3u) | (1u << 6u), (1u << 2u) | (1u << 3u)));
  legacyRules.push_back(
    std::make_unique<LifeLikeRuleSet>("LIFE_WITHOUT_DEATH", 1u << 3u, 0x1FFu));
  legacyRules.push_back(
    std::make_unique<LifeLikeRuleSet>("SEEDS", 1u << 2u, 0u));
  legacyRules.push_back(std::make_unique<WireworldRuleSet>());
  legacyRules.push_back(std::make_unique<Elementary1DRuleSet>("RULE_90", 90u));
  legacyRules.push_back(
    std::make_unique<Elementary1DRuleSet>("RULE_184", 184u));
  const std::vector<std::string> ids = registry.getKnownRules();
  testTrue(g,
           ids.size() >= legacyRules.size(),
           "the data file retains all nine compatibility built-ins");
  for (std::size_t index = 0u; index < legacyRules.size(); ++index) {
    const std::string& id = legacyRules[index]->getRuleTag();
    std::unique_ptr<RuleSet> compiled = registry.createRuleSet(id);
    testTrue(
      g, compiled != nullptr, "data-backed rule factory creates each ID");
    if (compiled == nullptr) {
      continue;
    }
    testEqInt(g,
              static_cast<int>(compiled->getStateCount()),
              static_cast<int>(legacyRules[index]->getStateCount()),
              "data-backed rule preserves state count");
    if (compiled->getNeighborhoodKind() ==
        RuleSet::NeighborhoodKind::Elementary1D) {
      for (unsigned char left = 0u; left < 2u; ++left) {
        for (unsigned char center = 0u; center < 2u; ++center) {
          for (unsigned char right = 0u; right < 2u; ++right) {
            testEqUChar(
              g,
              compiled->nextElementary(left, center, right),
              legacyRules[index]->nextElementary(left, center, right),
              "data-backed rule preserves each elementary neighborhood");
          }
        }
      }
    } else {
      for (unsigned int state = 0u; state < compiled->getStateCount();
           ++state) {
        for (unsigned int neighbors = 0u; neighbors <= 8u; ++neighbors) {
          testEqUChar(
            g,
            compiled->nextState(static_cast<unsigned char>(state),
                                static_cast<unsigned char>(neighbors)),
            legacyRules[index]->nextState(
              static_cast<unsigned char>(state),
              static_cast<unsigned char>(neighbors)),
            "data-backed rule preserves its Moore transition table");
        }
        unsigned char compiledColor[3]{};
        unsigned char legacyColor[3]{};
        compiled->evalCell(static_cast<unsigned char>(state), compiledColor);
        legacyRules[index]->evalCell(static_cast<unsigned char>(state),
                                     legacyColor);
        const std::string paletteMessage = id + " preserves every palette";
        testTrue(g,
                 compiledColor[0] == legacyColor[0] &&
                   compiledColor[1] == legacyColor[1] &&
                   compiledColor[2] == legacyColor[2],
                 paletteMessage.c_str());
      }
    }
  }

  RuleSetDefinition generations;
  generations.id = "CUSTOM_GENERATIONS";
  generations.name = "Six-state decay";
  RuleFamilyDefinition generationsFamily =
    *registry.getFamilyDefinition("GENERATIONS_3_STATE");
  generationsFamily.id = "CUSTOM_GENERATIONS_FAMILY";
  generationsFamily.name = "Six-state family";
  generationsFamily.builtIn = false;
  generationsFamily.stateNames.resize(6u);
  generationsFamily.stateColors.resize(6u);
  for (unsigned int state = 3u; state < 6u; ++state) {
    generationsFamily.stateNames[state] = "State " + std::to_string(state);
    generationsFamily.stateColors[state] = { 180u, 180u, 180u };
  }
  generationsFamily.stateCount = 6u;
  testTrue(g,
           registry.registerFamily(generationsFamily),
           "custom generations family registers independently");
  generations.familyId = generationsFamily.id;
  generations.birthMask = 1u << 2u;
  testTrue(g,
           registry.registerRule(generations),
           "data compiler accepts a custom generations state count");
  std::unique_ptr<RuleSet> decay = registry.createRuleSet("CUSTOM_GENERATIONS");
  testTrue(g,
           decay != nullptr && decay->getStateCount() == 6u &&
             decay->getStateName(5u) == "State 5",
           "custom state metadata reaches the runtime rule");
  if (decay != nullptr) {
    testEqUChar(g, decay->nextState(0u, 0u), 2u, "active begins decay");
    testEqUChar(g, decay->nextState(2u, 0u), 3u, "decay advances");
    testEqUChar(g, decay->nextState(5u, 0u), 1u, "decay returns to background");
    testEqUChar(
      g, decay->nextState(1u, 2u), 0u, "birth count activates a cell");
  }

  RuleSetRegistry roundTrip;
  const std::string serialized = RuleSetRegistry::serializeCatalog(
    registry.getFamilyDefinitions(), registry.getDefinitions());
  const bool loaded = roundTrip.loadFromCatalogTexts(
    RuleSetRegistry::serializeFamilies(registry.getFamilyDefinitions()),
    serialized);
  bool familyRoundTrip =
    loaded && roundTrip.getKnownRules() == registry.getKnownRules();
  for (const RuleSetDefinition& definition : registry.getDefinitions()) {
    const RuleSetDefinition* reloaded =
      roundTrip.getRuleSetDefinition(definition.id);
    familyRoundTrip = familyRoundTrip && reloaded != nullptr &&
                      reloaded->familyId == definition.familyId;
  }
  testTrue(g,
           familyRoundTrip,
           "compiled definitions serialize and reload transactionally");
}

static void
testShippedExperimentalFamilies()
{
  testSection(
    "RuleSetRegistry: shipped Generations and excitable-media families");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "shipped catalog with experimental families loads");

  const RuleFamilyDefinition* generations =
    registry.getFamilyDefinition("GENERATIONS_4_PHASE");
  const RuleFamilyDefinition* excitable =
    registry.getFamilyDefinition("EXCITABLE_MEDIA_5_PHASE");
  testTrue(g,
           generations != nullptr &&
             generations->kind == RuleFamily::Generations &&
             generations->stateCount == 4u &&
             generations->stateNames[2] == "Afterglow",
           "four-phase Generations family owns its decay schema");
  testTrue(g,
           excitable != nullptr && excitable->kind == RuleFamily::MooreTable &&
             excitable->stateCount == 5u &&
             excitable->stateNames[4] == "Refractory III",
           "excitable-media family owns five named phases");
  testTrue(
    g,
    registry.getKnownRules("GENERATIONS_4_PHASE") ==
      std::vector<std::string>{ "STAR_WARS", "NOVA_TRAILS", "CATERPILLARS" },
    "Generations family exposes only its three shipped rules");
  testTrue(
    g,
    registry.getKnownRules("EXCITABLE_MEDIA_5_PHASE") ==
      std::vector<std::string>{ "EXCITABLE_WAVES_T1", "EXCITABLE_WAVES_T2" },
    "excitable-media family exposes only its two thresholds");

  std::unique_ptr<RuleSet> starWars = registry.createRuleSet("STAR_WARS");
  std::unique_ptr<RuleSet> novaTrails = registry.createRuleSet("NOVA_TRAILS");
  testTrue(g,
           starWars != nullptr && starWars->getStateCount() == 4u,
           "Star Wars compiles against the four-phase family");
  testTrue(g,
           novaTrails != nullptr && novaTrails->getStateCount() == 4u,
           "Nova Trails compiles against the four-phase family");
  if (starWars != nullptr) {
    testEqUChar(g, starWars->nextState(1u, 2u), 0u, "Star Wars births on two");
    testEqUChar(
      g, starWars->nextState(0u, 4u), 0u, "Star Wars survives on four");
    testEqUChar(g,
                starWars->nextState(0u, 2u),
                2u,
                "Star Wars enters afterglow when it does not survive");
    testEqUChar(
      g,
      starWars->nextState(2u, 8u),
      3u,
      "Star Wars advances through recovery independently of neighbors");
    testEqUChar(g,
                starWars->nextState(3u, 0u),
                1u,
                "Star Wars recovery returns to background");
  }
  if (novaTrails != nullptr) {
    testEqUChar(
      g, novaTrails->nextState(1u, 3u), 0u, "Nova Trails adds birth on three");
    testEqUChar(g,
                novaTrails->nextState(0u, 5u),
                2u,
                "Nova Trails has a narrower survival band");
  }

  std::unique_ptr<RuleSet> thresholdOne =
    registry.createRuleSet("EXCITABLE_WAVES_T1");
  std::unique_ptr<RuleSet> thresholdTwo =
    registry.createRuleSet("EXCITABLE_WAVES_T2");
  testTrue(g,
           thresholdOne != nullptr && thresholdOne->getStateCount() == 5u,
           "threshold-one excitable media compiles");
  testTrue(g,
           thresholdTwo != nullptr && thresholdTwo->getStateCount() == 5u,
           "threshold-two excitable media compiles");
  if (thresholdOne != nullptr && thresholdTwo != nullptr) {
    testEqUChar(g,
                thresholdOne->nextState(1u, 1u),
                0u,
                "threshold one excites a resting cell from one neighbor");
    testEqUChar(g,
                thresholdTwo->nextState(1u, 1u),
                1u,
                "threshold two ignores one excited neighbor");
    testEqUChar(g,
                thresholdTwo->nextState(1u, 2u),
                0u,
                "threshold two excites from two neighbors");
    testEqUChar(g,
                thresholdTwo->nextState(0u, 8u),
                2u,
                "excitation enters the refractory chain");
    testEqUChar(g,
                thresholdTwo->nextState(2u, 0u),
                3u,
                "first refractory phase advances");
    testEqUChar(g,
                thresholdTwo->nextState(3u, 0u),
                4u,
                "second refractory phase advances");
    testEqUChar(
      g, thresholdTwo->nextState(4u, 0u), 1u, "final refractory phase rests");

    SparseCellGrid wave;
    wave.setCell(CellAddress{ -1, 0 }, 0u);
    wave.setCell(CellAddress{ 1, 0 }, 0u);
    testTrue(g, wave.advance(*thresholdTwo), "threshold-two wave advances");
    testEqUChar(g,
                wave.getCell(CellAddress{ 0, 0 }),
                0u,
                "two excited cells ignite their shared resting neighbor");
    testEqUChar(g,
                wave.getCell(CellAddress{ -1, 0 }),
                2u,
                "source excitation enters refractory state in the sparse grid");
  }
}

static void
testCyclicMultistateFamilies()
{
  testSection("RuleSetRegistry: cyclic multistate interaction families");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "shipped cyclic catalog loads");

  RuleFamily parsedFamily = RuleFamily::LifeLike;
  testTrue(g,
           RuleSetRegistry::parseFamily("cyclic", parsedFamily) &&
             parsedFamily == RuleFamily::Cyclic &&
             std::string(RuleSetRegistry::familyName(parsedFamily)) == "cyclic",
           "cyclic model has a stable schema name");
  const RuleFamilyDefinition* prism =
    registry.getFamilyDefinition("PRISMATIC_ECOLOGY_12");
  const RuleFamilyDefinition* elemental =
    registry.getFamilyDefinition("ELEMENTAL_COURT_9");
  testTrue(g,
           prism != nullptr && prism->kind == RuleFamily::Cyclic &&
             prism->stateCount == 12u && prism->stateNames[11] == "Rose",
           "prismatic family exposes twelve named interacting states");
  testTrue(g,
           elemental != nullptr && elemental->kind == RuleFamily::Cyclic &&
             elemental->stateCount == 9u && elemental->stateNames[8] == "Gold",
           "elemental family exposes nine named interacting states");
  if (prism == nullptr || elemental == nullptr) {
    return;
  }
  testTrue(g,
           registry.getKnownRules("PRISMATIC_ECOLOGY_12") ==
             std::vector<std::string>{
               "PRISM_RUSH", "CHROMATIC_STORM", "CRYSTAL_DOMAINS" },
           "prismatic family exposes its three threshold and step variants");
  testTrue(g,
           registry.getKnownRules("ELEMENTAL_COURT_9") ==
             std::vector<std::string>{ "ELEMENTAL_SURGE", "AURORA_CONFLICT" },
           "elemental family exposes its two conflict variants");

  std::unique_ptr<RuleSet> rush = registry.createRuleSet("PRISM_RUSH");
  std::unique_ptr<RuleSet> storm = registry.createRuleSet("CHROMATIC_STORM");
  testTrue(g,
           rush != nullptr && rush->getNeighborhoodKind() ==
                                RuleSet::NeighborhoodKind::MooreStateCounts,
           "cyclic rules request full neighbor-state counts");
  testTrue(g,
           storm != nullptr && storm->getStateCount() == 12u,
           "long-step cyclic rule compiles against its family");
  if (rush != nullptr) {
    RuleSet::NeighborStateCounts counts{};
    counts[3] = 1u;
    testEqUChar(g,
                rush->nextStateFromNeighborhood(2u, counts),
                3u,
                "one successor neighbor advances a Prism Rush cell");
    counts.fill(0u);
    counts[4] = 8u;
    testEqUChar(g,
                rush->nextStateFromNeighborhood(2u, counts),
                2u,
                "non-successor states do not trigger a cyclic transition");
    counts.fill(0u);
    testEqUChar(g,
                rush->nextStateFromNeighborhood(1u, counts),
                1u,
                "sparse background stays quiescent without its successor");

    HeadlessCanvasFixture dense(5, 5);
    dense.canvas->setCanvasPixel(2, 2, 4u);
    dense.canvas->setCanvasPixel(3, 2, 5u);
    std::unique_ptr<RuleSet> denseRush = registry.createRuleSet("PRISM_RUSH");
    if (denseRush != nullptr) {
      DenseRuleEvaluator(dense.canvas, *denseRush).calcGeneration(0, 0, 5, 5);
    }
    testEqUChar(g,
                dense.at(2, 2),
                5u,
                "dense compatibility grid uses the same state interaction");

    SparseCellGrid boundary;
    boundary.setCell(CellAddress{ 15, 0 }, 4u);
    boundary.setCell(CellAddress{ 16, 0 }, 5u);
    testTrue(g,
             boundary.advance(*rush),
             "cyclic interaction advances across a sparse chunk boundary");
    testEqUChar(g,
                boundary.getCell(CellAddress{ 15, 0 }),
                5u,
                "successor across the chunk boundary advances the cell");

    SparseCellGrid torus(1, 1);
    torus.setCell(CellAddress{ 0, 0 }, 2u);
    torus.setCell(CellAddress{ 15, 0 }, 3u);
    testTrue(g, torus.advance(*rush), "cyclic rule advances on a finite torus");
    testEqUChar(g,
                torus.getCell(CellAddress{ 0, 0 }),
                3u,
                "wrapped successor neighbor participates in the interaction");

    SparseCellGrid source;
    source.setCell(CellAddress{ -17, 4 }, 6u);
    source.setCell(CellAddress{ -16, 4 }, 7u);
    SparseCellGrid destination;
    testTrue(g,
             destination.advanceFrom(source, *rush),
             "direct cyclic generation advances into a spare grid");
    testEqUChar(g,
                destination.getCell(CellAddress{ -17, 4 }),
                7u,
                "direct generation preserves multistate interaction");
  }
  if (storm != nullptr) {
    RuleSet::NeighborStateCounts counts{};
    counts[7] = 1u;
    testEqUChar(g,
                storm->nextStateFromNeighborhood(2u, counts),
                2u,
                "Chromatic Storm waits below its successor threshold");
    counts[7] = 2u;
    testEqUChar(g,
                storm->nextStateFromNeighborhood(2u, counts),
                7u,
                "Chromatic Storm advances five states at its threshold");
  }

  RuleSetRegistry validation;
  RuleFamilyDefinition customFamily = *prism;
  customFamily.id = "CYCLIC_TEST_FAMILY";
  customFamily.builtIn = false;
  testTrue(g,
           validation.registerFamily(customFamily),
           "custom cyclic family registers");
  RuleSetDefinition valid;
  valid.id = "CYCLIC_TEST";
  valid.familyId = customFamily.id;
  valid.cyclicThreshold = 4u;
  valid.cyclicStep = 5u;
  testTrue(g, validation.registerRule(valid), "valid cyclic rule registers");
  RuleSetDefinition invalid = valid;
  invalid.id = "CYCLIC_BAD_STEP";
  invalid.cyclicStep = 6u;
  testTrue(g,
           !validation.registerRule(invalid),
           "non-coprime step that skips declared states is rejected");
  invalid.id = "CYCLIC_BAD_THRESHOLD";
  invalid.cyclicStep = 5u;
  invalid.cyclicThreshold = 0u;
  testTrue(
    g, !validation.registerRule(invalid), "zero cyclic threshold is rejected");

  RuleSetRegistry roundTrip;
  const std::string familyJson =
    RuleSetRegistry::serializeFamilies(validation.getFamilyDefinitions());
  const std::string ruleJson = RuleSetRegistry::serializeCatalog(
    validation.getFamilyDefinitions(), validation.getDefinitions());
  testTrue(g,
           roundTrip.loadFromCatalogTexts(familyJson, ruleJson),
           "cyclic catalog serializes and reloads");
  const RuleSetDefinition* reloaded =
    roundTrip.getRuleSetDefinition("CYCLIC_TEST");
  testTrue(g,
           reloaded != nullptr && reloaded->cyclicThreshold == 4u &&
             reloaded->cyclicStep == 5u,
           "cyclic threshold and step survive round-trip serialization");
}

static void
testResearchedInteractionFamilies()
{
  testSection("RuleSetRegistry: researched interaction families");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "researched family catalog loads");

  const RuleFamilyDefinition* immigrationFamily =
    registry.getFamilyDefinition("IMMIGRATION_LIFE_2_SPECIES");
  const RuleFamilyDefinition* quadFamily =
    registry.getFamilyDefinition("QUADLIFE_4_SPECIES");
  const RuleFamilyDefinition* largerFamily =
    registry.getFamilyDefinition("LARGER_THAN_LIFE_BINARY");
  const RuleFamilyDefinition* fireworksFamily =
    registry.getFamilyDefinition("GENERATIONS_21_PHASE");
  const RuleFamilyDefinition* classicCcaFamily =
    registry.getFamilyDefinition("CLASSIC_CCA_14");
  testTrue(g,
           immigrationFamily != nullptr &&
             immigrationFamily->kind == RuleFamily::SpeciesLife &&
             immigrationFamily->stateCount == 3u,
           "Immigration declares two live species and a background");
  testTrue(g,
           quadFamily != nullptr &&
             quadFamily->kind == RuleFamily::SpeciesLife &&
             quadFamily->stateCount == 5u,
           "QuadLife declares four interacting live species");
  testTrue(g,
           largerFamily != nullptr &&
             largerFamily->kind == RuleFamily::LargerThanLife,
           "Larger-than-Life has a dedicated model");
  testTrue(g,
           fireworksFamily != nullptr && fireworksFamily->stateCount == 21u,
           "Fireworks exposes its full twenty-one-state decay trail");
  testTrue(g,
           classicCcaFamily != nullptr && classicCcaFamily->stateCount == 14u,
           "classic CCA exposes the paper's fourteen colors");

  std::unique_ptr<RuleSet> immigration = registry.createRuleSet("IMMIGRATION");
  std::unique_ptr<RuleSet> quadLife = registry.createRuleSet("QUADLIFE");
  testTrue(g,
           immigration != nullptr &&
             immigration->getNeighborhoodKind() ==
               RuleSet::NeighborhoodKind::MooreStateCounts,
           "Immigration requests full parent-color counts");
  testTrue(g,
           quadLife != nullptr && quadLife->getStateCount() == 5u,
           "QuadLife compiles with four live colors");
  if (immigration != nullptr) {
    RuleSet::NeighborStateCounts counts{};
    counts[0] = 2u;
    counts[2] = 1u;
    testEqUChar(g,
                immigration->nextStateFromNeighborhood(1u, counts),
                0u,
                "Immigration birth inherits the majority parent species");
    counts[0] = 1u;
    counts[2] = 2u;
    testEqUChar(g,
                immigration->nextStateFromNeighborhood(1u, counts),
                2u,
                "the other Immigration species can win a birth");
    testEqUChar(g,
                immigration->nextStateFromNeighborhood(2u, counts),
                2u,
                "a surviving live cell preserves its species");
  }
  if (quadLife != nullptr) {
    RuleSet::NeighborStateCounts counts{};
    counts[0] = 1u;
    counts[2] = 1u;
    counts[3] = 1u;
    testEqUChar(g,
                quadLife->nextStateFromNeighborhood(1u, counts),
                4u,
                "three distinct QuadLife parents produce the fourth species");

    SparseCellGrid boundary;
    boundary.setCell(CellAddress{ 14, -1 }, 0u);
    boundary.setCell(CellAddress{ 15, -1 }, 2u);
    boundary.setCell(CellAddress{ 16, -1 }, 3u);
    testTrue(g, boundary.advance(*quadLife), "QuadLife advances sparsely");
    testEqUChar(g,
                boundary.getCell(CellAddress{ 15, 0 }),
                4u,
                "fourth-species birth works across a sparse chunk boundary");

    HeadlessCanvasFixture dense(7, 7);
    dense.canvas->setCanvasPixel(2, 2, 0u);
    dense.canvas->setCanvasPixel(3, 2, 2u);
    dense.canvas->setCanvasPixel(4, 2, 3u);
    std::unique_ptr<RuleSet> denseQuad = registry.createRuleSet("QUADLIFE");
    if (denseQuad != nullptr) {
      DenseRuleEvaluator(dense.canvas, *denseQuad).calcGeneration(0, 0, 7, 7);
    }
    testEqUChar(g,
                dense.at(3, 3),
                4u,
                "dense compatibility uses the same QuadLife color birth");
  }

  const RuleSetDefinition* boscoDefinition =
    registry.getRuleSetDefinition("BOSCO");
  std::unique_ptr<RuleSet> bosco = registry.createRuleSet("BOSCO");
  testTrue(g,
           boscoDefinition != nullptr && bosco != nullptr &&
             bosco->getNeighborhoodKind() ==
               RuleSet::NeighborhoodKind::ExtendedRange &&
             bosco->getNeighborhoodRadius() == 5u &&
             bosco->includesCenterInNeighborCount(),
           "Bosco compiles its range-five center-counted neighborhood");
  if (bosco != nullptr) {
    testEqUChar(g,
                bosco->nextStateFromExtendedCount(1u, 34u),
                0u,
                "Bosco births at its lower threshold");
    testEqUChar(g,
                bosco->nextStateFromExtendedCount(1u, 33u),
                1u,
                "Bosco rejects counts below its birth interval");
    testEqUChar(g,
                bosco->nextStateFromExtendedCount(0u, 58u),
                0u,
                "Bosco survives at its upper threshold");
    testEqUChar(g,
                bosco->nextStateFromExtendedCount(0u, 59u),
                1u,
                "Bosco dies above its survival interval");
  }

  if (largerFamily != nullptr) {
    RuleSetRegistry customRegistry;
    testTrue(g,
             customRegistry.registerFamily(*largerFamily),
             "extended-range family registers independently");
    RuleSetDefinition spread;
    spread.id = "RANGE_TWO_SPREAD";
    spread.name = "Range two spread";
    spread.familyId = largerFamily->id;
    spread.neighborhoodRadius = 2u;
    spread.birthMinimum = 1u;
    spread.birthMaximum = 1u;
    spread.survivalMinimum = 0u;
    spread.survivalMaximum = 0u;
    testTrue(
      g, customRegistry.registerRule(spread), "valid range-two rule registers");
    std::unique_ptr<RuleSet> spreadRule =
      customRegistry.createRuleSet("RANGE_TWO_SPREAD");
    if (spreadRule != nullptr) {
      SparseCellGrid sparse;
      sparse.setCell(CellAddress{ 15, 0 }, 0u);
      testTrue(
        g, sparse.advance(*spreadRule), "range-two sparse rule advances");
      testEqUChar(g,
                  sparse.getCell(CellAddress{ 17, 2 }),
                  0u,
                  "extended neighborhood crosses sparse chunk boundaries");
      testEqUChar(g,
                  sparse.getCell(CellAddress{ 18, 0 }),
                  1u,
                  "cells beyond the declared range remain background");

      SparseCellGrid torus(1, 1);
      torus.setCell(CellAddress{ 0, 0 }, 0u);
      testTrue(
        g, torus.advance(*spreadRule), "range-two rule advances on a torus");
      testEqUChar(g,
                  torus.getCell(CellAddress{ 15, 0 }),
                  0u,
                  "extended toroidal neighborhood wraps at the world edge");
    }

    RuleSetDefinition circular = spread;
    circular.id = "RANGE_TWO_CIRCULAR";
    circular.extendedNeighborhoodShape =
      RuleSet::ExtendedNeighborhoodShape::Circular;
    testTrue(g,
             customRegistry.registerRule(circular),
             "circular extended neighborhood registers");
    std::unique_ptr<RuleSet> circularRule =
      customRegistry.createRuleSet("RANGE_TWO_CIRCULAR");
    if (circularRule != nullptr) {
      SparseCellGrid circularGrid;
      circularGrid.setCell(CellAddress{ 0, 0 }, 0u);
      testTrue(g,
               circularGrid.advance(*circularRule),
               "circular range-two rule advances");
      testEqUChar(g,
                  circularGrid.getCell(CellAddress{ 2, 0 }),
                  0u,
                  "circular neighborhood includes its axial radius");
      testEqUChar(g,
                  circularGrid.getCell(CellAddress{ 2, 2 }),
                  1u,
                  "circular neighborhood excludes square-only corners");
    }

    RuleSetDefinition invalid = spread;
    invalid.id = "RANGE_B0_INVALID";
    invalid.birthMinimum = 0u;
    testTrue(g,
             !customRegistry.registerRule(invalid),
             "extended B0 is rejected for a sparse infinite background");
    invalid = spread;
    invalid.id = "RANGE_TOO_WIDE";
    invalid.neighborhoodRadius = 17u;
    testTrue(g,
             !customRegistry.registerRule(invalid),
             "extended range above the supported bound is rejected");
  }

  const RuleSetDefinition* quadDefinition =
    registry.getRuleSetDefinition("QUADLIFE");
  testTrue(g,
           quadDefinition != nullptr &&
             quadDefinition->seedPattern == RuleSeedPattern::SpeciesSoup &&
             boscoDefinition != nullptr &&
             boscoDefinition->seedPattern == RuleSeedPattern::ActiveSoup,
           "researched rules carry model-appropriate deterministic seeds");

  RuleSetRegistry roundTrip;
  const std::string familiesJson =
    RuleSetRegistry::serializeFamilies(registry.getFamilyDefinitions());
  const std::string rulesJson = RuleSetRegistry::serializeCatalog(
    registry.getFamilyDefinitions(), registry.getDefinitions());
  testTrue(g,
           roundTrip.loadFromCatalogTexts(familiesJson, rulesJson),
           "researched family catalog round-trips");
  const RuleSetDefinition* reloadedBosco =
    roundTrip.getRuleSetDefinition("BOSCO");
  testTrue(
    g,
    reloadedBosco != nullptr && reloadedBosco->neighborhoodRadius == 5u &&
      reloadedBosco->birthMinimum == 34u &&
      reloadedBosco->birthMaximum == 45u && reloadedBosco->includeCenter &&
      reloadedBosco->seedPattern == RuleSeedPattern::ActiveSoup,
    "extended thresholds and seed strategy survive serialization");
}

static void
testDirectionalAndChemicalFamilies()
{
  testSection("RuleSetRegistry: directional and chemical families");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "second researched family catalog loads");

  const RuleFamilyDefinition* hodgeFamily =
    registry.getFamilyDefinition("HODGEPODGE_101_LEVEL");
  const RuleFamilyDefinition* antFamily =
    registry.getFamilyDefinition("TURMITE_2_COLOR");
  const RuleFamilyDefinition* gasFamily =
    registry.getFamilyDefinition("HPP_LATTICE_GAS_16");
  const RuleFamilyDefinition* dominanceFamily =
    registry.getFamilyDefinition("RPSLS_5_SPECIES");
  testTrue(g,
           hodgeFamily != nullptr &&
             hodgeFamily->kind == RuleFamily::Hodgepodge &&
             hodgeFamily->stateCount == 101u,
           "Hodgepodge exposes one hundred infection levels plus healthy");
  testTrue(g,
           antFamily != nullptr && antFamily->kind == RuleFamily::Turmite &&
             antFamily->stateCount == 10u,
           "two-color Turmite includes tape and directional agent states");
  testTrue(g,
           gasFamily != nullptr && gasFamily->kind == RuleFamily::LatticeGas &&
             gasFamily->stateCount == 16u,
           "HPP exposes all four-direction occupancy combinations");
  testTrue(g,
           dominanceFamily != nullptr &&
             dominanceFamily->kind == RuleFamily::Dominance &&
             dominanceFamily->stateCount == 6u,
           "RPSLS exposes five species plus empty space");

  std::unique_ptr<RuleSet> hodge =
    registry.createRuleSet("HODGEPODGE_CLASSIC_G9");
  testTrue(g,
           hodge != nullptr && hodge->getNeighborhoodKind() ==
                                 RuleSet::NeighborhoodKind::MooreStateCounts,
           "Hodgepodge uses full neighborhood state levels");
  if (hodge != nullptr) {
    RuleSet::NeighborStateCounts counts{};
    counts[0] = 4u;
    counts[100] = 3u;
    testEqUChar(g,
                hodge->nextStateFromNeighborhood(1u, counts),
                3u,
                "healthy cell combines infected and ill neighbor weights");
    counts.fill(0u);
    counts[50] = 1u;
    testEqUChar(g,
                hodge->nextStateFromNeighborhood(50u, counts),
                59u,
                "infected cell averages active levels and adds g");
    testEqUChar(g,
                hodge->nextStateFromNeighborhood(100u, counts),
                1u,
                "maximally ill cell recovers to healthy");

    SparseCellGrid boundary;
    boundary.setCell(CellAddress{ 15, -1 }, 100u);
    boundary.setCell(CellAddress{ 15, 0 }, 100u);
    boundary.setCell(CellAddress{ 15, 1 }, 100u);
    testTrue(g, boundary.advance(*hodge), "Hodgepodge advances sparsely");
    testEqUChar(g,
                boundary.getCell(CellAddress{ 16, 0 }),
                0u,
                "Hodgepodge infection crosses a sparse chunk boundary");
  }

  std::unique_ptr<RuleSet> ant = registry.createRuleSet("LANGTON_ANT_RL");
  testTrue(g,
           ant != nullptr && ant->getNeighborhoodKind() ==
                               RuleSet::NeighborhoodKind::VonNeumannDirectional,
           "Turmite preserves directional von Neumann input");
  if (ant != nullptr) {
    SparseCellGrid trail;
    trail.setCell(CellAddress{ 15, 0 }, 2u);
    testTrue(g, trail.advance(*ant), "Langton ant advances sparsely");
    testEqUChar(g,
                trail.getCell(CellAddress{ 15, 0 }),
                0u,
                "ant increments the tape color it leaves");
    testEqUChar(g,
                trail.getCell(CellAddress{ 16, 0 }),
                3u,
                "right turn moves east across a sparse chunk boundary");
    testTrue(g, trail.advance(*ant), "Langton ant advances a second time");
    testEqUChar(g,
                trail.getCell(CellAddress{ 16, 1 }),
                4u,
                "second right turn carries the south-facing ant state");

    HeadlessCanvasFixture dense(5, 5);
    dense.canvas->setCanvasPixel(2, 2, 2u);
    std::unique_ptr<RuleSet> denseAnt =
      registry.createRuleSet("LANGTON_ANT_RL");
    if (denseAnt != nullptr) {
      DenseRuleEvaluator(dense.canvas, *denseAnt).calcGeneration(0, 0, 5, 5);
    }
    testEqUChar(
      g, dense.at(2, 2), 0u, "dense Turmite writes its departed tape cell");
    testEqUChar(g,
                dense.at(3, 2),
                3u,
                "dense directional compatibility moves the ant east");
  }

  std::unique_ptr<RuleSet> gas = registry.createRuleSet("HPP_GAS");
  testTrue(g,
           gas != nullptr && gas->getNeighborhoodKind() ==
                               RuleSet::NeighborhoodKind::VonNeumannDirectional,
           "HPP gas uses directional streaming");
  if (gas != nullptr) {
    SparseCellGrid stream;
    stream.setCell(CellAddress{ 0, -1 }, 4u);
    testTrue(g, stream.advance(*gas), "single HPP particle streams");
    testEqUChar(g,
                stream.getCell(CellAddress{ 0, 0 }),
                4u,
                "south-moving particle retains direction after streaming");

    SparseCellGrid collision;
    collision.setCell(CellAddress{ 0, 0 }, 5u);
    testTrue(g, collision.advance(*gas), "head-on HPP pair collides");
    testEqUChar(g,
                collision.getCell(CellAddress{ 1, 0 }),
                2u,
                "north-south collision emits an east particle");
    testEqUChar(g,
                collision.getCell(CellAddress{ -1, 0 }),
                8u,
                "north-south collision emits a west particle");

    SparseCellGrid torus(1, 1);
    torus.setCell(CellAddress{ 15, 0 }, 2u);
    testTrue(g, torus.advance(*gas), "HPP gas advances on a finite torus");
    testEqUChar(g,
                torus.getCell(CellAddress{ 0, 0 }),
                2u,
                "east particle wraps across the finite world");

    HeadlessCanvasFixture dense(5, 5);
    dense.canvas->setCanvasPixel(2, 2, 5u);
    std::unique_ptr<RuleSet> denseGas = registry.createRuleSet("HPP_GAS");
    if (denseGas != nullptr) {
      DenseRuleEvaluator(dense.canvas, *denseGas).calcGeneration(0, 0, 5, 5);
    }
    testEqUChar(g,
                dense.at(3, 2),
                2u,
                "dense HPP compatibility emits the east collision product");
    testEqUChar(g,
                dense.at(1, 2),
                8u,
                "dense HPP compatibility emits the west collision product");
  }

  std::unique_ptr<RuleSet> dominance =
    registry.createRuleSet("RPSLS_DOMAINS_T2");
  if (dominance != nullptr) {
    RuleSet::NeighborStateCounts counts{};
    counts[5] = 2u;
    counts[3] = 1u;
    testEqUChar(g,
                dominance->nextStateFromNeighborhood(0u, counts),
                5u,
                "strongest qualifying predator invades its prey");
    counts[5] = 1u;
    testEqUChar(g,
                dominance->nextStateFromNeighborhood(0u, counts),
                0u,
                "reinforced domains resist a lone predator");

    SparseCellGrid front;
    front.setCell(CellAddress{ 16, 0 }, 0u);
    front.setCell(CellAddress{ 15, -1 }, 5u);
    front.setCell(CellAddress{ 15, 0 }, 5u);
    testTrue(g, front.advance(*dominance), "dominance front advances sparsely");
    testEqUChar(g,
                front.getCell(CellAddress{ 16, 0 }),
                5u,
                "species invasion crosses a sparse chunk boundary");
  }

  const RuleSetDefinition* rrl = registry.getRuleSetDefinition("TURMITE_RRL");
  const RuleSetDefinition* rrll = registry.getRuleSetDefinition("TURMITE_RRLL");
  testTrue(g,
           rrl != nullptr && rrl->turnSequence == "RRL" && rrll != nullptr &&
             rrll->turnSequence == "RRLL",
           "researched multi-color Turmite words are preserved");

  RuleSetRegistry roundTrip;
  const std::string familiesJson =
    RuleSetRegistry::serializeFamilies(registry.getFamilyDefinitions());
  const std::string rulesJson = RuleSetRegistry::serializeCatalog(
    registry.getFamilyDefinitions(), registry.getDefinitions());
  testTrue(g,
           roundTrip.loadFromCatalogTexts(familiesJson, rulesJson),
           "directional and chemical catalog round-trips");
  const RuleSetDefinition* reloadedHodge =
    roundTrip.getRuleSetDefinition("HODGEPODGE_SPIRAL_G28");
  const RuleSetDefinition* reloadedDominance =
    roundTrip.getRuleSetDefinition("RPSLS_INVASION_T1");
  testTrue(g,
           reloadedHodge != nullptr && reloadedHodge->infectionDivisor == 3u &&
             reloadedHodge->infectionIncrement == 28u &&
             reloadedDominance != nullptr &&
             reloadedDominance->dominancePreyOffsets.size() == 2u &&
             reloadedDominance->seedPattern == RuleSeedPattern::SpeciesSoup,
           "new interaction parameters and starters survive serialization");

  if (antFamily != nullptr) {
    RuleSetRegistry validation;
    testTrue(g,
             validation.registerFamily(*antFamily),
             "Turmite family registers independently");
    RuleSetDefinition invalidAnt;
    invalidAnt.id = "INVALID_ANT";
    invalidAnt.familyId = antFamily->id;
    invalidAnt.turnSequence = "R";
    testTrue(g,
             !validation.registerRule(invalidAnt),
             "Turmite turn word must match its tape color count");
  }
}

// Non-background cells per state across every stored sparse chunk.
static std::array<std::size_t, 256>
sparseStateHistogram(const SparseCellGrid& grid)
{
  std::array<std::size_t, 256> histogram{};
  grid.visitChunks(
    [&histogram](const ChunkAddress&, const SparseCellGrid::ChunkCells& cells) {
      for (const unsigned char state : cells) {
        histogram[state] += 1u;
      }
    });
  histogram[SparseCellGrid::BackgroundState] = 0u;
  return histogram;
}

static std::size_t
sparsePopulation(const SparseCellGrid& grid)
{
  const std::array<std::size_t, 256> histogram = sparseStateHistogram(grid);
  std::size_t population = 0u;
  for (const std::size_t count : histogram) {
    population += count;
  }
  return population;
}

static bool
stampSeedRle(SparseCellGrid& grid,
             const RuleSetDefinition& definition,
             unsigned int stateCount,
             std::int64_t originX,
             std::int64_t originY)
{
  std::vector<RuleSeedCell> cells;
  if (!RuleSetRegistry::decodeSeedRle(definition.seedRle, stateCount, cells)) {
    return false;
  }
  for (const RuleSeedCell& cell : cells) {
    grid.setCell(CellAddress{ originX + cell.x, originY + cell.y }, cell.state);
  }
  return !cells.empty();
}

// Runs a 64x64 torus through the sparse grid and the dense reference
// evaluator and reports whether every cell agrees after each generation.
static bool
sparseTorusMatchesDense(const RuleSet& rule,
                        const std::vector<RuleSeedCell>& seed,
                        int generations)
{
  const int size = 64;
  HeadlessCanvasFixture dense(size, size);
  dense.clearDead();
  SparseCellGrid sparse(4, 4);
  for (const RuleSeedCell& cell : seed) {
    dense.canvas->setCanvasPixel(cell.x, cell.y, cell.state);
    sparse.setCell(CellAddress{ cell.x - size / 2, cell.y - size / 2 },
                   cell.state);
  }
  DenseRuleEvaluator evaluator(dense.canvas, rule);
  for (int generation = 0; generation < generations; ++generation) {
    evaluator.calcGeneration(0, 0, size, size);
    if (!sparse.advance(rule)) {
      return false;
    }
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        if (dense.at(x, y) !=
            sparse.getCell(CellAddress{ x - size / 2, y - size / 2 })) {
          return false;
        }
      }
    }
  }
  return true;
}

// Compares an infinite sparse grid against a dense canvas large enough that
// its wrap never reaches the evolving region.
static bool
sparseInfiniteMatchesDense(const RuleSet& rule,
                           const std::vector<RuleSeedCell>& seed,
                           int generations)
{
  const int size = 128;
  const int offset = size / 2;
  HeadlessCanvasFixture dense(size, size);
  dense.clearDead();
  SparseCellGrid sparse;
  for (const RuleSeedCell& cell : seed) {
    dense.canvas->setCanvasPixel(cell.x + offset, cell.y + offset, cell.state);
    sparse.setCell(CellAddress{ cell.x, cell.y }, cell.state);
  }
  DenseRuleEvaluator evaluator(dense.canvas, rule);
  for (int generation = 0; generation < generations; ++generation) {
    evaluator.calcGeneration(0, 0, size, size);
    if (!sparse.advance(rule)) {
      return false;
    }
  }
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (dense.at(x, y) !=
          sparse.getCell(CellAddress{ x - offset, y - offset })) {
        return false;
      }
    }
  }
  return true;
}

// Deterministic soup of `stateCount` states over a square centered on the
// origin, used for sparse/dense parity.
static std::vector<RuleSeedCell>
parityPhaseSoup(unsigned int stateCount, int radius, bool originAtCorner)
{
  std::vector<RuleSeedCell> cells;
  std::uint32_t value = 2463534242u;
  for (int y = -radius; y < radius; ++y) {
    for (int x = -radius; x < radius; ++x) {
      value ^= value << 13u;
      value ^= value >> 17u;
      value ^= value << 5u;
      const int shift = originAtCorner ? radius : 0;
      cells.push_back(RuleSeedCell{
        x + shift, y + shift, static_cast<unsigned char>(value % stateCount) });
    }
  }
  return cells;
}

static void
testVonNeumannTableFamilies()
{
  testSection("RuleSetRegistry: von Neumann rule tables and self-replicators");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "catalog with rule-table loops loads");
  RuleFamily parsedFamily = RuleFamily::LifeLike;
  testTrue(g,
           RuleSetRegistry::parseFamily("von_neumann_table", parsedFamily) &&
             parsedFamily == RuleFamily::VonNeumannTable &&
             std::string(RuleSetRegistry::familyName(parsedFamily)) ==
               "von_neumann_table",
           "von Neumann table model has a stable schema name");

  // Compiler semantics on a small hand-written table (Golly numbering).
  RuleFamilyDefinition tableFamily;
  tableFamily.id = "VN_TABLE_TEST";
  tableFamily.kind = RuleFamily::VonNeumannTable;
  tableFamily.stateCount = 3u;
  tableFamily.stateNames = { "One", "Zero", "Two" };
  tableFamily.stateColors = { { 1u, 1u, 1u }, { 0u, 0u, 0u }, { 2u, 2u, 2u } };
  RuleSetRegistry validation;
  testTrue(g,
           validation.registerFamily(tableFamily),
           "custom three-state table family registers");
  RuleSetDefinition table;
  table.id = "VN_TABLE_RULE";
  table.familyId = tableFamily.id;
  table.tableSymmetry = "none";
  table.ruleTable = { "# comment lines are ignored",
                      "var a={0,2}",
                      "1,a,0,0,0,a",
                      "1,2,0,0,0,1",
                      "1 0 0 0 2 2  # compact form with spaces" };
  testTrue(g, validation.registerRule(table), "hand-written table compiles");
  std::unique_ptr<RuleSet> tableRule = validation.createRuleSet(table.id);
  if (tableRule != nullptr) {
    testTrue(g,
             tableRule->getNeighborhoodKind() ==
               RuleSet::NeighborhoodKind::VonNeumannDirectional,
             "tables evaluate the ordered north-east-south-west neighborhood");
    testEqUChar(
      g,
      tableRule->nextStateFromDirectionalNeighborhood(0u, { 1u, 1u, 1u, 1u }),
      1u,
      "Golly state 0 maps to the Illumo background");
    testEqUChar(
      g,
      tableRule->nextStateFromDirectionalNeighborhood(0u, { 2u, 1u, 1u, 1u }),
      2u,
      "a repeated variable binds to one value; first match wins");
    testEqUChar(
      g,
      tableRule->nextStateFromDirectionalNeighborhood(0u, { 1u, 2u, 1u, 1u }),
      0u,
      "unmatched neighborhoods keep their center without symmetry");
    testEqUChar(
      g,
      tableRule->nextStateFromDirectionalNeighborhood(0u, { 1u, 1u, 1u, 2u }),
      2u,
      "compact digit transitions compile");
    testEqUChar(
      g,
      tableRule->nextStateFromDirectionalNeighborhood(1u, { 1u, 1u, 1u, 1u }),
      1u,
      "the quiescent neighborhood stays background");
  }
  RuleSetDefinition rotated = table;
  rotated.id = "VN_TABLE_ROTATED";
  rotated.tableSymmetry = "rotate4";
  testTrue(g, validation.registerRule(rotated), "rotate4 table compiles");
  std::unique_ptr<RuleSet> rotatedRule = validation.createRuleSet(rotated.id);
  if (rotatedRule != nullptr) {
    testEqUChar(
      g,
      rotatedRule->nextStateFromDirectionalNeighborhood(0u, { 1u, 2u, 1u, 1u }),
      2u,
      "rotate4 applies every rotation of a transition");
  }

  const std::vector<std::vector<std::string>> invalidTables = {
    {},
    { "1,0,0,0,0" },
    { "1,0,0,0,0,3" },
    { "1,b,0,0,0,1" },
    { "var a={0,1}", "var a={0,2}", "1,a,0,0,0,1" },
    { "var a={}", "1,a,0,0,0,1" },
    { "0,0,0,0,0,1" },
    { "n_states:3", "1,0,0,0,0,1" },
  };
  for (std::size_t index = 0u; index < invalidTables.size(); ++index) {
    RuleSetDefinition invalid = table;
    invalid.id = "VN_TABLE_INVALID_" + std::to_string(index);
    invalid.ruleTable = invalidTables[index];
    const std::string message = "malformed or background-breaking table " +
                                std::to_string(index) + " is rejected";
    testTrue(g, !validation.registerRule(invalid), message.c_str());
  }
  RuleSetDefinition badSymmetry = table;
  badSymmetry.id = "VN_TABLE_BAD_SYMMETRY";
  badSymmetry.tableSymmetry = "rotate8";
  testTrue(g,
           !validation.registerRule(badSymmetry),
           "Moore-only symmetries are rejected for von Neumann tables");
  RuleFamilyDefinition hugeFamily = tableFamily;
  hugeFamily.id = "VN_TABLE_HUGE";
  hugeFamily.stateCount = 13u;
  hugeFamily.stateNames.assign(13u, "State");
  hugeFamily.stateColors.assign(13u, { 0u, 0u, 0u });
  testTrue(g,
           !validation.registerFamily(hugeFamily),
           "dense tables above twelve states are rejected");

  RuleSetRegistry roundTrip;
  testTrue(
    g,
    roundTrip.loadFromCatalogTexts(
      RuleSetRegistry::serializeFamilies(validation.getFamilyDefinitions()),
      RuleSetRegistry::serializeCatalog(validation.getFamilyDefinitions(),
                                        validation.getDefinitions())),
    "rule-table catalog serializes and reloads");
  const RuleSetDefinition* reloadedTable =
    roundTrip.getRuleSetDefinition("VN_TABLE_ROTATED");
  const RuleSetDefinition* originalTable =
    validation.getRuleSetDefinition("VN_TABLE_ROTATED");
  testTrue(g,
           reloadedTable != nullptr && originalTable != nullptr &&
             reloadedTable->tableSymmetry == "rotate4" &&
             reloadedTable->ruleTable == originalTable->ruleTable &&
             reloadedTable->vonNeumannTransitions ==
               originalTable->vonNeumannTransitions,
           "table text, symmetry, and compiled transitions round-trip");

  // Shipped loops reproduce populations from an independent reference
  // implementation of Golly's RuleTable semantics (histograms in Illumo
  // encoding at generations 60 and 300).
  struct LoopReference
  {
    const char* id;
    std::vector<std::size_t> early;
    std::vector<std::size_t> late;
  };
  const std::vector<LoopReference> references = {
    { "LANGTONS_LOOPS",
      { 20, 0, 84, 0, 4, 0, 0, 9 },
      { 63, 0, 239, 1, 8, 0, 0, 27 } },
    { "BYL_LOOP", { 16, 0, 40, 16, 12, 0 }, { 207, 0, 1554, 478, 207, 207 } },
    { "CHOU_REGGIA_LOOP_1",
      { 42, 0, 0, 7, 8, 2, 8, 0 },
      { 1002, 0, 0, 25, 25, 12, 33, 0 } },
    { "CHOU_REGGIA_LOOP_2",
      { 29, 0, 0, 11, 16, 2, 0, 16 },
      { 1278, 0, 0, 619, 641, 18, 0, 141 } },
    { "SDSR_LOOPS",
      { 20, 0, 84, 0, 4, 0, 0, 9, 0 },
      { 63, 0, 239, 1, 8, 0, 0, 27, 0 } },
    { "EVOLOOP",
      { 32, 0, 130, 1, 3, 0, 0, 15, 0 },
      { 55, 0, 221, 0, 4, 0, 0, 27, 0 } },
  };
  for (const LoopReference& reference : references) {
    const RuleSetDefinition* definition =
      registry.getRuleSetDefinition(reference.id);
    std::unique_ptr<RuleSet> loop = registry.createRuleSet(reference.id);
    const std::string loaded = std::string(reference.id) + " ships a seed";
    testTrue(g,
             definition != nullptr && loop != nullptr &&
               definition->seedPattern == RuleSeedPattern::Rle,
             loaded.c_str());
    if (definition == nullptr || loop == nullptr) {
      continue;
    }
    SparseCellGrid grid;
    testTrue(g,
             stampSeedRle(grid, *definition, loop->getStateCount(), -7, -5),
             "loop seed stamps into the sparse grid");
    bool advanced = true;
    std::array<std::size_t, 256> early{};
    for (int generation = 1; generation <= 300; ++generation) {
      advanced = advanced && grid.advance(*loop);
      if (generation == 60) {
        early = sparseStateHistogram(grid);
      }
    }
    const std::array<std::size_t, 256> late = sparseStateHistogram(grid);
    bool matches = advanced;
    for (std::size_t state = 0u; state < reference.early.size(); ++state) {
      matches = matches && early[state] == reference.early[state] &&
                late[state] == reference.late[state];
    }
    const std::string message =
      std::string(reference.id) + " matches the reference evolution";
    testTrue(g, matches, message.c_str());
  }

  std::unique_ptr<RuleSet> langton = registry.createRuleSet("LANGTONS_LOOPS");
  const RuleSetDefinition* langtonDefinition =
    registry.getRuleSetDefinition("LANGTONS_LOOPS");
  if (langton != nullptr && langtonDefinition != nullptr) {
    std::vector<RuleSeedCell> seed;
    RuleSetRegistry::decodeSeedRle(langtonDefinition->seedRle, 8u, seed);
    for (RuleSeedCell& cell : seed) {
      cell.x += 4;
      cell.y += 20;
    }
    testTrue(g,
             sparseTorusMatchesDense(*langton, seed, 160),
             "sparse torus matches the dense reference across wrap edges");
  }
}

static void
testSandpileFamily()
{
  testSection("RuleSetRegistry: Abelian sandpile with pulsed sources");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "catalog with sandpiles loads");
  const RuleFamilyDefinition* family =
    registry.getFamilyDefinition("SANDPILE_PULSE_2");
  std::unique_ptr<RuleSet> sand = registry.createRuleSet("SANDPILE_MANDALA");
  testTrue(g,
           family != nullptr && family->kind == RuleFamily::Sandpile &&
             family->stateCount == 10u && sand != nullptr &&
             sand->getNeighborhoodKind() ==
               RuleSet::NeighborhoodKind::VonNeumannDirectional,
           "sandpile family exposes eight heights and two source phases");
  if (sand == nullptr) {
    return;
  }
  testEqUChar(
    g,
    sand->nextStateFromDirectionalNeighborhood(4u, { 1u, 1u, 1u, 1u }),
    1u,
    "a height-four cell topples all four grains");
  testEqUChar(
    g,
    sand->nextStateFromDirectionalNeighborhood(3u, { 4u, 8u, 9u, 3u }),
    5u,
    "toppling neighbors and the dropping source each add one grain");
  testEqUChar(
    g,
    sand->nextStateFromDirectionalNeighborhood(1u, { 7u, 1u, 1u, 1u }),
    0u,
    "background receives its first grain as state zero");
  testEqUChar(
    g,
    sand->nextStateFromDirectionalNeighborhood(8u, { 1u, 1u, 1u, 1u }),
    9u,
    "source phases cycle from drop to rest");
  testEqUChar(
    g,
    sand->nextStateFromDirectionalNeighborhood(9u, { 1u, 1u, 1u, 1u }),
    8u,
    "source phases wrap back to the dropping phase");

  // Seven grains on one cell relax without losing a grain.
  SparseCellGrid pile;
  pile.setCell(CellAddress{ 15, 15 }, 7u);
  bool advanced = true;
  for (int generation = 0; generation < 40; ++generation) {
    advanced = advanced && pile.advance(*sand);
  }
  const std::array<std::size_t, 256> relaxed = sparseStateHistogram(pile);
  std::size_t grains = relaxed[0];
  bool stable = true;
  for (unsigned int state = 2u; state < 8u; ++state) {
    grains += relaxed[state] * state;
    stable = stable && (state < 4u || relaxed[state] == 0u);
  }
  testTrue(g,
           advanced && grains == 7u && stable,
           "toppling conserves grains and settles below four per cell");

  // A lone source grows a pattern with the von Neumann square's symmetry.
  SparseCellGrid mandala;
  mandala.setCell(CellAddress{ 0, 0 }, 8u);
  for (int generation = 0; generation < 400; ++generation) {
    advanced = advanced && mandala.advance(*sand);
  }
  bool symmetric = advanced;
  for (int y = -30; y <= 30 && symmetric; ++y) {
    for (int x = -30; x <= 30; ++x) {
      if (mandala.getCell(CellAddress{ x, y }) !=
          mandala.getCell(CellAddress{ -y, x })) {
        symmetric = false;
        break;
      }
    }
  }
  testTrue(g,
           symmetric && sparsePopulation(mandala) > 100u,
           "single-source mandala keeps four-fold rotational symmetry");

  std::vector<RuleSeedCell> seed;
  seed.push_back(RuleSeedCell{ 30, 30, 8u });
  seed.push_back(RuleSeedCell{ 2, 33, 7u });
  testTrue(g,
           sparseTorusMatchesDense(*sand, seed, 200),
           "sandpile sparse torus matches the dense reference");

  const RuleSetDefinition* avalanche =
    registry.getRuleSetDefinition("SANDPILE_AVALANCHE");
  testTrue(g,
           avalanche != nullptr &&
             avalanche->seedPattern == RuleSeedPattern::Rle,
           "critical avalanche ships an exact starter");
  if (avalanche != nullptr) {
    SparseCellGrid critical;
    testTrue(g,
             stampSeedRle(critical, *avalanche, 10u, -40, -40),
             "critical square stamps");
    const std::array<std::size_t, 256> before = sparseStateHistogram(critical);
    testTrue(g,
             before[3] == 81u * 81u - 1u && before[8] == 1u,
             "avalanche starter is an 81x81 critical square with one source");
  }

  RuleFamilyDefinition shortFamily = *family;
  shortFamily.id = "SANDPILE_WITHOUT_SOURCE";
  shortFamily.stateCount = 8u;
  shortFamily.stateNames.resize(8u);
  shortFamily.stateColors.resize(8u);
  RuleSetRegistry validation;
  testTrue(g,
           !validation.registerFamily(shortFamily),
           "sandpile families must declare at least one source phase");
}

static void
testLongRangeCyclicAndTrails()
{
  testSection(
    "RuleSetRegistry: long-range cyclic and trailing Larger than Life");
  RuleSetRegistry registry;
  testTrue(g,
           RuleCatalogLoader::loadFromDefaultLocations(registry),
           "catalog with Griffeath and trail rules loads");

  std::unique_ptr<RuleSet> spirals =
    registry.createRuleSet("CCA_CYCLIC_SPIRALS");
  std::unique_ptr<RuleSet> stripes = registry.createRuleSet("CCA_STRIPES");
  std::unique_ptr<RuleSet> classic = registry.createRuleSet("CCA_313");
  testTrue(g,
           spirals != nullptr &&
             spirals->getNeighborhoodKind() ==
               RuleSet::NeighborhoodKind::ExtendedRange &&
             spirals->getNeighborhoodRadius() == 3u &&
             !spirals->includesCenterInNeighborCount(),
           "range-three cyclic rule uses the extended evaluator");
  testTrue(g,
           stripes != nullptr && stripes->getExtendedNeighborhoodShape() ==
                                   RuleSet::ExtendedNeighborhoodShape::Diamond,
           "Stripes counts a von Neumann diamond");
  testTrue(g,
           classic != nullptr && classic->getNeighborhoodKind() ==
                                   RuleSet::NeighborhoodKind::MooreStateCounts,
           "radius-one square cyclic rules keep the Moore histogram path");
  if (spirals != nullptr) {
    testEqUChar(g,
                spirals->getExtendedCountedState(2u),
                3u,
                "a cyclic cell counts its successor");
    testEqUChar(g,
                spirals->getExtendedCountedState(0u),
                2u,
                "the inert void is skipped when phase zero advances");
    testEqUChar(g,
                spirals->getExtendedCountedState(8u),
                0u,
                "the last phase counts the first");
    testEqUChar(g,
                spirals->nextStateFromExtendedCount(2u, 5u),
                3u,
                "threshold successors advance the cell");
    testEqUChar(g,
                spirals->nextStateFromExtendedCount(2u, 4u),
                2u,
                "below-threshold successors leave the cell");
    testEqUChar(g,
                spirals->nextStateFromExtendedCount(1u, 48u),
                1u,
                "an inert void never joins the cycle");
    const unsigned int spiralStates = spirals->getStateCount();
    testTrue(g,
             sparseTorusMatchesDense(
               *spirals, parityPhaseSoup(spiralStates, 32, true), 12),
             "range-three cyclic sparse torus matches the dense reference");
    testTrue(g,
             sparseInfiniteMatchesDense(
               *spirals, parityPhaseSoup(spiralStates, 18, false), 8),
             "range-three cyclic infinite grid matches the dense reference");

    // A void-free soup stays inside its dish on the infinite canvas.
    SparseCellGrid dish;
    for (const RuleSeedCell& cell : parityPhaseSoup(spiralStates, 12, false)) {
      dish.setCell(CellAddress{ cell.x, cell.y },
                   cell.state == 1u ? 0u : cell.state);
    }
    bool advanced = true;
    for (int generation = 0; generation < 30; ++generation) {
      advanced = advanced && dish.advance(*spirals);
    }
    bool bounded = advanced;
    dish.visitChunks([&bounded](const ChunkAddress& address,
                                const SparseCellGrid::ChunkCells& cells) {
      for (int index = 0; index < static_cast<int>(cells.size()); ++index) {
        const std::int64_t x = address.x * SparseCellGrid::kChunkDim +
                               index % SparseCellGrid::kChunkDim;
        const std::int64_t y = address.y * SparseCellGrid::kChunkDim +
                               index / SparseCellGrid::kChunkDim;
        const bool inside = x >= -12 && x < 12 && y >= -12 && y < 12;
        if (!inside && cells[static_cast<std::size_t>(index)] !=
                         SparseCellGrid::BackgroundState) {
          bounded = false;
        }
      }
    });
    testTrue(g,
             bounded && sparsePopulation(dish) == 24u * 24u,
             "an inert-background cyclic soup never invades the void");
  }
  if (stripes != nullptr) {
    testTrue(
      g,
      sparseTorusMatchesDense(
        *stripes, parityPhaseSoup(stripes->getStateCount(), 32, true), 12),
      "diamond cyclic sparse torus matches the dense reference");
  }

  const RuleFamilyDefinition* cyclicFamily =
    registry.getFamilyDefinition("GRIFFEATH_CCA_6");
  if (cyclicFamily != nullptr) {
    RuleSetRegistry validation;
    testTrue(g,
             validation.registerFamily(*cyclicFamily),
             "Griffeath family registers");
    RuleSetDefinition diamond;
    diamond.id = "DIAMOND_LIMIT";
    diamond.familyId = cyclicFamily->id;
    diamond.neighborhoodRadius = 2u;
    diamond.extendedNeighborhoodShape =
      RuleSet::ExtendedNeighborhoodShape::Diamond;
    diamond.cyclicThreshold = 12u;
    testTrue(g,
             validation.registerRule(diamond),
             "a radius-two diamond accepts all twelve neighbors");
    diamond.id = "DIAMOND_TOO_MANY";
    diamond.cyclicThreshold = 13u;
    testTrue(g,
             !validation.registerRule(diamond),
             "thresholds above the neighborhood size are rejected");
    RuleSetDefinition moore = diamond;
    moore.id = "MOORE_TOO_MANY";
    moore.neighborhoodRadius = 1u;
    moore.extendedNeighborhoodShape =
      RuleSet::ExtendedNeighborhoodShape::Square;
    moore.cyclicThreshold = 9u;
    testTrue(g,
             !validation.registerRule(moore),
             "radius-one square cyclic rules keep the eight-neighbor bound");
    RuleSetDefinition inert = moore;
    inert.id = "INERT_STEP_FIVE";
    inert.cyclicThreshold = 1u;
    inert.inertBackground = true;
    inert.cyclicStep = 5u;
    testTrue(g,
             validation.registerRule(inert),
             "inert cycles validate steps against the shortened cycle");
    inert.id = "INERT_STEP_THREE";
    inert.cyclicStep = 3u;
    testTrue(g,
             !validation.registerRule(inert),
             "inert cycle steps must be coprime with the phase count");
    RuleSetRegistry roundTrip;
    testTrue(
      g,
      roundTrip.loadFromCatalogTexts(
        RuleSetRegistry::serializeFamilies(validation.getFamilyDefinitions()),
        RuleSetRegistry::serializeCatalog(validation.getFamilyDefinitions(),
                                          validation.getDefinitions())),
      "long-range cyclic catalog round-trips");
    const RuleSetDefinition* reloaded =
      roundTrip.getRuleSetDefinition("DIAMOND_LIMIT");
    testTrue(g,
             reloaded != nullptr && reloaded->neighborhoodRadius == 2u &&
               reloaded->cyclicThreshold == 12u &&
               reloaded->extendedNeighborhoodShape ==
                 RuleSet::ExtendedNeighborhoodShape::Diamond,
             "cyclic range, shape, and threshold survive serialization");
    const RuleSetDefinition* reloadedInert =
      roundTrip.getRuleSetDefinition("INERT_STEP_FIVE");
    const RuleSetDefinition* reloadedDiamond =
      roundTrip.getRuleSetDefinition("DIAMOND_LIMIT");
    testTrue(g,
             reloadedInert != nullptr && reloadedInert->inertBackground &&
               reloadedDiamond != nullptr && !reloadedDiamond->inertBackground,
             "the inert-background flag survives serialization");
  }

  std::unique_ptr<RuleSet> comets = registry.createRuleSet("COMET_ROCKETS");
  const RuleSetDefinition* cometDefinition =
    registry.getRuleSetDefinition("COMET_ROCKETS");
  testTrue(g,
           comets != nullptr && cometDefinition != nullptr &&
             comets->getStateCount() == 7u &&
             cometDefinition->rule == "R2,C7,M1,S6..9,B6..8,NM",
           "Comet Rockets compiles as a seven-state trailing LtL rule");
  if (comets != nullptr && cometDefinition != nullptr) {
    testEqUChar(g,
                comets->nextStateFromExtendedCount(0u, 5u),
                2u,
                "an unsupported live cell starts its decay trail");
    testEqUChar(g,
                comets->nextStateFromExtendedCount(2u, 7u),
                3u,
                "trail cells ignore births and keep decaying");
    testEqUChar(g,
                comets->nextStateFromExtendedCount(6u, 7u),
                1u,
                "the last trail state returns to background");
    testEqUChar(g,
                comets->nextStateFromExtendedCount(1u, 6u),
                0u,
                "background births inside the birth interval");

    SparseCellGrid fleet;
    testTrue(
      g, stampSeedRle(fleet, *cometDefinition, 7u, 0, 0), "comet fleet stamps");
    std::int64_t startTop = 0;
    bool foundStart = false;
    for (std::int64_t y = 0; y < 40 && !foundStart; ++y) {
      for (std::int64_t x = 0; x < 40; ++x) {
        if (fleet.getCell(CellAddress{ x, y }) == 0u) {
          startTop = y;
          foundStart = true;
          break;
        }
      }
    }
    bool advanced = true;
    for (int generation = 0; generation < 20; ++generation) {
      advanced = advanced && fleet.advance(*comets);
    }
    const std::array<std::size_t, 256> histogram = sparseStateHistogram(fleet);
    bool movedNorth = false;
    for (std::int64_t x = 0; x < 40; ++x) {
      if (fleet.getCell(CellAddress{ x, startTop - 20 }) == 0u) {
        movedNorth = true;
      }
    }
    testTrue(g,
             advanced && histogram[0] == 44u && movedNorth,
             "four comets fly apart at light speed with intact bodies");
  }

  std::unique_ptr<RuleSet> coral = registry.createRuleSet("NEON_CORAL");
  if (coral != nullptr) {
    std::vector<RuleSeedCell> soup = parityPhaseSoup(3u, 16, false);
    for (RuleSeedCell& cell : soup) {
      cell.state = cell.state == 2u ? 1u : cell.state;
    }
    testTrue(g,
             sparseInfiniteMatchesDense(*coral, soup, 10),
             "trailing LtL infinite grid matches the dense reference");
  }

  const RuleFamilyDefinition* trails =
    registry.getFamilyDefinition("LTL_TRAILS_8");
  if (trails != nullptr) {
    RuleSetRegistry validation;
    validation.registerFamily(*trails);
    RuleSetDefinition diamond;
    diamond.id = "DIAMOND_TRAILS";
    diamond.familyId = trails->id;
    diamond.neighborhoodRadius = 2u;
    diamond.extendedNeighborhoodShape =
      RuleSet::ExtendedNeighborhoodShape::Diamond;
    diamond.includeCenter = true;
    diamond.birthMinimum = 2u;
    diamond.birthMaximum = 13u;
    diamond.survivalMinimum = 1u;
    diamond.survivalMaximum = 13u;
    testTrue(g,
             validation.registerRule(diamond),
             "a diamond LtL rule may count all thirteen cells");
    const RuleSetDefinition* compiled =
      validation.getRuleSetDefinition("DIAMOND_TRAILS");
    testTrue(g,
             compiled != nullptr &&
               compiled->rule == "R2,C8,M1,S1..13,B2..13,NN",
             "diamond LtL rules use Golly's NN notation");
    diamond.id = "DIAMOND_TRAILS_TOO_MANY";
    diamond.birthMaximum = 14u;
    testTrue(g,
             !validation.registerRule(diamond),
             "diamond LtL thresholds are bounded by the diamond size");
  }

  std::vector<RuleSeedCell> cells;
  testTrue(g,
           RuleSetRegistry::decodeSeedRle("2A$.B!", 3u, cells) &&
             cells.size() == 3u && cells[0].state == 0u && cells[2].x == 1 &&
             cells[2].y == 1 && cells[2].state == 2u,
           "seed RLE decodes Golly states into Illumo encoding");
  testTrue(g,
           RuleSetRegistry::decodeSeedRle("bo$2o!", 2u, cells) &&
             cells.size() == 3u,
           "two-state b/o RLE tokens decode");
  testTrue(g,
           !RuleSetRegistry::decodeSeedRle("C!", 3u, cells) &&
             !RuleSetRegistry::decodeSeedRle("2A", 3u, cells) &&
             !RuleSetRegistry::decodeSeedRle("2Z!", 30u, cells),
           "seed RLE rejects out-of-family states and missing terminators");
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
  registry.add("IllumoGame.Rules.RegistryValidation",
               []() { return runRuleSetCase(testRuleSetRegistryValidation); });
  registry.add("IllumoGame.Rules.DataCatalogParity",
               []() { return runRuleSetCase(testDataCatalogParity); });
  registry.add("IllumoGame.Rules.ExperimentalFamilies", []() {
    return runRuleSetCase(testShippedExperimentalFamilies);
  });
  registry.add("IllumoGame.Rules.CyclicMultistate",
               []() { return runRuleSetCase(testCyclicMultistateFamilies); });
  registry.add("IllumoGame.Rules.ResearchedInteractions", []() {
    return runRuleSetCase(testResearchedInteractionFamilies);
  });
  registry.add("IllumoGame.Rules.DirectionalChemicalFamilies", []() {
    return runRuleSetCase(testDirectionalAndChemicalFamilies);
  });
  registry.add("IllumoGame.Rules.VonNeumannTables",
               []() { return runRuleSetCase(testVonNeumannTableFamilies); });
  registry.add("IllumoGame.Rules.SandpileFamily",
               []() { return runRuleSetCase(testSandpileFamily); });
  registry.add("IllumoGame.Rules.LongRangeCyclicAndTrails",
               []() { return runRuleSetCase(testLongRangeCyclicAndTrails); });
  registry.add("IllumoGame.Rules.TransitionTable", []() {
    return runRuleSetCase(testTransitionTableCacheAndEquivalence);
  });
  registry.add("IllumoGame.Rules.ElementarySpaceTime",
               []() { return runRuleSetCase(testElementarySpaceTime); });
  registry.add("IllumoGame.Rules.RegistryParsing",
               []() { return runRuleSetCase(testRuleSetRegistryParsing); });
  registry.add("IllumoGame.Rules.RegistryFactory",
               []() { return runRuleSetCase(testRuleSetRegistryFactory); });
  registry.add("IllumoGame.Rules.RegistryDynamic", []() {
    return runRuleSetCase(testRuleSetRegistryDynamicRegistration);
  });
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
