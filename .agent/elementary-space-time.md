# Elementary 1D space-time advance

Authorized by the CA workshop program. Do not overload Moore `nextState(cell, count)`.

- `RuleSet::NeighborhoodKind::Elementary1D` uses `nextElementary(left, center, right)`.
- `SparseCellGrid::advance` takes a serial space-time path only: source row is the
  maximum Y among counted (`0`) cells; destination is `sourceY + 1` (torus wraps Y).
- Neighborhood at `x` is `(x-1, srcY)`, `(x, srcY)`, `(x+1, srcY)`. Infinite
  out-of-world cells are dead (`1`). Writes cover `[minX-1, maxX+1]` on the
  destination row; older rows remain frozen history.
- Dense `calcGeneration` remains Moore/compatibility. Worker-pool, halo, and
  frontier paths stay Moore-only.
