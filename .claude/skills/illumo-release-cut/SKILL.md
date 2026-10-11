---
name: illumo-release-cut
description: Start a new monthly Illumo release branch (release/vYY.MM), cut its version, and build and check a distribution. Use only when the user explicitly asks to start a month's release branch, cut a version, or assemble a dist build.
disable-model-invocation: true
---

# Illumo release cut

The owner works on one branch per month, `release/vYY.MM`, merged into `main`
by GitHub PR when the month ends. Feature work lands on the current month's
branch, not on `main` or an older release branch. Every Git step below needs
the user's explicit instruction in this session.

## Start a month

1. Confirm the previous month's PR is merged into `main`, and that `main` is
   current locally (the user decides whether to fetch).
2. Create `release/vYY.MM` from `main` (`git branch -vv` shows which month
   is current).
3. **Immediately** run `python build.py version --set YY.MM`. This updates
   `VERSION.txt` (D-F2); without it builds keep showing the old month. Never
   edit the version by hand or add `version` to a source manifest. Commit as
   "Start release vYY.MM".
4. `python build.py version` should print `vYY.MM_B`.

## Distribution build

- `python build.py dist` assembles `<build>/dist/<config>/`: the runtime,
  `engine.ilpk`, `apps/<name>.ilpk`, and on Windows a `.lnk` shortcut (with
  `.ico`) per app (D-E38, D-E39).
- Check: launch from the shortcut; the splash and Powered-by badge show; each
  app starts from its `.ilpk`.
- Staging never deletes. Old build trees can keep stale loose files (e.g.
  `apps/meshviewer/Assets/Skybox/`). Loose files win over archives, so test
  the dist folder, not the dev tree.
- `python build.py doctor` reports stale pre-WASM outputs to delete.
- Linux has no runnable distribution or shortcut yet; do not claim parity.

## Push and PR

- Push only when asked; use the `windows-agent-shell` skill for the
  `gh`-credential push. List `git log --oneline origin/<branch>..<branch>`
  first.
- The month-end PR goes from `release/vYY.MM` into `main`.
