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

  LifeLikeRuleSet rules(f.canvas);
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

  LifeLikeRuleSet rules(f.canvas);
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
  LifeLikeRuleSet rules(f.canvas);
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
  LifeLikeRuleSet rules(f.canvas);
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

  LifeLikeRuleSet rules(f.canvas, "SEEDS", 1u << 2, 0u);
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
  LifeLikeRuleSet rules(
    f.canvas, "HIGHLIFE", (1u << 3) | (1u << 6), (1u << 2) | (1u << 3));
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

  LifeLikeRuleSet gameOfLife(nullptr);
  LifeLikeRuleSet seeds(nullptr, "SEEDS", 1u << 2, 0u);
  BriansBrainRuleSet briansBrain(nullptr);
  LifeLikeRuleSet highlife(
    nullptr, "HIGHLIFE", (1u << 3) | (1u << 6), (1u << 2) | (1u << 3));
  LifeLikeRuleSet dayAndNight(nullptr,
                              "DAY_AND_NIGHT",
                              (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8),
                              (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) |
                                (1u << 8));
  LifeLikeRuleSet lifeWithoutDeath(
    nullptr, "LIFE_WITHOUT_DEATH", 1u << 3, (1u << 9) - 1u);
  WireworldRuleSet wireworld(nullptr);
  Elementary1DRuleSet rule90(nullptr, "RULE_90", 90u);
  Elementary1DRuleSet rule184(nullptr, "RULE_184", 184u);
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
  Elementary1DRuleSet rule90(nullptr, "RULE_90", 90u);
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

  Elementary1DRuleSet rule184(nullptr, "RULE_184", 184u);
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
  testTrue(g, known.size() >= 9u, "at least 9 default rules registered");
  testTrue(g, registry.isKnownRule("GAME_OF_LIFE"), "GoL known");
  testTrue(g,
           registry.isKnownRule(RuleSetRegistry::normalizeId("game_of_life")),
           "normalized known");
  testTrue(
    g, !registry.isKnownRule("game_of_life"), "exact match requires uppercase");
  testTrue(g, registry.isKnownRule("RULE_90"), "Rule 90 known");
  testTrue(g, registry.isKnownRule("BRIANS_BRAIN"), "Brian's Brain known");
  testTrue(g, registry.isKnownRule("WIREWORLD"), "Wireworld known");
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
  LifeLikeRuleSet rule(nullptr, "BOUNDS", 1u << 3, (1u << 2) | (1u << 3));
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
    nullptr, "GAME_OF_LIFE", 1u << 3u, (1u << 2u) | (1u << 3u)));
  legacyRules.push_back(std::make_unique<BriansBrainRuleSet>(nullptr));
  legacyRules.push_back(std::make_unique<LifeLikeRuleSet>(
    nullptr,
    "DAY_AND_NIGHT",
    (1u << 3u) | (1u << 6u) | (1u << 7u) | (1u << 8u),
    (1u << 3u) | (1u << 4u) | (1u << 6u) | (1u << 7u) | (1u << 8u)));
  legacyRules.push_back(std::make_unique<LifeLikeRuleSet>(
    nullptr, "HIGHLIFE", (1u << 3u) | (1u << 6u), (1u << 2u) | (1u << 3u)));
  legacyRules.push_back(std::make_unique<LifeLikeRuleSet>(
    nullptr, "LIFE_WITHOUT_DEATH", 1u << 3u, 0x1FFu));
  legacyRules.push_back(
    std::make_unique<LifeLikeRuleSet>(nullptr, "SEEDS", 1u << 2u, 0u));
  legacyRules.push_back(std::make_unique<WireworldRuleSet>(nullptr));
  legacyRules.push_back(
    std::make_unique<Elementary1DRuleSet>(nullptr, "RULE_90", 90u));
  legacyRules.push_back(
    std::make_unique<Elementary1DRuleSet>(nullptr, "RULE_184", 184u));
  const std::vector<std::string> ids = registry.getKnownRules();
  testEqInt(g,
            static_cast<int>(ids.size()),
            static_cast<int>(legacyRules.size()),
            "the data file contains the nine supported built-ins");
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
