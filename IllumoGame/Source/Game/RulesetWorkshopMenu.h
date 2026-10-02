#pragma once

#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Gui/GuiDropdownList.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>
#include <string>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

enum class RulesetWorkshopAction
{
  None,
  Import,
  Export,
  Apply,
  Cancel
};

// A focused, staged editor for the current rule. Changes are copied into the
// registry only after the owner drains simulation and accepts Save & Apply.
//
// The body reads top to bottom as the job: pick where to start (starter rule
// and cell family), name the draft, shape its behaviour, see what it does
// (one row per cell state showing the state every neighbor count leads to),
// then dress its states. File actions and the outcome buttons are pinned in
// the footer.
class RulesetWorkshopMenu : public DrawableBase
{
public:
  RulesetWorkshopMenu(IRenderWindow* window, Renderer* renderer);
  ~RulesetWorkshopMenu() override = default;

  RulesetWorkshopMenu(const RulesetWorkshopMenu&) = delete;
  RulesetWorkshopMenu& operator=(const RulesetWorkshopMenu&) = delete;

  bool open(const RuleFamilyDefinition& currentFamily,
            const RuleSetDefinition& currentRule,
            bool reducedMotion = false);
  void setDraft(const RuleFamilyDefinition& family,
                const RuleSetDefinition& rule);
  void close();
  bool isOpen() const { return openState; }
  void tick(float deltaSeconds);
  RulesetWorkshopAction update(InputManager* input);
  void setError(const std::string& error);
  const RuleFamilyDefinition& getFamilyDraft() const { return familyDraft; }
  bool isFamilyDraftChanged() const { return familyChanged; }
  const RuleSetDefinition& getDraft() const { return draft; }
  const std::string& getError() const { return errorMessage; }
  // The focused example transition, e.g. "Alive + 3 live neighbors -> Alive".
  const std::string& getPreviewText() const { return previewText; }
  // The compact notation the header shows for the draft, e.g. "B3/S23".
  std::string getNotationForTesting() const { return ruleNotation(); }
  int getSelectedRowForTesting() const { return selectedRow; }
  int getFirstVisibleRowForTesting() const { return firstVisibleRow; }
  int getControlIndexForTesting(const std::string& label) const;
  bool hasControlForTesting(const std::string& label) const;
  std::string getSelectedControlForTesting() const;
  float getBodyBottomForTesting() const
  {
    return firstRowY + rowHeight * static_cast<float>(visibleRows);
  }
  float getFooterTopForTesting() const
  {
    return panelY + panelHeight - footerHeight;
  }
  float getAnimationProgressForTesting() const;
  float getSelectionPositionForTesting() const;
  float getValuePulseForTesting() const;
  // The cell family or starter rule list, open while the player picks.
  const GuiDropdownList& getDropdownForTesting() const { return dropdown; }
  // The header, the rows' contents and the footer: every text the menu draws.
  GameVisual& getVisual() { return layers[kContentLayer]; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  enum class RowKind
  {
    Section,
    Control,
    Information,
    FooterAction
  };

  enum class Control
  {
    None,
    StarterRule,
    Family,
    FamilyName,
    RulesetName,
    BirthCounts,
    SurvivalCounts,
    GenerationStates,
    CyclicThreshold,
    CyclicStep,
    WolframNumber,
    TableInformation,
    // One row per shown cell state: the state each neighbor count leads to.
    PreviewStrip,
    // Elementary 1D: the next centre cell for each of the eight patterns.
    PreviewNeighborhood,
    PaletteState,
    StateName,
    Red,
    Green,
    Blue,
    Import,
    Export,
    Apply,
    Discard
  };

  struct MenuRow
  {
    RowKind kind = RowKind::Section;
    Control control = Control::None;
    std::string label;
    std::string detail;
    // The cell state a PreviewStrip row shows.
    unsigned int state = 0u;
  };

  // Back to front: the glass; the row surfaces; the selection drop; the
  // header, row contents and footer; the chips' cursor; then an open list's
  // card, highlight and labels above everything. The glass, drops and cursor
  // breathe, so each sits in its own layer and the host re-records only
  // those while the menu idles (D-R29).
  enum Layer : std::size_t
  {
    kGlassLayer,
    kSurfaceLayer,
    kDropLayer,
    kContentLayer,
    kCursorLayer,
    kListCardLayer,
    kListDropLayer,
    kListTextLayer,
    kLayerCount
  };

  // Footer buttons, left to right, in keyboard order.
  static constexpr int kFooterButtonCount = 4;
  // At most this many states get a preview row; longer decay chains add
  // nothing new past their first few states.
  static constexpr unsigned int kPreviewRowLimit = 6u;
  // Up to this many states pick by swatch; more step through a list.
  static constexpr unsigned int kSwatchPickerLimit = 12u;

  IRenderWindow* window;
  Renderer* renderer;
  std::array<GameVisual, kLayerCount> layers;
  // The fitted scale every layer is drawn at.
  float visualScale = 1.0f;
  GuiMenuAnimator animator;
  GuiPointerTracker pointer;
  // Spring-driven row emphasis (by body row), neighbor-count chip glow
  // (birth 0-8, survival 9-17), footer button focus and scrollbar thumb.
  GuiSpringArray rowFocus;
  GuiSpringArray chipGlow;
  GuiSpringArray footerFocus;
  GuiSpring scrollThumb;
  // The panel swivels toward the pointer; its layout origin carries the body
  // shift, so rows and footer buttons are hit where they are drawn.
  GuiPanelTilt tilt;
  GuiPanelFit panelFit;
  RuleFamilyDefinition familyDraft;
  RuleSetDefinition draft;
  std::vector<MenuRow> rows;
  std::vector<std::string> starterRuleIds;
  int starterRuleIndex = 0;
  int selectedRow = 0;
  int firstVisibleRow = 0;
  int bodyRowCount = 0;
  int visibleRows = 1;
  float panelX = 0.0f;
  float panelY = 0.0f;
  float panelWidth = 0.0f;
  float panelHeight = 0.0f;
  float headerHeight = 0.0f;
  float footerHeight = 0.0f;
  float firstRowY = 0.0f;
  float rowHeight = 0.0f;
  // The B/S chip under the keyboard cursor.
  unsigned int neighborCount = 3u;
  // The focused example: a state, its neighbor count, and (1D) a pattern.
  unsigned int previewNeighborCount = 3u;
  unsigned int previewNeighborhood = 2u;
  unsigned int previewState = 0u;
  unsigned int paletteState = 0u;
  bool openState = false;
  bool familyChanged = false;
  bool previewDirty = true;
  // Shift held: color channels step by one instead of eight.
  bool fineAdjust = false;
  // The color channel whose slider the pointer is dragging, or None.
  Control dragControl = Control::None;
  // Next state per previewed state row-major by neighbor count (0..8), and
  // per elementary pattern (0..7).
  std::vector<unsigned char> previewNext;
  std::array<unsigned char, 8> previewElementaryNext{};
  bool previewValid = false;
  // The open family or starter-rule list, the row it drops from, and the ids
  // its items stand for.
  GuiDropdownList dropdown;
  Control listControl = Control::None;
  std::vector<std::string> listIds;
  std::string errorMessage;
  std::string previewText;

  void updateLayout();
  float rowReveal(int row) const;
  float selectionRowPosition() const;
  void selectRow(int row);
  void changeSelected(int direction);
  void moveSelection(int direction);
  RulesetWorkshopAction activateSelected();
  RulesetWorkshopAction activateControl(Control control);
  bool changeControl(Control control, int direction);
  bool toggleNeighborCount(Control control, unsigned int count);
  bool setColorChannel(Control control, int value);
  bool isTextControl(int row) const;
  static bool isListControl(Control control);
  // The value field of a visible row in layout space: x, y, width, height.
  std::array<float, 4> valueField(int row) const;
  void openList(Control control);
  void chooseFromList(int index);
  RulesetWorkshopAction updateList(InputManager* input);
  void applyStarterRule(int index);
  bool isPreviewByCount() const;
  int bodyIndexForRow(int row) const;
  int rowForControl(Control control) const;
  int bodyRowForPoint(float x, float y) const;
  // Footer button `index` (Import, Export, Apply, Discard) in layout space:
  // x, y, width, height.
  std::array<float, 4> footerButtonRect(int index) const;
  // A row's value column and, inside it, the chip strip's geometry.
  float valueLeft() const;
  float valueRight() const;
  float sliderTrackLeft() const;
  float sliderTrackRight() const;
  void chipStrip(int count, float* firstX, float* chipWidth, float* gap) const;
  void rebuildRows();
  void appendSection(const std::string& label);
  void appendControl(Control control, const std::string& label);
  void appendInformation(Control control,
                         const std::string& label,
                         const std::string& detail);
  void cycleStarterRule(int direction);
  bool selectFamily(const std::string& familyId);
  void resizeGenerationStates(unsigned int count);
  void refreshPreview();
  void rebuildVisual();
  void drawHeader(float reveal, unsigned char panelOpacity);
  void drawFooter(unsigned char panelOpacity);
  void updateSprings(float deltaSeconds, bool snap);
  std::string ruleNotation() const;
  std::string valueForControl(Control control) const;
  std::string helpForControl(Control control) const;
  Control controlForRow(int row) const;
};
