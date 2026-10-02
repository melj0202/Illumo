#include "EditorInspector.h"

#include "EditorToolbar.h"
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <glm/gtc/quaternion.hpp>
#include <unordered_set>

// The leading choice of an optional asset reference (the skybox).
static const char* const kNoAsset = "(none)";
// The leading choice of the scene's game: whichever its behaviours name.
static const char* const kAutomaticGame = "(automatic)";
// The format's limits for a scene description and, with separators, a node's
// tags (32 of at most 64 bytes).
static constexpr size_t kMaximumDescriptionBytes = 4096;
static constexpr size_t kMaximumTagsBytes = 2304;

// ---------------------------------------------------------------------------
// Values: every numeric key resolves to a float or a color byte inside a node
// (or the environment), so building, mixed detection, scrubbing and applying
// share one table.

static std::string
formatNumber(double value)
{
  if (!std::isfinite(value)) {
    return "0";
  }
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.4f", value);
  std::string text(buffer);
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text == "-0" ? "0" : text;
}

// HSV and RGB, every channel in 0-1 (hue wraps).
static void
hsvToRgb(float hue, float saturation, float value, float* r, float* g, float* b)
{
  const float scaled = (hue - std::floor(hue)) * 6.0f;
  const int sector = static_cast<int>(std::floor(scaled)) % 6;
  const float fraction = scaled - std::floor(scaled);
  const float p = value * (1.0f - saturation);
  const float q = value * (1.0f - fraction * saturation);
  const float t = value * (1.0f - (1.0f - fraction) * saturation);
  const float table[6][3] = {
    { value, t, p }, { q, value, p }, { p, value, t },
    { p, q, value }, { t, p, value }, { value, p, q }
  };
  *r = table[sector][0];
  *g = table[sector][1];
  *b = table[sector][2];
}

static void
rgbToHsv(float r, float g, float b, float* hue, float* saturation, float* value)
{
  const float maximum = std::max({ r, g, b });
  const float minimum = std::min({ r, g, b });
  const float range = maximum - minimum;
  *value = maximum;
  *saturation = maximum > 0.0f ? range / maximum : 0.0f;
  if (range <= 0.0f) {
    // Grey keeps whatever hue the picker had.
    return;
  }
  float sector = 0.0f;
  if (maximum == r) {
    sector = (g - b) / range;
  } else if (maximum == g) {
    sector = 2.0f + (b - r) / range;
  } else {
    sector = 4.0f + (r - g) / range;
  }
  *hue = sector / 6.0f;
  if (*hue < 0.0f) {
    *hue += 1.0f;
  }
}

static ColorRgba
toBytes(float r, float g, float b, float a)
{
  return ColorRgba{
    static_cast<unsigned char>(std::lround(std::clamp(r, 0.0f, 1.0f) * 255)),
    static_cast<unsigned char>(std::lround(std::clamp(g, 0.0f, 1.0f) * 255)),
    static_cast<unsigned char>(std::lround(std::clamp(b, 0.0f, 1.0f) * 255)),
    static_cast<unsigned char>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255))
  };
}

// Arithmetic in number fields: numbers, + - * /, parentheses and unary
// signs, evaluated with an operator stack (no recursion). False on anything
// else, a division by zero or a non-finite result.
static int
operatorPrecedence(char op)
{
  return op == 'u' || op == 'p' ? 3 : (op == '*' || op == '/' ? 2 : 1);
}

static bool
applyOperator(char op, std::vector<double>& values)
{
  if (op == 'u' || op == 'p') {
    if (values.empty()) {
      return false;
    }
    if (op == 'u') {
      values.back() = -values.back();
    }
    return true;
  }
  if (values.size() < 2) {
    return false;
  }
  const double right = values.back();
  values.pop_back();
  double& left = values.back();
  switch (op) {
    case '+':
      left += right;
      return true;
    case '-':
      left -= right;
      return true;
    case '*':
      left *= right;
      return true;
    case '/':
      if (right == 0.0) {
        return false;
      }
      left /= right;
      return true;
    default:
      return false;
  }
}

static bool
evaluateExpression(const std::string& text, double* result)
{
  std::vector<double> values;
  std::vector<char> operators;
  bool expectOperand = true;
  size_t index = 0;
  while (index < text.size()) {
    const char character = text[index];
    if (character == ' ') {
      ++index;
      continue;
    }
    if (expectOperand) {
      if (character == '-' || character == '+') {
        operators.push_back(character == '-' ? 'u' : 'p');
        ++index;
        continue;
      }
      if (character == '(') {
        operators.push_back('(');
        ++index;
        continue;
      }
      if (!((character >= '0' && character <= '9') || character == '.')) {
        return false;
      }
      const char* start = text.c_str() + index;
      char* end = nullptr;
      const double value = std::strtod(start, &end);
      if (end == start) {
        return false;
      }
      values.push_back(value);
      index += static_cast<size_t>(end - start);
      expectOperand = false;
      continue;
    }
    if (character == ')') {
      while (!operators.empty() && operators.back() != '(') {
        if (!applyOperator(operators.back(), values)) {
          return false;
        }
        operators.pop_back();
      }
      if (operators.empty()) {
        return false;
      }
      operators.pop_back();
      ++index;
      continue;
    }
    if (character != '+' && character != '-' && character != '*' &&
        character != '/') {
      return false;
    }
    while (!operators.empty() && operators.back() != '(' &&
           operatorPrecedence(operators.back()) >=
             operatorPrecedence(character)) {
      if (!applyOperator(operators.back(), values)) {
        return false;
      }
      operators.pop_back();
    }
    operators.push_back(character);
    expectOperand = true;
    ++index;
  }
  if (expectOperand) {
    return false;
  }
  while (!operators.empty()) {
    if (operators.back() == '(' || !applyOperator(operators.back(), values)) {
      return false;
    }
    operators.pop_back();
  }
  if (values.size() != 1 || !std::isfinite(values.front())) {
    return false;
  }
  *result = values.front();
  return true;
}

static int
axisIndex(char axis)
{
  switch (axis) {
    case 'x':
    case 'r':
    case 'w':
      return 0;
    case 'y':
    case 'g':
    case 'h':
      return 1;
    case 'z':
    case 'b':
      return 2;
    default:
      return 3;
  }
}

template<typename T>
static T*
componentOf(SceneNode& node)
{
  for (SceneComponent& component : node.components) {
    T* value = std::get_if<T>(&component.value);
    if (value != nullptr) {
      return value;
    }
  }
  return nullptr;
}

// A float, integer or color byte inside a node (or an asset) for a key, or
// nothing.
struct NumberSlot
{
  float* number = nullptr;
  int* integer = nullptr;
  unsigned char* byte = nullptr;
  // Euler degree axis 0-2 for rotation keys, else -1.
  int rotationAxis = -1;
};

static bool
readSlot(const NumberSlot& slot, double* value)
{
  if (slot.number != nullptr) {
    *value = *slot.number;
    return true;
  }
  if (slot.integer != nullptr) {
    *value = *slot.integer;
    return true;
  }
  if (slot.byte != nullptr) {
    *value = *slot.byte;
    return true;
  }
  return false;
}

static bool
writeSlot(const NumberSlot& slot, double value)
{
  if (slot.number != nullptr) {
    *slot.number = static_cast<float>(value);
    return true;
  }
  if (slot.integer != nullptr) {
    *slot.integer =
      static_cast<int>(std::lround(std::clamp(value, -1.0e9, 1.0e9)));
    return true;
  }
  if (slot.byte != nullptr) {
    *slot.byte =
      static_cast<unsigned char>(std::lround(std::clamp(value, 0.0, 255.0)));
    return true;
  }
  return false;
}

// Asset table fields are keyed "asset.<property>:<asset id>"; the property
// never holds a colon, so the first one splits them.
static bool
splitAssetKey(const std::string& key, std::string* property, std::string* id)
{
  const std::string prefix = "asset.";
  const size_t colon = key.find(':');
  if (!key.starts_with(prefix) || colon == std::string::npos) {
    return false;
  }
  *property = key.substr(prefix.size(), colon - prefix.size());
  *id = key.substr(colon + 1);
  return true;
}

static NumberSlot
resolveAsset(SceneAsset& asset, const std::string& property)
{
  NumberSlot slot;
  if (property == "radius") {
    slot.number = &asset.mesh.targetRadius;
  } else if (property == "columns") {
    slot.integer = &asset.columns;
  } else if (property == "rows") {
    slot.integer = &asset.rows;
  }
  return slot;
}

// Copies the asset table, mutates one entry and records the result as one
// command; false when the asset is gone, the mutator declines or the scene
// rejects the table.
static bool
editAsset(EditorDocument& document,
          const std::string& id,
          const std::string& label,
          const std::string& mergeKey,
          const std::function<bool(SceneAsset&)>& mutate)
{
  std::vector<SceneAsset> assets = document.scene().document().assets;
  for (SceneAsset& asset : assets) {
    if (asset.id == id) {
      return mutate(asset) && document.setAssets(assets, label, mergeKey);
    }
  }
  return false;
}

static const SceneAsset*
findAsset(const EditorDocument& document, const std::string& id)
{
  return document.scene().document().findAsset(id);
}

// Tags as typed: comma separated, trimmed, empty and repeated ones dropped.
static std::vector<std::string>
parseTags(const std::string& text)
{
  std::vector<std::string> tags;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(',', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    size_t first = start;
    size_t last = end;
    while (first < last && text[first] == ' ') {
      ++first;
    }
    while (last > first && text[last - 1] == ' ') {
      --last;
    }
    const std::string tag = text.substr(first, last - first);
    if (!tag.empty() &&
        std::find(tags.begin(), tags.end(), tag) == tags.end()) {
      tags.push_back(tag);
    }
    start = end + 1;
  }
  return tags;
}

static std::string
joinTags(const std::vector<std::string>& tags)
{
  std::string text;
  for (const std::string& tag : tags) {
    text += text.empty() ? tag : ", " + tag;
  }
  return text;
}

static unsigned char*
colorByte(ColorRgba& color, char channel)
{
  switch (channel) {
    case 'r':
      return &color.r;
    case 'g':
      return &color.g;
    case 'b':
      return &color.b;
    default:
      return &color.a;
  }
}

static NumberSlot
resolveNode(SceneNode& node, const std::string& key)
{
  NumberSlot slot;
  const char last = key.empty() ? '\0' : key.back();
  if (key.starts_with("position.")) {
    slot.number = &node.transform.position[axisIndex(last)];
  } else if (key.starts_with("scale.")) {
    slot.number = &node.transform.scale[axisIndex(last)];
  } else if (key.starts_with("rotation.")) {
    slot.rotationAxis = axisIndex(last);
  } else if (key.starts_with("primitive.")) {
    ScenePrimitive* primitive = componentOf<ScenePrimitive>(node);
    if (primitive != nullptr && key.starts_with("primitive.extent.")) {
      slot.number = &primitive->extent[axisIndex(last)];
    } else if (primitive != nullptr && key.starts_with("primitive.color.")) {
      slot.byte = colorByte(primitive->color, last);
    }
  } else if (key.starts_with("mesh.tint.")) {
    SceneMeshRenderer* mesh = componentOf<SceneMeshRenderer>(node);
    if (mesh != nullptr) {
      slot.byte = colorByte(mesh->tint, last);
    }
  } else if (key.starts_with("sprite.")) {
    SceneSprite* sprite = componentOf<SceneSprite>(node);
    if (sprite == nullptr) {
      return slot;
    }
    if (key == "sprite.size.w") {
      slot.number = &sprite->width;
    } else if (key == "sprite.size.h") {
      slot.number = &sprite->height;
    } else if (key.starts_with("sprite.tint.")) {
      slot.byte = colorByte(sprite->tint, last);
    } else if (key == "sprite.region.u0") {
      slot.number = &sprite->u0;
    } else if (key == "sprite.region.v0") {
      slot.number = &sprite->v0;
    } else if (key == "sprite.region.u1") {
      slot.number = &sprite->u1;
    } else if (key == "sprite.region.v1") {
      slot.number = &sprite->v1;
    } else if (key == "sprite.cell.column") {
      slot.integer = &sprite->column;
    } else if (key == "sprite.cell.row") {
      slot.integer = &sprite->row;
    }
  } else if (key.starts_with("light.")) {
    SceneLight* light = componentOf<SceneLight>(node);
    if (light != nullptr && key.starts_with("light.color.")) {
      slot.number = &light->color[axisIndex(last)];
    } else if (light != nullptr && key == "light.intensity") {
      slot.number = &light->intensity;
    }
  } else if (key.starts_with("camera.")) {
    SceneCamera* camera = componentOf<SceneCamera>(node);
    if (camera != nullptr && key == "camera.fov") {
      slot.number = &camera->fovDegrees;
    } else if (camera != nullptr && key == "camera.near") {
      slot.number = &camera->nearPlane;
    } else if (camera != nullptr && key == "camera.far") {
      slot.number = &camera->farPlane;
    } else if (camera != nullptr && key == "camera.zoom") {
      slot.number = &camera->zoom;
    }
  }
  return slot;
}

static bool
readNumber(const SceneNode& source, const std::string& key, double* value)
{
  SceneNode node = source;
  const NumberSlot slot = resolveNode(node, key);
  if (slot.rotationAxis >= 0) {
    const Vector3 euler =
      glm::degrees(glm::eulerAngles(node.transform.rotation));
    *value = euler[slot.rotationAxis];
    return true;
  }
  return readSlot(slot, value);
}

static bool
writeNumber(SceneNode& node, const std::string& key, double value)
{
  const NumberSlot slot = resolveNode(node, key);
  if (slot.rotationAxis >= 0) {
    Vector3 euler = glm::eulerAngles(node.transform.rotation);
    euler[slot.rotationAxis] = static_cast<float>(glm::radians(value));
    node.transform.rotation = glm::normalize(Quaternion(euler));
    return true;
  }
  return writeSlot(slot, value);
}

static float*
resolveEnvironment(SceneEnvironment& environment, const std::string& key)
{
  const char last = key.empty() ? '\0' : key.back();
  if (key.starts_with("env.tint.")) {
    return &environment.skyboxTint[axisIndex(last)];
  }
  if (key.starts_with("env.ambient.")) {
    return &environment.ambient[axisIndex(last)];
  }
  if (key.starts_with("env.sun.direction.")) {
    return &environment.sun.direction[axisIndex(last)];
  }
  if (key.starts_with("env.sun.color.")) {
    return &environment.sun.color[axisIndex(last)];
  }
  if (key == "env.sun.intensity") {
    return &environment.sun.intensity;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Behaviour fields: "behaviour:<type>:<field>", plus ":<axis>" for a vector
// or color part. Types and field names never hold a colon.

struct BehaviourKey
{
  std::string type;
  std::string field;
  char axis = '\0';
};

static bool
splitBehaviourKey(const std::string& key, BehaviourKey* parts)
{
  const std::string prefix = "behaviour:";
  if (!key.starts_with(prefix)) {
    return false;
  }
  const size_t typeEnd = key.find(':', prefix.size());
  if (typeEnd == std::string::npos) {
    return false;
  }
  const size_t fieldEnd = key.find(':', typeEnd + 1);
  parts->type = key.substr(prefix.size(), typeEnd - prefix.size());
  parts->field = key.substr(
    typeEnd + 1,
    fieldEnd == std::string::npos ? std::string::npos : fieldEnd - typeEnd - 1);
  parts->axis = fieldEnd == std::string::npos || fieldEnd + 1 >= key.size()
                  ? '\0'
                  : key[fieldEnd + 1];
  return !parts->type.empty() && !parts->field.empty();
}

static const SceneOpaqueComponent*
behaviourOf(const SceneNode& node, const std::string& type)
{
  for (const SceneComponent& component : node.components) {
    const SceneOpaqueComponent* opaque =
      std::get_if<SceneOpaqueComponent>(&component.value);
    if (opaque != nullptr && opaque->type == type) {
      return opaque;
    }
  }
  return nullptr;
}

static SceneOpaqueComponent*
behaviourOf(SceneNode& node, const std::string& type)
{
  for (SceneComponent& component : node.components) {
    SceneOpaqueComponent* opaque =
      std::get_if<SceneOpaqueComponent>(&component.value);
    if (opaque != nullptr && opaque->type == type) {
      return opaque;
    }
  }
  return nullptr;
}

// The number a Number, Integer, vector part or color byte field shows.
static bool
behaviourNumber(const BehaviourValue& value, char axis, double* number)
{
  switch (value.kind) {
    case BehaviourFieldKind::Number:
    case BehaviourFieldKind::Integer:
      *number = value.number;
      return true;
    case BehaviourFieldKind::Vector3:
      if (axis != 'x' && axis != 'y' && axis != 'z') {
        return false;
      }
      *number = value.vector[axisIndex(axis)];
      return true;
    case BehaviourFieldKind::Color: {
      ColorRgba color = value.color;
      *number = *colorByte(color, axis);
      return true;
    }
    default:
      return false;
  }
}

// Writes a number into a field's value, within the field's limits.
static bool
setBehaviourNumber(BehaviourValue& value,
                   const BehaviourField& field,
                   char axis,
                   double number)
{
  if (!std::isfinite(number)) {
    return false;
  }
  switch (value.kind) {
    case BehaviourFieldKind::Integer:
      number = std::round(number);
      [[fallthrough]];
    case BehaviourFieldKind::Number:
      if (field.hasMinimum) {
        number = std::max(number, field.minimum);
      }
      if (field.hasMaximum) {
        number = std::min(number, field.maximum);
      }
      value.number = number;
      return true;
    case BehaviourFieldKind::Vector3:
      if (axis != 'x' && axis != 'y' && axis != 'z') {
        return false;
      }
      value.vector[axisIndex(axis)] =
        static_cast<float>(std::clamp(number, -1.0e9, 1.0e9));
      return true;
    case BehaviourFieldKind::Color:
      *colorByte(value.color, axis) =
        static_cast<unsigned char>(std::lround(std::clamp(number, 0.0, 255.0)));
      return true;
    default:
      return false;
  }
}

// How a field part shows, for mixed-selection comparison.
static std::string
behaviourText(const BehaviourValue& value, char axis)
{
  double number = 0.0;
  if (behaviourNumber(value, axis, &number)) {
    return formatNumber(number);
  }
  if (value.kind == BehaviourFieldKind::Bool) {
    return value.flag ? "On" : "Off";
  }
  return value.text;
}

// ---------------------------------------------------------------------------

EditorInspector::EditorInspector(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(2048u)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
}

void
EditorInspector::setPlacement(const GuiPanelPlacement& placement)
{
  if (placement.surface != m_placement.surface) {
    m_scrubKey.clear();
  }
  m_placement = placement;
  m_x = placement.area.x;
  m_y = placement.area.y;
  m_width = std::max(40.0f, placement.area.w);
  m_height = std::max(0.0f, placement.area.h);
  m_visible = placement.visible && m_height > 1.0f;
  if (!m_visible && m_edit.active()) {
    m_edit.end();
    m_editKey.clear();
  }
}

bool
EditorInspector::containsScreenPoint(float x, float y) const
{
  return m_visible && x >= m_x && x <= m_x + m_width && y >= m_y &&
         y <= m_y + m_height;
}

const InspectorField*
EditorInspector::field(const std::string& key) const
{
  for (const InspectorField& candidate : m_fields) {
    if (candidate.key == key) {
      return &candidate;
    }
  }
  return nullptr;
}

bool
EditorInspector::takeCopyRequest(std::string* text)
{
  if (!m_copyPending) {
    return false;
  }
  m_copyPending = false;
  if (text != nullptr) {
    *text = m_copyText;
  }
  return true;
}

bool
EditorInspector::takePasteRequest()
{
  const bool pending = m_pastePending;
  m_pastePending = false;
  return pending;
}

void
EditorInspector::providePaste(const std::string& text)
{
  if (m_edit.active()) {
    m_edit.insertText(text);
    m_invalid = false;
  }
}

// ---------------------------------------------------------------------------
// Field building.

void
EditorInspector::refreshFields(const EditorDocument& document,
                               const EditorSelection& selection)
{
  // The fields derive only from the document's scene, the selection and the
  // known behaviours. While those are unchanged the built fields (their
  // strings and callbacks) are kept instead of rebuilt every frame.
  const uint64_t behaviours =
    m_behaviours != nullptr ? m_behaviours->revision() : 0u;
  if (m_fieldsBuilt && m_fieldsDocument == &document &&
      m_fieldsGeneration == document.sceneGeneration() &&
      m_fieldsRevision == document.revision() &&
      m_fieldsBehaviours == behaviours &&
      m_fieldsPrimary == selection.primary() &&
      m_fieldsSelection == selection.ids()) {
    return;
  }
  buildFields(document, selection);
  moveRemoveButtonsToHeaders();
  applyFolding();
  m_fieldsBuilt = true;
  m_fieldsDocument = &document;
  m_fieldsGeneration = document.sceneGeneration();
  m_fieldsRevision = document.revision();
  m_fieldsBehaviours = behaviours;
  m_fieldsPrimary = selection.primary();
  m_fieldsSelection = selection.ids();
}

using InspectorSection = std::function<void(const std::string&)>;
using InspectorRowAdder =
  std::function<void(const std::string&, std::vector<InspectorField>)>;
using InspectorToggle =
  std::function<InspectorField(const std::string&, const char*, bool)>;
using InspectorChoice = std::function<InspectorField(const std::string&,
                                                     const char*,
                                                     std::vector<std::string>,
                                                     int)>;
using InspectorButton =
  std::function<InspectorField(const std::string&, const char*)>;

static InspectorField
assetNumber(const SceneAsset& asset,
            const std::string& property,
            const char* label,
            float step)
{
  InspectorField field;
  field.key = "asset." + property + ":" + asset.id;
  field.label = label;
  field.kind = InspectorFieldKind::Number;
  field.step = step;
  SceneAsset copy = asset;
  const NumberSlot slot = resolveAsset(copy, property);
  double value = 0.0;
  if (readSlot(slot, &value)) {
    field.value = formatNumber(value);
  }
  field.integer = slot.integer != nullptr;
  return field;
}

static InspectorField
readOnly(const std::string& key, const char* label, const std::string& value)
{
  InspectorField field;
  field.key = key;
  field.label = label;
  field.kind = InspectorFieldKind::ReadOnly;
  field.value = value;
  return field;
}

// A swatch for a color row's channel fields: their color (the primary
// node's; marked mixed when the selection disagrees), editing the same keys.
static InspectorField
makeSwatch(const std::vector<InspectorField>& channels, bool unit)
{
  InspectorField swatch;
  swatch.kind = InspectorFieldKind::Swatch;
  swatch.key =
    "swatch:" + (channels.empty() ? std::string() : channels.front().key);
  swatch.label = "Color";
  swatch.unitChannels = unit;
  unsigned char bytes[4] = { 255, 255, 255, 255 };
  for (size_t index = 0; index < channels.size() && index < 4; ++index) {
    swatch.channels.push_back(channels[index].key);
    swatch.mixed = swatch.mixed || channels[index].mixed;
    const double value = std::strtod(channels[index].value.c_str(), nullptr);
    bytes[index] = static_cast<unsigned char>(
      std::lround(std::clamp(unit ? value * 255.0 : value, 0.0, 255.0)));
  }
  swatch.swatch = ColorRgba{ bytes[0], bytes[1], bytes[2], bytes[3] };
  return swatch;
}

// What a component's section edits, for its menu (see InspectorRow::group);
// empty for a namespaced component no known behaviour describes.
static std::string
componentGroup(const SceneComponent& component,
               const EditorBehaviours* behaviours)
{
  switch (component.type()) {
    case SceneComponentType::Primitive:
      return "primitive";
    case SceneComponentType::Mesh:
      return "mesh";
    case SceneComponentType::Sprite:
      return "sprite";
    case SceneComponentType::Light:
      return "light";
    case SceneComponentType::Camera:
      return "camera";
    default:
      break;
  }
  const SceneOpaqueComponent* opaque =
    std::get_if<SceneOpaqueComponent>(&component.value);
  if (opaque != nullptr && behaviours != nullptr &&
      behaviours->schema().find(opaque->type) != nullptr) {
    return "behaviour:" + opaque->type;
  }
  return {};
}

// A component of the same kind with default values, keeping what it refers
// to (a mesh's asset, a sprite's texture), a primitive's shape and a
// camera's primary flag. Behaviours take their described defaults.
static SceneComponent
defaultComponentLike(const SceneComponent& current,
                     const EditorBehaviours* behaviours)
{
  SceneComponent result = current;
  if (const ScenePrimitive* primitive =
        std::get_if<ScenePrimitive>(&current.value)) {
    ScenePrimitive reset;
    reset.shape = primitive->shape;
    result.value = reset;
  } else if (const SceneMeshRenderer* mesh =
               std::get_if<SceneMeshRenderer>(&current.value)) {
    SceneMeshRenderer reset;
    reset.asset = mesh->asset;
    result.value = reset;
  } else if (const SceneSprite* sprite =
               std::get_if<SceneSprite>(&current.value)) {
    SceneSprite reset;
    reset.texture = sprite->texture;
    result.value = reset;
  } else if (std::get_if<SceneLight>(&current.value) != nullptr) {
    result.value = SceneLight{};
  } else if (const SceneCamera* camera =
               std::get_if<SceneCamera>(&current.value)) {
    SceneCamera reset;
    reset.primary = camera->primary;
    result.value = reset;
  } else if (const SceneOpaqueComponent* opaque =
               std::get_if<SceneOpaqueComponent>(&current.value)) {
    const BehaviourType* type =
      behaviours != nullptr ? behaviours->schema().find(opaque->type) : nullptr;
    if (type != nullptr) {
      result.value = BehaviourSchema::defaultComponent(*type);
    }
  }
  return result;
}

// The node with default transform and component values (see
// defaultComponentLike), for resetting a row through readNumber.
static SceneNode
defaultsOf(const SceneNode& node, const EditorBehaviours* behaviours)
{
  SceneNode defaults = node;
  defaults.transform = Transform3D{};
  for (SceneComponent& component : defaults.components) {
    component = defaultComponentLike(component, behaviours);
  }
  return defaults;
}

// A known behaviour component: one typed field per described field, mixed
// where other selected nodes holding the behaviour disagree, then Remove.
// Edits reach every selected node that holds the behaviour.
static void
buildBehaviourFields(const BehaviourType& type,
                     const SceneOpaqueComponent& component,
                     const std::vector<const SceneNode*>& nodes,
                     const SceneDocument& scene,
                     const InspectorSection& section,
                     const InspectorRowAdder& row)
{
  section(type.title);
  const BehaviourValues values =
    BehaviourSchema::decode(type, component.data, nullptr);
  std::vector<BehaviourValues> others;
  for (const SceneNode* node : nodes) {
    const SceneOpaqueComponent* other = behaviourOf(*node, type.type);
    if (other != nullptr && other != &component) {
      others.push_back(BehaviourSchema::decode(type, other->data, nullptr));
    }
  }
  for (const BehaviourField& described : type.fields) {
    const BehaviourValue* value = values.find(described.name);
    if (value == nullptr) {
      continue;
    }
    const std::string base = "behaviour:" + type.type + ":" + described.name;
    const std::function<InspectorField(char, const char*)> make =
      [&](char axis, const char* label) {
        InspectorField field;
        field.key = axis == '\0' ? base : base + ":" + std::string(1, axis);
        field.label = label;
        field.value = behaviourText(*value, axis);
        for (const BehaviourValues& other : others) {
          const BehaviourValue* theirs = other.find(described.name);
          field.mixed = field.mixed || theirs == nullptr ||
                        behaviourText(*theirs, axis) != field.value;
        }
        return field;
      };
    std::vector<InspectorField> fields;
    switch (described.kind) {
      case BehaviourFieldKind::Number:
      case BehaviourFieldKind::Integer: {
        InspectorField field = make('\0', "Value");
        field.kind = InspectorFieldKind::Number;
        field.integer = described.kind == BehaviourFieldKind::Integer;
        field.step =
          field.integer ? 0.1f
          : described.hasMinimum && described.hasMaximum
            ? static_cast<float>(std::clamp(
                (described.maximum - described.minimum) / 400.0, 0.001, 10.0))
            : 0.05f;
        fields.push_back(field);
        break;
      }
      case BehaviourFieldKind::Vector3:
        for (const char* axis : { "x", "y", "z" }) {
          InspectorField field = make(axis[0],
                                      axis[0] == 'x'   ? "X"
                                      : axis[0] == 'y' ? "Y"
                                                       : "Z");
          field.kind = InspectorFieldKind::Number;
          fields.push_back(field);
        }
        break;
      case BehaviourFieldKind::Color:
        fields.push_back(InspectorField{});
        for (const char* channel : { "r", "g", "b", "a" }) {
          InspectorField field = make(channel[0],
                                      channel[0] == 'r'   ? "R"
                                      : channel[0] == 'g' ? "G"
                                      : channel[0] == 'b' ? "B"
                                                          : "A");
          field.kind = InspectorFieldKind::Number;
          field.integer = true;
          field.step = 1.0f;
          fields.push_back(field);
        }
        fields.front() = makeSwatch(
          std::vector<InspectorField>(fields.begin() + 1, fields.end()), false);
        break;
      case BehaviourFieldKind::Bool: {
        InspectorField field = make('\0', "");
        field.kind = InspectorFieldKind::Toggle;
        field.toggle = value->flag;
        fields.push_back(field);
        break;
      }
      case BehaviourFieldKind::Text: {
        InspectorField field = make('\0', "Text");
        field.kind = InspectorFieldKind::Text;
        field.maxBytes = 1024;
        fields.push_back(field);
        break;
      }
      case BehaviourFieldKind::Choice:
      case BehaviourFieldKind::Asset:
      case BehaviourFieldKind::Node: {
        // Asset and Node references start with "(none)"; a reference to a
        // missing entry stays visible as its own choice.
        InspectorField field = make('\0', "Value");
        field.kind = InspectorFieldKind::Choice;
        if (described.kind == BehaviourFieldKind::Choice) {
          field.choices = described.options;
        } else {
          field.choices.push_back(kNoAsset);
          if (described.kind == BehaviourFieldKind::Asset) {
            for (const SceneAsset& asset : scene.assets) {
              if (described.assetTypes.empty() ||
                  std::find(described.assetTypes.begin(),
                            described.assetTypes.end(),
                            asset.type) != described.assetTypes.end()) {
                field.choices.push_back(asset.id);
              }
            }
          } else {
            for (const SceneNode& node : scene.nodes) {
              field.choices.push_back(node.id);
            }
          }
        }
        const std::string current =
          value->text.empty() && described.kind != BehaviourFieldKind::Choice
            ? std::string(kNoAsset)
            : value->text;
        std::vector<std::string>::const_iterator found =
          std::find(field.choices.begin(), field.choices.end(), current);
        if (found == field.choices.end()) {
          field.choices.push_back(current);
          found = field.choices.end() - 1;
        }
        field.choice = static_cast<int>(found - field.choices.begin());
        field.value = current;
        fields.push_back(field);
        break;
      }
    }
    row(described.title, fields);
  }
  InspectorField remove;
  remove.key = "behaviour.remove:" + type.type;
  remove.label = "Remove";
  remove.kind = InspectorFieldKind::Button;
  remove.value = "Remove";
  row("", { remove });
}

// The scene view's asset table: one section per entry with its import and
// sampling options, and Remove for entries nothing references.
static void
buildAssetFields(const SceneDocument& scene,
                 const InspectorSection& section,
                 const InspectorRowAdder& row,
                 const InspectorToggle& toggle,
                 const InspectorChoice& choice,
                 const InspectorButton& button)
{
  section("Assets");
  if (scene.assets.empty()) {
    row("", { readOnly("assets.none", "Assets", "Drop a mesh or texture") });
    return;
  }
  std::unordered_set<std::string> used;
  used.insert(scene.environment.skybox);
  for (const SceneNode& node : scene.nodes) {
    for (const SceneComponent& component : node.components) {
      if (const SceneMeshRenderer* mesh =
            std::get_if<SceneMeshRenderer>(&component.value)) {
        used.insert(mesh->asset);
      } else if (const SceneSprite* sprite =
                   std::get_if<SceneSprite>(&component.value)) {
        used.insert(sprite->texture);
      }
    }
  }
  for (const SceneAsset& asset : scene.assets) {
    const std::string suffix = ":" + asset.id;
    section(asset.id);
    const bool faces = asset.type == SceneAssetType::CubemapFaces;
    row("File",
        { readOnly("asset.path" + suffix,
                   "File",
                   faces ? asset.faces[0] + " +5" : asset.path) });
    if (asset.type == SceneAssetType::Mesh) {
      row("Type", { readOnly("asset.type" + suffix, "Type", "mesh") });
      InspectorField radius = assetNumber(asset, "radius", "Radius", 0.01f);
      // Every change rebuilds the scene's attachments, so radius is typed.
      radius.scrub = false;
      row("Normalize",
          { toggle(
              "asset.center" + suffix, "Center", asset.mesh.centerAndNormalize),
            radius });
      row("Import",
          { toggle("asset.flipv" + suffix, "Flip V", asset.mesh.flipV),
            toggle("asset.normals" + suffix,
                   "Normals",
                   asset.mesh.generateNormals) });
    } else {
      if (faces) {
        row("Type",
            { readOnly("asset.type" + suffix, "Type", "cubemap faces") });
      } else {
        const int current = asset.type == SceneAssetType::Atlas          ? 1
                            : asset.type == SceneAssetType::CubemapCross ? 2
                                                                         : 0;
        row("Type",
            { choice("asset.type" + suffix,
                     "Type",
                     { "texture", "atlas", "cubemap cross" },
                     current) });
      }
      if (asset.type == SceneAssetType::Atlas) {
        row("Grid",
            { assetNumber(asset, "columns", "Cols", 0.1f),
              assetNumber(asset, "rows", "Rows", 0.1f) });
      }
      row("Sampling",
          { choice("asset.filter" + suffix,
                   "Filter",
                   { "nearest", "linear" },
                   asset.texture.filter == SceneTextureFilter::Linear ? 1 : 0),
            choice("asset.wrap" + suffix,
                   "Wrap",
                   { "clamp", "repeat" },
                   asset.texture.wrap == SceneTextureWrap::Repeat ? 1 : 0) });
      row(
        "Mipmaps",
        { toggle("asset.mipmaps" + suffix, "Mipmaps", asset.texture.mipmaps) });
    }
    if (used.find(asset.id) == used.end()) {
      row("", { button("asset.remove" + suffix, "Remove unused") });
    }
  }
}

void
EditorInspector::buildFields(const EditorDocument& document,
                             const EditorSelection& selection)
{
  m_fields.clear();
  m_rows.clear();
  m_buildGroup.clear();
  const std::function<void(const std::string&)> section =
    [this](const std::string& title) {
      InspectorRow row;
      row.label = title;
      row.section = true;
      row.group = m_buildGroup;
      m_rows.push_back(row);
    };
  const std::function<void(const std::string&, std::vector<InspectorField>)>
    row = [this](const std::string& label, std::vector<InspectorField> fields) {
      InspectorRow entry;
      entry.label = label;
      entry.group = m_buildGroup;
      for (InspectorField& value : fields) {
        entry.fields.push_back(m_fields.size());
        m_fields.push_back(std::move(value));
      }
      m_rows.push_back(entry);
    };
  // A color row: its channel fields behind a swatch that opens the picker.
  const std::function<void(
    const std::string&, bool, std::vector<InspectorField>)>
    colorRow = [&row](const std::string& label,
                      bool unit,
                      std::vector<InspectorField> channels) {
      channels.insert(channels.begin(), makeSwatch(channels, unit));
      row(label, std::move(channels));
    };

  const SceneNode* primary = document.findNode(selection.primary());
  std::vector<const SceneNode*> nodes;
  for (const std::string& id : selection.ids()) {
    const SceneNode* node = document.findNode(id);
    if (node != nullptr) {
      nodes.push_back(node);
    }
  }
  const std::function<InspectorField(const std::string&, const char*, float)>
    number =
      [&nodes, primary](const std::string& key, const char* label, float step) {
        InspectorField field;
        field.key = key;
        field.label = label;
        field.kind = InspectorFieldKind::Number;
        field.step = step;
        double value = 0.0;
        if (primary != nullptr && readNumber(*primary, key, &value)) {
          field.value = formatNumber(value);
          for (const SceneNode* other : nodes) {
            double otherValue = 0.0;
            if (!readNumber(*other, key, &otherValue) ||
                formatNumber(otherValue) != field.value) {
              field.mixed = true;
            }
          }
        }
        return field;
      };
  const std::function<InspectorField(const std::string&, const char*, bool)>
    toggle = [](const std::string& key, const char* label, bool value) {
      InspectorField field;
      field.key = key;
      field.label = label;
      field.kind = InspectorFieldKind::Toggle;
      field.toggle = value;
      field.value = value ? "On" : "Off";
      return field;
    };
  const std::function<InspectorField(
    const std::string&, const char*, std::vector<std::string>, int)>
    choice = [](const std::string& key,
                const char* label,
                std::vector<std::string> choices,
                int selected) {
      InspectorField field;
      field.key = key;
      field.label = label;
      field.kind = InspectorFieldKind::Choice;
      field.choices = std::move(choices);
      field.choice = selected;
      field.value = field.choices.empty()
                      ? std::string()
                      : field.choices[static_cast<size_t>(selected)];
      return field;
    };
  const std::function<InspectorField(const std::string&, const char*)> button =
    [](const std::string& key, const char* label) {
      InspectorField field;
      field.key = key;
      field.label = label;
      field.kind = InspectorFieldKind::Button;
      field.value = label;
      return field;
    };
  const std::function<InspectorField(
    const std::string&, const char*, const std::string&)>
    text =
      [](const std::string& key, const char* label, const std::string& value) {
        InspectorField field;
        field.key = key;
        field.label = label;
        field.kind = InspectorFieldKind::Text;
        field.value = value;
        return field;
      };

  const SceneDocument& scene = document.scene().document();
  // A choice among the asset table entries of some types. With offerNone the
  // first choice is "(none)" for an empty reference; a reference to a missing
  // entry stays visible as its own choice.
  const std::function<InspectorField(const std::string&,
                                     const char*,
                                     const std::string&,
                                     const std::vector<SceneAssetType>&,
                                     bool)>
    assetChoice = [&scene, &choice](const std::string& key,
                                    const char* label,
                                    const std::string& current,
                                    const std::vector<SceneAssetType>& types,
                                    bool offerNone) {
      std::vector<std::string> ids;
      int selected = -1;
      if (offerNone) {
        ids.push_back(kNoAsset);
        if (current.empty()) {
          selected = 0;
        }
      }
      for (const SceneAsset& asset : scene.assets) {
        if (std::find(types.begin(), types.end(), asset.type) == types.end()) {
          continue;
        }
        if (asset.id == current) {
          selected = static_cast<int>(ids.size());
        }
        ids.push_back(asset.id);
      }
      if (selected < 0) {
        selected = static_cast<int>(ids.size());
        ids.push_back(current);
      }
      return choice(key, label, std::move(ids), selected);
    };

  if (primary == nullptr) {
    const SceneEnvironment& environment = scene.environment;
    section("Scene");
    row("Mode",
        { choice("scene.mode",
                 "Mode",
                 { "2D", "3D" },
                 scene.worldMode == SceneWorldMode::World3D ? 1 : 0) });
    row("Title", { text("meta.title", "Title", scene.metadata.title) });
    row("Author", { text("meta.author", "Author", scene.metadata.author) });
    InspectorField description =
      text("meta.description", "Description", scene.metadata.description);
    description.maxBytes = kMaximumDescriptionBytes;
    row("Description", { description });
    // The game Play launches: automatic (the game whose behaviours the scene
    // uses), or one of the installed games; a game no longer installed stays
    // visible as its own choice.
    const std::string game = document.playApplication();
    if (m_behaviours != nullptr &&
        (!m_behaviours->games().empty() || !game.empty())) {
      std::vector<std::string> games{ kAutomaticGame };
      int selected = 0;
      for (const std::string& candidate : m_behaviours->games()) {
        if (candidate == game) {
          selected = static_cast<int>(games.size());
        }
        games.push_back(candidate);
      }
      if (!game.empty() && selected == 0) {
        selected = static_cast<int>(games.size());
        games.push_back(game);
      }
      row("Play with", { choice("scene.play", "Game", games, selected) });
    }
    section("Environment");
    row("Skybox",
        { assetChoice(
          "env.skybox",
          "Skybox",
          environment.skybox,
          { SceneAssetType::CubemapCross, SceneAssetType::CubemapFaces },
          true) });
    const std::function<InspectorField(const std::string&, const char*, float)>
      environmentNumber =
        [&environment](const std::string& key, const char* label, float step) {
          InspectorField field;
          field.key = key;
          field.label = label;
          field.kind = InspectorFieldKind::Number;
          field.step = step;
          SceneEnvironment copy = environment;
          const float* slot = resolveEnvironment(copy, key);
          field.value = slot == nullptr ? std::string() : formatNumber(*slot);
          return field;
        };
    if (!environment.skybox.empty()) {
      colorRow("Sky tint",
               true,
               { environmentNumber("env.tint.r", "R", 0.01f),
                 environmentNumber("env.tint.g", "G", 0.01f),
                 environmentNumber("env.tint.b", "B", 0.01f) });
    }
    colorRow("Ambient",
             true,
             { environmentNumber("env.ambient.r", "R", 0.01f),
               environmentNumber("env.ambient.g", "G", 0.01f),
               environmentNumber("env.ambient.b", "B", 0.01f) });
    row("Sun", { toggle("env.sun", "Sun", environment.hasSun) });
    if (environment.hasSun) {
      row("Direction",
          { environmentNumber("env.sun.direction.x", "X", 0.01f),
            environmentNumber("env.sun.direction.y", "Y", 0.01f),
            environmentNumber("env.sun.direction.z", "Z", 0.01f) });
      colorRow("Color",
               true,
               { environmentNumber("env.sun.color.r", "R", 0.01f),
                 environmentNumber("env.sun.color.g", "G", 0.01f),
                 environmentNumber("env.sun.color.b", "B", 0.01f) });
      row("Intensity",
          { environmentNumber("env.sun.intensity", "I", 0.01f),
            toggle("env.sun.shadows", "Shadows", environment.sun.shadows) });
    }
    buildAssetFields(scene, section, row, toggle, choice, button);
    return;
  }

  const bool single = nodes.size() == 1;
  section(single ? "Node" : std::to_string(nodes.size()) + " nodes");
  InspectorField name = text("name", "Name", primary->name);
  if (!single) {
    name.kind = InspectorFieldKind::ReadOnly;
    name.value = primary->name + " +" + std::to_string(nodes.size() - 1);
  }
  row("Name", { name });
  row("Flags",
      { toggle("enabled", "Enabled", primary->enabled),
        toggle("visible", "Visible", primary->visible) });
  InspectorField tags = text("tags", "Tags", joinTags(primary->tags));
  tags.maxBytes = kMaximumTagsBytes;
  for (const SceneNode* other : nodes) {
    tags.mixed = tags.mixed || other->tags != primary->tags;
  }
  row("Tags", { tags });
  m_buildGroup = "transform";
  section("Transform");
  row("Position",
      { number("position.x", "X", 0.05f),
        number("position.y", "Y", 0.05f),
        number("position.z", "Z", 0.05f) });
  row("Rotation",
      { number("rotation.x", "X", 0.5f),
        number("rotation.y", "Y", 0.5f),
        number("rotation.z", "Z", 0.5f) });
  row("Scale",
      { number("scale.x", "X", 0.01f),
        number("scale.y", "Y", 0.01f),
        number("scale.z", "Z", 0.01f) });

  for (const SceneComponent& component : primary->components) {
    m_buildGroup = componentGroup(component, m_behaviours);
    if (const ScenePrimitive* primitive =
          std::get_if<ScenePrimitive>(&component.value)) {
      section("Primitive");
      std::vector<std::string> shapes;
      const ScenePrimitiveShape all[] = {
        ScenePrimitiveShape::Rect,     ScenePrimitiveShape::Ellipse,
        ScenePrimitiveShape::Triangle, ScenePrimitiveShape::Cube,
        ScenePrimitiveShape::Pyramid,  ScenePrimitiveShape::Sphere,
        ScenePrimitiveShape::WireCube, ScenePrimitiveShape::WireSphere
      };
      int current = 0;
      for (int index = 0; index < 8; ++index) {
        shapes.push_back(scenePrimitiveShapeName(all[index]));
        if (all[index] == primitive->shape) {
          current = index;
        }
      }
      row("Shape", { choice("primitive.shape", "Shape", shapes, current) });
      row("Extent",
          { number("primitive.extent.x", "X", 0.01f),
            number("primitive.extent.y", "Y", 0.01f),
            number("primitive.extent.z", "Z", 0.01f) });
      colorRow("Color",
               false,
               { number("primitive.color.r", "R", 1.0f),
                 number("primitive.color.g", "G", 1.0f),
                 number("primitive.color.b", "B", 1.0f),
                 number("primitive.color.a", "A", 1.0f) });
      row("", { button("component.remove.primitive", "Remove") });
    } else if (const SceneMeshRenderer* mesh =
                 std::get_if<SceneMeshRenderer>(&component.value)) {
      section("Mesh");
      row("Asset",
          { assetChoice("mesh.asset",
                        "Asset",
                        mesh->asset,
                        { SceneAssetType::Mesh },
                        false) });
      colorRow("Tint",
               false,
               { number("mesh.tint.r", "R", 1.0f),
                 number("mesh.tint.g", "G", 1.0f),
                 number("mesh.tint.b", "B", 1.0f),
                 number("mesh.tint.a", "A", 1.0f) });
      row("Shadows",
          { toggle("mesh.cast_shadows", "Cast", mesh->castShadows),
            button("component.remove.mesh", "Remove") });
    } else if (const SceneSprite* sprite =
                 std::get_if<SceneSprite>(&component.value)) {
      section("Sprite");
      row("Texture",
          { assetChoice("sprite.texture",
                        "Texture",
                        sprite->texture,
                        { SceneAssetType::Texture, SceneAssetType::Atlas },
                        false) });
      row("Source",
          { choice("sprite.source",
                   "Source",
                   { "region", "atlas cell" },
                   sprite->hasCell ? 1 : 0) });
      if (sprite->hasCell) {
        InspectorField column = number("sprite.cell.column", "Col", 0.1f);
        InspectorField cellRow = number("sprite.cell.row", "Row", 0.1f);
        column.integer = true;
        cellRow.integer = true;
        row("Cell", { column, cellRow });
      } else {
        row("Region min",
            { number("sprite.region.u0", "U", 0.005f),
              number("sprite.region.v0", "V", 0.005f) });
        row("Region max",
            { number("sprite.region.u1", "U", 0.005f),
              number("sprite.region.v1", "V", 0.005f) });
      }
      row("Size",
          { number("sprite.size.w", "W", 0.01f),
            number("sprite.size.h", "H", 0.01f) });
      row("Facing",
          { choice("sprite.facing",
                   "Facing",
                   { "world", "billboard" },
                   sprite->facing == SceneSpriteFacing::Billboard ? 1 : 0) });
      colorRow("Tint",
               false,
               { number("sprite.tint.r", "R", 1.0f),
                 number("sprite.tint.g", "G", 1.0f),
                 number("sprite.tint.b", "B", 1.0f),
                 number("sprite.tint.a", "A", 1.0f) });
      row("Flip",
          { toggle("sprite.flip.x", "X", sprite->flipX),
            toggle("sprite.flip.y", "Y", sprite->flipY),
            button("component.remove.sprite", "Remove") });
    } else if (const SceneLight* light =
                 std::get_if<SceneLight>(&component.value)) {
      section("Light");
      colorRow("Color",
               true,
               { number("light.color.r", "R", 0.01f),
                 number("light.color.g", "G", 0.01f),
                 number("light.color.b", "B", 0.01f) });
      row("Intensity",
          { number("light.intensity", "I", 0.01f),
            toggle("light.shadows", "Shadows", light->shadows) });
      row("", { button("component.remove.light", "Remove") });
    } else if (const SceneCamera* camera =
                 std::get_if<SceneCamera>(&component.value)) {
      section("Camera");
      row("Projection",
          { choice("camera.projection",
                   "Projection",
                   { "perspective", "orthographic" },
                   camera->projection == SceneProjection::Orthographic ? 1
                                                                       : 0) });
      row("Field of view", { number("camera.fov", "Deg", 0.2f) });
      row("Clip",
          { number("camera.near", "Near", 0.01f),
            number("camera.far", "Far", 1.0f) });
      row("Zoom",
          { number("camera.zoom", "Zoom", 0.2f),
            toggle("camera.primary", "Primary", camera->primary) });
      row("", { button("component.remove.camera", "Remove") });
    } else if (const SceneOpaqueComponent* opaque =
                 std::get_if<SceneOpaqueComponent>(&component.value)) {
      const BehaviourType* type = m_behaviours != nullptr
                                    ? m_behaviours->schema().find(opaque->type)
                                    : nullptr;
      if (type == nullptr) {
        section(opaque->type);
        InspectorField data;
        data.key = "opaque." + opaque->type;
        data.label = "Data";
        data.kind = InspectorFieldKind::ReadOnly;
        data.value = opaque->data;
        row("Data", { data });
        continue;
      }
      buildBehaviourFields(*type, *opaque, nodes, scene, section, row);
    }
  }
  m_buildGroup.clear();
  section("Add component");
  std::vector<InspectorField> adds;
  if (primary->find(SceneComponentType::Primitive) == nullptr) {
    adds.push_back(button("component.add.primitive", "Shape"));
  }
  // Mesh and sprite need an asset to show; they start on the first one.
  bool haveMesh = false;
  bool haveImage = false;
  for (const SceneAsset& asset : scene.assets) {
    haveMesh = haveMesh || asset.type == SceneAssetType::Mesh;
    haveImage = haveImage || asset.type == SceneAssetType::Texture ||
                asset.type == SceneAssetType::Atlas;
  }
  if (haveMesh && primary->find(SceneComponentType::Mesh) == nullptr) {
    adds.push_back(button("component.add.mesh", "Mesh"));
  }
  if (haveImage && primary->find(SceneComponentType::Sprite) == nullptr) {
    adds.push_back(button("component.add.sprite", "Sprite"));
  }
  if (primary->find(SceneComponentType::Light) == nullptr) {
    adds.push_back(button("component.add.light", "Light"));
  }
  if (primary->find(SceneComponentType::Camera) == nullptr) {
    adds.push_back(button("component.add.camera", "Camera"));
  }
  // At most three buttons a row keeps their labels readable.
  for (size_t start = 0; start < adds.size(); start += 3) {
    const size_t end = std::min(adds.size(), start + 3);
    row("",
        std::vector<InspectorField>(
          adds.begin() + static_cast<std::ptrdiff_t>(start),
          adds.begin() + static_cast<std::ptrdiff_t>(end)));
  }
  // Every known behaviour the node does not hold yet, with its defaults.
  std::vector<InspectorField> behaviours;
  if (m_behaviours != nullptr) {
    for (const BehaviourType& type : m_behaviours->schema().types()) {
      if (behaviourOf(*primary, type.type) == nullptr) {
        behaviours.push_back(
          button("behaviour.add:" + type.type, type.title.c_str()));
      }
    }
  }
  if (!behaviours.empty()) {
    section("Add behaviour");
  }
  for (size_t start = 0; start < behaviours.size(); start += 3) {
    const size_t end = std::min(behaviours.size(), start + 3);
    row("",
        std::vector<InspectorField>(
          behaviours.begin() + static_cast<std::ptrdiff_t>(start),
          behaviours.begin() + static_cast<std::ptrdiff_t>(end)));
  }
}

// ---------------------------------------------------------------------------
// Applying edits.

bool
EditorInspector::applyBehaviour(
  const std::string& key,
  const std::function<bool(BehaviourValue&, const BehaviourField&, char)>&
    change,
  const std::string& mergeKey,
  EditorDocument& document,
  const EditorSelection& selection)
{
  BehaviourKey parts;
  if (m_behaviours == nullptr || !splitBehaviourKey(key, &parts)) {
    return false;
  }
  const BehaviourType* type = m_behaviours->schema().find(parts.type);
  const BehaviourField* field =
    type != nullptr ? type->field(parts.field) : nullptr;
  if (field == nullptr) {
    return false;
  }
  return document.editNodes(
    selection.ids(),
    "Set " + type->title + " " + field->title,
    mergeKey,
    [type, field, &parts, &change](SceneNode& node) {
      SceneOpaqueComponent* component = behaviourOf(node, type->type);
      if (component == nullptr) {
        return false;
      }
      BehaviourValues values =
        BehaviourSchema::decode(*type, component->data, nullptr);
      const BehaviourValue* current = values.find(field->name);
      if (current == nullptr) {
        return false;
      }
      BehaviourValue next = *current;
      if (!change(next, *field, parts.axis) ||
          behaviourValuesEqual(next, *current)) {
        return false;
      }
      values.set(field->name, next);
      const std::string data =
        BehaviourSchema::encode(*type, values, component->data);
      if (data == component->data) {
        return false;
      }
      component->data = data;
      return true;
    });
}

bool
EditorInspector::applyNumber(const std::string& key,
                             double value,
                             bool relative,
                             const std::string& mergeKey,
                             EditorDocument& document,
                             const EditorSelection& selection)
{
  return applyNumberWith(
    key,
    [value, relative](double current) {
      return relative ? current + value : value;
    },
    mergeKey,
    document,
    selection);
}

bool
EditorInspector::applyNumberWith(
  const std::string& key,
  const std::function<double(double current)>& change,
  const std::string& mergeKey,
  EditorDocument& document,
  const EditorSelection& selection)
{
  if (key.starts_with("behaviour:")) {
    return applyBehaviour(
      key,
      [&change](
        BehaviourValue& target, const BehaviourField& field, char axis) {
        double current = 0.0;
        return behaviourNumber(target, axis, &current) &&
               setBehaviourNumber(target, field, axis, change(current));
      },
      mergeKey,
      document,
      selection);
  }
  if (key.starts_with("env.")) {
    SceneEnvironment environment = document.scene().document().environment;
    float* slot = resolveEnvironment(environment, key);
    if (slot == nullptr) {
      return false;
    }
    *slot = static_cast<float>(change(*slot));
    return document.setEnvironment(environment, mergeKey);
  }
  std::string property;
  std::string assetId;
  if (splitAssetKey(key, &property, &assetId)) {
    return editAsset(document,
                     assetId,
                     "Edit asset " + assetId,
                     mergeKey,
                     [&property, &change](SceneAsset& asset) {
                       const NumberSlot slot = resolveAsset(asset, property);
                       double current = 0.0;
                       if (!readSlot(slot, &current)) {
                         return false;
                       }
                       return writeSlot(slot, change(current));
                     });
  }
  const std::string label = "Set " + key;
  return document.editNodes(
    selection.ids(), label, mergeKey, [&key, &change](SceneNode& node) {
      double current = 0.0;
      if (!readNumber(node, key, &current)) {
        return false;
      }
      return writeNumber(node, key, change(current));
    });
}
bool
EditorInspector::applyText(const std::string& key,
                           const std::string& text,
                           EditorDocument& document,
                           const EditorSelection& selection)
{
  if (key == "picker.hex") {
    // "#RRGGBB" or "#RRGGBBAA", the '#' optional.
    std::string digits = text;
    digits.erase(std::remove(digits.begin(), digits.end(), ' '), digits.end());
    if (!digits.empty() && digits.front() == '#') {
      digits.erase(digits.begin());
    }
    if ((digits.size() != 6 && digits.size() != 8) ||
        digits.find_first_not_of("0123456789abcdefABCDEF") !=
          std::string::npos) {
      return false;
    }
    float channels[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    for (size_t index = 0; index * 2 < digits.size(); ++index) {
      channels[index] = static_cast<float>(std::strtol(
                          digits.substr(index * 2, 2).c_str(), nullptr, 16)) /
                        255.0f;
    }
    rgbToHsv(channels[0],
             channels[1],
             channels[2],
             &m_pickerHue,
             &m_pickerSaturation,
             &m_pickerValue);
    if (digits.size() == 8) {
      m_pickerAlpha = channels[3];
    }
    applyPicker(document, selection);
    return true;
  }
  if (key == "name") {
    return document.setName(selection.primary(), text);
  }
  if (key == "meta.title" || key == "meta.author" ||
      key == "meta.description") {
    SceneMetadata metadata = document.scene().document().metadata;
    std::string& slot = key == "meta.title"    ? metadata.title
                        : key == "meta.author" ? metadata.author
                                               : metadata.description;
    slot = text;
    return document.setMetadata(metadata);
  }
  if (key == "tags") {
    const std::vector<std::string> tags = parseTags(text);
    bool unchanged = true;
    for (const std::string& id : selection.ids()) {
      const SceneNode* node = document.findNode(id);
      unchanged = unchanged && (node == nullptr || node->tags == tags);
    }
    // Retyping the same tags (spacing aside) is accepted without an edit.
    if (unchanged) {
      return true;
    }
    return document.editNodes(
      selection.ids(), "Set tags", {}, [&tags](SceneNode& node) {
        if (node.tags == tags) {
          return false;
        }
        node.tags = tags;
        return true;
      });
  }
  const InspectorField* shown = field(key);
  if (key.starts_with("behaviour:") && shown != nullptr &&
      shown->kind == InspectorFieldKind::Text) {
    return applyBehaviour(
      key,
      [&text](BehaviourValue& target, const BehaviourField&, char) {
        if (target.kind != BehaviourFieldKind::Text) {
          return false;
        }
        target.text = text;
        return true;
      },
      {},
      document,
      selection);
  }
  // Arithmetic ("2*3"), or a relative edit of each target ("+=1", "*=2").
  std::string expression = text;
  char relative = '\0';
  const size_t first = expression.find_first_not_of(' ');
  if (first != std::string::npos && first + 1 < expression.size() &&
      expression[first + 1] == '=' &&
      std::string("+-*/").find(expression[first]) != std::string::npos) {
    relative = expression[first];
    expression = expression.substr(first + 2);
  }
  double value = 0.0;
  if (!evaluateExpression(expression, &value) ||
      (relative == '/' && value == 0.0)) {
    return false;
  }
  return applyNumberWith(
    key,
    [relative, value](double current) {
      switch (relative) {
        case '+':
          return current + value;
        case '-':
          return current - value;
        case '*':
          return current * value;
        case '/':
          return current / value;
        default:
          return value;
      }
    },
    {},
    document,
    selection);
}

static bool
removeComponent(SceneNode& node, SceneComponentType type)
{
  for (size_t index = 0; index < node.components.size(); ++index) {
    if (node.components[index].type() == type) {
      node.components.erase(node.components.begin() +
                            static_cast<std::ptrdiff_t>(index));
      return true;
    }
  }
  return false;
}

bool
EditorInspector::activate(const InspectorField& target,
                          bool reverse,
                          EditorDocument& document,
                          const EditorSelection& selection)
{
  const std::string& key = target.key;
  if (target.kind == InspectorFieldKind::Swatch) {
    if (m_pickerOpen && m_pickerSwatch == key) {
      m_pickerOpen = false;
    } else {
      openPicker(target);
    }
    return false;
  }
  std::string property;
  std::string assetId;
  const bool assetField = splitAssetKey(key, &property, &assetId);
  if (target.kind == InspectorFieldKind::Text ||
      target.kind == InspectorFieldKind::Number) {
    return beginEdit(target);
  }
  if (target.kind == InspectorFieldKind::Toggle) {
    // A mixed toggle turns every node on first.
    const bool next = target.mixed || !target.toggle;
    if (key.starts_with("behaviour:")) {
      return applyBehaviour(
        key,
        [next](BehaviourValue& value, const BehaviourField&, char) {
          if (value.kind != BehaviourFieldKind::Bool) {
            return false;
          }
          value.flag = next;
          return true;
        },
        {},
        document,
        selection);
    }
    if (assetField) {
      return editAsset(document,
                       assetId,
                       "Edit asset " + assetId,
                       {},
                       [&property, next](SceneAsset& asset) {
                         bool* flag =
                           property == "mipmaps" ? &asset.texture.mipmaps
                           : property == "center"
                             ? &asset.mesh.centerAndNormalize
                           : property == "flipv"   ? &asset.mesh.flipV
                           : property == "normals" ? &asset.mesh.generateNormals
                                                   : nullptr;
                         if (flag == nullptr) {
                           return false;
                         }
                         *flag = next;
                         return true;
                       });
    }
    if (key == "env.sun" || key == "env.sun.shadows") {
      SceneEnvironment environment = document.scene().document().environment;
      (key == "env.sun" ? environment.hasSun : environment.sun.shadows) = next;
      return document.setEnvironment(environment, {});
    }
    if (key == "enabled" || key == "visible") {
      bool changed = false;
      for (const std::string& id : selection.ids()) {
        changed = (key == "enabled" ? document.setEnabled(id, next)
                                    : document.setVisible(id, next)) ||
                  changed;
      }
      return changed;
    }
    return document.editNodes(
      selection.ids(), "Toggle " + key, {}, [&key, next](SceneNode& node) {
        if (key == "mesh.cast_shadows") {
          SceneMeshRenderer* mesh = componentOf<SceneMeshRenderer>(node);
          return mesh != nullptr && ((mesh->castShadows = next), true);
        }
        if (key == "sprite.flip.x" || key == "sprite.flip.y") {
          SceneSprite* sprite = componentOf<SceneSprite>(node);
          if (sprite == nullptr) {
            return false;
          }
          (key == "sprite.flip.x" ? sprite->flipX : sprite->flipY) = next;
          return true;
        }
        if (key == "light.shadows") {
          SceneLight* light = componentOf<SceneLight>(node);
          return light != nullptr && ((light->shadows = next), true);
        }
        if (key == "camera.primary") {
          SceneCamera* camera = componentOf<SceneCamera>(node);
          return camera != nullptr && ((camera->primary = next), true);
        }
        return false;
      });
  }
  if (target.kind == InspectorFieldKind::Choice) {
    const int count = static_cast<int>(target.choices.size());
    if (count == 0) {
      return false;
    }
    return applyChoice(target,
                       (target.choice + (reverse ? count - 1 : 1)) % count,
                       document,
                       selection);
  }
  if (target.kind == InspectorFieldKind::Button) {
    if (key.starts_with("behaviour.remove:")) {
      const std::string type = key.substr(17);
      return document.editNodes(
        selection.ids(), "Remove " + type, {}, [&type](SceneNode& node) {
          return std::erase_if(
                   node.components, [&type](const SceneComponent& component) {
                     const SceneOpaqueComponent* opaque =
                       std::get_if<SceneOpaqueComponent>(&component.value);
                     return opaque != nullptr && opaque->type == type;
                   }) != 0;
        });
    }
    if (key.starts_with("behaviour.add:")) {
      const BehaviourType* type =
        m_behaviours != nullptr ? m_behaviours->schema().find(key.substr(14))
                                : nullptr;
      if (type == nullptr) {
        return false;
      }
      return document.editNodes(
        selection.ids(), "Add " + type->title, {}, [type](SceneNode& node) {
          if (behaviourOf(node, type->type) != nullptr) {
            return false;
          }
          SceneComponent component;
          component.value = BehaviourSchema::defaultComponent(*type);
          node.components.push_back(component);
          return true;
        });
    }
    if (assetField && property == "remove") {
      std::vector<SceneAsset> assets = document.scene().document().assets;
      std::erase_if(assets, [&assetId](const SceneAsset& asset) {
        return asset.id == assetId;
      });
      return document.setAssets(assets, "Remove asset " + assetId);
    }
    if (key.starts_with("component.remove.")) {
      const std::string kind = key.substr(17);
      const SceneComponentType type =
        kind == "primitive" ? SceneComponentType::Primitive
        : kind == "mesh"    ? SceneComponentType::Mesh
        : kind == "sprite"  ? SceneComponentType::Sprite
        : kind == "light"   ? SceneComponentType::Light
                            : SceneComponentType::Camera;
      return document.editNodes(
        selection.ids(), "Remove " + kind, {}, [type](SceneNode& node) {
          return removeComponent(node, type);
        });
    }
    if (key.starts_with("component.add.")) {
      const std::string kind = key.substr(14);
      // Mesh and sprite components start on the first compatible asset.
      std::string meshAsset;
      std::string imageAsset;
      for (const SceneAsset& asset : document.scene().document().assets) {
        if (meshAsset.empty() && asset.type == SceneAssetType::Mesh) {
          meshAsset = asset.id;
        }
        if (imageAsset.empty() && (asset.type == SceneAssetType::Texture ||
                                   asset.type == SceneAssetType::Atlas)) {
          imageAsset = asset.id;
        }
      }
      return document.editNodes(
        selection.ids(),
        "Add " + kind,
        {},
        [&kind, &meshAsset, &imageAsset](SceneNode& node) {
          SceneComponent component;
          SceneComponentType type = SceneComponentType::Camera;
          if (kind == "primitive") {
            type = SceneComponentType::Primitive;
            component.value = ScenePrimitive{};
          } else if (kind == "mesh" && !meshAsset.empty()) {
            type = SceneComponentType::Mesh;
            SceneMeshRenderer mesh;
            mesh.asset = meshAsset;
            component.value = mesh;
          } else if (kind == "sprite" && !imageAsset.empty()) {
            type = SceneComponentType::Sprite;
            SceneSprite sprite;
            sprite.texture = imageAsset;
            component.value = sprite;
          } else if (kind == "light") {
            type = SceneComponentType::Light;
            component.value = SceneLight{};
          } else if (kind == "camera") {
            component.value = SceneCamera{};
          } else {
            return false;
          }
          if (node.find(type) != nullptr) {
            return false;
          }
          node.components.push_back(component);
          return true;
        });
    }
  }
  return false;
}

bool
EditorInspector::applyChoice(const InspectorField& target,
                             int next,
                             EditorDocument& document,
                             const EditorSelection& selection)
{
  const std::string& key = target.key;
  if (next < 0 || next >= static_cast<int>(target.choices.size())) {
    return false;
  }
  std::string property;
  std::string assetId;
  const bool assetField = splitAssetKey(key, &property, &assetId);
  if (key == "scene.mode") {
    return document.setWorldMode(next == 1 ? SceneWorldMode::World3D
                                           : SceneWorldMode::World2D);
  }
  if (key == "scene.play") {
    return document.setPlayApplication(
      next == 0 ? std::string() : target.choices[static_cast<size_t>(next)]);
  }
  const std::string picked = target.choices[static_cast<size_t>(next)];
  if (key.starts_with("behaviour:")) {
    return applyBehaviour(
      key,
      [&picked](BehaviourValue& value, const BehaviourField& field, char) {
        if (field.kind == BehaviourFieldKind::Choice) {
          value.text = picked;
          return true;
        }
        if (field.kind != BehaviourFieldKind::Asset &&
            field.kind != BehaviourFieldKind::Node) {
          return false;
        }
        // The first choice is "(none)".
        value.text = picked == kNoAsset ? std::string() : picked;
        return true;
      },
      {},
      document,
      selection);
  }
  if (key == "env.skybox") {
    // The first choice is "(none)".
    SceneEnvironment environment = document.scene().document().environment;
    environment.skybox = next == 0 ? std::string() : picked;
    return document.setEnvironment(environment, {});
  }
  if (assetField) {
    return editAsset(
      document,
      assetId,
      "Edit asset " + assetId,
      {},
      [&property, next](SceneAsset& asset) {
        if (property == "type") {
          const SceneAssetType types[] = { SceneAssetType::Texture,
                                           SceneAssetType::Atlas,
                                           SceneAssetType::CubemapCross };
          asset.type = types[std::clamp(next, 0, 2)];
        } else if (property == "filter") {
          asset.texture.filter = next == 0 ? SceneTextureFilter::Nearest
                                           : SceneTextureFilter::Linear;
        } else if (property == "wrap") {
          asset.texture.wrap =
            next == 0 ? SceneTextureWrap::Clamp : SceneTextureWrap::Repeat;
        } else {
          return false;
        }
        return true;
      });
  }
  return document.editNodes(
    selection.ids(),
    "Set " + key,
    {},
    [&key, &picked, next, &document](SceneNode& node) {
      if (key == "mesh.asset") {
        SceneMeshRenderer* mesh = componentOf<SceneMeshRenderer>(node);
        return mesh != nullptr && ((mesh->asset = picked), true);
      }
      if (key == "sprite.texture" || key == "sprite.source") {
        SceneSprite* sprite = componentOf<SceneSprite>(node);
        if (sprite == nullptr) {
          return false;
        }
        if (key == "sprite.source") {
          sprite->hasCell = next == 1;
          return true;
        }
        sprite->texture = picked;
        // Only an atlas has cells; a plain texture shows its region.
        const SceneAsset* asset = findAsset(document, picked);
        if (asset == nullptr || asset->type != SceneAssetType::Atlas) {
          sprite->hasCell = false;
        }
        return true;
      }
      if (key == "primitive.shape") {
        ScenePrimitive* primitive = componentOf<ScenePrimitive>(node);
        return primitive != nullptr &&
               parseScenePrimitiveShape(picked, primitive->shape);
      }
      if (key == "sprite.facing") {
        SceneSprite* sprite = componentOf<SceneSprite>(node);
        if (sprite == nullptr) {
          return false;
        }
        sprite->facing =
          next == 1 ? SceneSpriteFacing::Billboard : SceneSpriteFacing::World;
        return true;
      }
      if (key == "camera.projection") {
        SceneCamera* camera = componentOf<SceneCamera>(node);
        if (camera == nullptr) {
          return false;
        }
        camera->projection = next == 1 ? SceneProjection::Orthographic
                                       : SceneProjection::Perspective;
        return true;
      }
      return false;
    });
}

bool
EditorInspector::beginEdit(const InspectorField& target)
{
  if (target.kind != InspectorFieldKind::Text &&
      target.kind != InspectorFieldKind::Number) {
    return false;
  }
  m_edit.begin(target.mixed ? std::string() : target.value, target.maxBytes);
  m_editKey = target.key;
  m_invalid = false;
  return false;
}

bool
EditorInspector::commitEdit(EditorDocument& document,
                            const EditorSelection& selection)
{
  const std::string key = m_editKey;
  const std::string text = m_edit.text();
  const InspectorField* current = field(key);
  if (current != nullptr && !current->mixed && text == current->value) {
    m_edit.end();
    m_editKey.clear();
    return false;
  }
  if (!applyText(key, text, document, selection)) {
    // Invalid text stays in the field, marked, for correction or Escape.
    m_invalid = true;
    return false;
  }
  m_edit.end();
  m_editKey.clear();
  m_invalid = false;
  return true;
}

bool
EditorInspector::activateField(const std::string& key,
                               EditorDocument* document,
                               const EditorSelection* selection)
{
  if (document == nullptr || selection == nullptr) {
    return false;
  }
  refreshFields(*document, *selection);
  const InspectorField* target = field(key);
  if (target == nullptr) {
    return false;
  }
  const InspectorField copy = *target;
  return activate(copy, false, *document, *selection);
}

bool
EditorInspector::scrubForTesting(const std::string& key,
                                 float pixels,
                                 EditorDocument* document,
                                 const EditorSelection* selection)
{
  if (document == nullptr || selection == nullptr) {
    return false;
  }
  refreshFields(*document, *selection);
  const InspectorField* target = field(key);
  if (target == nullptr || target->kind != InspectorFieldKind::Number) {
    return false;
  }
  if (m_scrubKey != key) {
    m_scrubKey = key;
    m_scrubCarry = 0.0;
    ++m_scrubSerial;
  }
  // Like a real drag, so the release on the next update does not start
  // typing into the field.
  m_scrubAccumulated = std::max(m_scrubAccumulated, 1000.0f);
  const InspectorField copy = *target;
  return scrubBy(
    copy, static_cast<double>(pixels * copy.step), *document, *selection);
}

bool
EditorInspector::scrubBy(const InspectorField& target,
                         double amount,
                         EditorDocument& document,
                         const EditorSelection& selection)
{
  double applied = amount;
  if (target.integer) {
    m_scrubCarry += amount;
    applied = std::trunc(m_scrubCarry);
    m_scrubCarry -= applied;
    if (applied == 0.0) {
      return false;
    }
  }
  return applyNumber(target.key,
                     applied,
                     true,
                     "scrub:" + std::to_string(m_scrubSerial),
                     document,
                     selection);
}

// ---------------------------------------------------------------------------
// Frame update, layout and drawing.

void
EditorInspector::layout()
{
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float rowHeight = std::max(20.0f, std::round(22.0f * fontScale));
  // The label column fits the longest label (and a swatch), within limits.
  const float labelFont = std::max(9.0f, std::round(12.0f * fontScale));
  float widest = 0.0f;
  for (const InspectorRow& row : m_rows) {
    if (row.section || row.label.empty()) {
      continue;
    }
    float width = GuiToolStyle::textWidth(row.label, labelFont);
    if (!row.fields.empty() &&
        m_fields[row.fields.front()].kind == InspectorFieldKind::Swatch) {
      width += rowHeight + 2.0f;
    }
    widest = std::max(widest, width);
  }
  m_labelWidth =
    std::round(std::clamp(widest + GuiToolStyle::kPad + 10.0f * fontScale,
                          m_width * 0.26f,
                          m_width * 0.36f));
  const float labelWidth = m_labelWidth;
  const float gap = std::round(3.0f * fontScale);
  float y = m_y + std::round(4.0f * fontScale) - m_scroll;
  for (InspectorRow& row : m_rows) {
    row.y = y;
    if (row.section) {
      // Header buttons sit at the header's right end, right to left.
      const float size = rowHeight - 6.0f;
      float right = m_x + m_width - 8.0f * fontScale;
      for (size_t index : row.fields) {
        InspectorField& target = m_fields[index];
        right -= size;
        target.x = right;
        target.y = y + 2.0f;
        target.width = size;
        right -= gap;
      }
    } else if (!row.fields.empty()) {
      // A swatch sits at the right end of the label column; the other
      // fields share the rest of the row.
      std::vector<size_t> flowing;
      for (size_t index : row.fields) {
        InspectorField& target = m_fields[index];
        if (target.kind == InspectorFieldKind::Swatch) {
          const float size = rowHeight - 3.0f;
          target.x = m_x + labelWidth - size - 6.0f * fontScale;
          target.y = y;
          target.width = size;
        } else {
          flowing.push_back(index);
        }
      }
      const float startX =
        row.label.empty() ? m_x + 8.0f * fontScale : m_x + labelWidth;
      const float available = m_x + m_width - 8.0f * fontScale - startX;
      const float count =
        static_cast<float>(std::max<size_t>(1, flowing.size()));
      const float fieldWidth = (available - gap * (count - 1.0f)) / count;
      for (size_t index = 0; index < flowing.size(); ++index) {
        InspectorField& target = m_fields[flowing[index]];
        target.x = startX + static_cast<float>(index) * (fieldWidth + gap);
        target.y = y;
        target.width = fieldWidth;
      }
    }
    y += row.section ? std::round(rowHeight * 1.1f) : rowHeight;
  }
  m_contentHeight = y + m_scroll - m_y;
}

int
EditorInspector::hitTest(float x, float y) const
{
  if (!containsScreenPoint(x, y)) {
    return -1;
  }
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float rowHeight = std::max(20.0f, std::round(22.0f * fontScale));
  for (size_t index = 0; index < m_fields.size(); ++index) {
    const InspectorField& target = m_fields[index];
    if (target.hidden) {
      continue;
    }
    if (x >= target.x && x <= target.x + target.width && y >= target.y &&
        y < target.y + rowHeight - 2.0f) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool
EditorInspector::update(InputManager* input,
                        EditorDocument* document,
                        const EditorSelection* selection,
                        float dt)
{
  m_consumedPress = false;
  if (document == nullptr || selection == nullptr) {
    return false;
  }
  m_edit.tick(dt);
  bool changed = false;
  // The field being edited may vanish (its node was deleted, undo ran).
  refreshFields(*document, *selection);
  // The picker's hex box edits no field of its own.
  if (m_edit.active() && field(m_editKey) == nullptr &&
      !(m_pickerOpen && m_editKey == "picker.hex")) {
    m_edit.end();
    m_editKey.clear();
  }
  if (m_edit.active() && input != nullptr) {
    const GuiTextEditEvents events = m_edit.handleInput(*input);
    if (events.copyRequested) {
      m_copyText = events.copied;
      m_copyPending = true;
    }
    if (events.pasteRequested) {
      m_pastePending = true;
    }
    if (events.changed) {
      m_invalid = false;
    }
    if (events.cancelled) {
      m_edit.end();
      m_editKey.clear();
      m_invalid = false;
    } else if (events.committed) {
      changed = commitEdit(*document, *selection) || changed;
    }
  }
  if (changed) {
    refreshFields(*document, *selection);
  }
  layout();

  m_pointer.sample(m_placement, m_window, m_renderer, input);
  const float mouseX = m_pointer.x();
  const float mouseY = m_pointer.y();
  m_hoverField = hitTest(mouseX, mouseY);
  if (input != nullptr && containsScreenPoint(mouseX, mouseY)) {
    const float wheel = m_pointer.takeWheel();
    const InspectorField* wheeled =
      m_hoverField >= 0 ? &m_fields[static_cast<size_t>(m_hoverField)]
                        : nullptr;
    if (wheel != 0.0f && m_listOpen) {
      const int last =
        std::max(0, static_cast<int>(m_listOptions.size()) - kListRows);
      m_listFirst =
        std::clamp(m_listFirst - static_cast<int>(std::round(wheel)), 0, last);
    } else if (wheel != 0.0f && input->isControlPressed() &&
               wheeled != nullptr &&
               wheeled->kind == InspectorFieldKind::Number &&
               !m_edit.active()) {
      // Ctrl+wheel steps the number under the pointer (Shift finer); one
      // command per field however many notches.
      const InspectorField target = *wheeled;
      const double unit = target.integer
                            ? 1.0
                            : static_cast<double>(target.step) * 10.0 *
                                (input->isShiftPressed() ? 0.1 : 1.0);
      changed = applyNumber(target.key,
                            static_cast<double>(wheel) * unit,
                            true,
                            "wheel:" + target.key,
                            *document,
                            *selection) ||
                changed;
    } else if (wheel != 0.0f) {
      const float maximum = std::max(0.0f, m_contentHeight - m_height + 8.0f);
      m_scroll = std::clamp(m_scroll - wheel * 40.0f, 0.0f, maximum);
      layout();
    }
  }

  // An open option list takes the next press: an option is chosen, anything
  // else closes it.
  if (m_listOpen) {
    const InspectorField* choice = field(m_listKey);
    if (choice == nullptr || choice->hidden) {
      m_listOpen = false;
    }
  }
  if (m_listOpen) {
    m_listHover = listOptionAt(mouseX, mouseY);
    if (m_pointer.clicked() || m_pointer.rightClicked()) {
      const int option = m_pointer.clicked() ? m_listHover : -1;
      m_listOpen = false;
      m_consumedPress = true;
      const InspectorField* choice = field(m_listKey);
      if (option >= 0 && choice != nullptr) {
        const InspectorField copy = *choice;
        changed = applyChoice(copy, option, *document, *selection) || changed;
      }
      if (changed) {
        refreshFields(*document, *selection);
        layout();
      }
      rebuildVisual();
      return changed;
    }
  }
  // The open menu takes the next press: an item runs, anything else closes.
  if (m_menuOpen) {
    m_menuHover = menuItemAt(mouseX, mouseY);
    if (m_pointer.clicked() || m_pointer.rightClicked()) {
      const int item = m_pointer.clicked() ? m_menuHover : -1;
      m_menuOpen = false;
      m_consumedPress = true;
      if (item >= 0 && m_menu[static_cast<size_t>(item)].enabled) {
        changed = runMenu(m_menu[static_cast<size_t>(item)].action,
                          *document,
                          *selection) ||
                  changed;
      }
      if (changed) {
        refreshFields(*document, *selection);
        layout();
      }
      rebuildVisual();
      return changed;
    }
  } else if (m_pointer.rightClicked() && containsScreenPoint(mouseX, mouseY)) {
    m_consumedPress = true;
    const int row = rowAt(mouseY);
    if (row >= 0) {
      openMenu(row, mouseX, mouseY, *document, *selection);
    }
  }
  const bool pickerTook =
    updatePicker(mouseX, mouseY, &changed, *document, *selection);
  if (pickerTook) {
    m_consumedPress = true;
  } else if (m_pointer.clicked() && containsScreenPoint(mouseX, mouseY)) {
    m_consumedPress = true;
    const int hit = hitTest(mouseX, mouseY);
    const int headerRow = hit < 0 ? rowAt(mouseY) : -1;
    if (headerRow >= 0 && m_rows[static_cast<size_t>(headerRow)].section) {
      // A section header folds or unfolds its section.
      const std::string& title = m_rows[static_cast<size_t>(headerRow)].label;
      if (!m_folded.erase(title)) {
        m_folded.insert(title);
      }
      m_fieldsBuilt = false;
      refreshFields(*document, *selection);
      layout();
    }
    if (m_edit.active() &&
        (hit < 0 || m_fields[static_cast<size_t>(hit)].key != m_editKey)) {
      changed = commitEdit(*document, *selection) || changed;
    }
    if (hit >= 0) {
      const InspectorField target = m_fields[static_cast<size_t>(hit)];
      if (target.kind == InspectorFieldKind::Number && target.scrub) {
        // Held and dragged, a number scrubs; clicked, it edits.
        m_scrubKey = target.key;
        m_scrubLastX = mouseX;
        m_scrubAccumulated = 0.0f;
        m_scrubCarry = 0.0;
        ++m_scrubSerial;
      } else if (target.kind == InspectorFieldKind::Choice &&
                 !target.choices.empty()) {
        // The arrows step through the options; the middle opens the list.
        const float zone = std::min(18.0f, target.width * 0.25f);
        const int count = static_cast<int>(target.choices.size());
        if (mouseX < target.x + zone) {
          changed = applyChoice(target,
                                (target.choice + count - 1) % count,
                                *document,
                                *selection) ||
                    changed;
        } else if (mouseX > target.x + target.width - zone) {
          changed =
            applyChoice(
              target, (target.choice + 1) % count, *document, *selection) ||
            changed;
        } else {
          openList(target);
        }
      } else if (!m_edit.active() || target.key != m_editKey) {
        changed = activate(target, false, *document, *selection) || changed;
      }
    }
  } else if (m_pointer.clicked() && m_edit.active()) {
    changed = commitEdit(*document, *selection) || changed;
  }
  if (!m_scrubKey.empty()) {
    const InspectorField* target = field(m_scrubKey);
    if (m_pointer.pressed() && target != nullptr) {
      const float delta = mouseX - m_scrubLastX;
      m_scrubAccumulated += std::fabs(delta);
      m_scrubLastX = mouseX;
      if (m_scrubAccumulated > 3.0f && delta != 0.0f && !target->mixed) {
        // Shift scrubs ten times finer, Ctrl ten times coarser.
        const float factor = input == nullptr            ? 1.0f
                             : input->isShiftPressed()   ? 0.1f
                             : input->isControlPressed() ? 10.0f
                                                         : 1.0f;
        const InspectorField copy = *target;
        changed = scrubBy(copy,
                          static_cast<double>(delta * copy.step * factor),
                          *document,
                          *selection) ||
                  changed;
      }
      m_consumedPress = true;
    } else {
      if (target != nullptr && m_scrubAccumulated <= 3.0f) {
        beginEdit(*target);
      }
      m_scrubKey.clear();
    }
  }
  if (changed) {
    refreshFields(*document, *selection);
    layout();
  }
  rebuildVisual();
  return changed;
}

void
EditorInspector::rebuildVisual()
{
  m_visual.clearPrimitives();
  if (!m_visible) {
    return;
  }
  m_visual.setPixelClipRect(Rect2{ m_x, m_y, m_width, m_height });
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float rowHeight = std::max(20.0f, std::round(22.0f * fontScale));
  const float fontSize = std::max(9.0f, std::round(12.0f * fontScale));
  for (const InspectorRow& row : m_rows) {
    if (row.y + rowHeight < m_y || row.y > m_y + m_height) {
      continue;
    }
    if (row.section) {
      // A fold arrow: right when folded, down when open.
      const float arrowX = m_x + GuiToolStyle::kPad;
      const float arrowY = row.y + rowHeight * 0.5f;
      const float size = std::max(6.0f, std::round(7.0f * fontScale));
      if (row.folded) {
        m_visual.addLine(arrowX,
                         arrowY - size * 0.5f,
                         arrowX + size * 0.6f,
                         arrowY,
                         GuiToolPalette::dim,
                         1.0f);
        m_visual.addLine(arrowX + size * 0.6f,
                         arrowY,
                         arrowX,
                         arrowY + size * 0.5f,
                         GuiToolPalette::dim,
                         1.0f);
      } else {
        m_visual.addLine(arrowX,
                         arrowY - size * 0.3f,
                         arrowX + size * 0.5f,
                         arrowY + size * 0.3f,
                         GuiToolPalette::dim,
                         1.0f);
        m_visual.addLine(arrowX + size * 0.5f,
                         arrowY + size * 0.3f,
                         arrowX + size,
                         arrowY - size * 0.3f,
                         GuiToolPalette::dim,
                         1.0f);
      }
      GuiToolStyle::text(m_visual,
                         row.label,
                         arrowX + size + 6.0f * fontScale,
                         row.y + 5.0f * fontScale,
                         fontSize,
                         GuiToolPalette::faint);
      m_visual.addLine(m_x + GuiToolStyle::kPad,
                       row.y + rowHeight - 0.5f,
                       m_x + m_width - GuiToolStyle::kPad,
                       row.y + rowHeight - 0.5f,
                       GuiToolPalette::rule,
                       1.0f);
      continue;
    }
    if (!row.label.empty()) {
      const bool swatch =
        !row.fields.empty() &&
        m_fields[row.fields.front()].kind == InspectorFieldKind::Swatch;
      GuiToolStyle::fittedText(
        m_visual,
        row.label,
        m_x + GuiToolStyle::kPad,
        row.y + std::round((rowHeight - fontSize) * 0.5f) - 1.0f,
        m_labelWidth - GuiToolStyle::kPad - 4.0f -
          (swatch ? rowHeight + 4.0f : 0.0f),
        fontSize,
        GuiToolPalette::dim);
    }
  }
  for (size_t index = 0; index < m_fields.size(); ++index) {
    const InspectorField& target = m_fields[index];
    if (target.hidden || target.y + rowHeight < m_y ||
        target.y > m_y + m_height) {
      continue;
    }
    const float h = rowHeight - 3.0f;
    const bool hovered = m_hoverField == static_cast<int>(index);
    const bool focused = m_edit.active() && target.key == m_editKey;
    switch (target.kind) {
      case InspectorFieldKind::Text:
      case InspectorFieldKind::Number:
      case InspectorFieldKind::ReadOnly:
        GuiToolStyle::textField(
          m_visual,
          GuiToolRect{ target.x, target.y, target.width, h },
          target.mixed ? std::string("-") : target.value,
          focused ? &m_edit : nullptr,
          focused && m_invalid,
          hovered && target.kind != InspectorFieldKind::ReadOnly,
          target.kind == InspectorFieldKind::ReadOnly,
          fontSize);
        break;
      case InspectorFieldKind::Swatch: {
        // Checks show through a translucent color; a mixed selection reads
        // as a dash.
        const float half = h * 0.5f;
        m_visual.addFilledRect(
          target.x, target.y, target.width, h, GuiToolPalette::text);
        m_visual.addFilledRect(
          target.x, target.y, half, half, GuiToolPalette::faint);
        m_visual.addFilledRect(
          target.x + half, target.y + half, half, half, GuiToolPalette::faint);
        if (!target.mixed) {
          m_visual.addFilledRect(
            target.x, target.y, target.width, h, target.swatch);
        } else {
          m_visual.addFilledRect(
            target.x, target.y, target.width, h, GuiToolPalette::field);
          GuiKit::drawTextCentered(m_visual,
                                   "-",
                                   target.x + target.width * 0.5f,
                                   target.y + h * 0.5f,
                                   fontSize,
                                   GuiToolPalette::text);
        }
        const bool open = m_pickerOpen && m_pickerSwatch == target.key;
        m_visual.addOutlineRect(target.x,
                                target.y,
                                target.width,
                                h,
                                open || hovered ? GuiToolPalette::accent
                                                : GuiToolPalette::border,
                                1.0f);
        break;
      }
      case InspectorFieldKind::Toggle: {
        // A check box and its label, like the Tools panel; a mixed
        // selection shows a dash in the box.
        if (hovered) {
          m_visual.addFilledRect(
            target.x, target.y, target.width, h, GuiToolPalette::hover);
        }
        const float box = std::min(12.0f * fontScale, h - 6.0f);
        const float boxX = target.x + 3.0f;
        const float boxY = target.y + std::round((h - box) * 0.5f);
        m_visual.addFilledRect(boxX, boxY, box, box, GuiToolPalette::field);
        m_visual.addOutlineRect(boxX,
                                boxY,
                                box,
                                box,
                                target.toggle || hovered
                                  ? GuiToolPalette::accent
                                  : GuiToolPalette::border,
                                1.0f);
        if (target.mixed) {
          m_visual.addFilledRect(boxX + 3.0f,
                                 boxY + box * 0.5f - 1.0f,
                                 box - 6.0f,
                                 2.0f,
                                 GuiToolPalette::accent);
        } else if (target.toggle) {
          m_visual.addFilledRect(boxX + 3.0f,
                                 boxY + 3.0f,
                                 box - 6.0f,
                                 box - 6.0f,
                                 GuiToolPalette::accent);
        }
        // A lone toggle named like its row ("Sun") needs no text of its own.
        const bool named =
          std::any_of(m_rows.begin(),
                      m_rows.end(),
                      [&target, index](const InspectorRow& row) {
                        return row.fields.size() == 1 &&
                               row.fields.front() == index &&
                               row.label == target.label;
                      });
        GuiToolStyle::fittedText(m_visual,
                                 named ? std::string() : target.label,
                                 boxX + box + 5.0f,
                                 target.y + std::round((h - fontSize) * 0.5f) -
                                   1.0f,
                                 target.width - box - 10.0f,
                                 fontSize,
                                 GuiToolPalette::text);
        break;
      }
      case InspectorFieldKind::Choice:
      case InspectorFieldKind::Button: {
        const bool isButton = target.kind == InspectorFieldKind::Button;
        if (target.header) {
          // A small cross in the section header.
          if (hovered) {
            m_visual.addFilledRect(target.x,
                                   target.y,
                                   target.width,
                                   target.width,
                                   GuiToolPalette::pressed);
          }
          const float inset = target.width * 0.3f;
          const ColorRgba cross =
            hovered ? GuiToolPalette::error : GuiToolPalette::dim;
          m_visual.addLine(target.x + inset,
                           target.y + inset,
                           target.x + target.width - inset,
                           target.y + target.width - inset,
                           cross,
                           1.0f);
          m_visual.addLine(target.x + target.width - inset,
                           target.y + inset,
                           target.x + inset,
                           target.y + target.width - inset,
                           cross,
                           1.0f);
          break;
        }
        m_visual.addFilledRect(target.x,
                               target.y,
                               target.width,
                               h,
                               hovered    ? GuiToolPalette::pressed
                               : isButton ? GuiToolPalette::button
                                          : GuiToolPalette::field);
        m_visual.addOutlineRect(target.x,
                                target.y,
                                target.width,
                                h,
                                hovered ? GuiToolPalette::faint
                                        : GuiToolPalette::border,
                                1.0f);
        GuiKit::drawTextCentered(m_visual,
                                 isButton ? target.value
                                          : "< " + target.value + " >",
                                 target.x + target.width * 0.5f,
                                 target.y + h * 0.5f,
                                 fontSize,
                                 GuiToolPalette::text);
        break;
      }
    }
  }
  drawPicker();
  if (m_menuOpen) {
    GuiToolStyle::dropdown(
      m_visual, m_menuX, m_menuY, menuItems(), m_menuHover);
  }
  if (m_listOpen) {
    const std::vector<GuiToolStyle::MenuItem> items = listItems();
    GuiToolStyle::dropdown(
      m_visual, m_listX, m_listY, items, m_listHover - m_listFirst);
    const int total = static_cast<int>(m_listOptions.size());
    if (total > kListRows) {
      const float height =
        6.0f + static_cast<float>(items.size()) * GuiToolStyle::kRowHeight;
      GuiToolStyle::scrollbar(
        m_visual,
        GuiToolRect{ m_listX + listWidth() - GuiToolStyle::kScrollbar - 2.0f,
                     m_listY + 3.0f,
                     GuiToolStyle::kScrollbar,
                     height - 6.0f },
        static_cast<float>(m_listFirst),
        static_cast<float>(kListRows),
        static_cast<float>(total));
    }
  }
}
void
EditorInspector::moveRemoveButtonsToHeaders()
{
  std::vector<InspectorRow> kept;
  kept.reserve(m_rows.size());
  int section = -1;
  for (InspectorRow& row : m_rows) {
    if (row.section) {
      kept.push_back(row);
      section = static_cast<int>(kept.size()) - 1;
      continue;
    }
    std::vector<size_t> rest;
    for (size_t index : row.fields) {
      InspectorField& target = m_fields[index];
      const bool remove = target.kind == InspectorFieldKind::Button &&
                          (target.key.starts_with("component.remove.") ||
                           target.key.starts_with("behaviour.remove:"));
      if (remove && section >= 0) {
        target.header = true;
        kept[static_cast<size_t>(section)].fields.push_back(index);
      } else {
        rest.push_back(index);
      }
    }
    if (rest.empty() && !row.fields.empty()) {
      continue;
    }
    row.fields = rest;
    kept.push_back(row);
  }
  m_rows.swap(kept);
}

void
EditorInspector::openList(const InspectorField& choice)
{
  m_listOpen = true;
  m_listKey = choice.key;
  m_listOptions = choice.choices;
  m_listCurrent = choice.choice;
  m_listHover = -1;
  const int total = static_cast<int>(m_listOptions.size());
  m_listFirst = std::clamp(
    m_listCurrent - kListRows / 2, 0, std::max(0, total - kListRows));
  const float height = 6.0f + static_cast<float>(std::min(total, kListRows)) *
                                GuiToolStyle::kRowHeight;
  const float width = listWidth();
  m_listX = std::clamp(
    choice.x, m_x + 2.0f, std::max(m_x + 2.0f, m_x + m_width - width - 2.0f));
  m_listY = choice.y + rowHeight() - 2.0f;
  if (m_listY + height > m_y + m_height) {
    m_listY = std::max(m_y, choice.y - height + 2.0f);
  }
}

std::vector<GuiToolStyle::MenuItem>
EditorInspector::listItems() const
{
  std::vector<GuiToolStyle::MenuItem> items;
  const int total = static_cast<int>(m_listOptions.size());
  for (int index = m_listFirst;
       index < std::min(total, m_listFirst + kListRows);
       ++index) {
    GuiToolStyle::MenuItem item;
    item.label = m_listOptions[static_cast<size_t>(index)];
    item.checked = index == m_listCurrent;
    items.push_back(item);
  }
  return items;
}

float
EditorInspector::listWidth() const
{
  std::vector<GuiToolStyle::MenuItem> all;
  for (const std::string& option : m_listOptions) {
    GuiToolStyle::MenuItem item;
    item.label = option;
    item.checked = true;
    all.push_back(item);
  }
  const InspectorField* choice = field(m_listKey);
  return std::max(GuiToolStyle::dropdownWidth(all),
                  choice != nullptr ? choice->width : 0.0f);
}

int
EditorInspector::listOptionAt(float x, float y) const
{
  if (!m_listOpen) {
    return -1;
  }
  const int row =
    GuiToolStyle::dropdownItemAt(m_listX, m_listY, listItems(), x, y);
  return row < 0 ? -1 : m_listFirst + row;
}

bool
EditorInspector::openListForTesting(const std::string& key,
                                    EditorDocument* document,
                                    const EditorSelection* selection)
{
  if (document == nullptr || selection == nullptr) {
    return false;
  }
  refreshFields(*document, *selection);
  layout();
  const InspectorField* choice = field(key);
  if (choice == nullptr || choice->kind != InspectorFieldKind::Choice) {
    return false;
  }
  openList(*choice);
  return true;
}

bool
EditorInspector::chooseListForTesting(int option,
                                      EditorDocument* document,
                                      const EditorSelection* selection)
{
  const InspectorField* choice = field(m_listKey);
  if (!m_listOpen || choice == nullptr || document == nullptr ||
      selection == nullptr) {
    return false;
  }
  m_listOpen = false;
  const InspectorField copy = *choice;
  return applyChoice(copy, option, *document, *selection);
}

void
EditorInspector::applyFolding()
{
  std::vector<InspectorRow> kept;
  kept.reserve(m_rows.size());
  bool folding = false;
  for (InspectorRow& row : m_rows) {
    if (row.section) {
      row.folded = m_folded.contains(row.label);
      folding = row.folded;
      kept.push_back(row);
      continue;
    }
    if (folding) {
      for (size_t index : row.fields) {
        m_fields[index].hidden = true;
      }
      continue;
    }
    kept.push_back(row);
  }
  m_rows.swap(kept);
}

float
EditorInspector::rowHeight() const
{
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  return std::max(20.0f, std::round(22.0f * fontScale));
}

int
EditorInspector::rowAt(float y) const
{
  for (size_t index = 0; index < m_rows.size(); ++index) {
    const InspectorRow& row = m_rows[index];
    const float height =
      row.section ? std::round(rowHeight() * 1.1f) : rowHeight();
    if (y >= row.y && y < row.y + height) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void
EditorInspector::openMenu(int rowIndex,
                          float x,
                          float y,
                          const EditorDocument& document,
                          const EditorSelection& selection)
{
  (void)document;
  (void)selection;
  const InspectorRow& target = m_rows[static_cast<size_t>(rowIndex)];
  std::string title;
  for (int index = rowIndex; index >= 0; --index) {
    if (m_rows[static_cast<size_t>(index)].section) {
      title = m_rows[static_cast<size_t>(index)].label;
      break;
    }
  }
  m_menu.clear();
  m_menuKeys.clear();
  m_menuGroup = target.group;
  if (!target.section) {
    bool resettable = false;
    for (size_t index : target.fields) {
      const InspectorField& field = m_fields[index];
      resettable = resettable || field.kind == InspectorFieldKind::Number ||
                   field.key.starts_with("behaviour:");
      m_menuKeys.push_back(field.key);
    }
    if (resettable) {
      m_menu.push_back({ "Reset " + (target.label.empty() ? std::string("value")
                                                          : target.label),
                         MenuAction::ResetRow,
                         true });
    }
  }
  if (!m_menuGroup.empty()) {
    m_menu.push_back({ "Reset " + title, MenuAction::ResetGroup, true });
    m_menu.push_back({ "Copy " + title, MenuAction::CopyGroup, true });
    m_menu.push_back({ "Paste " + title + " Values",
                       MenuAction::PasteGroup,
                       m_clipboardGroup == m_menuGroup });
    if (m_menuGroup != "transform") {
      m_menu.push_back({ "Remove " + title, MenuAction::RemoveGroup, true });
    }
  }
  if (m_menu.empty()) {
    return;
  }
  m_menuOpen = true;
  m_menuHover = -1;
  const float width = GuiToolStyle::dropdownWidth(menuItems());
  const float height =
    6.0f + static_cast<float>(m_menu.size()) * GuiToolStyle::kRowHeight;
  m_menuX = std::clamp(
    x, m_x + 2.0f, std::max(m_x + 2.0f, m_x + m_width - width - 2.0f));
  m_menuY = std::clamp(y, m_y, std::max(m_y, m_y + m_height - height));
}

std::vector<GuiToolStyle::MenuItem>
EditorInspector::menuItems() const
{
  std::vector<GuiToolStyle::MenuItem> items;
  for (const MenuEntry& entry : m_menu) {
    GuiToolStyle::MenuItem item;
    item.label = entry.label;
    item.enabled = entry.enabled;
    items.push_back(item);
  }
  return items;
}

int
EditorInspector::menuItemAt(float x, float y) const
{
  return m_menuOpen
           ? GuiToolStyle::dropdownItemAt(m_menuX, m_menuY, menuItems(), x, y)
           : -1;
}

bool
EditorInspector::runMenu(MenuAction action,
                         EditorDocument& document,
                         const EditorSelection& selection)
{
  const std::string group = m_menuGroup;
  const EditorBehaviours* behaviours = m_behaviours;
  switch (action) {
    case MenuAction::ResetRow: {
      if (m_menuKeys.empty()) {
        return false;
      }
      const std::string& first = m_menuKeys.front();
      if (first.starts_with("behaviour:")) {
        return applyBehaviour(
          first,
          [](BehaviourValue& value, const BehaviourField& field, char) {
            value = field.defaultValue;
            return true;
          },
          {},
          document,
          selection);
      }
      if (first.starts_with("env.")) {
        SceneEnvironment environment = document.scene().document().environment;
        SceneEnvironment defaults;
        bool any = false;
        for (const std::string& key : m_menuKeys) {
          float* slot = resolveEnvironment(environment, key);
          const float* reset = resolveEnvironment(defaults, key);
          if (slot != nullptr && reset != nullptr) {
            *slot = *reset;
            any = true;
          }
        }
        return any && document.setEnvironment(environment, {});
      }
      const std::vector<std::string> keys = m_menuKeys;
      return document.editNodes(selection.ids(),
                                "Reset values",
                                {},
                                [&keys, behaviours](SceneNode& node) {
                                  const SceneNode defaults =
                                    defaultsOf(node, behaviours);
                                  bool any = false;
                                  for (const std::string& key : keys) {
                                    double value = 0.0;
                                    if (readNumber(defaults, key, &value)) {
                                      any =
                                        writeNumber(node, key, value) || any;
                                    }
                                  }
                                  return any;
                                });
    }
    case MenuAction::ResetGroup:
      return document.editNodes(
        selection.ids(),
        "Reset values",
        {},
        [&group, behaviours](SceneNode& node) {
          if (group == "transform") {
            node.transform = Transform3D{};
            return true;
          }
          for (SceneComponent& component : node.components) {
            if (componentGroup(component, behaviours) == group) {
              component = defaultComponentLike(component, behaviours);
              return true;
            }
          }
          return false;
        });
    case MenuAction::CopyGroup: {
      const SceneNode* node = document.findNode(selection.primary());
      if (node == nullptr) {
        return false;
      }
      if (group == "transform") {
        m_clipboardTransform = node->transform;
        m_clipboardGroup = group;
        return false;
      }
      for (const SceneComponent& component : node->components) {
        if (componentGroup(component, behaviours) == group) {
          m_clipboardComponent = component;
          m_clipboardGroup = group;
        }
      }
      return false;
    }
    case MenuAction::PasteGroup: {
      if (m_clipboardGroup != group) {
        return false;
      }
      const SceneComponent copied = m_clipboardComponent;
      const Transform3D transform = m_clipboardTransform;
      return document.editNodes(
        selection.ids(),
        "Paste values",
        {},
        [&group, &copied, &transform, behaviours](SceneNode& node) {
          if (group == "transform") {
            node.transform = transform;
            return true;
          }
          for (SceneComponent& component : node.components) {
            if (componentGroup(component, behaviours) == group) {
              component = copied;
              return true;
            }
          }
          return false;
        });
    }
    case MenuAction::RemoveGroup: {
      InspectorField remove;
      remove.kind = InspectorFieldKind::Button;
      remove.key = group.starts_with("behaviour:")
                     ? "behaviour.remove:" + group.substr(10)
                     : "component.remove." + group;
      return activate(remove, false, document, selection);
    }
  }
  return false;
}

void
EditorInspector::toggleSectionForTesting(const std::string& title)
{
  if (!m_folded.erase(title)) {
    m_folded.insert(title);
  }
  m_fieldsBuilt = false;
}

bool
EditorInspector::openMenuForTesting(const std::string& key,
                                    const std::string& sectionTitle,
                                    EditorDocument* document,
                                    const EditorSelection* selection)
{
  if (document == nullptr || selection == nullptr) {
    return false;
  }
  refreshFields(*document, *selection);
  layout();
  for (size_t index = 0; index < m_rows.size(); ++index) {
    const InspectorRow& row = m_rows[index];
    bool match = key.empty() && row.section && row.label == sectionTitle;
    for (size_t field : row.fields) {
      match = match || (!key.empty() && m_fields[field].key == key);
    }
    if (match) {
      openMenu(
        static_cast<int>(index), m_x + 10.0f, row.y, *document, *selection);
      return m_menuOpen;
    }
  }
  return false;
}

std::vector<std::string>
EditorInspector::menuLabelsForTesting() const
{
  std::vector<std::string> labels;
  for (const MenuEntry& entry : m_menu) {
    labels.push_back(entry.label);
  }
  return labels;
}

bool
EditorInspector::chooseMenuForTesting(const std::string& label,
                                      EditorDocument* document,
                                      const EditorSelection* selection)
{
  if (!m_menuOpen || document == nullptr || selection == nullptr) {
    return false;
  }
  for (const MenuEntry& entry : m_menu) {
    if (entry.label == label && entry.enabled) {
      m_menuOpen = false;
      runMenu(entry.action, *document, *selection);
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// The color picker.

void
EditorInspector::openPicker(const InspectorField& swatch)
{
  m_pickerOpen = true;
  m_pickerSwatch = swatch.key;
  m_pickerChannels = swatch.channels;
  m_pickerUnit = swatch.unitChannels;
  rgbToHsv(static_cast<float>(swatch.swatch.r) / 255.0f,
           static_cast<float>(swatch.swatch.g) / 255.0f,
           static_cast<float>(swatch.swatch.b) / 255.0f,
           &m_pickerHue,
           &m_pickerSaturation,
           &m_pickerValue);
  m_pickerAlpha = m_pickerChannels.size() == 4
                    ? static_cast<float>(swatch.swatch.a) / 255.0f
                    : 1.0f;
  m_pickerDrag = 0;
  ++m_pickerSerial;
}

GuiToolRect
EditorInspector::pickerRect() const
{
  const InspectorField* swatch = field(m_pickerSwatch);
  if (!m_pickerOpen || swatch == nullptr) {
    return {};
  }
  const float strips = m_pickerChannels.size() == 4 ? 2.0f : 1.0f;
  const float height = 6.0f + 110.0f + strips * 18.0f + 6.0f + 20.0f + 6.0f;
  // Below the swatch's row, or above it when the panel runs out.
  float y = swatch->y + rowHeight();
  if (y + height > m_y + m_height) {
    y = std::max(m_y, swatch->y - height);
  }
  return { m_x + 8.0f, y, std::max(60.0f, m_width - 16.0f), height };
}

GuiToolRect
EditorInspector::pickerSquare() const
{
  const GuiToolRect rect = pickerRect();
  return { rect.x + 6.0f, rect.y + 6.0f, rect.w - 12.0f, 110.0f };
}

GuiToolRect
EditorInspector::pickerHueStrip() const
{
  const GuiToolRect square = pickerSquare();
  return { square.x, square.y + square.h + 6.0f, square.w, 12.0f };
}

GuiToolRect
EditorInspector::pickerAlphaStrip() const
{
  const GuiToolRect hue = pickerHueStrip();
  if (m_pickerChannels.size() != 4) {
    return { hue.x, hue.y, hue.w, 0.0f };
  }
  return { hue.x, hue.y + hue.h + 6.0f, hue.w, 12.0f };
}

GuiToolRect
EditorInspector::pickerHexBox() const
{
  const GuiToolRect last =
    m_pickerChannels.size() == 4 ? pickerAlphaStrip() : pickerHueStrip();
  return { last.x, last.y + last.h + 6.0f, last.w, 20.0f };
}

bool
EditorInspector::applyPicker(EditorDocument& document,
                             const EditorSelection& selection)
{
  float rgba[4] = { 0.0f, 0.0f, 0.0f, m_pickerAlpha };
  hsvToRgb(m_pickerHue,
           m_pickerSaturation,
           m_pickerValue,
           &rgba[0],
           &rgba[1],
           &rgba[2]);
  bool changed = false;
  const std::string mergeKey = "picker:" + std::to_string(m_pickerSerial);
  for (size_t index = 0; index < m_pickerChannels.size() && index < 4;
       ++index) {
    const double value =
      m_pickerUnit ? static_cast<double>(rgba[index])
                   : static_cast<double>(std::lround(rgba[index] * 255.0f));
    changed =
      applyNumber(
        m_pickerChannels[index], value, false, mergeKey, document, selection) ||
      changed;
  }
  return changed;
}

bool
EditorInspector::updatePicker(float x,
                              float y,
                              bool* changed,
                              EditorDocument& document,
                              const EditorSelection& selection)
{
  if (!m_pickerOpen) {
    return false;
  }
  const InspectorField* swatch = field(m_pickerSwatch);
  if (swatch == nullptr || swatch->hidden) {
    // The selection or the document moved on.
    m_pickerOpen = false;
    return false;
  }
  bool took = false;
  if (m_pointer.clicked()) {
    const GuiToolRect rect = pickerRect();
    if (!rect.contains(x, y)) {
      // A press elsewhere closes it; on the swatch, the swatch closes it.
      const bool onSwatch = x >= swatch->x && x <= swatch->x + swatch->width &&
                            y >= swatch->y && y < swatch->y + rowHeight();
      if (!onSwatch) {
        m_pickerOpen = false;
      }
      return false;
    }
    took = true;
    if (pickerSquare().contains(x, y)) {
      m_pickerDrag = 1;
    } else if (pickerHueStrip().contains(x, y)) {
      m_pickerDrag = 2;
    } else if (pickerAlphaStrip().h > 0.0f &&
               pickerAlphaStrip().contains(x, y)) {
      m_pickerDrag = 3;
    } else if (pickerHexBox().contains(x, y)) {
      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      hsvToRgb(m_pickerHue, m_pickerSaturation, m_pickerValue, &r, &g, &b);
      const ColorRgba color = toBytes(r, g, b, m_pickerAlpha);
      char hex[16];
      std::snprintf(
        hex, sizeof(hex), "#%02X%02X%02X", color.r, color.g, color.b);
      std::string text(hex);
      if (m_pickerChannels.size() == 4) {
        std::snprintf(hex, sizeof(hex), "%02X", color.a);
        text += hex;
      }
      m_edit.begin(text, 9);
      m_editKey = "picker.hex";
      m_invalid = false;
    }
  }
  if (m_pickerDrag != 0) {
    took = true;
    if (!m_pointer.pressed()) {
      m_pickerDrag = 0;
      return took;
    }
    if (m_pickerDrag == 1) {
      const GuiToolRect square = pickerSquare();
      m_pickerSaturation = std::clamp((x - square.x) / square.w, 0.0f, 1.0f);
      m_pickerValue = 1.0f - std::clamp((y - square.y) / square.h, 0.0f, 1.0f);
    } else {
      const GuiToolRect strip =
        m_pickerDrag == 2 ? pickerHueStrip() : pickerAlphaStrip();
      const float along = std::clamp((x - strip.x) / strip.w, 0.0f, 1.0f);
      if (m_pickerDrag == 2) {
        m_pickerHue = std::min(along, 0.9999f);
      } else {
        m_pickerAlpha = along;
      }
    }
    *changed = applyPicker(document, selection) || *changed;
  }
  return took;
}

void
EditorInspector::drawPicker()
{
  if (!m_pickerOpen || field(m_pickerSwatch) == nullptr) {
    return;
  }
  const GuiToolRect rect = pickerRect();
  m_visual.addFilledRect(rect.x, rect.y, rect.w, rect.h, GuiToolPalette::bar);
  m_visual.addOutlineRect(
    rect.x, rect.y, rect.w, rect.h, GuiToolPalette::border, 1.0f);
  const ColorRgba white{ 255, 255, 255, 255 };
  const ColorRgba black{ 0, 0, 0, 255 };
  const ColorRgba clear{ 0, 0, 0, 0 };
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  hsvToRgb(m_pickerHue, 1.0f, 1.0f, &r, &g, &b);
  const ColorRgba pure = toBytes(r, g, b, 1.0f);
  // Saturation across, value down: white to the pure hue, then darkened.
  const GuiToolRect square = pickerSquare();
  m_visual.addGradientRect(
    square.x, square.y, square.w, square.h, white, pure, pure, white);
  m_visual.addGradientRect(
    square.x, square.y, square.w, square.h, clear, clear, black, black);
  const float markX = square.x + m_pickerSaturation * square.w;
  const float markY = square.y + (1.0f - m_pickerValue) * square.h;
  m_visual.addOutlineRect(markX - 4.0f, markY - 4.0f, 8.0f, 8.0f, black, 1.0f);
  m_visual.addOutlineRect(markX - 3.0f, markY - 3.0f, 6.0f, 6.0f, white, 1.0f);
  // The hue strip in six spans.
  const GuiToolRect hue = pickerHueStrip();
  for (int span = 0; span < 6; ++span) {
    float r0 = 0.0f;
    float g0 = 0.0f;
    float b0 = 0.0f;
    float r1 = 0.0f;
    float g1 = 0.0f;
    float b1 = 0.0f;
    hsvToRgb(static_cast<float>(span) / 6.0f, 1.0f, 1.0f, &r0, &g0, &b0);
    hsvToRgb(
      static_cast<float>(span + 1) / 6.0f - 0.0001f, 1.0f, 1.0f, &r1, &g1, &b1);
    const ColorRgba left = toBytes(r0, g0, b0, 1.0f);
    const ColorRgba right = toBytes(r1, g1, b1, 1.0f);
    const float width = hue.w / 6.0f;
    m_visual.addGradientRect(hue.x + width * static_cast<float>(span),
                             hue.y,
                             width,
                             hue.h,
                             left,
                             right,
                             right,
                             left);
  }
  const float hueX = hue.x + m_pickerHue * hue.w;
  m_visual.addFilledRect(hueX - 1.0f, hue.y - 2.0f, 3.0f, hue.h + 4.0f, white);
  hsvToRgb(m_pickerHue, m_pickerSaturation, m_pickerValue, &r, &g, &b);
  if (m_pickerChannels.size() == 4) {
    const GuiToolRect alpha = pickerAlphaStrip();
    const float check = alpha.h * 0.5f;
    for (float x = alpha.x; x < alpha.x + alpha.w; x += check * 2.0f) {
      m_visual.addFilledRect(x,
                             alpha.y,
                             std::min(check, alpha.x + alpha.w - x),
                             check,
                             GuiToolPalette::faint);
      if (x + check < alpha.x + alpha.w) {
        m_visual.addFilledRect(x + check,
                               alpha.y + check,
                               std::min(check, alpha.x + alpha.w - x - check),
                               check,
                               GuiToolPalette::faint);
      }
    }
    m_visual.addGradientRect(alpha.x,
                             alpha.y,
                             alpha.w,
                             alpha.h,
                             toBytes(r, g, b, 0.0f),
                             toBytes(r, g, b, 1.0f),
                             toBytes(r, g, b, 1.0f),
                             toBytes(r, g, b, 0.0f));
    const float alphaX = alpha.x + m_pickerAlpha * alpha.w;
    m_visual.addFilledRect(
      alphaX - 1.0f, alpha.y - 2.0f, 3.0f, alpha.h + 4.0f, white);
  }
  // The hex box, with a preview of the color at its right.
  const GuiToolRect hex = pickerHexBox();
  const ColorRgba color = toBytes(r, g, b, m_pickerAlpha);
  char text[16];
  std::snprintf(text, sizeof(text), "#%02X%02X%02X", color.r, color.g, color.b);
  std::string shown(text);
  if (m_pickerChannels.size() == 4) {
    std::snprintf(text, sizeof(text), "%02X", color.a);
    shown += text;
  }
  const bool editing = m_edit.active() && m_editKey == "picker.hex";
  GuiToolStyle::textField(
    m_visual,
    GuiToolRect{ hex.x, hex.y, hex.w - hex.h - 4.0f, hex.h },
    shown,
    editing ? &m_edit : nullptr,
    editing && m_invalid,
    false,
    false,
    GuiToolStyle::kSmallFontSize);
  m_visual.addFilledRect(
    hex.x + hex.w - hex.h, hex.y, hex.h, hex.h, toBytes(r, g, b, 1.0f));
  m_visual.addOutlineRect(
    hex.x + hex.w - hex.h, hex.y, hex.h, hex.h, GuiToolPalette::border, 1.0f);
}

bool
EditorInspector::pickForTesting(float hue,
                                float saturation,
                                float value,
                                float alpha,
                                EditorDocument* document,
                                const EditorSelection* selection)
{
  if (!m_pickerOpen || document == nullptr || selection == nullptr) {
    return false;
  }
  m_pickerHue = hue;
  m_pickerSaturation = saturation;
  m_pickerValue = value;
  m_pickerAlpha = alpha;
  return applyPicker(*document, *selection);
}

bool
EditorInspector::AppendCommands(Renderer* renderer)
{
  if (!m_visible) {
    return true;
  }
  return m_visual.AppendCommands(renderer);
}
