#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <string>

// What IllEd writes beside a dirty document into its private storage
// (docs/illed-editing-wave4-plan.md, 5.2): the encoded scene plus the
// "illed.autosave" extension below, which the next start reads to offer
// recovery. Slots are flat storage names, since the file protocol cannot
// list or remove storage entries: a free slot holds an empty file.
struct EditorAutosaveRecord
{
  static constexpr const char* kExtension = "illed.autosave";
  // Storage slots "autosave-0.ilsc" .. "autosave-<kSlots-1>.ilsc".
  static constexpr int kSlots = 8;
  static std::string slotName(int slot);

  // What the UI called the document.
  std::string label;
  // The document's location when it was a tree path ("vfs:/project/..."),
  // which the next start can write back to; empty for a new document or a
  // file granted by a dialog, which cannot be reopened.
  std::string location;
  // The package root its relative references resolved against.
  std::string root;
  // When the slot was written, "YYYY-MM-DD HH:MM" in local time.
  std::string savedAt;

  // Reads the extension; false when the document carries none.
  bool read(const SceneDocument& document);
  // Replaces the extension with this record.
  void write(SceneDocument& document) const;
  // Removes the extension, so a recovered document saves without it.
  static void strip(SceneDocument& document);
  // The current local time, as savedAt stores it.
  static std::string now();
};
