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

static bool
parseNumber(const std::string& text, double* value)
{
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(text.c_str(), &end);
  while (end != nullptr && *end == ' ') {
    ++end;
  }
  if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
    return false;
  }
  *value = parsed;
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

// A float field or a color byte inside a node for a key, or nothing.
struct NumberSlot
{
  float* number = nullptr;
  unsigned char* byte = nullptr;
  // Euler degree axis 0-2 for rotation keys, else -1.
  int rotationAxis = -1;
};

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
    if (sprite != nullptr && key == "sprite.size.w") {
      slot.number = &sprite->width;
    } else if (sprite != nullptr && key == "sprite.size.h") {
      slot.number = &sprite->height;
    } else if (sprite != nullptr && key.starts_with("sprite.tint.")) {
      slot.byte = colorByte(sprite->tint, last);
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
  if (slot.number != nullptr) {
    *value = *slot.number;
    return true;
  }
  if (slot.byte != nullptr) {
    *value = *slot.byte;
    return true;
  }
  return false;
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
  if (slot.number != nullptr) {
    *slot.number = static_cast<float>(value);
    return true;
  }
  if (slot.byte != nullptr) {
    *slot.byte =
      static_cast<unsigned char>(std::clamp(std::lround(value), 0l, 255l));
    return true;
  }
  return false;
}

static float*
resolveEnvironment(SceneEnvironment& environment, const std::string& key)
{
  const char last = key.empty() ? '\0' : key.back();
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
EditorInspector::buildFields(const EditorDocument& document,
                             const EditorSelection& selection)
{
  m_fields.clear();
  m_rows.clear();
  const std::function<void(const std::string&)> section =
    [this](const std::string& title) {
      InspectorRow row;
      row.label = title;
      row.section = true;
      m_rows.push_back(row);
    };
  const std::function<void(const std::string&, std::vector<InspectorField>)>
    row = [this](const std::string& label, std::vector<InspectorField> fields) {
      InspectorRow entry;
      entry.label = label;
      for (InspectorField& value : fields) {
        entry.fields.push_back(m_fields.size());
        m_fields.push_back(std::move(value));
      }
      m_rows.push_back(entry);
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

  if (primary == nullptr) {
    const SceneDocument& scene = document.scene().document();
    const SceneEnvironment& environment = scene.environment;
    section("Scene");
    row("Mode",
        { choice("scene.mode",
                 "Mode",
                 { "2D", "3D" },
                 scene.worldMode == SceneWorldMode::World3D ? 1 : 0) });
    row("Title", { text("meta.title", "Title", scene.metadata.title) });
    row("Author", { text("meta.author", "Author", scene.metadata.author) });
    section("Environment");
    row("Skybox", { text("env.skybox", "Skybox", environment.skybox) });
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
    row("Ambient",
        { environmentNumber("env.ambient.r", "R", 0.01f),
          environmentNumber("env.ambient.g", "G", 0.01f),
          environmentNumber("env.ambient.b", "B", 0.01f) });
    row("Sun", { toggle("env.sun", "Sun", environment.hasSun) });
    if (environment.hasSun) {
      row("Direction",
          { environmentNumber("env.sun.direction.x", "X", 0.01f),
            environmentNumber("env.sun.direction.y", "Y", 0.01f),
            environmentNumber("env.sun.direction.z", "Z", 0.01f) });
      row("Color",
          { environmentNumber("env.sun.color.r", "R", 0.01f),
            environmentNumber("env.sun.color.g", "G", 0.01f),
            environmentNumber("env.sun.color.b", "B", 0.01f) });
      row("Intensity",
          { environmentNumber("env.sun.intensity", "I", 0.01f),
            toggle("env.sun.shadows", "Shadows", environment.sun.shadows) });
    }
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
      row("Color",
          { number("primitive.color.r", "R", 1.0f),
            number("primitive.color.g", "G", 1.0f),
            number("primitive.color.b", "B", 1.0f),
            number("primitive.color.a", "A", 1.0f) });
      row("", { button("component.remove.primitive", "Remove") });
    } else if (const SceneMeshRenderer* mesh =
                 std::get_if<SceneMeshRenderer>(&component.value)) {
      section("Mesh");
      row("Asset", { text("mesh.asset", "Asset", mesh->asset) });
      row("Tint",
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
      row("Texture", { text("sprite.texture", "Texture", sprite->texture) });
      row("Size",
          { number("sprite.size.w", "W", 0.01f),
            number("sprite.size.h", "H", 0.01f) });
      row("Facing",
          { choice("sprite.facing",
                   "Facing",
                   { "world", "billboard" },
                   sprite->facing == SceneSpriteFacing::Billboard ? 1 : 0) });
      row("Tint",
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
      row("Color",
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
      section(opaque->type);
      InspectorField data;
      data.key = "opaque." + opaque->type;
      data.label = "Data";
      data.kind = InspectorFieldKind::ReadOnly;
      data.value = opaque->data;
      row("Data", { data });
    }
  }
  section("Add component");
  std::vector<InspectorField> adds;
  if (primary->find(SceneComponentType::Primitive) == nullptr) {
    adds.push_back(button("component.add.primitive", "Shape"));
  }
  if (primary->find(SceneComponentType::Light) == nullptr) {
    adds.push_back(button("component.add.light", "Light"));
  }
  if (primary->find(SceneComponentType::Camera) == nullptr) {
    adds.push_back(button("component.add.camera", "Camera"));
  }
  if (!adds.empty()) {
    row("", adds);
  }
}

// ---------------------------------------------------------------------------
// Applying edits.

bool
EditorInspector::applyNumber(const std::string& key,
                             double value,
                             bool relative,
                             const std::string& mergeKey,
                             EditorDocument& document,
                             const EditorSelection& selection)
{
  if (key.starts_with("env.")) {
    SceneEnvironment environment = document.scene().document().environment;
    float* slot = resolveEnvironment(environment, key);
    if (slot == nullptr) {
      return false;
    }
    *slot = static_cast<float>(relative ? *slot + value : value);
    return document.setEnvironment(environment, mergeKey);
  }
  const std::string label = "Set " + key;
  return document.editNodes(
    selection.ids(), label, mergeKey, [&key, value, relative](SceneNode& node) {
      double current = 0.0;
      if (!readNumber(node, key, &current)) {
        return false;
      }
      return writeNumber(node, key, relative ? current + value : value);
    });
}

bool
EditorInspector::applyText(const std::string& key,
                           const std::string& text,
                           EditorDocument& document,
                           const EditorSelection& selection)
{
  if (key == "name") {
    return document.setName(selection.primary(), text);
  }
  if (key == "meta.title" || key == "meta.author") {
    SceneMetadata metadata = document.scene().document().metadata;
    (key == "meta.title" ? metadata.title : metadata.author) = text;
    return document.setMetadata(metadata);
  }
  if (key == "env.skybox") {
    SceneEnvironment environment = document.scene().document().environment;
    environment.skybox = text;
    return document.setEnvironment(environment, {});
  }
  if (key == "mesh.asset" || key == "sprite.texture") {
    return document.editNodes(
      selection.ids(), "Set " + key, {}, [&key, &text](SceneNode& node) {
        if (key == "mesh.asset") {
          SceneMeshRenderer* mesh = componentOf<SceneMeshRenderer>(node);
          if (mesh != nullptr) {
            mesh->asset = text;
          }
          return mesh != nullptr;
        }
        SceneSprite* sprite = componentOf<SceneSprite>(node);
        if (sprite != nullptr) {
          sprite->texture = text;
        }
        return sprite != nullptr;
      });
  }
  double value = 0.0;
  if (!parseNumber(text, &value)) {
    return false;
  }
  return applyNumber(key, value, false, {}, document, selection);
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
  if (target.kind == InspectorFieldKind::Text ||
      target.kind == InspectorFieldKind::Number) {
    return beginEdit(target);
  }
  if (target.kind == InspectorFieldKind::Toggle) {
    const bool next = !target.toggle;
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
    const int next = (target.choice + (reverse ? count - 1 : 1)) % count;
    if (key == "scene.mode") {
      return document.setWorldMode(next == 1 ? SceneWorldMode::World3D
                                             : SceneWorldMode::World2D);
    }
    const std::string picked = target.choices[static_cast<size_t>(next)];
    return document.editNodes(
      selection.ids(),
      "Set " + key,
      {},
      [&key, &picked, next](SceneNode& node) {
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
  if (target.kind == InspectorFieldKind::Button) {
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
      return document.editNodes(
        selection.ids(), "Add " + kind, {}, [&kind](SceneNode& node) {
          SceneComponent component;
          if (kind == "primitive") {
            if (node.find(SceneComponentType::Primitive) != nullptr) {
              return false;
            }
            component.value = ScenePrimitive{};
          } else if (kind == "light") {
            if (node.find(SceneComponentType::Light) != nullptr) {
              return false;
            }
            component.value = SceneLight{};
          } else {
            if (node.find(SceneComponentType::Camera) != nullptr) {
              return false;
            }
            component.value = SceneCamera{};
          }
          node.components.push_back(component);
          return true;
        });
    }
  }
  return false;
}

bool
EditorInspector::beginEdit(const InspectorField& target)
{
  if (target.kind != InspectorFieldKind::Text &&
      target.kind != InspectorFieldKind::Number) {
    return false;
  }
  m_edit.begin(target.mixed ? std::string() : target.value);
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
  buildFields(*document, *selection);
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
  buildFields(*document, *selection);
  const InspectorField* target = field(key);
  if (target == nullptr || target->kind != InspectorFieldKind::Number) {
    return false;
  }
  if (m_scrubKey != key) {
    m_scrubKey = key;
    ++m_scrubSerial;
  }
  return applyNumber(key,
                     static_cast<double>(pixels * target->step),
                     true,
                     "scrub:" + std::to_string(m_scrubSerial),
                     *document,
                     *selection);
}

// ---------------------------------------------------------------------------
// Frame update, layout and drawing.

void
EditorInspector::layout()
{
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float rowHeight = std::max(20.0f, std::round(22.0f * fontScale));
  const float labelWidth = std::round(m_width * 0.34f);
  const float gap = std::round(3.0f * fontScale);
  float y = m_y + std::round(4.0f * fontScale) - m_scroll;
  for (InspectorRow& row : m_rows) {
    row.y = y;
    if (!row.fields.empty()) {
      const float startX =
        row.label.empty() ? m_x + 8.0f * fontScale : m_x + labelWidth;
      const float available = m_x + m_width - 8.0f * fontScale - startX;
      const float count = static_cast<float>(row.fields.size());
      const float fieldWidth = (available - gap * (count - 1.0f)) / count;
      for (size_t index = 0; index < row.fields.size(); ++index) {
        InspectorField& target = m_fields[row.fields[index]];
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
  buildFields(*document, *selection);
  if (m_edit.active() && field(m_editKey) == nullptr) {
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
    buildFields(*document, *selection);
  }
  layout();

  m_pointer.sample(m_placement, m_window, m_renderer, input);
  const float mouseX = m_pointer.x();
  const float mouseY = m_pointer.y();
  m_hoverField = hitTest(mouseX, mouseY);
  if (input != nullptr && containsScreenPoint(mouseX, mouseY)) {
    const float wheel = m_pointer.takeWheel();
    if (wheel != 0.0f) {
      const float maximum = std::max(0.0f, m_contentHeight - m_height + 8.0f);
      m_scroll = std::clamp(m_scroll - wheel * 40.0f, 0.0f, maximum);
      layout();
    }
  }

  if (m_pointer.clicked() && containsScreenPoint(mouseX, mouseY)) {
    m_consumedPress = true;
    const int hit = hitTest(mouseX, mouseY);
    if (m_edit.active() &&
        (hit < 0 || m_fields[static_cast<size_t>(hit)].key != m_editKey)) {
      changed = commitEdit(*document, *selection) || changed;
    }
    if (hit >= 0) {
      const InspectorField target = m_fields[static_cast<size_t>(hit)];
      if (target.kind == InspectorFieldKind::Number) {
        // Held and dragged, a number scrubs; clicked, it edits.
        m_scrubKey = target.key;
        m_scrubLastX = mouseX;
        m_scrubAccumulated = 0.0f;
        ++m_scrubSerial;
      } else if (!m_edit.active() || target.key != m_editKey) {
        const bool reverse = input != nullptr && input->isShiftPressed() &&
                             target.kind == InspectorFieldKind::Choice;
        changed = activate(target, reverse, *document, *selection) || changed;
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
        changed = applyNumber(target->key,
                              static_cast<double>(delta * target->step),
                              true,
                              "scrub:" + std::to_string(m_scrubSerial),
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
    buildFields(*document, *selection);
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
      GuiToolStyle::text(m_visual,
                         row.label,
                         m_x + GuiToolStyle::kPad,
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
      GuiToolStyle::fittedText(
        m_visual,
        row.label,
        m_x + GuiToolStyle::kPad,
        row.y + std::round((rowHeight - fontSize) * 0.5f) - 1.0f,
        std::round(m_width * 0.34f) - GuiToolStyle::kPad - 4.0f,
        fontSize,
        GuiToolPalette::dim);
    }
  }
  for (size_t index = 0; index < m_fields.size(); ++index) {
    const InspectorField& target = m_fields[index];
    if (target.y + rowHeight < m_y || target.y > m_y + m_height) {
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
      case InspectorFieldKind::Toggle: {
        m_visual.addFilledRect(target.x,
                               target.y,
                               target.width,
                               h,
                               target.toggle ? GuiToolPalette::selection
                                             : GuiToolPalette::field);
        m_visual.addOutlineRect(target.x,
                                target.y,
                                target.width,
                                h,
                                target.toggle || hovered
                                  ? GuiToolPalette::accent
                                  : GuiToolPalette::border,
                                1.0f);
        GuiKit::drawTextCentered(m_visual,
                                 target.label +
                                   (target.toggle ? " on" : " off"),
                                 target.x + target.width * 0.5f,
                                 target.y + h * 0.5f,
                                 fontSize,
                                 GuiToolPalette::text);
        break;
      }
      case InspectorFieldKind::Choice:
      case InspectorFieldKind::Button: {
        const bool isButton = target.kind == InspectorFieldKind::Button;
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
}
bool
EditorInspector::AppendCommands(Renderer* renderer)
{
  if (!m_visible) {
    return true;
  }
  return m_visual.AppendCommands(renderer);
}
