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

## Current state — as of 2026-10-23 (verified from repo inspection + Linux build test)

- Renderer is multi-backend: OpenGL + Vulkan. OpenGL is the default.
- Vulkan backend is complete (plan milestones M0-M8) and implements the
  full IBackend interface with the same observable results as OpenGL.
- Switching: `GraphicsAPI` setting, `--graphics-api`, or
  `set GraphicsAPI vulkan` in the console (restart required).
  A failed Vulkan start falls back to OpenGL.
- Validation: BackendParity renders seven scenes through both backends,
  matching within one pixel. Full Release build passes 709 tests,
  clang-tidy clean. Every package holds 60 FPS on both backends on RTX 4080;
  uncapped Vulkan is at or above OpenGL.
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
  A local-clone-only experiment with narrow includes was started; nothing
  about a fix has been pushed. The VM has no display, so Linux GUI/GLX
  smoke testing is impossible there.

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

## Decision log

- 2026-10-22 — Vulkan backend declared complete (M0-M8), GL/Vulkan parity validated as above.
- 2026-10-23 — Two-assistant sync set up by Ryan + jed: this file is the shared
  source of truth between jed (Muse) and Claude (incl. Claude Code on the web).
  CLAUDE.md now points Claude at this file. A browser chat with Claude was
  also opened for direct handoffs — chat is a supplement, this file is the record.

## Handoff note template (use for each session)

### YYYY-MM-DD — [Assistant name] — [Task]
- Did:
- Verified:
- Left open / next:
- Files changed:
