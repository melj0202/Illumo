# Illumo Rendering

Scene keeps host fallback passes separate from application `SetLayerPasses`
overrides. Nonempty overrides win; empty lists, clear, and reset restore the
current fallback. Host motion-blur changes preserve overrides installed in
Start, Update, or dispatch. Effective pass queries include the fallback.

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
mesh/sprite adapter, including optional lighting, shadow mapping, and
previous-MVP motion blur. Immutable model geometry is reference-counted by
`AssetManager`; visuals keep only a non-owning `MeshHandle`, draw metadata, and
per-instance transform/tint state. Typed slot+generation resource handles, the
bounded command queue, painter-correct `GameVisual` overlay composition,
`WorldLook` `uMVP` contract, transforms, sprites/animation, text, and
primitive-composed UI retain their existing behavior.

`Renderer` captures window dimensions and the primary camera MVP once for each
`RenderScene` extraction. `GameVisual` consumes that transient frame context
when it shares the renderer's window and camera, while overlay draws push a
screen ortho as `uMVP`. `CanvasView` reuses upload-rectangle scratch storage and
`MeshVisual` keeps dynamic handles for procedural geometry, updating dirty
vertex ranges instead of recreating meshes, while managed model handles emit no
per-instance buffer upload. The product `Camera` is orthographic by default and
can switch to perspective look-at without a private view-projection helper.

The Debug renderer demo proves assets, sprites, transforms, animation, and
reload through the same library path consumed by IllumoGame. D-E6 supersedes
the prior deferred-extraction rule: the public static-library boundary now
exists. D-E8 adds the deliberately bounded persistent scene hierarchy consumed
by IllEd geometry and IllumoGame's opt-in `render3dTest` diagnostic. The
simulator's sparse cell domain and product UI remain separate from that graph.

Cubemap assets preserve their source kind and all six canonical dependencies
(or one cross path). Initial acquisition remains synchronous. Reloads decode
complete RGBA faces on the CPU queue and publish through
`Renderer::replaceCubemap` on pump. Missing or mismatched faces and rejected
replacements retain the previous ready resource and revision. Explicit path
reload and timestamp polling observe every face; cross orientation is shared
by initial and reload decoding.

The initial frame clear and ordinary layer boundaries explicitly select the
screen framebuffer and full-window viewport. Custom passes establish their own
targets. The OpenGL framebuffer cache starts unknown for each submission, so
screen binding cannot be skipped based on a stale zero value.

Pass color and depth clears are independent. Depth-clear tokens carry the
requested value; no-argument depth clears and combined screen clears default
to depth one instead of inheriting a previous pass's custom value.

Font atlas caches use a weak renderer lifetime identity instead of its address.
Each live renderer retains a separate enrollment; expired entries are pruned
and retired texture handles are reenrolled. Fonts do not own renderer or GPU
lifetimes. Renderer destruction invalidates its identity before backend
teardown, so shared fonts safely survive successive renderer lifetimes.
