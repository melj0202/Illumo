#include <Illumo/Content/PackageManifest.h>
#include <Illumo/Content/ScenePlay.h>
#include <Illumo/Content/VirtualPath.h>
#include <algorithm>
#include <nlohmann/json.hpp>

using Json = nlohmann::json;

static Json
parseObject(const std::string& data)
{
  Json parsed = Json::parse(data, nullptr, false);
  return parsed.is_object() ? parsed : Json::object();
}

bool
ScenePlay::read(const std::vector<SceneExtension>& extensions, ScenePlay* play)
{
  *play = ScenePlay{};
  for (const SceneExtension& extension : extensions) {
    if (extension.key != kExtension) {
      continue;
    }
    const Json data = parseObject(extension.data);
    if (data.contains("app") && data["app"].is_string() &&
        validPackageId(data["app"].get<std::string>())) {
      play->application = data["app"].get<std::string>();
    }
    if (data.contains("root") && data["root"].is_string()) {
      const std::string root = data["root"].get<std::string>();
      std::string normalized;
      if (!root.empty() && root.front() == '/' &&
          VirtualPath::normalize(root, normalized) && normalized == root) {
        play->root = root;
      }
    }
    return true;
  }
  return false;
}

void
ScenePlay::write(std::vector<SceneExtension>& extensions, const ScenePlay& play)
{
  std::vector<SceneExtension>::iterator found = std::find_if(
    extensions.begin(), extensions.end(), [](const SceneExtension& extension) {
      return extension.key == kExtension;
    });
  Json data =
    found != extensions.end() ? parseObject(found->data) : Json::object();
  data.erase("app");
  data.erase("root");
  if (!play.application.empty()) {
    data["app"] = play.application;
  }
  if (!play.root.empty()) {
    data["root"] = play.root;
  }
  if (data.empty()) {
    if (found != extensions.end()) {
      extensions.erase(found);
    }
    return;
  }
  // nlohmann's object keeps keys sorted, so this is the codec's canonical
  // form.
  const std::string text =
    data.dump(-1, ' ', false, Json::error_handler_t::replace);
  if (found != extensions.end()) {
    found->data = text;
    return;
  }
  extensions.push_back({ kExtension, text });
}
