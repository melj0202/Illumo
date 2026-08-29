# IllEd world editor

IllEd is the second in-tree Illumo application. It shares the host, renderer,
and `CreateIllumoApplication` seam with IllumoGame, but it does not simulate
cellular automata. It authors a persistent `SceneGraph` and writes `.ilsc`
files so later Illumo applications can load the same worlds.

Think of Illumo as the engine, IllEd as the editor that bootstraps products,
and IllumoGame as the first runtime product.

## Ownership

| Piece | Owner |
|---|---|
| Window, tokens, SceneGraph, dialogs, console host | Illumo |
| Editor document, toolbar, `.ilsc` codec, module factory | IllEd |
| CA grid, rulesets, `.illumo` | IllumoGame (untouched) |

`SceneGraph` remains a runtime hierarchy. Names, stable document ids, and
attachment recipes live in `EditorDocument`. Handles are never persisted.

## `.ilsc` version 1

UTF-8 JSON, pretty-printed, extension `.ilsc`:

```json
{
  "format": "ilsc",
  "version": 1,
  "camera": { "x": 0.0, "y": 0.0, "zoom": 32.0 },
  "nodes": [
    {
      "id": "n1",
      "parent": null,
      "name": "Root",
      "enabled": true,
      "visible": true,
      "transform": {
        "position": [0.0, 0.0, 0.0],
        "rotation": { "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0 },
        "scale": [1.0, 1.0, 1.0]
      },
      "kind": "empty"
    }
  ]
}
```

Known kinds: `empty`, `filled_rect`, `filled_ellipse`, `filled_triangle`,
`solid_cube`, `solid_pyramid`, `wire_sphere`. Geometric kinds store a
`primitive` object with `extent` and `color`. `world_mode` is `"2d"` or
`"3d"`. Unknown kinds, missing parents, and cycles fail closed. Extra keys
are ignored. Native dialogs use description `Illumo Scene` and pattern
`*.ilsc`.

## Prototype UI

A screen-space File / Edit / Create / View menu bar, a right-hand tool
sidebar (mode, primitive tools, scene inspector), and a status line, composed
with `GameVisual`. Tool chrome samples `Assets/IllEd/editor-ui-atlas.jpg`
as a 6x6 sprite atlas through the token path. The document stores a 2D or 3D world mode used for
presentation and picking. 2D kinds emit through `GameVisual`; 3D kinds use
`MeshVisual`. Editor grid and selection wireframes are not saved.
