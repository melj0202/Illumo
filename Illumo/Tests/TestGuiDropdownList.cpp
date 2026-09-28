#include <Illumo/Gui/GuiDropdownList.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <string>
#include <vector>

// Twenty items: a few with shared first letters for type-ahead, then filler.
static std::vector<GuiDropdownItem>
sampleItems()
{
  std::vector<GuiDropdownItem> items = {
    { "Alpha", "" }, { "Amber", "" }, { "Beta", "2 rules" },
    { "Brick", "" }, { "Brine", "" }, { "Cedar", "" }
  };
  for (int index = 1; items.size() < 20u; ++index) {
    items.push_back({ "Filler " + std::to_string(index), "" });
  }
  return items;
}

static int
testDropdownPlacement()
{
  TestCounters counters;
  GuiDropdownList list;
  testTrue(counters, !list.isOpen(), "a new list is closed");
  list.open(
    sampleItems(), 12, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  testTrue(counters,
           list.isOpen() && !list.opensUpward() &&
             list.visibleRowCount() == GuiDropdownList::kMaximumVisibleRows,
           "with room below, the list opens under its field with a full "
           "window of rows");
  testTrue(counters,
           list.listY() > 130.0f && list.listX() == 100.0f &&
             list.listWidth() == 200.0f,
           "the list sits just under the field, as wide as it");
  testTrue(counters,
           list.highlightedIndex() == 12 && list.currentIndex() == 12 &&
             list.firstVisibleRow() == 8,
           "the current value opens highlighted in the middle of the window");

  list.open(
    sampleItems(), 0, 100.0f, 420.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  testTrue(counters,
           list.opensUpward() && list.listY() + list.listHeight() <= 420.0f,
           "near the bottom of its space the list opens above its field");

  std::vector<GuiDropdownItem> few = { { "One", "" }, { "Two", "" } };
  list.open(few, 1, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  testTrue(counters,
           list.visibleRowCount() == 2 && list.firstVisibleRow() == 0,
           "a short list shows only its own rows");

  list.open({}, 0, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  testTrue(counters, !list.isOpen(), "an empty list does not open");
  return counters.failures;
}

static int
testDropdownKeyboard()
{
  TestCounters counters;
  GuiDropdownList list;
  list.open(
    sampleItems(), 12, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  list.handleKey(KeyCode::Down);
  testTrue(counters,
           list.highlightedIndex() == 13 && list.firstVisibleRow() == 8,
           "Down moves the highlight inside the window");
  list.handleKey(KeyCode::End);
  testTrue(counters,
           list.highlightedIndex() == 19 && list.firstVisibleRow() == 12,
           "End jumps to the last item and scrolls to it");
  list.handleKey(KeyCode::Down);
  testTrue(
    counters, list.highlightedIndex() == 19, "Down stops at the last item");
  list.handleKey(KeyCode::Home);
  testTrue(counters,
           list.highlightedIndex() == 0 && list.firstVisibleRow() == 0,
           "Home jumps to the first item");
  list.handleKey(KeyCode::PageDown);
  testTrue(counters,
           list.highlightedIndex() == 8 && list.firstVisibleRow() == 1,
           "Page Down moves a window's worth and keeps the highlight in view");
  testTrue(counters,
           list.handleKey(KeyCode::Enter) == GuiDropdownResult::Chosen &&
             list.chosenIndex() == 8 && !list.isOpen(),
           "Enter chooses the highlighted item and closes the list");

  list.open(
    sampleItems(), 3, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  testTrue(counters,
           list.handleKey(KeyCode::Escape) == GuiDropdownResult::Dismissed &&
             !list.isOpen() && list.chosenIndex() == -1,
           "Escape closes the list without a choice");
  return counters.failures;
}

static int
testDropdownTypeAhead()
{
  TestCounters counters;
  GuiDropdownList list;
  list.open(
    sampleItems(), 0, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  list.handleCharacter('b');
  testTrue(counters,
           list.highlightedIndex() == 2,
           "a letter jumps to the next item starting with it");
  list.handleCharacter('R');
  testTrue(counters,
           list.highlightedIndex() == 3,
           "letters typed together build a prefix, ignoring case");
  list.handleCharacter('i');
  list.handleCharacter('n');
  testTrue(counters, list.highlightedIndex() == 4, "\"brin\" finds Brine");
  list.tick(GuiDropdownList::kTypeAheadSeconds + 0.1f);
  list.handleCharacter('b');
  testTrue(counters,
           list.highlightedIndex() == 2,
           "after a pause a repeated letter walks on, wrapping to Beta");
  list.handleCharacter('!');
  testTrue(counters,
           list.highlightedIndex() == 2,
           "punctuation does not move the highlight");
  return counters.failures;
}

static int
testDropdownPointerAndWheel()
{
  TestCounters counters;
  GuiDropdownList list;
  list.open(
    sampleItems(), 0, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  testTrue(counters,
           list.rowBounds(5, &x, &y, &width, &height) &&
             !list.rowBounds(12, nullptr, nullptr, nullptr, nullptr),
           "rows report their bounds only while in view");
  list.handlePointer(x + width * 0.5f, y + height * 0.5f, true, false);
  testTrue(counters,
           list.highlightedIndex() == 5 && list.isOpen(),
           "hovering a row highlights it");
  testTrue(
    counters,
    list.handlePointer(x + width * 0.5f, y + height * 0.5f, false, true) ==
        GuiDropdownResult::Chosen &&
      list.chosenIndex() == 5,
    "clicking a row chooses it");

  list.open(
    sampleItems(), 0, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  testTrue(counters,
           list.handlePointer(150.0f, list.listY() + 1.0f, false, true) ==
               GuiDropdownResult::None &&
             list.isOpen(),
           "a click in the list's padding neither chooses nor closes");
  testTrue(counters,
           list.handlePointer(150.0f, 110.0f, false, true) ==
               GuiDropdownResult::Dismissed &&
             !list.isOpen(),
           "a click on the field or elsewhere closes the list");

  list.open(
    sampleItems(), 0, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  list.scrollRows(3);
  testTrue(counters,
           list.firstVisibleRow() == 3 && list.highlightedIndex() == 0,
           "the wheel scrolls the window without moving the highlight");
  list.scrollRows(100);
  testTrue(counters,
           list.firstVisibleRow() == 20 - list.visibleRowCount(),
           "scrolling stops at the last window");
  return counters.failures;
}

static int
testDropdownDrawing()
{
  TestCounters counters;
  GuiDropdownList list;
  std::vector<GuiDropdownItem> items = sampleItems();
  items[1].label = "An extraordinarily long item name that cannot possibly "
                   "fit inside a narrow list";
  GameVisual card(4096u);
  GameVisual highlight(1024u);
  GameVisual text(1024u);
  list.draw(card, highlight, text, 255);
  testTrue(counters,
           card.shapeCount() == 0u && text.textCount() == 0u,
           "a closed list draws nothing");

  list.open(items, 0, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, true);
  list.draw(card, highlight, text, 255);
  testTrue(counters, card.shapeCount() > 0u, "an open list draws its card");
  testTrue(counters,
           highlight.shapeCount() > 0u,
           "the highlighted row draws its liquid drop");
  // Eight labels plus Beta's detail.
  testEqSize(counters, text.textCount(), 9u, "every visible row has a label");
  bool cut = false;
  for (std::size_t index = 0; index < text.textCount(); ++index) {
    const std::string& content = text.getText(index)->content;
    cut = cut || (content.size() > 3u &&
                  content.compare(content.size() - 3u, 3u, "...") == 0);
  }
  testTrue(counters, cut, "a label too long for the list ends in \"...\"");

  GameVisual opening(4096u);
  GameVisual openingHighlight(1024u);
  GameVisual openingText(1024u);
  list.open(items, 0, 100.0f, 100.0f, 200.0f, 30.0f, 0.0f, 480.0f, false);
  list.draw(opening, openingHighlight, openingText, 255);
  testTrue(counters,
           opening.shapeCount() == 0u && openingText.textCount() == 0u,
           "with motion, the list pops open from nothing");
  list.tick(0.05f);
  GameVisual growing(4096u);
  GameVisual growingHighlight(1024u);
  GameVisual growingText(1024u);
  list.draw(growing, growingHighlight, growingText, 255);
  testTrue(counters,
           growing.shapeCount() > 0u && growingText.textCount() < 9u,
           "rows appear as the opening card uncovers them");
  return counters.failures;
}

void
registerGuiDropdownListTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Gui.DropdownPlacement",
               []() { return testDropdownPlacement(); });
  registry.add("Illumo.Gui.DropdownKeyboard",
               []() { return testDropdownKeyboard(); });
  registry.add("Illumo.Gui.DropdownTypeAhead",
               []() { return testDropdownTypeAhead(); });
  registry.add("Illumo.Gui.DropdownPointerAndWheel",
               []() { return testDropdownPointerAndWheel(); });
  registry.add("Illumo.Gui.DropdownDrawing",
               []() { return testDropdownDrawing(); });
}
