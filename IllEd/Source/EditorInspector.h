#pragma once

#include "EditorBehaviours.h"
#include "EditorDocument.h"
#include "EditorSelection.h"
#include <Illumo/Gui/GuiPanelPointer.h>
#include <Illumo/Gui/GuiTextEdit.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

enum class InspectorFieldKind
{
  Text,
  Number,
  Toggle,
  Choice,
  Button,
  ReadOnly,
  // A color row's swatch, drawn in its label column; opens the picker.
  Swatch
};

// One editable value. Keys name what it edits ("position.x",
// "primitive.color.r", "env.sun.intensity", "component.add"...). Asset table
// entries use "asset.<property>:<asset id>"; behaviour fields use
// "behaviour:<type>:<field>", with ":<axis>" for vector and color parts.
struct InspectorField
{
  std::string key;
  std::string label;
  InspectorFieldKind kind = InspectorFieldKind::Text;
  std::string value;
  bool toggle = false;
  // The selection disagrees; the field shows a dash until edited.
  bool mixed = false;
  std::vector<std::string> choices;
  int choice = 0;
  // Scrub speed in value units per pixel for Number fields.
  float step = 0.05f;
  // Number fields: whole values only (scrubs carry fractions), and whether a
  // drag scrubs at all (false: a click always starts typing).
  bool integer = false;
  bool scrub = true;
  // Longest text a Text or Number field accepts.
  size_t maxBytes = GuiTextEdit::kDefaultMaximumBytes;
  // In a folded section: kept for field(), never laid out, drawn or hit.
  bool hidden = false;
  // A small button in its section's header (a component's remove).
  bool header = false;
  // Swatch fields: the color shown, the channel fields it edits (r, g, b
  // and maybe a) and whether those hold 0-1 floats (else bytes).
  ColorRgba swatch{ 128, 128, 128, 255 };
  std::vector<std::string> channels;
  bool unitChannels = false;
  // Layout, in UI space.
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
};

// A label and the fields beside it, or a section header. `group` names what
// the section edits, for its menu: "transform", a core component
// ("primitive", "mesh", "sprite", "light", "camera") or
// "behaviour:<type>"; empty for anything else.
struct InspectorRow
{
  std::string label;
  bool section = false;
  bool folded = false;
  std::string group;
  std::vector<size_t> fields;
  float y = 0.0f;
};

// The Inspector dock panel's content: a typed property editor for the
// selection (or the scene when nothing is selected), drawn into the rectangle
// and surface it is given; text entry works in a detached window too. Fields
// are rebuilt from the document every frame, so the inspector holds no copy of
// scene state beyond the one field being edited. Every change goes through
// EditorDocument as one history command covering the whole selection; number
// scrubs merge into one command per drag.
class EditorInspector : public DrawableBase
{
public:
  EditorInspector(IRenderWindow* window, Renderer* renderer);
  ~EditorInspector() override = default;
  EditorInspector(const EditorInspector&) = delete;
  EditorInspector& operator=(const EditorInspector&) = delete;

  void setFontSize(float sizePt) { m_fontSize = sizePt; }
  // Known behaviours show as typed fields and in Add behaviour; without them
  // (or for an unknown type) a behaviour component shows its JSON.
  void setBehaviours(const EditorBehaviours* behaviours)
  {
    m_behaviours = behaviours;
  }
  // The content rectangle and surface for this frame; hiding it ends an edit.
  void setPlacement(const GuiPanelPlacement& placement);
  const GuiPanelPlacement& placement() const { return m_placement; }

  // Returns true when the document changed.
  bool update(InputManager* input,
              EditorDocument* document,
              const EditorSelection* selection,
              float dt);
  // A text field has focus: keyboard shortcuts and camera keys must wait.
  bool editing() const { return m_edit.active(); }
  bool consumedPress() const { return m_consumedPress; }
  bool containsScreenPoint(float x, float y) const;

  // Clipboard hand-off: the module performs the platform clipboard work.
  bool takeCopyRequest(std::string* text);
  bool takePasteRequest();
  void providePaste(const std::string& text);

  const std::vector<InspectorField>& fields() const { return m_fields; }
  const InspectorField* field(const std::string& key) const;
  // Activates a field as a click would (F2 focuses "name"; tests drive it).
  bool activateField(const std::string& key,
                     EditorDocument* document,
                     const EditorSelection* selection);
  // Folds or unfolds a section by its title, as clicking its header does.
  void toggleSectionForTesting(const std::string& title);
  bool sectionFolded(const std::string& title) const
  {
    return m_folded.contains(title);
  }
  // The right-click menu: opens on a row (a field's key, or a section title
  // when `key` is empty), lists its labels, and runs one by label.
  bool openMenuForTesting(const std::string& key,
                          const std::string& sectionTitle,
                          EditorDocument* document,
                          const EditorSelection* selection);
  std::vector<std::string> menuLabelsForTesting() const;
  bool chooseMenuForTesting(const std::string& label,
                            EditorDocument* document,
                            const EditorSelection* selection);
  bool menuOpen() const { return m_menuOpen; }
  // A Choice field's option list (a click on its middle opens it).
  bool listOpen() const { return m_listOpen; }
  bool openListForTesting(const std::string& key,
                          EditorDocument* document,
                          const EditorSelection* selection);
  bool chooseListForTesting(int option,
                            EditorDocument* document,
                            const EditorSelection* selection);
  float labelWidthForTesting() const { return m_labelWidth; }
  // The color picker a swatch opens: whose channels it edits, and picking a
  // color (hue, saturation and value in 0-1, alpha in 0-1) as a drag would.
  bool pickerOpen() const { return m_pickerOpen; }
  const std::string& pickerSwatch() const { return m_pickerSwatch; }
  // Types into the picker's hex box and presses Enter.
  bool typeHexForTesting(const std::string& text,
                         EditorDocument* document,
                         const EditorSelection* selection)
  {
    return m_pickerOpen && document != nullptr && selection != nullptr &&
           applyText("picker.hex", text, *document, *selection);
  }
  bool pickForTesting(float hue,
                      float saturation,
                      float value,
                      float alpha,
                      EditorDocument* document,
                      const EditorSelection* selection);
  bool scrubForTesting(const std::string& key,
                       float pixels,
                       EditorDocument* document,
                       const EditorSelection* selection);
  const std::string& editingKey() const { return m_editKey; }
  bool invalidInput() const { return m_invalid; }

  GameVisual& getVisual() { return m_visual; }
  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  IRenderWindow* m_window;
  Renderer* m_renderer;
  const EditorBehaviours* m_behaviours = nullptr;
  GameVisual m_visual;
  GuiPanelPointer m_pointer;
  GuiPanelPlacement m_placement;
  std::vector<InspectorField> m_fields;
  std::vector<InspectorRow> m_rows;
  GuiTextEdit m_edit;
  std::string m_editKey;
  bool m_invalid = false;
  bool m_consumedPress = false;
  bool m_visible = true;
  float m_x = 0.0f;
  float m_y = 0.0f;
  float m_width = 200.0f;
  float m_height = 400.0f;
  float m_fontSize = 13.0f;
  float m_scroll = 0.0f;
  float m_contentHeight = 0.0f;
  int m_hoverField = -1;
  // Number scrubbing: pressed on a field's label area and dragged.
  std::string m_scrubKey;
  float m_scrubLastX = 0.0f;
  float m_scrubAccumulated = 0.0f;
  // Fraction an integer scrub has not applied yet.
  double m_scrubCarry = 0.0;
  uint64_t m_scrubSerial = 0;
  std::string m_copyText;
  bool m_copyPending = false;
  bool m_pastePending = false;
  // Inputs the current fields were built from; see refreshFields.
  const EditorDocument* m_fieldsDocument = nullptr;
  uint64_t m_fieldsGeneration = 0;
  uint64_t m_fieldsRevision = 0;
  uint64_t m_fieldsBehaviours = 0;
  std::string m_fieldsPrimary;
  std::vector<std::string> m_fieldsSelection;
  bool m_fieldsBuilt = false;
  // The label column's width this frame: fits the longest label, within
  // 26-36% of the panel.
  float m_labelWidth = 0.0f;
  // A Choice field's open option list: its key, options, the current one,
  // the first row shown and the hovered row.
  static constexpr int kListRows = 10;
  bool m_listOpen = false;
  std::string m_listKey;
  std::vector<std::string> m_listOptions;
  int m_listCurrent = -1;
  int m_listFirst = 0;
  int m_listHover = -1;
  float m_listX = 0.0f;
  float m_listY = 0.0f;
  // Section titles folded by clicking their headers (session state).
  std::unordered_set<std::string> m_folded;
  // The group buildFields is filling (see InspectorRow::group).
  std::string m_buildGroup;
  // The right-click menu: what it acts on and its entries.
  enum class MenuAction
  {
    ResetRow,
    ResetGroup,
    CopyGroup,
    PasteGroup,
    RemoveGroup
  };
  struct MenuEntry
  {
    std::string label;
    MenuAction action = MenuAction::ResetRow;
    bool enabled = true;
  };
  bool m_menuOpen = false;
  float m_menuX = 0.0f;
  float m_menuY = 0.0f;
  int m_menuHover = -1;
  std::vector<MenuEntry> m_menu;
  std::vector<std::string> m_menuKeys;
  std::string m_menuGroup;
  // The color picker: the swatch it belongs to and its channels, its color
  // in HSV, which part a drag holds (0 none, 1 square, 2 hue, 3 alpha) and a
  // serial so one open picker is one undo step.
  bool m_pickerOpen = false;
  std::string m_pickerSwatch;
  std::vector<std::string> m_pickerChannels;
  bool m_pickerUnit = false;
  float m_pickerHue = 0.0f;
  float m_pickerSaturation = 0.0f;
  float m_pickerValue = 1.0f;
  float m_pickerAlpha = 1.0f;
  int m_pickerDrag = 0;
  uint64_t m_pickerSerial = 0;
  // Copied values: a component (by group) or a transform.
  std::string m_clipboardGroup;
  SceneComponent m_clipboardComponent;
  Transform3D m_clipboardTransform;

  // Rebuilds the fields only when their inputs changed: the document's
  // scene (generation and revision), the selection or the known behaviours.
  void refreshFields(const EditorDocument& document,
                     const EditorSelection& selection);
  void buildFields(const EditorDocument& document,
                   const EditorSelection& selection);
  void layout();
  void rebuildVisual();
  // Drops the rows of folded sections and hides their fields.
  void applyFolding();
  // Moves components' Remove buttons into their section headers.
  void moveRemoveButtonsToHeaders();
  void openList(const InspectorField& choice);
  std::vector<GuiToolStyle::MenuItem> listItems() const;
  float listWidth() const;
  int listOptionAt(float x, float y) const;
  // The row at a panel y (fields or a section header); -1 for none.
  int rowAt(float y) const;
  float rowHeight() const;
  void openMenu(int row,
                float x,
                float y,
                const EditorDocument& document,
                const EditorSelection& selection);
  std::vector<GuiToolStyle::MenuItem> menuItems() const;
  int menuItemAt(float x, float y) const;
  bool runMenu(MenuAction action,
               EditorDocument& document,
               const EditorSelection& selection);
  // The color picker (EditorInspector.cpp, end).
  void openPicker(const InspectorField& swatch);
  GuiToolRect pickerRect() const;
  GuiToolRect pickerSquare() const;
  GuiToolRect pickerHueStrip() const;
  GuiToolRect pickerAlphaStrip() const;
  GuiToolRect pickerHexBox() const;
  // Writes the picker's color to its channels as one merged command.
  bool applyPicker(EditorDocument& document, const EditorSelection& selection);
  // Pointer handling while the picker is open; true when it took the press.
  bool updatePicker(float x,
                    float y,
                    bool* changed,
                    EditorDocument& document,
                    const EditorSelection& selection);
  void drawPicker();
  int hitTest(float x, float y) const;
  bool beginEdit(const InspectorField& field);
  // Sets a Choice field to option index next (cycling and the list share
  // it).
  bool applyChoice(const InspectorField& target,
                   int next,
                   EditorDocument& document,
                   const EditorSelection& selection);
  bool commitEdit(EditorDocument& document, const EditorSelection& selection);
  bool activate(const InspectorField& field,
                bool reverse,
                EditorDocument& document,
                const EditorSelection& selection);
  bool applyNumber(const std::string& key,
                   double value,
                   bool relative,
                   const std::string& mergeKey,
                   EditorDocument& document,
                   const EditorSelection& selection);
  // A behaviour field edit on every selected node holding the behaviour.
  bool applyBehaviour(
    const std::string& key,
    const std::function<
      bool(BehaviourValue&, const BehaviourField&, char axis)>& change,
    const std::string& mergeKey,
    EditorDocument& document,
    const EditorSelection& selection);
  // A number edit as a function of each target's current value (typed
  // "*=2", a wheel step).
  bool applyNumberWith(const std::string& key,
                       const std::function<double(double current)>& change,
                       const std::string& mergeKey,
                       EditorDocument& document,
                       const EditorSelection& selection);
  bool applyText(const std::string& key,
                 const std::string& text,
                 EditorDocument& document,
                 const EditorSelection& selection);
  // Applies a scrub of `amount` value units to a Number field; integer
  // fields apply whole units and carry the rest to the next call.
  bool scrubBy(const InspectorField& target,
               double amount,
               EditorDocument& document,
               const EditorSelection& selection);
};
