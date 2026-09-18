#include "LinuxGtk.h"
#include <Illumo/Platform/Clipboard.h>

#include <gtkmm.h>

std::string
Clipboard::GetText()
{
  if (!EnsureLinuxGtkInitialized()) {
    return std::string();
  }
  Glib::RefPtr<Gtk::Clipboard> clipboard = Gtk::Clipboard::get();
  if (!clipboard) {
    return std::string();
  }
  const Glib::ustring text = clipboard->wait_for_text();
  return std::string(text);
}

bool
Clipboard::SetText(const std::string& text)
{
  if (!EnsureLinuxGtkInitialized()) {
    return false;
  }
  Glib::RefPtr<Gtk::Clipboard> clipboard = Gtk::Clipboard::get();
  if (!clipboard) {
    return false;
  }
  clipboard->set_text(text);
  clipboard->store();
  return true;
}
