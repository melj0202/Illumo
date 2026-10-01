#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Field descriptions of scene behaviours: the `behaviours.json` file a game
// package ships beside `illumo.json` (format "illumo-behaviours" 1). A
// behaviour is a namespaced scene component ("playground.spinner"); its
// description lists typed fields, so editors show typed inspector fields and
// games read typed values from the component's JSON. See
// docs/scene-behaviours-design.md.

enum class BehaviourFieldKind
{
  Number,
  Integer,
  Bool,
  Text,
  Color,
  Vector3,
  // One of the field's options.
  Choice,
  // An asset id of the scene's asset table.
  Asset,
  // A node id of the same scene.
  Node
};

const char*
behaviourFieldKindName(BehaviourFieldKind kind);

// One value of any field kind; the members a kind does not use stay at their
// defaults. Integer values are whole numbers held in `number`; Text, Choice,
// Asset and Node values are held in `text`.
struct BehaviourValue
{
  BehaviourFieldKind kind = BehaviourFieldKind::Number;
  double number = 0.0;
  bool flag = false;
  std::string text;
  Vector3 vector{ 0.0f };
  ColorRgba color{ 255, 255, 255, 255 };
};

bool
behaviourValuesEqual(const BehaviourValue& a, const BehaviourValue& b);

struct BehaviourField
{
  std::string name;
  // What editors show; the name when the file gives none.
  std::string title;
  BehaviourFieldKind kind = BehaviourFieldKind::Number;
  BehaviourValue defaultValue;
  // Number and Integer limits; values outside them are clamped.
  bool hasMinimum = false;
  double minimum = 0.0;
  bool hasMaximum = false;
  double maximum = 0.0;
  // Choice options, at least one.
  std::vector<std::string> options;
  // Asset types an Asset field accepts; empty accepts any.
  std::vector<SceneAssetType> assetTypes;
};

struct BehaviourType
{
  // The namespaced component type ("vendor.name").
  std::string type;
  std::string title;
  std::vector<BehaviourField> fields;

  const BehaviourField* field(std::string_view name) const;
};

// The values of one behaviour component, in field order.
class BehaviourValues
{
public:
  const BehaviourValue* find(std::string_view name) const;
  double number(std::string_view name, double fallback = 0.0) const;
  int integer(std::string_view name, int fallback = 0) const;
  bool flag(std::string_view name, bool fallback = false) const;
  // Text, Choice, Asset and Node values; empty when the field is missing.
  const std::string& text(std::string_view name) const;
  Vector3 vector(std::string_view name, Vector3 fallback = Vector3(0.0f)) const;
  ColorRgba color(std::string_view name,
                  ColorRgba fallback = ColorRgba{ 255, 255, 255, 255 }) const;
  // Replaces the value of a field, or appends it.
  void set(std::string_view name, const BehaviourValue& value);
  const std::vector<std::pair<std::string, BehaviourValue>>& entries() const
  {
    return m_entries;
  }

private:
  std::vector<std::pair<std::string, BehaviourValue>> m_entries;
};

class BehaviourSchema
{
public:
  static constexpr const char* kFileName = "behaviours.json";
  static constexpr int kFormatVersion = 1;
  static constexpr std::size_t kMaximumFileBytes = 1024u * 1024u;
  static constexpr std::size_t kMaximumTypes = 1024;
  static constexpr std::size_t kMaximumFields = 64;
  static constexpr std::size_t kMaximumOptions = 256;

  // Parses a behaviours.json strictly (unknown keys, wrong kinds, duplicate
  // types or fields and defaults that do not fit their field all fail).
  // Returns false with a message naming the first problem and leaves the
  // schema unchanged.
  bool parse(std::string_view text, std::string& error);

  const std::vector<BehaviourType>& types() const { return m_types; }
  const BehaviourType* find(std::string_view type) const;
  bool empty() const { return m_types.empty(); }
  // Adds another schema's types; a type already present is kept and its
  // duplicate reported in `conflicts`.
  void merge(const BehaviourSchema& other, std::vector<std::string>* conflicts);
  void clear() { m_types.clear(); }

  // A component's values: every field's default, replaced by the members of
  // `data` (a JSON object) that fit their field. Members of the wrong kind
  // are ignored and numbers outside a field's range are clamped, each with a
  // message in `warnings`; unknown members are ignored silently.
  static BehaviourValues decode(const BehaviourType& type,
                                std::string_view data,
                                std::vector<std::string>* warnings);
  // Canonical component data (compact JSON, keys sorted, as IlscCodec keeps
  // opaque data) holding every field of `values` that `type` describes, plus
  // the members of `previous` the type does not describe.
  static std::string encode(const BehaviourType& type,
                            const BehaviourValues& values,
                            std::string_view previous = "{}");
  // A component of this type holding every field's default.
  static SceneOpaqueComponent defaultComponent(const BehaviourType& type);

private:
  std::vector<BehaviourType> m_types;
};
