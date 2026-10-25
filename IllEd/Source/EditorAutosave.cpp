#include "EditorAutosave.h"

#include <Illumo/Content/SceneExtensionList.h>
#include <ctime>

std::string
EditorAutosaveRecord::slotName(int slot)
{
  return "autosave-" + std::to_string(slot) + ".ilsc";
}

bool
EditorAutosaveRecord::read(const SceneDocument& document)
{
  bool found = false;
  for (const SceneExtension& extension : document.extensions) {
    if (extension.key == kExtension) {
      found = true;
    }
  }
  if (!found) {
    return false;
  }
  label = SceneExtensionText::read(document.extensions, kExtension, "label");
  location =
    SceneExtensionText::read(document.extensions, kExtension, "location");
  root = SceneExtensionText::read(document.extensions, kExtension, "root");
  savedAt = SceneExtensionText::read(document.extensions, kExtension, "saved");
  return true;
}

void
EditorAutosaveRecord::write(SceneDocument& document) const
{
  SceneExtensionText::remove(document.extensions, kExtension);
  // The label is always written, so the extension exists even for an
  // untitled document.
  SceneExtensionText::write(
    document.extensions, kExtension, "label", label.empty() ? "-" : label);
  SceneExtensionText::write(
    document.extensions, kExtension, "location", location);
  SceneExtensionText::write(document.extensions, kExtension, "root", root);
  SceneExtensionText::write(document.extensions, kExtension, "saved", savedAt);
}

void
EditorAutosaveRecord::strip(SceneDocument& document)
{
  SceneExtensionText::remove(document.extensions, kExtension);
}

std::string
EditorAutosaveRecord::now()
{
  const std::time_t seconds = std::time(nullptr);
  std::tm parts{};
#if defined(_WIN32)
  if (localtime_s(&parts, &seconds) != 0) {
    return {};
  }
#else
  // Guests have no time zone; UTC is what they get from localtime too.
  const std::tm* local = std::localtime(&seconds);
  if (local == nullptr) {
    return {};
  }
  parts = *local;
#endif
  char text[32];
  if (std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &parts) == 0) {
    return {};
  }
  return text;
}
