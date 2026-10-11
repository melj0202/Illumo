---
name: ship
description: Finish the current Illumo effort - verify it, commit only its files, fast-forward it into the current month's release branch, and push when asked. Use only when the user runs /ship or explicitly asks to commit and merge finished work.
argument-hint: "[push]"
disable-model-invocation: true
---

# Ship the current effort

Running `/ship` is the user's instruction to commit the current effort and,
from a feature branch, fast-forward it into the current release branch.
Push only when the arguments contain `push` or the user says so in this
session. Anything beyond that (rebase, force, reset, deleting branches or
worktrees, fetch or pull) still needs its own instruction.

Use the PowerShell tool throughout. The `windows-agent-shell` skill has the
commit-message, push and pipe-race traps; read it before step 5.

## 1. Establish the target

1. `git status --short`, `git branch -vv` and `git worktree list`.
2. The target is the current month's `release/vYY.MM` (today's date;
   `python build.py version` should print the same month). If HEAD is on
   `main`, an older release branch, or a detached HEAD, stop and ask.
3. If HEAD is a feature branch, check the release branch is an ancestor of
   it (`git merge-base --is-ancestor`). If not, stop and ask: reconciling
   needs its own instruction.

## 2. Scope the commit

1. List every modified and untracked file and sort them into this effort,
   other work, and strays (logs, captures, scratch files, build output,
   `envvars.json`, untracked `Assets/` material). Only this effort's files
   go in. If the split is unclear, ask with the list rather than guessing.
2. Several distinct efforts in the tree mean one commit each, in dependency
   order. Do not squash them.
3. Never `git add -A` or `git add .`; stage explicit paths with
   `git add -- <paths>`, then check `git diff --cached --stat`.

## 3. Clean the diff

1. On touched C++ files, format only changed lines; if a formatter
   reformatted lines you did not change, use the `strip-format-churn`
   skill. Confirm no include or line was dropped.
2. `git diff --check`, and the EOL comparison from `windows-agent-shell`
   (`--numstat` with and without `--ignore-cr-at-eol`) for edited files.
3. Read the full staged diff once for debug output, stray TODOs, and
   generated or hand-edited outputs (PDFs, staged runtime assets).

## 4. Verify

Follow `illumo-verify-change`. The full suite is not run here; it runs in
step 7, before a push. For a commit, build only the affected targets and
run the exact cases and package tests that cover the change, e.g.:

```powershell
cmake --build build --config Release --target IllumoGameTests
build/Release/IllumoGameTests.exe --run <IllumoGame.exact-name>
ctest --test-dir build -C Release -R "<package-test-regex>" --output-on-failure
```

- Before linking, make sure no `IllumoRuntime.exe` or test runner from this
  tree is still running; never rebuild while a suite runs from the same
  tree. A one-off file lock on link (e.g. Defender) gets one retry.
- Add `python build.py tidy`, Debug/ASan, coverage, captures or benchmarks
  when the change's risk calls for them.
- A visual change needs a capture from a build of the configuration being
  shown, made after the last edit (`illumo-visual-capture-check`).
- Any failure stops the ship. Pre-existing failures that also fail on the
  target branch are reported by name, not fixed, and the user decides
  whether to ship anyway.
- Documentation-only or skill-only changes need no build; say so.

## 5. Docs, then commit

1. Behaviour, contract or architecture changes update their canonical
   docs in the same commit (`docs/architecture-consensus.md`, the LaTeX
   chapter, a decision via `illumo-record-decision`). Ask if a needed doc
   update is missing rather than committing without it.
2. Write the message to a file in the scratchpad and run
   `git commit -F <file>`. House style: an imperative subject under ~72
   characters with the decision ID in parentheses when there is one, a
   blank line, wrapped prose paragraphs on what changed and why, a line
   naming new or replaced tests, then the attribution trailer.
3. Never `--no-verify` or `--amend` unless asked. If a hook fails, fix the
   cause and make a new commit.

## 6. Merge into the release branch

Skip when HEAD is already the release branch. Otherwise follow
`csim-integrate-feature-branch`: `git merge --ff-only <feature>` in the
worktree that has the release branch checked out, then confirm its tip is
the feature tip. A fast-forward that fails is a stop, not a merge commit.

## 7. Push (only when asked)

1. List `git log --oneline origin/<release>..<release>` and check for
   commits that are not this effort's (other sessions may have merged).
2. Run the full suite on the release tip being pushed:

   ```powershell
   cmake --build build --config Release
   ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure
   ```

   (A tree configured with `ILLUMO_RUN_TESTS_ON_BUILD=ON` already ran the
   suite during the build; skip the `ctest` line there.)

   Any failure blocks the push. Find the cause, fix it in a new commit
   (through steps 2-6), and rerun the suite before pushing. Pre-existing
   failures are named, and the user decides whether to push anyway.
3. Push with the `gh` credential helper from `windows-agent-shell`, then
   confirm the local and remote-tracking tips match.

## 8. Report

Commit hashes and subjects, the branch each landed on, build configuration
and test totals (passed, failed, skipped), checks not run and why, files left
uncommitted, and whether anything was pushed.
