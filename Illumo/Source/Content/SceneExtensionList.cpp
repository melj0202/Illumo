#include <Illumo/Content/SceneExtensionList.h>
#include <algorithm>
#include <nlohmann/json.hpp>

using Json = nlohmann::json;

std::vector<std::string>
SceneExtensionList::read(const std::vector<SceneExtension>& extensions,
                         std::string_view key,
                         std::string_view member)
{
  std::vector<std::string> values;
  for (const SceneExtension& extension : extensions) {
    if (extension.key != key) {
      continue;
    }
    const Json data = Json::parse(extension.data, nullptr, false);
    const std::string name(member);
    if (!data.is_object() || !data.contains(name) || !data[name].is_array()) {
      return values;
    }
    for (const Json& entry : data[name]) {
      if (entry.is_string()) {
        values.push_back(entry.get<std::string>());
      }
    }
    return values;
  }
  return values;
}

// Replaces one member of the extension with `value` (null removes it),
// keeping the others; the extension goes when nothing is left in it.
static void
writeMember(std::vector<SceneExtension>& extensions,
            std::string_view key,
            std::string_view member,
            Json value)
{
  std::vector<SceneExtension>::iterator found = std::find_if(
    extensions.begin(), extensions.end(), [key](const SceneExtension& entry) {
      return entry.key == key;
    });
  Json data = Json::object();
  if (found != extensions.end()) {
    Json parsed = Json::parse(found->data, nullptr, false);
    if (parsed.is_object()) {
      data = std::move(parsed);
    }
  }
  const std::string name(member);
  data.erase(name);
  if (!value.is_null()) {
    data[name] = std::move(value);
  }
  if (data.empty()) {
    if (found != extensions.end()) {
      extensions.erase(found);
    }
    return;
  }
  // nlohmann's object keeps keys sorted: the codec's canonical form.
  const std::string text =
    data.dump(-1, ' ', false, Json::error_handler_t::replace);
  if (found != extensions.end()) {
    found->data = text;
    return;
  }
  extensions.push_back({ std::string(key), text });
}

void
SceneExtensionList::write(std::vector<SceneExtension>& extensions,
                          std::string_view key,
                          std::string_view member,
                          const std::vector<std::string>& values)
{
  writeMember(extensions, key, member, values.empty() ? Json() : Json(values));
}

std::string
SceneExtensionText::read(const std::vector<SceneExtension>& extensions,
                         std::string_view key,
                         std::string_view member)
{
  for (const SceneExtension& extension : extensions) {
    if (extension.key != key) {
      continue;
    }
    const Json data = Json::parse(extension.data, nullptr, false);
    const std::string name(member);
    if (!data.is_object() || !data.contains(name) || !data[name].is_string()) {
      return {};
    }
    return data[name].get<std::string>();
  }
  return {};
}

void
SceneExtensionText::write(std::vector<SceneExtension>& extensions,
                          std::string_view key,
                          std::string_view member,
                          const std::string& value)
{
  writeMember(extensions, key, member, value.empty() ? Json() : Json(value));
}

bool
SceneExtensionText::remove(std::vector<SceneExtension>& extensions,
                           std::string_view key)
{
  const std::vector<SceneExtension>::iterator found = std::find_if(
    extensions.begin(), extensions.end(), [key](const SceneExtension& entry) {
      return entry.key == key;
    });
  if (found == extensions.end()) {
    return false;
  }
  extensions.erase(found);
  return true;
}
