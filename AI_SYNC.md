# AI_SYNC.md — Shared context for AI assistants working on Illumo

> Purpose: This file is the single common ground between different AI tools
> (e.g. Muse / jed, and Claude). Neither assistant can see the other's
> private chats or memory. If it's not written here (or linked from here),
> the other assistant does not know it.
>
> Rule for every AI session:
> 0. Messages jed sends to Claude in Claude's own UI (chat / Claude Code web)
>    are prepended with **MUSE** — that's how you tell jed's messages from
>    Ryan typing himself. Ryan set this rule 2026-10-23.
> 1. Read this file first.
> 2. Update the "Current state" and "Open decisions" sections before you finish.
> 3. Append, don't rewrite history, in the Decision Log. Date every entry.
> 4. Keep it factual and short — status, decisions, and gotchas, not chat transcripts.

## Project in one paragraph

Illumo (repo: melj0202/Illumo) is a personal C++ engine library growing
toward a general-purpose engine. It began as a terminal Conway's Game of
Life sim, was rewritten for OpenGL, and the renderer/engine code was split
into a reusable library. The Game of Life sim is CSim.

## Current state — as of 2026-10-24 (renderer and Linux sections updated 2026-10-24)

- Renderer is multi-backend. On `main`: OpenGL + Vulkan, OpenGL default.
- On `release/v26.10` (HEAD 108f687, checked 2026-10-24) there is also a
  DirectX 12 backend: `Illumo/Source/Rendering/D3D12/` with ~175 KB of
  sources (DeviceCommands 43.5 KB, DeviceResources 33 KB, Device 27 KB,
  DevicePresent 23.5 KB, ShaderCompiler 12.5 KB). Status per Ryan
  (2026-10-24): it passes and works — he has run it. jed verified the
  sources exist; jed has NOT run the D3D12 backend.
- Branch-scope gotcha: `main` has no D3D12 directory, and jed's local
  clone is main-only. A grep of main or of the local clone does NOT
  settle questions about release-branch features. State the branch
  with every backend claim.
- Vulkan backend is complete (plan milestones M0-M8) and implements the
  full IBackend interface with the same observable results as OpenGL.
- Switching: `GraphicsAPI` setting, `--graphics-api`, or
  `set GraphicsAPI vulkan` in the console (restart required).
  A failed Vulkan start falls back to OpenGL.
- Validation: BackendParity renders seven scenes through the OpenGL and
  Vulkan backends, matching within one pixel. Full Release build passes
  709 tests, clang-tidy clean. Every package holds 60 FPS on both of
  those backends on RTX 4080; uncapped Vulkan is at or above OpenGL.
  No D3D12 parity result is recorded — do not extend the parity claim
  to D3D12.
- CSim runs through Vulkan: it has a Renderer row in its Video tab, and
  title-scene tests / package captures run on both backends.
- Verified on Windows / NVIDIA only. No validation layers on the
  reference machine. Targets Vulkan 1.3. KosmicKrisp / Vulkan 1.4 on
  Apple silicon is plausible in principle, untested — do not claim it works.
- Linux build — CORRECTION 2026-10-23, supersedes any "Linux compiles" claim:
  Actually tested on an Ubuntu 24.04.5 VM (GCC 13.3 / CMake 3.28, HEAD a0aaf67).
  Configure passes clean, but the build FAILS. The three Linux GTK files
  (LinuxGtk.cpp, LinuxSaveLoad.cpp, LinuxClipboard.cpp) include the
  <gtkmm.h> umbrella header, whose treeview.h template uses `catch`, while
  the workspace builds globally with -fno-exceptions
  (cmake/IllumoBuild.cmake); error at gtkmm treeview.h:2543, build stopped
  at step 74/346. Minimal repro verified: umbrella + -fno-exceptions fails,
  narrow gtkmm includes (filechooserdialog/filefilter/clipboard) compile fine.
  Update 2026-10-24, superseding the "experiment started" status: the
  local-clone experiment FINISHED 2026-10-23 — with narrow includes in the
  three GTK files the full Linux build SUCCEEDS and all 626 registered
  tests pass (Illumo 308, IllumoGame 216, IllEd 84, IllMeshViewer 18).
  The patch is +3/-3 across LinuxGtk.cpp, LinuxSaveLoad.cpp,
  LinuxClipboard.cpp, byte-identical on `main` and `release/v26.10`,
  prepared in a landing pack (2026-10-23) and still uncommitted. Nothing
  about a fix has been pushed. The VM has no display, so Linux GUI/GLX
  smoke testing is impossible there; GUI/GLX smoke on Linux remains
  unproven.
  Independently confirmed 2026-10-23 by Claude Code (Ubuntu 24.04 container,
  GCC 13.3.0, gtkmm 3.24.9): the minimal repro gives the same error at
  treeview.h:2543; the same file compiles with exceptions on; the narrow
  three-header set compiles with -fno-exceptions. The full workspace build
  (the 74/346 stop) was NOT re-run by Claude.
  Stale docs: AGENTS.md ("Linux sources and CMake are repaired") and
  docs/packages/platform-linux.md ("can configure and compile the engine
  libraries") still claim Linux compiles. Not yet corrected.

## Working style / guardrails

- AI tools are used heavily for research and execution — speed is high,
  so verification is the priority. Don't let AI-assisted changes spiral.
- See: verification checklist (scope → build → diff review → smoke run → commit).

## Open decisions

- [ ] TaskGraph: A cleaned `TaskGraph.h` + `task_graph_test.cpp` exists
      outside the repo (in jed's goal files, 2026-10-22). It covers the
      frame graph Input → Physics/AI/Audio → Animation → Cull → RenderSubmit,
      double Execute(), cycle detection, and exception propagation.
      Verified: warning-free -Wall -Wextra -Werror, all tests pass,
      20 repeat runs + ThreadSanitizer clean.
      NOT yet applied to Illumo. Decide: adopt, adapt, or drop.
      Check before adopting: it uses exception propagation, but Illumo builds
      with no C++ exceptions (D-F3, AGENTS.md: no try/catch/throw). It needs
      an error-return path to fit, or it stays out.
- [ ] Linux build fix: replace <gtkmm.h> with narrow gtkmm includes in the
      three Linux GTK files. This keeps D-F3 (one exception mode for every
      target, cmake/IllumoBuild.cmake). A per-file -fexceptions workaround
      would break that rule. Fix the two stale Linux docs (see Current
      state) in the same change. Not started in the repo.

## Decision log

- 2026-10-22 — Vulkan backend declared complete (M0-M8), GL/Vulkan parity validated as above.
- 2026-10-23 — Two-assistant sync set up by Ryan + jed: this file is the shared
  source of truth between jed (Muse) and Claude (incl. Claude Code on the web).
  CLAUDE.md now points Claude at this file. A browser chat with Claude was
  also opened for direct handoffs — chat is a supplement, this file is the record.
- 2026-10-23 — Claude (Claude Code on the web) read this file and CLAUDE.md on
  branch ai-sync-setup and accepted the sync setup. Claude will read this file
  first and update it at the end of every Illumo session.
- 2026-10-24 — D3D12 backend confirmed present on `release/v26.10`
  (sources verified by jed; passes/works per Ryan). jed's earlier
  "OpenGL + Vulkan only" claim was a branch-scope error from inspecting
  `main`. Backend claims must name their branch.

## Handoff note template (use for each session)

### YYYY-MM-DD — [Assistant name] — [Task]
- Did:
- Verified:
- Left open / next:
- Files changed:

## Handoff notes (newest last)

### 2026-10-23 — Claude (Claude Code on the web) — Sync introduction
- Did: Read CLAUDE.md, AGENTS.md and this file on ai-sync-setup. Spot-checked
  the Current state against the repo. Re-ran the Linux gtkmm minimal repro.
  No code changes, as jed asked.
- Verified: Vulkan claims (M0-M8 complete, seven-scene BackendParity within
  one pixel, 709 tests, RTX 4080 at 60 FPS) match docs/vulkan-backend-plan.md.
  These are doc records; Claude did not re-run them (no Windows/GPU here).
  Global -fno-exceptions is at cmake/IllumoBuild.cmake:136. All three Linux
  GTK files include <gtkmm.h>. Minimal repro confirmed (see Current state).
- Left open / next: TaskGraph decision (now with the D-F3 exceptions check).
  Linux narrow-include fix plus the two stale Linux docs.
- Files changed: AI_SYNC.md only. (libgtkmm-3.0-dev was installed in the
  ephemeral session container only; nothing in the repo.)

### 2026-10-24 — jed (Muse) — AI_SYNC backend and Linux status correction
- Did: Corrected the renderer bullets (D3D12 present on release/v26.10
  only; branch-scope gotcha; BackendParity scope kept at OpenGL + Vulkan)
  and the Linux paragraph (narrow-include experiment finished: full build
  succeeds, 626/626 tests pass; patch prepared, still uncommitted).
  Landed at Ryan's approval via PR on both branches.
- Verified: D3D12 source listing on release/v26.10 @ 108f687 via GitHub
  (file names and sizes in Current state). Main has no D3D12 directory.
- Left open / next: Linux narrow-include fix still unlanded in the repo.
  D3D12 parity not recorded. TaskGraph decision unchanged.
- Files changed: AI_SYNC.md only.
