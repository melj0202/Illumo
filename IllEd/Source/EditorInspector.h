#pragma once

#include "EditorDocument.h"
#include "EditorSelection.h"
#include <Illumo/Gui/GuiPanelPointer.h>
#include <Illumo/Gui/GuiTextEdit.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <string>
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
  ReadOnly
};

// One editable value. Keys name what it edits ("position.x",
// "primitive.color.r", "env.sun.intensity", "component.add"...).
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
  // Layout, in UI space.
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
};

// A label and the fields beside it, or a section header.
struct InspectorRow
{
  std::string label;
  bool section = false;
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
  uint64_t m_scrubSerial = 0;
  std::string m_copyText;
  bool m_copyPending = false;
  bool m_pastePending = false;

  void buildFields(const EditorDocument& document,
                   const EditorSelection& selection);
  void layout();
  void rebuildVisual();
  int hitTest(float x, float y) const;
  bool beginEdit(const InspectorField& field);
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
  bool applyText(const std::string& key,
                 const std::string& text,
                 EditorDocument& document,
                 const EditorSelection& selection);
};
