---
name: illumo-record-decision
description: Record a closed Illumo design decision (D-E, D-R, D-P, D-UI, D-F ...) and synchronize the canonical docs. Use when a change closes an architecture, rendering, performance, UI or foundation decision, when the user asks to log or document a decision, or at the end of an effort that introduced one. Documentation writes only; never commits.
---

# Record an Illumo design decision

AGENTS.md requires that an architecture change update
`docs/architecture-consensus.md` and the matching LaTeX chapter, and that a
closed decision receive a new formal log entry. Do all of it in one change.

## 1. Pick the ID

- Families seen in the log: `D-E` (engine/host), `D-R` (rendering), `D-P`
  (performance/platform pacing), `D-UI`, `D-F` (foundation/build policy).
  Confirm the family against neighbouring entries rather than guessing.
- Find the highest existing number in the family:
  `Select-String docs/latex/sections/09-design-decision-log.tex -Pattern '\\subsection\*\{D-E\d+'`
  and use the next one. Superseded entries keep their IDs; never reuse.

## 2. Write the log entry

Append to `docs/latex/sections/09-design-decision-log.tex`, matching the
existing shape:

```latex
\subsection*{D-XX## --- Short imperative title}
\begin{decisionbox}[title={D-XX## \quad YYYY-MM-DD}]
\textbf{Decision:} ...
\end{decisionbox}
```

Copy the remaining fields (rationale, consequences, alternatives, evidence)
from the most recent entry of the same family. Use today's absolute date.
Name symbols in `\texttt{}` and escape `_`, `&`, `%`, `#`.

**EOL trap:** this file has mixed line endings, and a whole-file rewrite or
some Edit paths renormalize unrelated blocks. Insert only the new block, then
check `git diff --numstat` against `git diff --numstat --ignore-cr-at-eol`;
the counts must match. If they differ, splice the entry into the HEAD bytes
instead.

## 3. Synchronize current-state docs

- `docs/architecture-consensus.md`: update the affected section and the
  decision catalog line.
- The matching LaTeX chapter in `docs/latex/sections/` (e.g.
  `05-rendering-current.tex`, `04-runtime-loop.tex`,
  `08-services-platform.tex`, `07-game-and-rules.tex`).
- `docs/packages/<package>.md` if package ownership changed; the effort's
  design/plan doc (mark the milestone done, link the D-ID).
- Closed open questions: remove or resolve them in `10-open-questions.tex`.
- Do not rewrite older dated entries or superseded decisions; add a
  "superseded by D-XX##" note instead.
- AGENTS.md only if a durable invariant, command or workflow changed.

## 4. Rebuild and check

- `./docs/build.ps1`; it must finish without LaTeX errors, and
  `docs/output/illumo.pdf` must be newer than the sources.
- Scan the new entry in the PDF (box renders, no overfull boxes from long
  `\texttt`).
- Review `git diff` for unrelated churn (see the EOL trap).

Report the ID, the files touched, and the build result. Leave committing to
the user.
