#include "LinuxGtk.h"
#include <Illumo/Platform/SaveLoad.h>

#include <cctype>
#include <filesystem>
#include <gtkmm.h>
#include <string>
#include <system_error>

struct LinuxWorkingDirectoryGuard
{
  std::filesystem::path original;
  bool valid = false;

  LinuxWorkingDirectoryGuard()
  {
    std::error_code error;
    original = std::filesystem::current_path(error);
    valid = !error;
  }

  ~LinuxWorkingDirectoryGuard()
  {
    if (valid) {
      std::error_code error;
      std::filesystem::current_path(original, error);
    }
  }

  LinuxWorkingDirectoryGuard(const LinuxWorkingDirectoryGuard&) = delete;
  LinuxWorkingDirectoryGuard& operator=(const LinuxWorkingDirectoryGuard&) =
    delete;
  LinuxWorkingDirectoryGuard(LinuxWorkingDirectoryGuard&&) = delete;
  LinuxWorkingDirectoryGuard& operator=(LinuxWorkingDirectoryGuard&&) = delete;
};

static void
addFilterPatterns(const Glib::RefPtr<Gtk::FileFilter>& filter,
                  const std::string& extensionPattern)
{
  std::size_t start = 0;
  while (start <= extensionPattern.size()) {
    const std::size_t separator = extensionPattern.find(';', start);
    const std::size_t end =
      separator == std::string::npos ? extensionPattern.size() : separator;
    const std::string pattern = extensionPattern.substr(start, end - start);
    if (!pattern.empty()) {
      filter->add_pattern(pattern);
      std::string lowered = pattern;
      for (std::size_t index = 0; index < lowered.size(); ++index) {
        lowered[index] = static_cast<char>(
          std::tolower(static_cast<unsigned char>(lowered[index])));
      }
      if (lowered != pattern) {
        filter->add_pattern(lowered);
      }
    }
    if (separator == std::string::npos) {
      break;
    }
    start = separator + 1;
  }
}

static std::string
chooseFile(const SaveLoadDialogSpec& specification, bool save)
{
  LinuxWorkingDirectoryGuard cwdGuard;
  if (!EnsureLinuxGtkInitialized()) {
    return std::string();
  }

  const std::string description = specification.fileDescription.empty()
                                    ? "Illumo File Format"
                                    : specification.fileDescription;
  const std::string pattern = specification.extensionPattern.empty()
                                ? "*.ILLUMO"
                                : specification.extensionPattern;
  const std::string defaultFilename = specification.defaultFilename.empty()
                                        ? "MyCanvas.illumo"
                                        : specification.defaultFilename;

  try {
    Gtk::FileChooserDialog dialog(save ? "Save File" : "Open File",
                                  save ? Gtk::FILE_CHOOSER_ACTION_SAVE
                                       : Gtk::FILE_CHOOSER_ACTION_OPEN);
    dialog.set_modal(true);
    dialog.set_keep_above(true);
    dialog.set_local_only(true);
    dialog.set_select_multiple(false);
    dialog.set_do_overwrite_confirmation(save);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button(save ? "_Save" : "_Open", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);
    dialog.set_current_name(
      std::filesystem::path(defaultFilename).filename().string());

    Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();
    filter->set_name(description + " (" + pattern + ")");
    addFilterPatterns(filter, pattern);
    dialog.add_filter(filter);

    const int response = dialog.run();
    dialog.hide();
    if (response != Gtk::RESPONSE_OK) {
      return std::string();
    }
    const Glib::ustring filename = dialog.get_filename();
    if (filename.empty()) {
      return std::string();
    }
    return std::string(filename);
  } catch (...) {
    return std::string();
  }
}

std::string
SaveLoad::GetLoadLocation(const SaveLoadDialogSpec& specification)
{
  return chooseFile(specification, false);
}

std::string
SaveLoad::GetSaveLocation(const SaveLoadDialogSpec& specification)
{
  return chooseFile(specification, true);
}
