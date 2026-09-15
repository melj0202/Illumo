# In-game frame profiler

## Scope and controls

DebugModule presents an optional pie and matching legend through its Debug
layer. F6 and `profiler [on|off|toggle]` control it in Debug and RelWithDebInfo.
It starts disabled with no persisted flag. FPS, memory diagnostics, and Tracy
are independent. Release retains a disabled collector, without the overlay.

Root keys 1–3 select Update, Rendering, and Presentation / waits. Key 0 returns
to Frame; leaf rows and Other have no deeper children. Fixed ordering keeps keys
stable as proportions change. Console editing takes priority. Unmodified number
events and polling are captured while visible, including held keys between repeat
events, so navigation cannot change the Wireworld brush. Numeric character
events are consumed too, preserving other product text input. Other keys and modified
shortcuts remain available.

## What a slice means

| Root | Measured phases |
|---|---|
| Update | Input/global hotkeys; camera; optional/debug modules; required product module |
| Rendering | Pipeline and drawable-list preparation; asset pump; command generation and synchronous CPU submission |
| Presentation / waits | Backend EndFrame including swap; frame limiter |
| Other | Remaining elapsed time inside the measured loop body |

A sample spans beginFrame to endFrame in the application loop. Close negotiation
before that body, startup, and shutdown are excluded. Sequential steady-clock
marks partition elapsed time into exclusive phases. Renderer reports a timestamp
between SubmitCommandQueue and backend EndFrame without changing their order.
Presentation can include bookkeeping and driver blocking; it is not pure GPU
wait time. No GPU query or forced synchronization is added.

A fixed 120-frame ring maintains running totals. The chart uses completed-frame
averages, refreshes every 250 ms, and rebuilds on navigation or resizing. Until
the first complete sample it shows a waiting message. Percentages divide summed
elapsed times rather than averaging per-frame percentages. Disabling clears
history and partial samples; enabling starts with the next complete frame.
Disabled marks make no clock queries and allocate no dynamic storage.

These values are elapsed time, not CPU utilization, GPU execution, generation
latency, or process-wide CPU time. Workers overlap the main thread and must not
be added to this pie. Product update includes waits performed there but excludes
concurrent simulation work. The profiler's own costs remain included.

## Ownership and rendering

Illumo owns FrameProfiler. The runner/host mark existing phases; DebugModule
borrows it through an optional constructor argument. IllumoContext is unchanged.
The collector outlives modules and is main-thread affine. ProfilerOverlay owns
one bounded GameVisual with triangle-fan slices and text. Geometry persists
through submission, uses logical pixels and the Debug layer, and scales to fit
the window. Existing no-argument DebugModule and Renderer::EndFrame calls remain
supported.

InputManager::suppressKeyForFrame masks keyboard polling and bound actions until
the next update without manufacturing release events or changing queued events.
DebugModule consumes events separately and refreshes held-key capture before the
required product update.

## Verification

`Illumo.Profiler.Accounting` checks disabled behavior, exclusive totals, rolling
eviction, partial-frame reset, and invalid marks with injected timestamps.
`Illumo.Profiler.ControlsAndTokens` checks input ownership, held-key capture,
navigation, small-window fitting, renderer tokens, and the presentation boundary.
MockBackend tests do not establish live OpenGL appearance or native keyboard
behavior; those require a Debug/RelWithDebInfo visual smoke test.

The optional `IllumoCaptureGpuTests` target also renders root and small-window
Rendering views through real OpenGL and checks slice pixels. Run its default
suite or save a layout preview with deterministic example timings:

```powershell
cmake --build build --config Release --target IllumoCaptureGpuTests
build/Release/IllumoCaptureGpuTests.exe
build/Release/IllumoCaptureGpuTests.exe --profiler-preview build/profiler-preview.png
```

The preview refuses to overwrite an existing file. It verifies layout using
fixed timings, not a measured application workload.

### Implementation validation (2026-09-14)

- Windows/MSVC Release: full build and 377 workspace tests passed.
- Debug with configured ASan: 166 library cases passed; after building missing
  helper executables, the remaining three CTest checks passed (169 total).
- Ninja/Clang IllumoTidy: 137 first-party translation units passed. The GPU
  fixture was also checked directly after its final edit. Formatting passed.
- Real OpenGL root/small-child captures and the GPU suite passed; the root
  preview was visually inspected. Native application keyboard interaction and
  a RelWithDebInfo build were not exercised.
- Documentation PDFs rebuilt; existing LaTeX box warnings remain. The build
  also reports existing unused GuiKit parameters, the unused `spEqStr` test
  helper, deprecated `getenv` calls in rule-catalog tests, and Debug ASan's
  incremental-link warning.
- Collector-only benchmark: Intel Core i7-10700K, Windows x64, Clang `-O2`,
  seven runs of 200,000 frames, ten phase marks per frame and a 120-frame ring.
  Median 0.246 microseconds/frame (range 0.225–0.252), with analysis work also
  running. This excludes chart composition, drawing, and application workload;
  it is not an end-to-end frame-time or FPS claim.
