# Windows platform guidance

This file specializes `Illumo/Source/Platform/AGENTS.md` for Windows.

## Scope and contracts

Windows supplies the supported process entry and native save/load dialogs. It
adapts Win32 process/dialog APIs to Illumo's application and SaveLoad contracts;
it must not include game types or own persistence behavior.

- Forward the complete argument vector and consumer application definition to
  `RunIllumoApplication`.
- Keep Win32 headers and handles inside this directory.
- Preserve Debug CRT setup only as platform bootstrap; DebugOverlay composition
  belongs to the engine runner.
- Keep dialog labels/defaults data-driven through `SaveLoadDialogSpec`.

## Verification

Build Release and Debug, then manually verify startup, clean shutdown, save and
load dialogs, overwrite/new-file behavior, Unicode/long paths, and cancellation.
Headless replacements do not prove the native implementation.
