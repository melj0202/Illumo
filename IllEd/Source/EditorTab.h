#pragma once

#include "EditorDocument.h"
#include "EditorSceneGraphView.h"
#include "EditorSelection.h"
#include <cstdint>
#include <functional>
#include <string>

// One open scene (docs/illed-editing-wave4-plan.md, 5.4): a document that
// owns its SceneInstance, its selection, and the view and autosave state
// that belong to that document rather than to the session. EditorScene owns
// the tabs; only the active one is bound to the render world, drawn and
// edited, and every busy file operation pins the active tab (switching is
// refused while one is in flight), so completions act on the tab they
// started on.
struct EditorTab
{
  // Never reused within a session; completions resolve tabs through it.
  uint64_t serial = 0;
  EditorDocument document;
  EditorSelection selection;
  // The node outlined under the cursor in the viewport; empty for none.
  std::string hoverId;
  // Isolation (Shift+H) and the scene generation it was made for, since a
  // new document ends it.
  bool isolated = false;
  uint64_t isolatedGeneration = 0;
  // The camera's height target in 3D.
  float cameraTargetY = 0.0f;
  // The guest asset cache ticket holding the document's fetched assets
  // (0 when none).
  std::uint64_t fetchTicket = 0;
  EditorHierarchyState hierarchy;

  // Autosave (EditorSceneAutosave.cpp): the tab's storage slot once it
  // needed one (-1 until then; kept until the tab closes), when its history
  // last changed, when the slot last matched the document (a write, or the
  // document being clean), the history uid the slot holds and the writes
  // in flight.
  struct Autosave
  {
    int slot = -1;
    double editTime = 0.0;
    double anchor = 0.0;
    uint64_t seenUid = 0;
    uint64_t uid = 0;
    bool live = false;
    bool inFlight = false;
    bool freeWanted = false;
    std::function<void()> freeDone;
  } autosave;
};
