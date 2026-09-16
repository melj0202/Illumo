#pragma once

#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
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
  const std::string& getPreviewText() const { return previewText; }
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
  GameVisual& getVisual() { return visual; }

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
    ModelKind,
    RulesetName,
    BirthCounts,
    SurvivalCounts,
    GenerationStates,
    CyclicThreshold,
    CyclicStep,
    WolframNumber,
    TableInformation,
    PreviewState,
    PreviewNeighbors,
    PreviewNeighborhood,
    PreviewResult,
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
  };

  static constexpr float kOpenAnimationSeconds = 0.42f;
  static constexpr float kSelectionAnimationSeconds = 0.14f;
  static constexpr float kValuePulseSeconds = 0.20f;
  IRenderWindow* window;
  Renderer* renderer;
  GameVisual visual;
  RuleFamilyDefinition familyDraft;
  RuleSetDefinition draft;
  std::vector<MenuRow> rows;
  std::vector<std::string> starterRuleIds;
  int starterRuleIndex = 0;
  int selectedRow = 0;
  int firstVisibleRow = 0;
  int bodyRowCount = 0;
  int visibleRows = 1;
  float animationElapsed = 0.0f;
  float selectionFromRow = 0.0f;
  float selectionAnimationElapsed = kSelectionAnimationSeconds;
  float valuePulseElapsed = kValuePulseSeconds;
  float ambientPhase = 0.0f;
  float layoutScale = 1.0f;
  float panelX = 0.0f;
  float panelY = 0.0f;
  float panelWidth = 0.0f;
  float panelHeight = 0.0f;
  float headerHeight = 0.0f;
  float footerHeight = 0.0f;
  float firstRowY = 0.0f;
  float rowHeight = 0.0f;
  unsigned int neighborCount = 3u;
  unsigned int previewNeighborCount = 3u;
  unsigned int previewNeighborhood = 2u;
  unsigned int previewState = 0u;
  unsigned int paletteState = 0u;
  bool openState = false;
  bool mouseWasDown = false;
  float previousMouseX = -1.0f;
  float previousMouseY = -1.0f;
  bool reducedMotion = false;
  bool familyChanged = false;
  bool previewDirty = true;
  unsigned char previewInputState = 0u;
  unsigned char previewOutputState = 1u;
  std::string errorMessage;
  std::string previewText;

  void updateLayout();
  float animationProgress() const;
  float panelReveal() const;
  float panelOffsetY() const;
  float rowReveal(int row) const;
  float selectionRowPosition() const;
  float valuePulse() const;
  void triggerValuePulse();
  void selectRow(int row);
  void changeSelected(int direction);
  void moveSelection(int direction);
  RulesetWorkshopAction activateSelected();
  RulesetWorkshopAction activateControl(Control control);
  bool changeControl(Control control, int direction);
  bool toggleNeighborCount(Control control, unsigned int count);
  bool isTextControl(int row) const;
  int bodyIndexForRow(int row) const;
  int rowForControl(Control control) const;
  int bodyRowForPoint(float x, float y) const;
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
  std::string valueForControl(Control control) const;
  std::string helpForControl(Control control) const;
  Control controlForRow(int row) const;
};
