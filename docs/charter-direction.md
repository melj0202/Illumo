# Design charter direction and implementation boundary

The owner supplied Illumo Design Charter version 1.0 on 2026-09-11 and authorized
the baseline and independent-rendering milestones. Its target identity is a
modular C++ game framework centered on rendering. A playable 3D corridor game,
CSim/IllumoGame and independent tools are concrete consumers. Reusable physics,
collision, audio, scene services and tooling are legitimate future engine scope;
application behavior and content meaning remain downstream.

The charter distinguishes confirmed direction, proposed defaults and open
decisions. It is not evidence that every capability exists. Keep current code
contracts in architecture-consensus.md and future work in this document and
the execution plan. Historical decisions remain provenance; obsolete CA-only
or renderer-only descriptions must not exclude a concrete approved consumer.

## Current milestones

Milestone 1 fixes input registration lifetime, records generated-project source
provenance and reconciles current documentation. Milestone 2 supplies the bounded
OpenGL capture service and tool documented in frame-capture.md. Engine/application
separation, synchronous token submission and existing scene hierarchy are reused.

## Later requirements, not implemented by these milestones

- Registered game definitions with validated values/actions, authoring context,
  coordinated exactly-once C++ object destruction and stale callback rejection.
- Shared scene access with one simulation authority; gameplay-plus-physics ticks,
  pause/step and deliberate paused-edit pose synchronization.
- A wrapped physics/collision implementation and audio as required by the game.
- Editor save/load of registered objects into a separately launched application.
- Scene-wide lighting/shadows, local/spot lights and visible beams with readable
  fallbacks. Current MeshVisual shadows are per-drawable self-shadowing.
- Platform-supported backend selection/capability outcomes and native Linux
  validation alongside Windows. macOS is not a current target; its scaffold has been removed.
- An agreed reference GPU, resolution, representative sequence and frame-time
  acceptance method for the 60 FPS target. No current capture timing proves it.

GLSL-to-SPIR-V/reflection, exact physics/audio libraries, a second production
graphics backend, a universal ECS, a render graph, ray tracing, WASM and live C++
reload are not selected by these two milestones. Game-specific engine versions
and deliberate API upgrades remain valid; copied source needs identifiable
provenance and explicit migration, not an automatic fork workflow.

The architecture-audit skill's CA-only sentence is stale relative to the root
project identity and this direction. Skills are not changed by this task; use
the live root boundary and this explicit direction when interpreting that advice.
