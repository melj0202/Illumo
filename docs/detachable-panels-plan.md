# Detachable tool panels: execution record

**Design:** `docs/detachable-panels-design.md`, authorized by the owner on 2026-09-23.
Each milestone builds and passes its own tests before the next begins.

| M | Scope | Status |
|---|---|---|
| 0 | Design and this record | done |
| 1 | `IBackend::readFramebuffer` (GL double-buffered PBO, Mock fill) | done |
| 2 | Host windows: `Windows` capability, Window service, frame schema v5 surfaces, input v2, `WasmPanelWindows` (replay, readback, present, events) | done |
| 3 | `IPanelSurfaces` seam: guest `GuestPanelSurfaces` and surface recording, `IllumoContext::panelSurfaces`, `FakePanelSurfaces` | done |
| 4 | `GuiToolStyle`, `GuiPanelDock` (layout, splitters, tear-off, dock, persistence), per-surface pointer tracking | done |
| 5 | IllEd redesign on the dock (four detachable panels, plain look, cross-window drags) | done |
| 6 | IllMeshViewer redesign (Info and Display panels) | done |
| 7 | Docs and decisions (D-UI7, D-E27, D-R27), full gate, Windows smoke with real pop-out windows | done |

## Tests per milestone

| M | Tests |
|---|---|
| 1 | `Illumo.Renderer.OffscreenReadback` (Mock), a GPU readback case in `IllumoCaptureGpuTests` |
| 2 | `Illumo.Wasm.WindowServiceDecoder`, `.WindowDeny`, `.FrameV5Surfaces`, `.InputV2`, `.PanelWindowsLifecycle` (the `Bench.PanelSurface` benchmark moves to M7, where real panels exist) |
| 3 | `Illumo.Wasm.GuestPanelSurfaces` (SDK against the host through the exchange bridge), `Illumo.Gui.FakePanelSurfaces` |
| 4 | `Illumo.Gui.PanelDockLayout`, `.PanelDockSplitters`, `.PanelDockTearOff`, `.PanelDockPersistence`, `.ToolStyleTokens`, `.SurfacePointerTracker` |
| 5 | Updated `IllEd.*` layout tests, `IllEd.Panels.PopOutAndDock`, `.DetachedTextEntry`, `.CrossWindowAssetDrop`, `.LayoutPersists`, `IllEd.Wasm.Package` |
| 6 | Updated `IllMeshViewer.*`, `IllMeshViewer.Panels.DisplayEdits`, `.PopOutAndDock`, `IllMeshViewer.Wasm.Package` |
| 7 | Full Release build and `-L IllumoWorkspace`, `IllumoTidy`, ASan Debug, coverage gate, benchmarks, real-GPU capture, manual pop-out smoke |

## Validation log

### M0 (2026-09-23)

- Design written from the current code. The investigation found:
  - The guest ABI has no second surface or window input.
  - `SoftwareCanvas` rasterizes rects, lines and text only.
  - The host can render offscreen and read back only the backbuffer.
  - There is no panel framework; the IllEd and viewer panels are listed
    with their fixed-coordinate tests.
- Owner decisions: plain tool look; IllEd's Hierarchy, Inspector, Assets and
  Tools panels all detachable; viewer Info and Display panels.

### M1 (2026-09-23)

- `IBackend` gained an asynchronous readback of an offscreen framebuffer's
  first colour attachment:
  - `requestFramebufferReadback(stream, framebuffer, width, height)`
  - `takeFramebufferReadback(stream, wait, out)`, returning top-down RGBA8
  - `releaseReadbackStream(stream)`
  - The defaults refuse.
- **GL implementation:**
  - Each caller-numbered stream owns two pixel-pack buffers, each copy fenced
    with `glFenceSync`, so up to two copies are in flight.
  - `take` returns the oldest completed copy; it polls with a zero timeout,
    or with `wait` blocks for at most 1 s.
  - All read and pack bindings are restored, and streams are released on
    `Shutdown`.
- **MockBackend:** tracks the colour each framebuffer was last cleared to by
  submitted commands, and returns it as the readback fill, so tests can prove
  which target was drawn.
- **`Renderer::renderOffscreen(target, width, height, drawables, clear,
  uiScale)`:**
  - Draws outside `RenderScene`: bind target, viewport, blend, clear, append,
    submit, then restore the screen target and viewport.
  - Pixel-space drawables lay out for the target because the frame context is
    active at the target size.
  - It is refused inside `RenderScene` and for invalid targets or sizes, and
    it keeps the caller's frame error untouched.
- **Deviation:** the design named `IBackend::readFramebuffer`; the shipped
  shape is the stream triple above, because the two-deep asynchronous queue
  is what the host needs.
- **Tests:**
  - `Illumo.Renderer.OffscreenReadback` (Mock): targeting, clear, draw,
    restore, two in flight and a third refused, fill colour, empty take,
    invalid arguments, refusal inside `RenderScene`.
  - `IllumoCaptureGpuTests` gained a real-GPU check: a 16×8 target, blue
    clear plus a red top half, read back with top-down rows. It passes.
  - `ctest -L Illumo`: 595/595.
- **Pre-existing, unrelated:** the explicit `IllumoCaptureGpuTests` target
  still fails its first check ("Scene shadow pass did not activate", shadow
  pixel parity) independently of this change. The M1 diff is purely additive
  and touches no shadow or `MeshVisual` code. It is left for a separate fix.

### M2 (2026-09-23)

- **ABI:**
  - Capability `Windows` (bit 10); `KnownCapabilities` is now `(1 << 11) - 1`.
  - `GuestService::Window = 17`. The design said 16, but `JobLanes`
    already held 16. `GuestServices::read` accepts up to 17.
  - `IllumoGuest/Windows.h`:
    - `GuestWindowRequest` (v1: Open, Close, SetTitle). Open's bounds are
      160×120 to 4096², an offset of at most ±16384 from the main client
      origin, and a title of at most 128 bytes of UTF-8 without control
      characters. Close and SetTitle carry no geometry, and Close has no
      title.
    - `GuestWindowOpened` is the completion payload of an Open.
- **Frame schema v5:**
  - A trailing `surfaces` section holds at most 8 entries. Each has an id
    between 1 and 2^31−1, is unique, and carries a logical size and a nonzero
    revision. It is either `same` or a batch list that is UI only (no World,
    LitMesh, Skybox or depth test).
  - Batch, vertex and index quotas span the main frame and all surfaces.
  - `GuestFrame::writeBatch` and `readBatch` are now shared by the main
    frame and surfaces; the v1–v4 decode is unchanged.
  - The guest writer now always emits v5, with zero surfaces.
- **Input v2:** the v1 layout followed by:
  - a surface tag per key and character event;
  - the main client screen origin;
  - the focused surface;
  - up to 8 surface records: size, screen origin, cursor (which may lie
    outside while dragging), buttons, wheel, focus;
  - up to 64 window events: Closed, Resized, Moved, FocusGained, FocusLost.
  - Tags must name open surfaces, and events never name surface 0.
  - The host sends v2 only to guests granted `Windows`.
- **Platform:**
  - New public `Illumo/Platform/SurfaceWindow.h`: `ISurfaceWindow`,
    `ISurfaceWindowFactory` and `PlatformSurfaceWindows()`, over
    `PixelWindow`.
  - `PixelWindow` gained client origin, title, focus, cursor and minimum
    size.
  - New windows are clamped into the work area of the monitor holding their
    centre, with 32 px left for the title bar.
- **Host `WasmPanelWindows`:**
  - Validates and applies requests: at most 8 windows, one per surface,
    opened relative to the main window.
  - `collectInput` builds v2 from each window's events and state. A close is
    reported once, and the guest decides what happens.
  - `present` runs at the start of `WasmGameModule::Update`, after the
    previous frame's texture writes and before the next accept. It replays a
    surface through `Renderer::renderOffscreen` into a per-window target only
    when its revision or window size changes, queues the readback, and
    presents the newest completed copy. Repaint requests re-present the
    cached image.
- **`WasmFrameRenderer`:**
  - Surfaces keep their own mesh pools. `accept()` validates main and
    surface content transactionally, with one resident-budget check.
  - A `same` surface must match the stored revision; a changed one must
    advance it. Surfaces the frame omits are freed.
  - `surfaces()` and `surfaceDrawable()` expose the content.
  - Slot assignment, growth and enrolment were factored into helpers shared
    with the main frame.
- **Integration:**
  - `WasmGameServices` completes Window requests synchronously and rejects
    them without the grant or a window host.
  - `WasmGameModule` grants `Windows` only when its factory is available.
    `setSurfaceWindows(nullptr)` turns it off, and the runtime does so for
    `--capture` and `--bench-*`.
  - Windows close on failure and on Exit.
  - The key mapping moved to `WasmInputMapping.h`.
- **Tests:**
  - `Illumo.Wasm.WindowServiceDecoder`, `.InputV2`, `.FrameV5Surfaces`,
    `.WindowDeny` and `.PanelWindowsLifecycle`, in the new
    `IllumoWasmWindowTests`. They use a fake window platform and MockBackend
    readbacks. The lifecycle test covers placement, the first replay and
    present, no replay while unchanged, repaint, a new revision, a stale
    revision refused, tagged input, focus/resize/move events, a replay at the
    new size, close reported once, Close, the eight-window limit and
    `closeAll`.
  - `Illumo.Wasm.RetainedResources` was updated: its v3 rewrite now drops
    the v4 and v5 trailing sections.
  - Full Release build plus the in-build workspace run: 598/598.

### M3 (2026-09-23)

- **Core seam:** `Illumo/Gui/PanelSurfaces.h` defines `IPanelSurfaces`,
  `PanelSurfaceState`, `PanelSurfacePointer` and `PanelSurfaceEvent`
  (Opened, Failed, CloseRequested, Resized, Moved, focus).
  - Surface 0 is the main window. Its size, origin and pointer are reported
    so product code can treat every surface the same way.
  - Keys and characters stay in the one `InputManager` queue; `focused()`
    says which window they came from. This is simpler for products than
    per-surface key queues, and the ABI still tags every event.
- **Context:** `IllumoContext::panelSurfaces` is composed by the guest
  application only when the host granted `Windows`.
- **Guest `GuestPanelSurfaces`** (`IllumoGuest/PanelSurfaces.h`):
  - Requests:
    - Open, Close and SetTitle are Window service requests.
    - Titles are sanitized: control characters become spaces, then the
      title is truncated at a UTF-8 boundary.
    - Geometry is clamped to the ABI bounds.
    - A close while opening waits for the answer.
    - Refused opens become Failed with a Failed event.
  - `accept(input)` reads the main size, pointer and origin, the surface
    records, and window events.
  - `record()` draws each Open surface's scene with the snapshot window
    temporarily reporting that surface's size, so pixel-space drawables lay
    out for it.
  - `finish()` runs once the frame will be delivered. It compares the
    encoded size and batches with the previous revision, and treats frame
    mesh or texture writes to anything the surface reads as a change. The
    result is `same` or a revision bump.
- **`GuestRecordingBackend`:**
  - `beginSurface` and `endSurface` redirect batches into the surface.
  - Clip conversion, the viewport check and the "mid-frame clear" check use
    the target's size and batches.
  - Recording a surface's scene leaves the frame's world camera and casters
    alone.
  - World, lit, skybox and depth-tested draws inside a surface throw.
  - `demoteDynamic` also rewrites surface batches.
- **`GuestModuleApplication`:** accepts and pumps the surfaces during
  update, clears their scenes before dispatch, records them after the main
  scene, and finishes after `takeFrame`.
- **`GuestSnapshotWindow`:** `overrideDimensions` / `clearOverride`.
- **`Illumo::TestSupport` `FakePanelSurfaces`:** opens answer on `step()`,
  a `refuseNextOpen` switch, and by-hand pointer, focus, resize, move and
  user close.
- **Tests:**
  - `Illumo.Wasm.GuestPanelSurfaces` runs the real guest SDK natively (panel
    surfaces, recording backend, renderer, service queue) against the real
    host services, `WasmPanelWindows` and `WasmFrameRenderer`. It covers the
    open handshake and title sanitizing, surface batches recorded apart from
    the main frame, host present, `same` for unchanged content with no
    replay, revision advance on change, host input v2 reaching the guest
    (pointer, focus, main origin), a user close delivered as a request,
    Close removing the window and its content, a refused open failing, and
    no grant meaning no opens.
  - `FakePanelSurfaces` is exercised by the product tests in M5 and M6; it
    has no case of its own, a deviation from the M3 test row.
  - Full Release build plus the workspace run: 599/599.

### M4 (2026-09-23)

- **`GuiToolStyle`** (`Illumo/Gui/GuiToolStyle.h`, native and guest):
  - `GuiToolPalette`: a neutral dark palette in the spirit of the console's
    `ConsoleColors`, with one accent for selection, focus and active state.
  - `GuiToolRect`.
  - Flat drawing with rectangles, lines and text only: panel, title bar with
    PopOut/Dock/Hide glyph buttons, splitter, section header, selectable
    row, button, toggle, slider with its track rectangle, label/value, text
    field with caret and selection, scrollbar, menu bar, dropdown (hints,
    checks, separators, hit testing) and status bar.
  - Metrics: 13 px text, 22 px titles, 20 px rows.
- **`GuiPanelDock`** (`Illumo/Gui/GuiPanelDock.h`):
  - Panels are declared with an id, title, side, surface id, weight, minimum
    height and detached size.
  - `layout` stacks the Docked (and Opening) panels of each column between
    the reserved bands. Each column is at least 160 px wide and at most 45%
    of the window. Both columns shrink together so the centre keeps at least
    120 px. Row splitters sit between panels and column splitters beside the
    centre. Detached panels fill their window: surface pixels divided by the
    UI scale.
  - `update(mainPointer)` does four things:
    - drains surface events (Opened, Failed, CloseRequested, Moved, Resized);
    - docks a panel whose window the host lost;
    - reopens restored detached panels;
    - handles title buttons (Hide, PopOut), title-bar tear-off past the main
      edge by 12 layout units (the grab point stays under the cursor),
      column and row splitter drags (row weights become current heights,
      the boundary moves, minimums hold), and the Dock button in detached
      windows.
  - Per-surface `consumedPress` lets products skip presses that landed on
    dock chrome.
  - `serialize` / `restore`: an `illumo-dock 1` text form of column widths
    and per-panel mode, weight and window rectangle.
    - The file is validated as a whole: bounded, finite, with known
      keywords.
    - Panels a build no longer has are ignored.
    - A detached panel reopens where its window was, or stays docked when
      no surfaces exist.
  - Deviation: panels keep their declared order within a column; there is
    no drag reordering or cross-column docking in v1.
- **`GuiPointerTracker::sample(PanelSurfacePointer, scale)`:** the same
  press, click and release bookkeeping for a detached window's pointer,
  valid outside the window during a drag.
- **Tests:**
  - `Illumo.Gui.PanelDockLayout`, `.PanelDockSplitters`, `.PanelDockTearOff`
    (through `FakePanelSurfaces` at UI scale 2), `.PanelDockPersistence`,
    `.ToolStyleTokens` and `.SurfacePointerTracker`.
  - Full Release build plus the workspace run: 605/605.

### M5 (2026-09-23)

- **Dock:** `EditorModule` owns one `GuiPanelDock`:
  - left column: Hierarchy (surface 101) above Assets (102);
  - right column: Tools (103) above Inspector (104).
  - `EditorModulePanels.cpp` lays it out between the menu and status bars,
    runs its chrome input with the main pointer, places each panel, draws
    docked chrome into one visual and each detached title bar into its
    window's visual, and dispatches content to the main scene or to
    `panelSurfaces->scene(surface)`.
- **Content renderers:** `EditorSceneGraphView`, `EditorAssetBrowser`,
  `EditorInspector` and the new `EditorToolsPanel` (which replaces
  `EditorSidebar`: mode, Move/Rotate/Scale, local axes, snap, Create tools)
  draw into a `GuiPanelPlacement` (content rectangle plus surface).
  - Their collapse strips, headers, self-layout and animated accents are
    gone; drawing uses `GuiToolStyle`.
  - The hierarchy's context menu is clamped inside its panel, so it works
    in a detached window.
- **`GuiPanelPlacement` / `GuiPanelPointer`** (`Illumo/Gui/GuiPanelPointer.h`,
  moved out of IllEd for the viewer):
  - samples the main window or the placement's surface;
  - gives left edges, a right-click edge and a consumed wheel;
  - adopts the button state when a panel changes windows;
  - honours `inputBlocked`, which the module sets when the menu bar, an open
    menu, a dialog or dock chrome took the press.
- **`EditorToolbar`:** plain menu bar and dropdowns (checks, disabled items,
  separators), a flat mode label and toasts placed in the dock's centre
  rectangle, and the status bar.
  - A View menu lists each panel's show/hide and pop-out/dock items and
    Reset Layout. Pop-out is disabled where windows are unavailable.
  - `EditorCommand` gained `Toggle*Panel`, `PopOut*Panel` and `ResetLayout`,
    replacing `ToggleSceneGraph` and `ToggleSidebar`.
- **Cross-window drop:** the Assets panel reports a drop with its release
  point in its own window's pixels. The module maps it to main-window pixels
  through the two screen origins and places the asset when the point lies
  in the viewport centre. `uiBlocksWorld` is now the menu bar, the dock and
  everything outside the centre rectangle.
- **Keys:** unchanged; the one queue serves every window. A focused
  inspector field in a detached window takes keys before shortcuts.
- **Persistence:** the dock text is stored in the `panelLayout` setting,
  with `;` for newlines. It is saved when it changes (never mid-drag) and
  on Exit, and applied once settings arrive; guest settings load
  asynchronously. Detached panels reopen on the next start. Exit closes the
  panel windows.
- **Deviations:**
  - `fontSize` scales panel content only; the chrome (menu bar, status bar,
    title bars) keeps the tool metrics and follows the renderer's UI scale.
    The toolbar's fixed-height tests became `IllEd.Toolbar.ChromeMetrics`.
  - The IllEd guest's `describe()` is unchanged: `Windows` is granted
    opportunistically, like `ProjectFiles`. Requiring it would refuse
    hosts without windows.
- **Tests:**
  - Updated for placements: `IllEd.SceneGraphView.*` (plus `.Placement`;
    `.CollapseExpand` removed), `IllEd.Hierarchy.*`, `IllEd.Assets.*`,
    `IllEd.Inspector.*`, `IllEd.Module.*` (`.PanelCommands` now drives dock
    modes).
  - `IllEd.Tools.ControlHits` and `.PlacementAndState` replace
    `IllEd.Sidebar.*`; `IllEd.Toolbar.ViewMenuPanels` is new.
  - New `IllEd.Panels.PopOutAndDock` (View menu, window close, title
    tear-off, refused open), `.DetachedTextEntry`, `.CrossWindowAssetDrop`
    (pointer path through a detached window) and `.LayoutPersists`
    (hidden panel, column width, detached reopen, invalid fallback), all
    against `FakePanelSurfaces`.
  - Full Release build plus the workspace run: 609/609, including
    `IllEd.Wasm.Package`.

### M6 (2026-09-23)

- **Chrome:** `MeshViewerUi` became the plain main-window chrome:
  - a File menu (Open) and a View menu (Reset Camera; Grid, Axes, Sky and
    Wireframe with checks; per-panel show/hide and pop-out/dock; Reset
    Layout);
  - the empty-state card and toasts, placed inside the dock's centre;
  - a status bar with key hints and camera yaw, pitch and distance.
  - The info card and header buttons are gone. `MeshViewerAction` gained the
    panel actions.
- **Panels** (`MeshViewerPanels.*`), in one right-column `GuiPanelDock`
  (`MeshViewerModulePanels.cpp`), both detachable:
  - **Info** (surface 201): mesh vertices, triangles, submeshes, materials
    and size, or scene nodes, assets and missing count.
  - **Display** (surface 202): Grid, Axes, Sky and Wireframe toggles;
    Lighting with light X/Y/Z and ambient sliders; Shadows with soft edges,
    radius and bias; Motion blur with amount.
  - The Display panel returns edits instead of applying them: a display
    toggle or an env var and its value. The module writes the env vars
    (`lightingEnabled`, `lightDir*`, `ambientColor*`, `shadowsEnabled`,
    `shadowPcf`, `shadowRadius`, `shadowBias`, `motionBlurEnabled`,
    `motionBlurAmount`), applies them at once and saves. The per-frame env
    read stays the only source of those settings. Ambient scales the default
    blue-grey tint.
  - Display toggles now persist in `showGrid`, `showAxes`, `showSkybox` and
    `showWireframe`.
- **Camera:** a mouse drag orbits or pans only when it began in the
  viewport, and the wheel zooms only over it. A slider drag in a docked
  panel therefore never moves the camera.
- **Persistence and windows:** as in IllEd. The layout is saved in
  `panelLayout`, and panel windows close on Exit and reopen on the next
  start.
- **Deviation:** Reset Camera and framing still use the whole window, not
  the visible centre rectangle, because the camera has no viewport offset.
  With the right column docked the framed object sits slightly left of the
  visible centre. This is left as a follow-up.
- **Test isolation fix:** `EnvVars` loads and saves `envvars.json` in the
  working directory. Once toggles and layouts persisted, the product
  fixtures leaked state between cases through that shared file, and a
  wireframe toggle failed depending on test order.
  - The IllEd and viewer module and panel fixtures now use a fresh settings
    file in the temp directory.
  - The keys earlier runs had left in `build-workspace/Release/envvars.json`
    were removed.
- **Tests:**
  - `IllMeshViewer.Ui.*` was rewritten for the menus, empty card and plain
    chrome; `.FontSizeScaling` was replaced by `.Menus`.
  - New `IllMeshViewer.Panels.DisplayEdits`: toggles, the Lighting env var,
    a Radius slider drag that sets the shadow radius without orbiting, and
    the Ambient slider, with the Info panel listing a loaded mesh.
  - New `.PopOutAndDock`: View menu pop-out, drawing only in the window, a
    click in the detached window toggling wireframe, docking on close,
    hiding Info, and the saved layout.
  - Full Release build plus the workspace run: 611/611, including
    `IllMeshViewer.Wasm.Package`, `.ScenePackage` and the IllEd packages.

### M7 (2026-09-23)

- **Decisions:**
  - D-UI7 (plain tool look and detachable panels), D-E27 (panel surfaces)
    and D-R27 (surface frames and offscreen readback) were added to
    `docs/architecture-consensus.md` and the LaTeX decision log.
- **Documentation updated:**
  - the consensus GUI row;
  - root `AGENTS.md` (GUI paragraph and a source-map row for the panel
    windows);
  - `Illumo/Source/Gui`, `Platform` and `Rendering` guidance;
  - IllEd and IllMeshViewer source and test guidance;
  - `docs/packages/{illed,engine,platform,rendering,tests,source-layout}.md`;
  - `docs/wasm-game-runtime-design.md` (the `Windows` capability, Window
    service, input v2 and frame v5);
  - `README.md`, whose IllEd row still said `.ilsc` version 1;
  - LaTeX chapters 02, 08 and B.
  - `docs/output/illumo.pdf` was rebuilt by the build's docs target.
- **Benchmark:** `Illumo.Wasm.Bench.PanelSurface` (label `IllumoBenchmark`,
  in `IllumoWasmWindowTests`).
  - Setup: a 240-row panel (rect, outline and rule per row) in a 400×800
    surface; the guest SDK records it, and the host accepts, replays and
    reads back through MockBackend.
  - Release, per frame:
    - changed content: guest record and finish 118–149 µs; host 419–479 µs;
    - unchanged content: guest 2.7–2.9 µs; host 0.5 µs, with no replay.
  - `Illumo.Wasm.Bench.FileThroughput` is unchanged: 444 and 456 MB/s.
- **Full gate:**
  - Release build plus the workspace run: 611/611.
  - `IllumoTidy`: clean (242 sources).
  - ASan Debug `ctest -L IllumoWorkspace -E "Bench|Oracle"`: 592/592, the
    same method as the previous effort; the Oracle passes in Release.
    - `build.py test --config Debug` builds only the native runners.
    - The runtime, `IllumoPack` and the WASM runners were built explicitly
      first; without them 20 cases were Not Run.
    - A first attempt failed to link with LNK1201 because only about 4 GB of
      disk were free while the coverage tree built.
  - Coverage (`build-f34-coverage`, full build first, 613/613): 85.34%
    production lines (gate 85%; M15 was 85.11%).
- **Real pop-out smoke** (`IllumoRuntime`, Windows, real GPU, interactive):
  - The app's guest settings were seeded with a detached panel. The
    runtime was launched, its top-level windows enumerated, and each
    window's client area captured.
  - IllEd: an "Inspector" window (360×520) showed the complete inspector
    with its Dock button, and Tools filled the right column in the main
    window.
  - IllMeshViewer: a "Display" window showed every toggle and slider, and
    Info filled the right column.
  - The layout, including the window's new position, was saved back to
    guest storage on exit.
  - The captures showed three truncations, now fixed and rechecked:
    - the slider value column is wider, so Bias reads "0.0010";
    - the viewer's empty-card hint is shorter;
    - the hierarchy's empty hint is on two lines.
  - Two observations:
    - One launch with a 12 s wait found no detached window; with a longer
      wait it passed three times. The guest settings load asynchronously
      and the window opens only after them.
    - One capture showed the fresh document as modified ("Untitled *");
      two further launches were clean. The cause is not established
      (possibly stray desktop input during the run).
- **Follow-ups:**
  - frame and reset the camera on the visible centre rectangle (viewer and
    IllEd);
  - an optional `fontSize` scale for the chrome;
  - cross-column panel docking;
  - Linux presentation (still unsupported, as for the console).
