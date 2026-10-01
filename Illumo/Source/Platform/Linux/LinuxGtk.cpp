#include "LinuxGtk.h"

#include <gtk/gtk.h>
#include <gtkmm.h>

bool
EnsureLinuxGtkInitialized()
{
  static bool attempted = false;
  static bool ready = false;
  if (attempted) {
    return ready;
  }
  attempted = true;
  char arg0[] = "illumo";
  char* args[] = { arg0, nullptr };
  int argc = 1;
  char** argv = args;
  ready = gtk_init_check(&argc, &argv) == TRUE;
  return ready;
}
