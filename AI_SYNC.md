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

## Current state — as of 2026-10-29 (renderer and Linux sections updated 2026-10-24; D3D12 parity and size corrected 2026-10-25; Wasmtime pin and Linux WASM runtime added 2026-10-29)

- Renderer is multi-backend. On `main`: OpenGL + Vulkan, OpenGL default.
- On `release/v26.10` (HEAD 108f687, checked 2026-10-24) there is also a
  DirectX 12 backend: `Illumo/Source/Rendering/D3D12/` with 14 source
  files, 196 KB (162 KB .cpp + 34 KB .h, from git ls-tree at 108f687;
  the earlier "~175 KB" was low), plus an AGENTS.md. Largest files:
  DeviceCommands 43.5 KB, DeviceResources 33 KB, Device 27 KB,
  DevicePresent 23.5 KB, Device.h 22 KB. Status per Ryan
  (2026-10-24): it passes and works — he has run it. jed verified the
  sources exist; jed has NOT run the D3D12 backend.
  The release branch's own docs agree: docs/d3d12-backend-plan.md says
  "Status: complete, M0-M8 (D-R36)"; D-R36 is in
  docs/architecture-consensus.md (decided 2026-10-01). Selection:
  `GraphicsAPI` `DIRECTX12`/`d3d12` or `--graphics-api d3d12`; a failed
  start falls back to OpenGL.
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
  D3D12 parity — CORRECTION 2026-10-25 (the 2026-10-24 text said no D3D12
  parity result is recorded; that was true for `main` only): on
  `release/v26.10`, BackendParity also covers D3D12
  (Illumo/Tests/Gpu/GpuBackendParity.cpp creates the D3D12 backend).
  docs/d3d12-backend-plan.md section 10 (M5) and D-R36 record: all seven
  scenes match OpenGL within tolerance, D3D12 as closely as Vulkan (the
  world scene differs in one pixel on both); Illumo.Gpu.D3D12*Parity and
  `IllumoCaptureGpuTests --api d3d12` pass. These are repo records.
  Neither jed nor Claude has run them. The plan says non-NVIDIA drivers
  are not verified.
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
  LinuxClipboard.cpp, prepared in a landing pack (2026-10-23) and
  submitted 2026-10-25 as pull requests to `main` and `release/v26.10`.
  #112 merged into `release/v26.10` 2026-10-25 (eab18f5b); #111 (`main`)
  still open. The 626/626 result is for `main` a0aaf67: `release/v26.10`
  adds Linux sources (LinuxChildProcess.cpp, relaunch arguments,
  SaveLoad::GetFolderLocation) that nobody has built on Linux.
  The VM has no display, so Linux GUI/GLX
  smoke testing is impossible there; GUI/GLX smoke on Linux remains
  unproven.
  Update 2026-10-29 (Claude Code, Ubuntu 24.04.5 cloud container, GCC 13.3,
  CMake 3.28, no CSim): `release/v26.10` with its release-only Linux sources
  now builds on Linux; the runtime-OFF Release build passes all 452
  registered tests (the 626 figure includes CSim's cases; CSim is private
  and was not cloned here).
- Wasmtime pin (2026-10-29): bumped 48.0.2 -> 48.0.7 in
  tools/bootstrap-wasm.ps1 and cmake/IllumoWasm.cmake only, PR #114 into
  `release/v26.10` (branch `wasmtime-48.0.7`). Its Windows CI run
  (40877186529, windows-2025, Release, no CSim) passed 543/543, including
  every Illumo.Wasm.* and Illumo.Runtime.* case. The 48.0.7 C API headers
  differ from 48.0.2 only in the version macros. Not merged as of this entry.
- Linux WASM runtime (D-PL2, 2026-10-29): on branch `linux-wasm-runtime`
  (stacked on the bump; PR into `release/v26.10`). Opt-in on Linux
  (`ILLUMO_BUILD_WASM_RUNTIME` default OFF there; `bash
  tools/bootstrap-wasm.sh` or `python3 build.py wasm-tools`, then `--wasm`).
  Same pins (Wasmtime 48.0.7, WASI SDK 34.0). With it the registered tests go
  452 -> 541 (+89: 68 Illumo.Wasm, 7 Illumo.Runtime, 6 IllEd.Wasm,
  2 IllMeshViewer.Wasm, 2 Playground.Wasm, 3 Illumo.Dist, 1 Illumo.Pack) and
  all 541 pass under Xvfb. GOTCHA: without a display, four WILL_FAIL runtime
  cases (MountMissingDir, ProjectMissingDir, PackageMissing,
  CaptureScriptNeedsCapture) pass for the wrong reason (GLFW start-up fails
  first); run them under xvfb-run. Under Xvfb + Mesa llvmpipe, IllEd,
  IllMeshViewer and Playground captures render correctly and
  IllEd.Gpu.Smoke passes. Linux is still NOT supported: no desktop, GPU,
  input, GTK dialog, fullscreen, audio or Vulkan check on Linux. The jed
  estimate "626 toward 709" assumed CSim; without CSim the measured pair is
  452 -> 541.
  Independently confirmed 2026-10-23 by Claude Code (Ubuntu 24.04 container,
  GCC 13.3.0, gtkmm 3.24.9): the minimal repro gives the same error at
  treeview.h:2543; the same file compiles with exceptions on; the narrow
  three-header set compiles with -fno-exceptions. The full workspace build
  (the 74/346 stop) was NOT re-run by Claude.
  Stale docs corrected in the same change: AGENTS.md and
  docs/packages/platform-linux.md now state the verified scope — the
  build succeeds and the 626 registered tests pass (2026-10-23);
  GUI/GLX smoke is untested and Linux is not a supported platform.

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
- [x] Linux build fix: replace <gtkmm.h> with narrow gtkmm includes in the
      three Linux GTK files. This keeps D-F3 (one exception mode for every
      target, cmake/IllumoBuild.cmake). A per-file -fexceptions workaround
      would break that rule. Patch prepared by jed (+3/-3, 626/626 tests on
      jed's VM, 2026-10-23) and submitted 2026-10-25 as pull requests to
      `main` and `release/v26.10`, with the two stale Linux docs corrected
      in the same change.
- [ ] Merge order (2026-10-29): PR #114 (Wasmtime 48.0.7, green) first,
      then the Linux WASM runtime PR (stacked on it). Ryan decides.
- [ ] Linux WASM runtime default: stays OFF until desktop GPU smoke
      (launch, input, GTK dialogs, fullscreen, audio, shutdown) passes on a
      named Ubuntu 24.04 host with a display. Who runs that smoke, and where?
- [ ] Weak runtime refusal tests: Illumo.Runtime.{MountMissingDir,
      ProjectMissingDir,PackageMissing,CaptureScriptNeedsCapture} only
      check "exit nonzero". Without a display they pass on GLFW failure;
      without CSim CaptureScriptNeedsCapture fails on the missing `game` app
      (Windows CI too). Option: add PASS_REGULAR_EXPRESSION on the log
      reason. Not changed (out of scope).
- [ ] Linux compiler helper gaps vs the Windows job object (documented in
      D-PL2): no one-process limit (descendants die with the process group);
      RLIMIT_DATA is close to, not equal to, the job's commit limit. Decide
      whether seccomp or a cgroup is wanted before Linux ships.

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
- 2026-10-25 — Claude corrected the D3D12 parity line: `release/v26.10`
  records D3D12 BackendParity results (D-R36; d3d12-backend-plan.md M5).
  The 2026-10-24 "no D3D12 parity recorded" was the same branch-scope error:
  the release branch's sources were checked, but not its docs. Rule: for a
  release-branch feature, read that branch's docs and tests as well as its
  sources before saying what is or is not recorded.
- 2026-10-29 — Ryan approved three decisions (relayed by jed in a MUSE
  message to Claude Code): (1) Wasmtime 48.0.2 -> 48.0.7 in the two pin
  files only, not Wasmtime 50; (2) start the Linux WASM port in a scoped
  order (Linux bootstrap, CMake .so branch, POSIX compiler helper, ASan
  staging guard check); (3) base the work on `release/v26.10`, not `main`
  (#111 still open there). Claude recorded the port as D-PL2 in
  docs/latex/sections/09-design-decision-log.tex.
- 2026-10-25 — IllEd editing wave 4 (D-E42: autosave and recovery, project
  backups, Revert to Saved, editor tabs, the `Project` guest service for
  switching projects, `IllEd.Gpu.Smoke`, play programs that host Saturn's
  scene) committed on `release/v26.10` as 3bf4c0ff. Local only, not pushed
  as of this entry. Plan: docs/illed-editing-wave4-plan.md.

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

### 2026-10-25 — Claude (Claude Code on the web) — Check of jed's 2026-10-24 update
- Did: Read jed's sync update and the merged AI_SYNC.md on `main` and
  `release/v26.10`. Checked its claims against both branches. Corrected the
  D3D12 parity line and the D3D12 source size. Updated the Linux open item.
  Branch `ai-sync-setup` restarted from `main` (03d593c) for this change.
- Verified: Merges 03d593c (PR #107, main) and 13a44de (PR #108, release)
  exist, and AI_SYNC.md was identical on both before this change. D3D12
  directory: present at 108f687, absent on main. D3D12 parity is recorded on
  release (see Current state). Both branches still include <gtkmm.h> in the
  three Linux GTK files. Not verified: Ryan's D3D12 run, the recorded GPU
  results, and the 626/626 Linux result (no Windows/GPU here; the Linux
  patch is not in the repo).
- Left open / next: This correction is on `ai-sync-setup` only. It needs a
  PR to `main` and the same commit on `release/v26.10`. The commit applies
  cleanly to both. Linux narrow-include patch still unlanded. TaskGraph
  decision unchanged.
- Files changed: AI_SYNC.md only.

### 2026-10-25 — Claude (Claude Code desktop, Windows) — Commit IllEd wave 4 and sync with origin
- Did: Committed the finished, previously uncommitted IllEd wave 4 work
  (D-E42) on local `release/v26.10`. Rebased the three unpushed local
  commits (asset-table reload fix, World shadow pass position D-RP2,
  wave 4) onto `origin/release/v26.10` (138723e6, the AI_SYNC PRs); no
  overlapping files. Moved local `main` to `origin/main`; its 36 local-only
  commits were a stale local fast-forward and all are on
  `origin/release/v26.10`.
- Verified (Windows, RTX machine, Release): full `IllumoWorkspace` CTest
  785/785 with CSim cloned, and `IllEd.Gpu.Smoke` in a real window. Run on
  the pre-rebase tree; the rebase changed only AI_SYNC.md and CLAUDE.md.
- Left open / next: `release/v26.10` is 3 commits ahead of origin, not
  pushed. IllEd File > Open Project switching not checked in a live window.
  CSim `main` is 2 commits ahead of its origin, not pushed. Linux and
  TaskGraph items unchanged.
- Files changed: AI_SYNC.md (this note), committed with the CI change below.
- Later the same session: the GitHub "Windows tests" job had failed on every
  `release/v26.10` run since it was added (2026-10-20). It never reached the
  tests: `windows-2022`'s MSVC 19.44 stops on error C3615 in the vendored
  tiny_obj_loader.h (`tinyobj_ff::distance` is not constexpr), while local
  VS 2026 (MSVC 19.51) accepts it. Switched the workflow to `windows-2025`
  (VS 2026, CMake 4.4.4) and updated AGENTS.md and README.md; pushed
  with the three commits above. Result of that first run: check GitHub. CI also skips the CSim checkout
  (`CSIM_READ_TOKEN` missing or unable to read), so game tests do not run
  there.

### 2026-10-25 — jed (Muse) — Linux narrow-include fix submitted as pull requests

- Did: Opened pull requests landing the Linux narrow-include fix (+3/-3 in
  LinuxGtk.cpp, LinuxSaveLoad.cpp, LinuxClipboard.cpp) to `main` and
  `release/v26.10`, from branches `linux-narrow-includes` and
  `linux-narrow-includes-release`. The stale Linux wording in AGENTS.md
  and docs/packages/platform-linux.md is corrected in the same change,
  and the Current state above is updated to match.
- Verified: the code change is the patch verified 2026-10-23 on jed's
  Ubuntu 24.04.5 VM (GCC 13.3, CMake 3.28) — full build succeeds,
  626/626 registered tests pass. GUI/GLX smoke remains untested (the VM
  has no display). This change was not re-built on Windows.
- Files changed: the three Linux GTK sources, AGENTS.md,
  docs/packages/platform-linux.md, AI_SYNC.md.

### 2026-10-25 — Claude (Claude Code desktop, Windows) — CI follow-up and review of #112
- Did: First VS 2026 CI run (40249300903 at 8b5ed4f6) built with MSVC
  19.51 and passed 454/543; the other 89 (WASM, runtime, Dist, Pack) had
  no `IllumoRuntime.exe` or staged apps. Cause: on a fresh tree CTest lists
  a test whose executable is missing with an empty command, so `build.py
  test` never built them. Fixed in `build.py` `build_test_executables`
  (builds the default target once in that case) with a case in
  tools/test_build.py. Reviewed and merged #112 (Linux narrow includes)
  into `release/v26.10`: the include changes cover every gtkmm symbol the
  three files use on this branch, including the release-only folder
  dialog. Corrected its AGENTS.md and platform-linux.md wording to name
  `main` a0aaf67 as the verified Linux tree.
- Verified: tools/test_build.py 143 pass, 2 skipped. The Linux change was
  read, not compiled (no WSL or Docker on this machine); the release-only
  Linux files include the headers they need.
- Left open / next: the CI run on this push decides whether all 543 pass.
  #111 (same fix to `main`) not merged. CI still skips CSim
  (`CSIM_READ_TOKEN`).
- Files changed: build.py, tools/test_build.py, AGENTS.md,
  docs/packages/platform-linux.md, AI_SYNC.md.

### 2026-10-29 — Claude (Claude Code on the web) — Wasmtime 48.0.7 and the Linux WASM runtime
- Did: Bumped Wasmtime 48.0.2 -> 48.0.7 in tools/bootstrap-wasm.ps1 and
  cmake/IllumoWasm.cmake only (PR #114, branch `wasmtime-48.0.7`). Ported
  the WASM runtime build to Linux x86_64 on branch `linux-wasm-runtime`,
  stacked on the bump, in the approved order: tools/bootstrap-wasm.sh;
  CMake host branch (libwasmtime.so, IMPORTED_NO_SONAME, $ORIGIN, no
  implib); LinuxWasmCompiler.cpp + LinuxWasmCompilerMain.cpp (memfd,
  process group, pidfd deadline, RLIMIT_DATA); confirmed
  illumo_stage_msvc_asan is a no-op off MSVC. build.py: `wasm-tools`,
  `doctor` and `--wasm` work on Linux; default stays OFF there. Fixed a
  pre-existing CMake 3.x bug in cmake/IllumoDistribution.cmake (script
  mode, CMP0057). Recorded D-PL2 and synced README, AGENTS.md,
  platform-linux.md, architecture-consensus.md, frame-capture.md and the
  LaTeX chapters 08/09/10.
- Verified: Windows CI on #114: 543/543 (Release, windows-2025, no CSim).
  Linux (Ubuntu 24.04.5 container, GCC 13.3, CMake 3.28, glibc 2.39, no
  CSim): runtime OFF 452/452; runtime ON 541/541 under Xvfb; helper memory
  limit (IllEd.wasm compiles at 256 MiB, killed at 64 MiB), 1 ms deadline,
  and descriptor isolation checked with a scratch probe; IllEd,
  IllMeshViewer and Playground captures and IllEd.Gpu.Smoke under Xvfb +
  Mesa llvmpipe; clang-tidy clean on the two new C++ files;
  tools/test_build.py 143 run: 2 failures, the same 2 as before the change
  (Windows-only assumptions), 6 skipped.
- Not verified: CSim game package on Linux (CSim not cloned); any desktop,
  GPU, input, GTK dialog, fullscreen, audio or Vulkan check on Linux; a
  Linux Debug/ASan runtime build (configure only); the Linux branch on
  Windows (its PR's CI run decides); the PDF book (no TeX in the container,
  docs/build.ps1 not run).
- Left open / next: see Open decisions (merge order, Linux default, weak
  refusal tests, helper gaps). README.md toolchain line said 48.0.2 after
  #114; the Linux branch corrects it. Pre-existing quirk noted: a runtime
  that fails during start-up can leave a launch option (GuestPackage) in
  envvars.json; it never replays because launch options are cleared before
  parsing.
- Files changed: #114: tools/bootstrap-wasm.ps1, cmake/IllumoWasm.cmake.
  Linux branch: tools/bootstrap-wasm.sh, Illumo/Source/Platform/Linux/
  LinuxWasmCompiler*.cpp, cmake/IllumoWasm.cmake, IllumoWasiToolchain.cmake,
  IllumoDistribution.cmake, build.py, tools/test_build.py, AGENTS.md,
  Illumo/Source/Platform/Linux/AGENTS.md, README.md, docs (listed above),
  IllMeshViewer/Tests/README.md, AI_SYNC.md.
