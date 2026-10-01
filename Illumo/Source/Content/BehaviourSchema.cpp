#include <Illumo/Content/BehaviourSchema.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <nlohmann/json.hpp>

using Json = nlohmann::json;

const char*
behaviourFieldKindName(BehaviourFieldKind kind)
{
  switch (kind) {
    case BehaviourFieldKind::Number:
      return "number";
    case BehaviourFieldKind::Integer:
      return "integer";
    case BehaviourFieldKind::Bool:
      return "bool";
    case BehaviourFieldKind::Text:
      return "text";
    case BehaviourFieldKind::Color:
      return "color";
    case BehaviourFieldKind::Vector3:
      return "vector3";
    case BehaviourFieldKind::Choice:
      return "choice";
    case BehaviourFieldKind::Asset:
      return "asset";
    case BehaviourFieldKind::Node:
      return "node";
  }
  return "number";
}

static bool
parseFieldKind(const std::string& name, BehaviourFieldKind* kind)
{
  const BehaviourFieldKind all[] = {
    BehaviourFieldKind::Number, BehaviourFieldKind::Integer,
    BehaviourFieldKind::Bool,   BehaviourFieldKind::Text,
    BehaviourFieldKind::Color,  BehaviourFieldKind::Vector3,
    BehaviourFieldKind::Choice, BehaviourFieldKind::Asset,
    BehaviourFieldKind::Node
  };
  for (const BehaviourFieldKind candidate : all) {
    if (name == behaviourFieldKindName(candidate)) {
      *kind = candidate;
      return true;
    }
  }
  return false;
}

static bool
parseAssetType(const std::string& name, SceneAssetType* type)
{
  const SceneAssetType all[] = { SceneAssetType::Mesh,
                                 SceneAssetType::Texture,
                                 SceneAssetType::Atlas,
                                 SceneAssetType::CubemapCross,
                                 SceneAssetType::CubemapFaces };
  for (const SceneAssetType candidate : all) {
    if (name == sceneAssetTypeName(candidate)) {
      *type = candidate;
      return true;
    }
  }
  return false;
}

bool
behaviourValuesEqual(const BehaviourValue& a, const BehaviourValue& b)
{
  return a.kind == b.kind && a.number == b.number && a.flag == b.flag &&
         a.text == b.text && a.vector == b.vector && a.color.r == b.color.r &&
         a.color.g == b.color.g && a.color.b == b.color.b &&
         a.color.a == b.color.a;
}

// ---------------------------------------------------------------------------
// Values.

const BehaviourField*
BehaviourType::field(std::string_view name) const
{
  for (const BehaviourField& candidate : fields) {
    if (candidate.name == name) {
      return &candidate;
    }
  }
  return nullptr;
}

const BehaviourValue*
BehaviourValues::find(std::string_view name) const
{
  for (const std::pair<std::string, BehaviourValue>& entry : m_entries) {
    if (entry.first == name) {
      return &entry.second;
    }
  }
  return nullptr;
}

double
BehaviourValues::number(std::string_view name, double fallback) const
{
  const BehaviourValue* value = find(name);
  return value != nullptr && (value->kind == BehaviourFieldKind::Number ||
                              value->kind == BehaviourFieldKind::Integer)
           ? value->number
           : fallback;
}

int
BehaviourValues::integer(std::string_view name, int fallback) const
{
  const BehaviourValue* value = find(name);
  return value != nullptr && (value->kind == BehaviourFieldKind::Number ||
                              value->kind == BehaviourFieldKind::Integer)
           ? static_cast<int>(
               std::lround(std::clamp(value->number, -2.0e9, 2.0e9)))
           : fallback;
}

bool
BehaviourValues::flag(std::string_view name, bool fallback) const
{
  const BehaviourValue* value = find(name);
  return value != nullptr && value->kind == BehaviourFieldKind::Bool
           ? value->flag
           : fallback;
}

const std::string&
BehaviourValues::text(std::string_view name) const
{
  static const std::string empty;
  const BehaviourValue* value = find(name);
  return value != nullptr ? value->text : empty;
}

Vector3
BehaviourValues::vector(std::string_view name, Vector3 fallback) const
{
  const BehaviourValue* value = find(name);
  return value != nullptr && value->kind == BehaviourFieldKind::Vector3
           ? value->vector
           : fallback;
}

ColorRgba
BehaviourValues::color(std::string_view name, ColorRgba fallback) const
{
  const BehaviourValue* value = find(name);
  return value != nullptr && value->kind == BehaviourFieldKind::Color
           ? value->color
           : fallback;
}

void
BehaviourValues::set(std::string_view name, const BehaviourValue& value)
{
  for (std::pair<std::string, BehaviourValue>& entry : m_entries) {
    if (entry.first == name) {
      entry.second = value;
      return;
    }
  }
  m_entries.emplace_back(std::string(name), value);
}

// ---------------------------------------------------------------------------
// JSON helpers. Every read checks the JSON type first: the build has no
// exceptions, so a mistyped get<> would end the process.

static bool
onlyKeys(const Json& object,
         std::initializer_list<const char*> allowed,
         const std::string& where,
         std::string& error)
{
  if (!object.is_object()) {
    error = where + " must be an object";
    return false;
  }
  for (Json::const_iterator item = object.begin(); item != object.end();
       ++item) {
    bool known = false;
    for (const char* name : allowed) {
      known = known || item.key() == name;
    }
    if (!known) {
      error = where + " has an unknown key \"" + item.key() + "\"";
      return false;
    }
  }
  return true;
}

static bool
readText(const Json& object, const char* key, std::string* value)
{
  if (!object.contains(key)) {
    return true;
  }
  const Json& item = object[key];
  if (!item.is_string()) {
    return false;
  }
  *value = item.get<std::string>();
  return true;
}

static bool
finiteNumber(const Json& item, double* value)
{
  if (!item.is_number()) {
    return false;
  }
  const double number = item.get<double>();
  if (!std::isfinite(number)) {
    return false;
  }
  *value = number;
  return true;
}

static bool
plainName(const std::string& name)
{
  if (name.empty() || name.size() > 64 || name.front() < 'a' ||
      name.front() > 'z' || name == "type") {
    return false;
  }
  for (const char character : name) {
    const bool allowed = (character >= 'a' && character <= 'z') ||
                         (character >= '0' && character <= '9') ||
                         character == '_';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

static bool
singleLine(const std::string& text, std::size_t maximum)
{
  return text.size() <= maximum &&
         text.find_first_of("\r\n") == std::string::npos;
}

// Reads a JSON value as a field's kind; false when it does not fit (wrong
// type, a non-integer Integer, an unknown option). Ranges are not applied.
static bool
readValue(const BehaviourField& field, const Json& item, BehaviourValue* value)
{
  BehaviourValue result;
  result.kind = field.kind;
  switch (field.kind) {
    case BehaviourFieldKind::Number:
      if (!finiteNumber(item, &result.number)) {
        return false;
      }
      break;
    case BehaviourFieldKind::Integer:
      if (!finiteNumber(item, &result.number) ||
          result.number != std::floor(result.number) ||
          std::fabs(result.number) > 2.0e9) {
        return false;
      }
      break;
    case BehaviourFieldKind::Bool:
      if (!item.is_boolean()) {
        return false;
      }
      result.flag = item.get<bool>();
      break;
    case BehaviourFieldKind::Text:
    case BehaviourFieldKind::Asset:
    case BehaviourFieldKind::Node:
      if (!item.is_string()) {
        return false;
      }
      result.text = item.get<std::string>();
      if (!singleLine(result.text, 1024)) {
        return false;
      }
      break;
    case BehaviourFieldKind::Choice:
      if (!item.is_string()) {
        return false;
      }
      result.text = item.get<std::string>();
      if (std::find(field.options.begin(), field.options.end(), result.text) ==
          field.options.end()) {
        return false;
      }
      break;
    case BehaviourFieldKind::Vector3: {
      if (!item.is_array() || item.size() != 3) {
        return false;
      }
      for (std::size_t axis = 0; axis < 3; ++axis) {
        double component = 0.0;
        if (!finiteNumber(item[axis], &component)) {
          return false;
        }
        result.vector[static_cast<int>(axis)] = static_cast<float>(component);
      }
      break;
    }
    case BehaviourFieldKind::Color: {
      if (!item.is_array() || item.size() != 4) {
        return false;
      }
      unsigned char channels[4] = { 0, 0, 0, 0 };
      for (std::size_t channel = 0; channel < 4; ++channel) {
        double component = 0.0;
        if (!finiteNumber(item[channel], &component) || component < 0.0 ||
            component > 255.0 || component != std::floor(component)) {
          return false;
        }
        channels[channel] = static_cast<unsigned char>(component);
      }
      result.color =
        ColorRgba{ channels[0], channels[1], channels[2], channels[3] };
      break;
    }
  }
  *value = result;
  return true;
}

static Json
writeValue(const BehaviourValue& value)
{
  switch (value.kind) {
    case BehaviourFieldKind::Number:
      return Json(value.number);
    case BehaviourFieldKind::Integer:
      return Json(static_cast<long long>(std::llround(value.number)));
    case BehaviourFieldKind::Bool:
      return Json(value.flag);
    case BehaviourFieldKind::Text:
    case BehaviourFieldKind::Choice:
    case BehaviourFieldKind::Asset:
    case BehaviourFieldKind::Node:
      return Json(value.text);
    case BehaviourFieldKind::Vector3:
      return Json::array({ static_cast<double>(value.vector.x),
                           static_cast<double>(value.vector.y),
                           static_cast<double>(value.vector.z) });
    case BehaviourFieldKind::Color:
      return Json::array({ static_cast<int>(value.color.r),
                           static_cast<int>(value.color.g),
                           static_cast<int>(value.color.b),
                           static_cast<int>(value.color.a) });
  }
  return Json();
}

static BehaviourValue
emptyDefault(const BehaviourField& field)
{
  BehaviourValue value;
  value.kind = field.kind;
  if (field.kind == BehaviourFieldKind::Choice && !field.options.empty()) {
    value.text = field.options.front();
  }
  return value;
}

// Clamps a number into the field's range; true when it changed.
static bool
clampToRange(const BehaviourField& field, BehaviourValue* value)
{
  if (field.kind != BehaviourFieldKind::Number &&
      field.kind != BehaviourFieldKind::Integer) {
    return false;
  }
  const double before = value->number;
  if (field.hasMinimum) {
    value->number = std::max(value->number, field.minimum);
  }
  if (field.hasMaximum) {
    value->number = std::min(value->number, field.maximum);
  }
  return value->number != before;
}

// ---------------------------------------------------------------------------
// Parsing.

static bool
parseField(const Json& entry,
           const std::string& where,
           BehaviourField* field,
           std::string& error)
{
  if (!onlyKeys(entry,
                { "name",
                  "title",
                  "kind",
                  "default",
                  "min",
                  "max",
                  "options",
                  "asset_types" },
                where,
                error)) {
    return false;
  }
  std::string kind;
  if (!readText(entry, "name", &field->name) || !plainName(field->name)) {
    error = where + " needs a name of 1-64 lowercase letters, digits or _ "
                    "that starts with a letter and is not \"type\"";
    return false;
  }
  const std::string at = where + " \"" + field->name + "\"";
  if (!readText(entry, "kind", &kind) || !parseFieldKind(kind, &field->kind)) {
    error = at + " needs a kind: number, integer, bool, text, color, "
                 "vector3, choice, asset or node";
    return false;
  }
  if (!readText(entry, "title", &field->title) ||
      !singleLine(field->title, 128)) {
    error = at + " title must be a single line of at most 128 bytes";
    return false;
  }
  if (field->title.empty()) {
    field->title = field->name;
  }
  if (entry.contains("options")) {
    const Json& options = entry["options"];
    if (field->kind != BehaviourFieldKind::Choice || !options.is_array() ||
        options.empty() || options.size() > BehaviourSchema::kMaximumOptions) {
      error = at + " options belong to a choice field: a list of 1-256 "
                   "strings";
      return false;
    }
    for (const Json& option : options) {
      if (!option.is_string() || option.get<std::string>().empty() ||
          !singleLine(option.get<std::string>(), 128) ||
          std::find(field->options.begin(),
                    field->options.end(),
                    option.get<std::string>()) != field->options.end()) {
        error = at + " options must be unique non-empty single lines";
        return false;
      }
      field->options.push_back(option.get<std::string>());
    }
  }
  if (field->kind == BehaviourFieldKind::Choice && field->options.empty()) {
    error = at + " is a choice and needs options";
    return false;
  }
  if (entry.contains("asset_types")) {
    const Json& types = entry["asset_types"];
    if (field->kind != BehaviourFieldKind::Asset || !types.is_array()) {
      error = at + " asset_types belong to an asset field: a list of asset "
                   "types";
      return false;
    }
    for (const Json& type : types) {
      SceneAssetType parsed = SceneAssetType::Texture;
      if (!type.is_string() ||
          !parseAssetType(type.get<std::string>(), &parsed)) {
        error = at + " names an unknown asset type";
        return false;
      }
      field->assetTypes.push_back(parsed);
    }
  }
  const bool numeric = field->kind == BehaviourFieldKind::Number ||
                       field->kind == BehaviourFieldKind::Integer;
  if ((entry.contains("min") || entry.contains("max")) && !numeric) {
    error = at + " min and max belong to number and integer fields";
    return false;
  }
  if (entry.contains("min")) {
    field->hasMinimum = finiteNumber(entry["min"], &field->minimum);
  }
  if (entry.contains("max")) {
    field->hasMaximum = finiteNumber(entry["max"], &field->maximum);
  }
  if ((entry.contains("min") && !field->hasMinimum) ||
      (entry.contains("max") && !field->hasMaximum) ||
      (field->hasMinimum && field->hasMaximum &&
       field->minimum > field->maximum)) {
    error = at + " min and max must be numbers with min <= max";
    return false;
  }
  field->defaultValue = emptyDefault(*field);
  if (entry.contains("default")) {
    BehaviourValue value;
    if (!readValue(*field, entry["default"], &value)) {
      error = at + " default does not fit its kind";
      return false;
    }
    BehaviourValue clamped = value;
    if (clampToRange(*field, &clamped)) {
      error = at + " default lies outside min and max";
      return false;
    }
    field->defaultValue = value;
  }
  return true;
}

bool
BehaviourSchema::parse(std::string_view text, std::string& error)
{
  if (text.size() > kMaximumFileBytes) {
    error = "behaviours.json is larger than 1 MiB";
    return false;
  }
  const Json document = Json::parse(text.begin(), text.end(), nullptr, false);
  if (document.is_discarded()) {
    error = "behaviours.json is not valid JSON";
    return false;
  }
  if (!onlyKeys(document,
                { "format", "format_version", "behaviours" },
                "behaviours.json",
                error)) {
    return false;
  }
  std::string format;
  double version = 0.0;
  if (!readText(document, "format", &format) || format != "illumo-behaviours") {
    error = "behaviours.json needs \"format\": \"illumo-behaviours\"";
    return false;
  }
  if (!document.contains("format_version") ||
      !finiteNumber(document["format_version"], &version) ||
      version != static_cast<double>(kFormatVersion)) {
    error = "behaviours.json needs format_version 1";
    return false;
  }
  if (!document.contains("behaviours") || !document["behaviours"].is_array() ||
      document["behaviours"].size() > kMaximumTypes) {
    error = "behaviours.json needs a behaviours list of at most 1024 types";
    return false;
  }
  std::vector<BehaviourType> types;
  for (const Json& entry : document["behaviours"]) {
    BehaviourType type;
    if (!onlyKeys(entry, { "type", "title", "fields" }, "a behaviour", error)) {
      return false;
    }
    if (!readText(entry, "type", &type.type) ||
        !validSceneNamespace(type.type)) {
      error = "a behaviour needs a namespaced type (vendor.name)";
      return false;
    }
    const std::string where = "behaviour \"" + type.type + "\"";
    for (const BehaviourType& earlier : types) {
      if (earlier.type == type.type) {
        error = where + " is listed twice";
        return false;
      }
    }
    if (!readText(entry, "title", &type.title) ||
        !singleLine(type.title, 128)) {
      error = where + " title must be a single line of at most 128 bytes";
      return false;
    }
    if (type.title.empty()) {
      type.title = type.type;
    }
    if (entry.contains("fields")) {
      const Json& fields = entry["fields"];
      if (!fields.is_array() || fields.size() > kMaximumFields) {
        error = where + " fields must be a list of at most 64 fields";
        return false;
      }
      for (const Json& item : fields) {
        BehaviourField field;
        if (!parseField(item, where + " field", &field, error)) {
          return false;
        }
        if (type.field(field.name) != nullptr) {
          error = where + " field \"" + field.name + "\" is listed twice";
          return false;
        }
        type.fields.push_back(std::move(field));
      }
    }
    types.push_back(std::move(type));
  }
  m_types = std::move(types);
  error.clear();
  return true;
}

const BehaviourType*
BehaviourSchema::find(std::string_view type) const
{
  for (const BehaviourType& candidate : m_types) {
    if (candidate.type == type) {
      return &candidate;
    }
  }
  return nullptr;
}

void
BehaviourSchema::merge(const BehaviourSchema& other,
                       std::vector<std::string>* conflicts)
{
  for (const BehaviourType& type : other.m_types) {
    if (find(type.type) != nullptr) {
      if (conflicts != nullptr) {
        conflicts->push_back("behaviour \"" + type.type +
                             "\" is described twice; the first is kept");
      }
      continue;
    }
    m_types.push_back(type);
  }
}

BehaviourValues
BehaviourSchema::decode(const BehaviourType& type,
                        std::string_view data,
                        std::vector<std::string>* warnings)
{
  BehaviourValues values;
  for (const BehaviourField& field : type.fields) {
    values.set(field.name, field.defaultValue);
  }
  const Json object = Json::parse(data.begin(), data.end(), nullptr, false);
  if (object.is_discarded() || !object.is_object()) {
    if (warnings != nullptr) {
      warnings->push_back(type.type + ": component data is not a JSON "
                                      "object; using defaults");
    }
    return values;
  }
  for (const BehaviourField& field : type.fields) {
    if (!object.contains(field.name)) {
      continue;
    }
    BehaviourValue value;
    if (!readValue(field, object[field.name], &value)) {
      if (warnings != nullptr) {
        warnings->push_back(
          type.type + ": \"" + field.name + "\" is not a valid " +
          behaviourFieldKindName(field.kind) + "; using its default");
      }
      continue;
    }
    if (clampToRange(field, &value) && warnings != nullptr) {
      warnings->push_back(type.type + ": \"" + field.name +
                          "\" was outside its range and is clamped");
    }
    values.set(field.name, value);
  }
  return values;
}

std::string
BehaviourSchema::encode(const BehaviourType& type,
                        const BehaviourValues& values,
                        std::string_view previous)
{
  Json object = Json::parse(previous.begin(), previous.end(), nullptr, false);
  if (object.is_discarded() || !object.is_object()) {
    object = Json::object();
  }
  object.erase("type");
  for (const BehaviourField& field : type.fields) {
    const BehaviourValue* value = values.find(field.name);
    object[field.name] = writeValue(
      value != nullptr && value->kind == field.kind ? *value
                                                    : field.defaultValue);
  }
  // Matches IlscCodec's canonical opaque data: sorted keys, compact.
  return object.dump(-1, ' ', false, Json::error_handler_t::replace);
}

SceneOpaqueComponent
BehaviourSchema::defaultComponent(const BehaviourType& type)
{
  SceneOpaqueComponent component;
  component.type = type.type;
  component.data = encode(type, BehaviourValues{}, "{}");
  return component;
}
