# Vulkan backend guidance

This file specializes `Illumo/Source/Rendering/AGENTS.md` for
`Illumo/Source/Rendering/Vulkan/`.

## Status

This directory holds file stubs only. There is no Vulkan instance, device,
swapchain, or command submission yet; `VulkanBackend::Initialize()`
deliberately returns `false`. It is not composed into
`Illumo/Source/Rendering/OpenGL/CreateOpenGLBackend.cpp`'s call site or any
CMake target, and `Illumo/AGENTS.md` still names OpenGL as the only supported
rendering backend. Do not describe Vulkan as available until real
initialization, resource creation, and submission exist and are verified.

Adding the Vulkan SDK is a new third-party dependency and requires explicit
user approval plus a licensing, maintenance, build, and deployment assessment
(`AGENTS.md`) before any CMake wiring or `<vulkan/vulkan.h>` include lands.

## Scope and boundaries (once implemented)

This directory will be the sole implementation boundary for command execution
with Vulkan: backend creation, command decoding, resource registries, meshes,
textures, shaders, device/queue state, and draw submission, mirroring the
`OpenGL/` boundary. Do not leak Vulkan headers, handles, or state assumptions
into neutral Rendering, Game, or Services.

## Required invariants (once implemented)

Follow the same contract `OpenGL/AGENTS.md` states for its backend:
deterministic in-order token execution, registries owning concrete GPU
resources with safe partial-initialization and failure handling,
backend-neutral handles that never masquerade a failed create as valid,
preserved mesh/index/texture capacities and formats, and correct pipeline
layout/descriptor bindings for `FrameUniformsBlockName` at
`FrameUniformsBindingPoint`. Keep all raw Vulkan calls here; window/surface
creation remains in the shared window boundary.

## Verification (once implemented)

Use MockBackend tests to protect the neutral contract; a Vulkan-specific
live-window smoke test class (mirroring `IllumoGpuTests`) is required before
this backend replaces or supplements OpenGL in any build configuration.
Update this file only for durable Vulkan backend rules.
