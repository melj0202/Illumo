# Charter milestones 1 and 2

Status: implemented and verified on Windows, 2026-09-11.

## Objective and acceptance

Close the observed input-context lifetime defect, distinguish the charter's
future direction from current implementation, record generated-project source
provenance, and provide a standalone one-frame rendering tool. Preserve all
existing applications and the synchronous token renderer.

Success requires safe repeated module entry, explicit unknown/dirty provenance,
and a noninteractive real-GPU capture with equivalent direct and SceneGraph
fixtures, diagnostic failures, and clean shutdown. These implement charter
A1/A2/A10/A11 evidence in bounded slices, not the later physics/editor program.

## Baseline evidence

- InputManager has 32 append-only context slots. Failed registration returns -1
  but activation indexes it unchecked; CellGameModule neither checks nor retires it.
- Scene is a transient borrowed drawable list. SceneGraph contributes to it as
  one drawable; both producers already use Renderer::RenderScene.
- Renderer owns a backend, while MeshVisual borrows its renderer for destruction.
  Commands borrow payloads until synchronous submission returns.
- OpenGL/context construction is private. No image-readback contract exists.
- tools/create_project.py copies sources without recording revision or dirtiness.
- Canonical prose understates current mesh/pass functionality; nested rendering
  guidance still describes the old fixed queue. Linux remains unverified.

## Design and compatibility

1. Preserve long input IDs and 32 live slots. Assign monotonically increasing
   IDs, never reusing an issued ID within a manager. Validate lookup by ID,
   retire registrations explicitly, and use an empty neutral context when the
   active registration retires. Invalid activation leaves selection unchanged.
   CellGameModule registers before allocating domain state and retires on Exit.
2. Standalone generated workspaces receive engine-provenance.json with schema,
   source commit, dirty/unknown state, template/options, and copy mode. No machine
   paths or remote URLs. A dirty copy is provenance, not a reproducible Git pin.
3. Add an engine-owned bounded FrameCapture invocation. It creates a hidden,
   unpaced, non-MSAA window/context, backend, camera, and renderer, then invokes
   a synchronous producer callback. The callback owns its content, submits through
   RenderScene or direct tokens, and destroys it before returning. Read pixels
   before any swap; release renderer/backend before context. No runtime host,
   modules, input, audio, persistent configuration, or gameplay updates start.
4. Add backend-neutral RGBA8 readback with explicit unsupported default, plus
   frame diagnostics that survive queue reset. OpenGL packs top-down bytes and
   preserves readback state. Renderer supports strict token-only capture failure
   reporting; normal immediate fallback remains compatible.
5. IllumoCapture is a console client of the public API. A fixed 3D fixture accepts
   direct/scene mode, camera, dimensions, rotation, output and shader overrides.
   Optional OBJ input uses MeshLoader without starting the host. Both modes share
   the same fixture emitter and shaders; no .ilsc migration is implied.
6. Save PNG using the already vendored stb writer, with its notice accounted for.
   Return structured diagnostics and nonzero status for invalid input, resource,
   submission/readback, or output failure. Success does not imply portable
   bit-identical images. Temporal effects remain disabled in this fixture.

Alternatives rejected: a second renderer, raw-GL tool drawing, forced persistent
scene nodes, serialized pointer-bearing tokens, a general render graph, and a
physics/editor rewrite. No backend beyond OpenGL is added. No dependencies are
downloaded. Existing CMake remains authoritative; the tool is also available in
standalone library builds.

## Ordered milestones

- [x] Input lifetime implementation and regression tests.
- [x] Generated-project provenance and tests.
- [x] Public capture/readback/error contracts and CLI fixture.
- [x] Headless contract and CLI checks.
- [x] Real hidden OpenGL direct/scene captures and negative cases.
- [x] Documentation/guidance reconciliation and final review.

## Verification and containment

Run modified C++ through clang-format. Build the full Release workspace and run
IllumoWorkspace CTest; use focused input/module/capture tests first. Run project
generator unittest tests. Configure a standalone library build to check application
isolation. Run configured clang-tidy on affected translation units using the
repository Ninja/Clang workflow; use existing Debug sanitizer support for input
lifetime and capture where available. Rebuild documentation after edits.

GPU checks must inspect decoded images, dimensions and nonbackground content,
compare equivalent producers on this machine, and exercise invalid shaders,
missing resources and output errors. Report Windows/GPU environment. Linux and
interactive UI behavior remain unverified unless separately exercised; no FPS
claim is made. Keep validation artifacts in ignored build directories.

Changes remain additive except the defective input lifecycle behavior. Existing
callers may ignore activation's success result. Unsupported custom backends fail
readback explicitly. The new tool can be excluded from builds; production apps
retain their visible window defaults and existing frame scheduling. Git actions
and later charter milestones are out of scope.

## Results and open decisions

The user approved these two milestones, including their concrete implementation
and validation. No further product decision blocks this bounded fixture. Physics
timing, game completion, reference performance hardware, additional backends,
and game-type persistence remain decisions for later milestones.

Validation on Windows with an NVIDIA GeForce RTX 4080 (driver 32.0.16.1088):

- Full final Release workspace build passed, including 335/335 labeled tests.
- Native MSVC Debug AddressSanitizer build passed; all seven focused input,
  module-lifetime and capture tests passed. Lifetime checks cover 64 module
  Start/Exit cycles, full capacity, invalid/stale IDs and identifier exhaustion.
- `tools/verify_capture.py` passed all 11 invocations in both Release and native
  Debug: decoded direct/SceneGraph pixels match, geometry is visible, known
  occlusion is correct, and invalid arguments/assets/shaders/output paths fail
  without publishing a result. Existing output remains intact; no configuration
  file or staging residue is created. Artifacts are in ignored
  `build/capture-validation/release-final` and `debug-final`.
- The explicit `IllumoCaptureGpuTests` passed in Release and native Debug:
  overflow and invalid-resource errors survive explicit queue clearing,
  exceptions and producer errors remain failures, and another context can
  render successfully after failed captures.
- A generated source copy made with `--no-debug-tools` was configured directly
  at its `Illumo` directory, without the original applications. Its capture and
  public-header consumer built; the consumer ran and all 11 GPU checks passed.
  Its provenance records the source commit and dirty working-tree state.
- All five generator tests passed, including clean, dirty and unavailable
  provenance cases. No machine paths or remote URLs enter the manifest.
- Affected engine, tool and test translation units compiled with configured
  Clang 22 clang-tidy. Clang Debug test discovery cannot run on this machine:
  its ASan/Debug CRT combination reports a bad free during CRT initialization.
  An isolated iostream program with no Illumo linkage reproduces the failure.
  This is a separate toolchain limitation; native MSVC ASan validation passed.
- C++ formatting and diff whitespace checks passed. Documentation was rebuilt
  through `docs/build.ps1`/the workspace documentation target. Existing unused
  parameters, member-order warnings, optional Freetype dependency notices,
  MSVC ASan incremental-link notices and LaTeX/Perl warnings remain unrelated.

Review also caught two first-frame issues: the capture camera must explicitly
select perspective projection, and the GL state cache must reflect the fresh
context's initially disabled depth test. The decoded depth fixture verifies
the resulting behavior. PNG publication uses exclusive staging and atomic
no-replace hard links; unsupported destination filesystems fail explicitly.

Interactive application UI/native dialogs, Linux/macOS and a 60 FPS target were
not tested. These results establish the bounded capture and baseline contracts,
not completion of the later charter requirements. No Git changes were staged,
committed or published.
