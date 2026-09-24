# Detachable tool panels for IllEd and IllMeshViewer

**Status:** proposed 2026-09-23, awaiting owner authorization (Tier 3).
**Tracker:** `docs/detachable-panels-plan.md`.
**Precedent:** D-UI5 (the developer console pops out into its own OS window).

## 1. Problem

The owner asked to redo the IllEd and IllMeshViewer interfaces the way the
developer console was redone: a plain tool look, with panels that can be
broken off into separate OS windows.

Today neither product can do that:

- **No window of its own.** Both ship only as WASM guests. The console's
  pop-out window belongs to the native `DebugModule`. The guest ABI has no
  window requests, no second render surface, and input for one window only:
  one mouse position, and key events with no window id (`Input.h`).
- **One surface per frame.** A frame (schema v4, `Frame.h`) has one
  `width`/`height`, and all World batches must come before all Ui batches.
  The guest recorder rejects offscreen framebuffers and partial viewports
  (`RecordingBackend.cpp:1236-1253`).
- **The console's presenter cannot draw these panels.** `SoftwareCanvas`
  rasterizes only rects, lines and text from a host `GameVisual`. Guest
  frames arrive as textured triangles, IllEd panels use atlas sprites and
  ellipses everywhere, and the host never sees guest `GameVisual`s.
- **No panel framework.** Every IllEd panel hand-lays itself against the full
  window and implements its own collapse strip, scrolling, clamping and drag
  thresholds (toolbar 743 lines, sidebar 674, hierarchy 1,283, inspector
  1,188, asset browser 306). The viewer's UI is one 473-line drawable with a
  header of toggle buttons and an info card; its lighting, shadow and motion
  blur settings exist only as env vars.

## 2. Owner decisions (2026-09-23)

- **Look:** a plain tool look like the reworked console. Panels are flat and
  dense and use their own neutral palette. Each has a thin title bar with
  pop-out and dock buttons. There are no pulsing accents or decorative
  chrome.
- **IllEd:** the Hierarchy, Inspector, Asset browser and Tools panels can all
  pop out.
- **IllMeshViewer:** a detachable Info panel (mesh or scene statistics) and a
  new detachable Display panel. Display covers grid, axes, sky and wireframe,
  plus the lighting, shadow and motion blur settings that are env-var only
  today.

## 3. End state

- Both apps have a plain menu bar and status bar, a central viewport, and
  dock columns holding their panels: left and right in IllEd, right in the
  viewer. Splitters between panels are draggable.
- Every panel's title bar has a pop-out button. A panel also pops out when
  its title bar is dragged past the main window's edge, as the console does.
- A popped-out panel is a native OS window with its own title bar, move,
  resize and close. It renders with exactly the same primitives as when
  docked: sprites, ellipses and text.
- A panel docks back from its dock button, by closing its window, or through
  View > Reset layout.
- Input works in whichever window has focus:
  - The detached inspector edits text.
  - The detached hierarchy reorders by drag.
  - An asset dragged from a detached Assets window and dropped in the main
    viewport places it.
  - Editor shortcuts work from any editor window unless a text field has
    focus.
- The layout persists per app in its storage: dock sizes, hidden panels, and
  which panels are detached, with their window rectangles. Detached panels
  reopen on the next launch.
- Hosts that cannot present extra windows keep everything docked, and the
  pop-out buttons are hidden. That covers Linux, `--capture`, `--bench-*` and
  headless tests. Behaviour is otherwise identical.

## 4. Non-goals

- A general retained widget tree, event bubbling, or tabbed dock groups.
  `Illumo/Source/Gui/AGENTS.md` still applies.
- Detaching the 3D viewport, or multiple viewports.
- Popping out the menu bar, status bar or dialogs. Dialogs stay modal in the
  main window.
- Rendering the world into a scissored sub-viewport. The world keeps drawing
  full-window behind the opaque dock columns, as today. Framing and "reset
  view" use the visible centre rectangle. Detaching panels simply shows more
  of the world.
- Changing the developer console. It keeps its own implementation (D-UI5).
- Linux presentation. It stays unavailable there, as for the console.

## 5. Design

### 5.1 Rendering a detached panel: GPU offscreen plus readback

The host replays the batches of a panel surface into an offscreen render
target the size of its window. It reads the pixels back and presents them
through the existing `PixelWindow` (a GLFW `GLFW_NO_API` window with GDI
present).

- **Replay:** `WasmFrameRenderer` already replays batches into any pass. Its
  clip rectangles are fractions of the frame and rescale to the current pass
  viewport (`WasmFrameRenderer.cpp:889-908`). Replay into a pooled
  fixed-size target (`RenderPassDesc` with `targetDesc`) needs no new
  drawing code.
- **Readback:** new `IBackend::readFramebuffer(target, width, height,
  pixels)`, top-down RGBA8.
  - The GL backend uses two pixel-pack buffers per window, so a readback
    never stalls: frame N's pixels are presented at frame N+1. That is one
    frame of latency, invisible for a tool panel.
  - The first frame and resizes read synchronously.
  - `MockBackend` returns a deterministic fill so headless tests can check
    the path.
- **Cost:** only for panels whose content changed. Each surface carries a
  revision, and an unchanged surface is neither replayed nor read. A
  400×800 panel is 1.3 MB per changed frame.
- **Alternatives rejected:**
  - A CPU triangle rasterizer with texture sampling means a second renderer
    to keep in sync, plus CPU copies of every guest texture.
  - A second GL context breaks the single-context backend contract (D-UI5).
  - Guest-side `SoftwareCanvas` loses sprites and ellipses, and it doubles
    the drawing path.

### 5.2 Guest ABI: `Windows` capability, frame schema v5, input v2

All additions are gated by a new capability `Windows` (bit 10), which widens
`KnownCapabilities` to `(1 << 11) - 1`. The host offers it only when it can
present extra windows. Every new request shape gets decoder and deny tests
(root `AGENTS.md`).

**Window service** (`GuestService::Window = 16`). Requests:

| Request | Fields |
|---|---|
| `Open` | `surface`, `title` (≤ 128 bytes UTF-8), `x`, `y`, `width`, `height` (logical, relative to the main window's client origin) |
| `Close` | `surface` |
| `SetTitle` | `surface`, `title` |

The host completes `Open` with the actual client size, or with a refusal
reason.

- **Bounds:** at most 8 open windows per guest. Each is 160×120 to 4096×4096.
  Positions are clamped onto a monitor.
- Surface id 0 is the main window and cannot be opened or closed. Ids are
  guest-chosen, 1 to 2^31 - 1.

**Window events** are appended to `GuestInput` v2:

- `Closed`: the user closed the window. The guest decides whether that
  docks.
- `Resized`
- `Moved`, used for persistence.
- `Focus`

**Frame schema v5** adds an optional trailing `surfaces` section. Each entry
has:

- `surface` id, `width` and `height`
- `revision` (u64)
- either `same` (reuse the previous content) or a batch list with the same
  batch format as the main frame. Surfaces are Ui layer only: Shape, Sprite
  and Canvas styles, no World, lit meshes or casters.

Texture and mesh writes stay frame-level and apply before any batch of any
surface. Versions 1–4 remain valid. The existing per-frame size limits apply
across all surfaces together.

**Input v2** keeps the v1 layout and appends:

- the main window's screen origin
- per open surface: id, logical size, screen origin, pointer position,
  buttons, scroll, and a focus flag
- one surface id per key and character event (0 = main)

The host converts each window's cursor into that window's logical
coordinates. It also reports screen origins, so the guest can map a drag that
leaves one window into another. While a button is held, GLFW keeps
delivering the pointer to the source window, even outside its client area.

### 5.3 Engine-level seam: `IPanelSurfaces`

Product modules are shared between the guest and the native test oracles, so
they must not see guest types. Add a core interface
(`Illumo/Include/Illumo/Gui/PanelSurfaces.h`):

```text
class IPanelSurfaces {
  bool available() const;               // can pop out at all
  bool open(id, title, rect);            // request; result arrives later
  void close(id);
  SurfaceState state(id) const;          // Closed, Opening, Open, Failed
  Size size(id); Point origin(id);       // logical size, screen origin
  PointerSample pointer(id);             // position, buttons, scroll
  id focused() const;                    // 0 = main window
  Scene* scene(id);                      // drawables for this frame
  std::vector<WindowEvent> takeEvents(); // closed/resized/moved/focus
};
```

It is published as `IllumoContext::panelSurfaces`. Like `fileTree`, the
owner publishes it during Start and withdraws it on Exit; readers look it up
at use time.

- **Guest implementation** (`IllumoGuest`): `GuestPanelSurfaces` over the
  Window service and input v2. After recording the main scene,
  `GuestModuleApplication::frame()` records each open surface's `Scene` with
  that surface's size into the frame's `surfaces` section. It then bumps the
  revision only when the recorded bytes differ from the previous frame's.
- **Native oracle:** a deterministic `FakePanelSurfaces` in `Illumo::TestSupport`
  opens instantly and lets tests inject pointer, focus and close events.
- **Host implementation** (`Illumo/Source/Wasm/WasmPanelWindows`): owns the
  `PixelWindow`s and validates the service requests. Each frame it drains
  window events into the input snapshot, then replays, reads back and
  presents the surfaces that changed. On guest failure or Exit it closes
  every window.

### 5.4 GUI toolkit: plain tool style and panel docking

These are new units in `Illumo/Gui`, native and guest. All are immediate-mode
state and layout helpers, with no retained widget tree.

- **`GuiToolStyle`:** the plain palette and metrics, in the same spirit as
  the console's `ConsoleColors` but shared by tools. It also draws the
  primitives the tools need: panel body, title bar with pop-out/dock/hide
  buttons, splitter, row, section header, flat button, toggle, slider,
  stepper, text field, scrollbar, menu bar, dropdown and status bar. Existing
  `GuiKit` helpers are reused where they already draw flat.
- **`GuiPanelDock`:** layout and interaction state for one app window.
  - **Panels:** each has an `id`, `title`, minimum size and preferred column.
  - **States:** Docked (column, order, size), Detached (surface id, window
    rect) or Hidden.
  - `layout(windowRect, reservedTop, reservedBottom)` returns each visible
    panel's content rectangle and surface, the splitter rectangles, and the
    remaining centre rectangle.
  - **Interaction:**
    - splitter drags, clamped to minimum sizes;
    - title-bar buttons;
    - tear-off when a title-bar drag passes the window edge by 12 logical
      pixels, keeping the grabbed point under the cursor as the console does;
    - dock requests;
    - `Closed` window events re-dock the panel.
  - **Persistence:** `serialize()`/`restore()` to a compact string stored in
    the app's settings (`panelLayout`). Unknown or invalid layouts fall back
    to the default.
- **Per-surface pointer:** `GuiPointerTracker` gains a surface source. The
  same press, click and release bookkeeping reads `IPanelSurfaces::pointer(id)`
  instead of the main window.
- **Content contract:** a panel's content code draws into a caller-supplied
  rectangle of a `GameVisual` in a given surface's pixel space, and reads
  input through that surface. The rectangle is local to the surface: docked
  means main-window coordinates, detached means `(0, 0, windowSize)`. Panel
  code never knows which case it is in.

### 5.5 IllEd

- **Layout:**
  - Plain menu bar: File, Edit, Create, Tools, View, with the current
    shortcut table unchanged.
  - Plain status bar.
  - Left column: Hierarchy above Assets. Right column: Tools above
    Inspector.
  - View menu: show/hide each panel, pop out or dock each panel, and Reset
    layout.
- **Units:**
  - `EditorSceneGraphView`, `EditorAssetBrowser` and `EditorInspector` become
    content renderers that draw into the rectangle and surface they are
    given. Their collapse strips and full-window layout go away, and their
    drawing moves to `GuiToolStyle`.
  - `EditorSidebar` becomes `EditorToolsPanel`: 2D/3D mode, gizmo tool,
    space, snapping, and Create shortcuts.
  - `EditorToolbar` keeps only the menu bar, HUD pill, toasts and status bar.
  - `EditorModule` owns one `GuiPanelDock` and routes each frame:
    1. the focused text field;
    2. then the panel under the pointer in each surface;
    3. then the viewport.
- **Atlas:** the editor UI atlas is a guest texture, so sprites draw
  identically in detached windows.
- **Cross-window drags:**
  - **Assets:** a drag from the Assets panel (docked or detached) that
    releases over the main window's centre rectangle places the asset at the
    release point. That point is mapped through the surfaces' screen
    origins.
  - **Hierarchy:** reorder drags stay inside the hierarchy's own surface.
- **Keys:**
  - A focused text field in any surface gets text first.
  - Otherwise editor shortcuts apply whichever editor window is focused.
  - The Escape, Delete and F keys act on the selection as today.
- **Tests:** the fixed-coordinate tests in `TestEditorToolbar`,
  `TestEditorSidebar`, `TestEditorSceneGraphView`, `TestEditorInspector`,
  `TestEditorAssets` and `TestEditorModule` move to layout-derived rectangles
  from `GuiPanelDock`. New tests cover the pop-out/dock lifecycle through
  `FakePanelSurfaces`, detached text entry, cross-window asset drops and
  layout persistence.

### 5.6 IllMeshViewer

- **Layout:**
  - Plain header/menu row: Open, Reset view, View (toggles).
  - Status bar with key hints and camera info.
  - Right column: Info above Display.
- **Info panel:** the current info card's content, with mesh versus scene
  rows as today.
- **Display panel:**
  - View toggles: grid, axes, sky, wireframe.
  - Lighting: enable, direction as yaw/pitch sliders, light colour, ambient.
  - Shadows: enable, map size stepper, radius, bias, slope, normal offset,
    PCF.
  - Motion blur: enable, amount, maximum.
  - It edits the same env vars as today, so settings persist unchanged and
    `MeshVisual` still never reads env vars.
- **Viewport:** the camera frames scenes and meshes in the visible centre
  rectangle.
- **Tests:** `TestMeshViewerUi` and the drawable counts in
  `TestMeshViewerModule` move to layout-derived rectangles. New tests cover
  Display panel edits reaching the env vars and visuals, and the panel
  lifecycle.

## 6. Invariants and constraints

- The WASM host stays product-agnostic. Windows are generic surfaces, and the
  host never learns panel names.
- Every guest request is bounded and validated, and window titles are
  sanitized. A guest cannot place a window off-screen, resize past the
  bounds, or open more than 8.
- Main-thread affinity: `PixelWindow`s are created, pumped and destroyed on
  the main thread, before GLFW terminates.
- Frame and input changes are versioned. v1–v4 frames and v1 input from
  older guests stay valid.
- The house rules apply: no `auto`, no namespaces, no recursion, Mozilla
  `clang-format`, and docs in sync.

## 7. Risks

| Risk | Mitigation |
|---|---|
| Readback stall or GDI present cost | Dirty surfaces only; double-buffered PBOs; measure with a new `Illumo.Wasm.Bench.PanelSurface` |
| Focus and key routing across windows | Explicit focused surface id; tests with the fake surfaces; manual smoke on Windows |
| DPI and UI scale in pop-out windows | Surfaces use the app's UI scale; the logical size is reported with the pixel size |
| Test churn from new geometry | Tests read rectangles from the layout instead of constants |
| Cross-window drag coordinate mapping | Screen origins in input v2; unit tests of the mapping |

## 8. Rollback and containment

- Each milestone lands on its own.
- The ABI additions are dormant until a guest asks for `Windows`.
- The UI redesigns (M5, M6) can each be reverted as a unit. They depend on
  M4's toolkit but not on the ABI. With `panelSurfaces` absent, both apps run
  fully docked.
