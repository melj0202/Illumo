# IllEd guidance

This file specializes the repository `AGENTS.md` for `IllEd/Source/`.

## Product identity

IllEd is an Illumo application: it uses the same runner, platform entry,
services, renderer, and `CreateIllumoApplication` seam as IllumoGame. Its job
is to author SceneGraph documents that later Illumo applications load. Treat it
as the in-tree editor bootstrap (Unreal Editor to Unreal), not as a second
cellular-automata product.

## Scope and boundaries

- Depend only on `Illumo::Illumo`. Do not link Game, Rulesets, or
  `IllumoGameCore`.
- Do not put names, components, or file I/O on `SceneGraph`. The editor
  document owns stable string ids, display names, and attachment recipes;
  the graph is a runtime view.
- Write UTF-8 JSON `.ilsc` version 1 with 2D/3D primitive kinds and
  `world_mode`. Do not write `.illumo` or serialize `SceneNodeHandle` values.
- Keep UI primitive-composed through `GameVisual`. No retained widget tree.
- Main-thread affine. Iterative hierarchy walks only.

## Persistence

`.ilsc` is the interchange contract between IllEd and future scene consumers.
Validate a complete document before replacing live state. Extra JSON keys may
be ignored; unknown kinds and cycles fail closed. Promote the codec into
Illumo only when a second loader exists.
