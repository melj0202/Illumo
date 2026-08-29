# Illumo Rendering

The supported reusable path is:

```text
Drawable::AppendCommands -> Renderer -> CommandQueue -> IBackend
                                             |            |
                                             |            +-- private OpenGL
                                             +-- ordered tagged-union tokens
```

IllumoGame consumes public rendering contracts from `Illumo/Include/Illumo`.
OpenGL implementation headers remain under `Illumo/Source/Rendering/OpenGL` and
are private. `MockBackend` is exposed only by `Illumo::TestSupport`.

`Rendering::Scene` is a non-owning ordered frame list. The separate retained
`SceneGraph` owns hierarchy nodes and appears in that list as one drawable.
Borrowed `ISceneRenderAttachment` implementations receive a resolved world
transform and append backend-neutral tokens; `MeshVisual` is the world
mesh/sprite adapter. Typed slot+generation resource handles, the bounded command
queue, managed `AssetManager`, painter-correct `GameVisual` overlay composition,
`WorldLook` `uMVP` contract, transforms, sprites/animation, text, and
primitive-composed UI retain their existing behavior.

`Renderer` captures window dimensions and the primary camera MVP once for each
`RenderScene` extraction. `GameVisual` consumes that transient frame context
when it shares the renderer's window and camera, while overlay draws push a
screen ortho as `uMVP`. `CanvasView` reuses upload-rectangle scratch storage and
`MeshVisual` keeps dynamic mesh handles, updating dirty vertex ranges instead of
recreating meshes. The product `Camera` is orthographic by default and can
switch to perspective look-at without a private view-projection helper.

The Debug renderer demo proves assets, sprites, transforms, animation, and
reload through the same library path consumed by IllumoGame. D-E6 supersedes
the prior deferred-extraction rule: the public static-library boundary now
exists. D-E8 adds a deliberately bounded persistent scene hierarchy for future
consumers. The current IllumoGame cellular-automata path does not instantiate a
graph and keeps the simulator's sparse domain and product UI unwired.
