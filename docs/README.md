# Illumo workspace documentation

This directory is the canonical home for the Illumo library and IllumoGame
product's first-party technical
documentation. Source-package folders contain code plus narrowly scoped
operational `AGENTS.md` guidance; their former descriptive README files are
preserved under `packages/`. Root/nested `AGENTS.md` and `.agent/` are the
intentional operational-guidance exceptions established by D-DOC2.

## Documentation tree

```text
docs/
  README.md                    This index and PDF build instructions
  architecture-consensus.md   Canonical current architecture and work order
  scene-graph-v1-design.md     Persistent hierarchy contract and rollout plan
  current-issues.md            Reproducible product/correctness punch list
  contributing.md              Project coding and dependency rules
  packages/                    Focused maps of the source packages
  sessions/                    Dated implementation and verification records
  history/                     Original requests and superseded material
  latex/
    illumo.tex                 Canonical prose-book PDF entrypoint
    architecture-map.tex      Current chart-only PDF entrypoint
    sections/                  Long-form chapters and formal decision log
    figures/                   Optional source figures
  output/                      Generated PDF output; never a source of truth
```

The root `README.md` remains the repository landing page. The root and nested
`AGENTS.md` files plus `.agent/` govern automation; they do not replace this
tree's architecture or decision records. Licenses and vendored dependency
READMEs stay beside the assets they govern.

## Reading order

1. Read `architecture-consensus.md` for current code truth, locked decisions,
   known issues, and work order.
2. Read `scene-graph-v1-design.md` when changing persistent scene ownership,
   hierarchy, transforms, or render attachment extraction.
3. Read `sessions/` when the rationale or exact verification history matters.
4. Read the LaTeX book for the long-form architecture and complete decision
   log.
5. Treat `history/` as provenance, not current implementation guidance.

If code and documentation disagree, code wins until the documentation is
updated in the same change.

## Build the PDF

Use the wrapper script from the repository root:

```powershell
.\docs\build.ps1
```

It creates `docs/output/` and passes the output-directory option to `latexmk`
as one PowerShell argument, avoiding the PowerShell argument-splitting issue.

To remove generated LaTeX output:

```powershell
.\docs\build.ps1 -Clean
```

The results are `docs/output/illumo.pdf` and
`docs/output/architecture-map.pdf`. A normal CMake build also invokes this
script through the `IllumoDocs` target when Windows PowerShell and `latexmk`
are on `PATH`; turn that off with `-DILLUMO_BUILD_DOCUMENTATION=OFF` at
configure time. Focused targets such as `IllumoTests`, `IllumoGameTests`, and
`IllumoGame` do not require TeX.

A full TeX installation is expected;
the book uses packages including `tcolorbox`, `booktabs`, `tikz`, `listings`,
`hyperref`, and `ulem`.

## Editing rules

| Change | Files to update |
|---|---|
| Runtime behavior or architecture | `architecture-consensus.md` and the relevant `latex/sections/*.tex` chapter |
| Closed design choice | The files above plus `latex/sections/09-design-decision-log.tex` |
| Product bug or resolved issue | `current-issues.md` and the matching current-state chapter |
| Significant implementation session | A dated file under `sessions/` |
| Package ownership or file map | `packages/` and the LaTeX file map |

Edit textual sources, not `.aux`, `.log`, `.toc`, or PDF output.
