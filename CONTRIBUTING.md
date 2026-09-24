<!-- SPDX-License-Identifier: MIT -->
# Contributing

`main` changes only through pull requests. Branch protection is not available on
these private repositories, so the merge gates below are a convention, not
enforced by GitHub. Keep to them anyway.

## Merge gates
1. **CI green.** Every required leg must pass:
   - `openETQW`: **CI Matrix**. Build legs skip cleanly while `openETQW-game` is private.
   - `openETQW-game`: **Commit Validation** and **Engine Contract**.
   - Experimental legs report without blocking.
2. **Bot review.** Codacy's check is resolved, and the ChatGPT Codex connector has reviewed when it is available (comment `@codex review`). Every finding is answered: fixed, or explained in the PR.
3. **Local evidence for runtime changes.** Anything touching the engine, the game or the network gets the full meson suite with retail data (`ETQW_DATA_PATH=... meson test -C build -j 1`), and the result goes in the PR.
4. **Merge commit.** Branches are merged with a merge commit, which keeps the per-fix history.

## Cross-repo changes
The engine builds against the game at the commit pinned in `openETQW/tools/ci/companion.ref`.
1. Merge the game PR first.
2. Pin that commit with `python3 tools/ci/bump_companion.py --sha <sha>`.
3. Merge the engine PR.

## Records
Record deliberate deviations from retail 1.5 in `openETQW/docs/compat-ledger.md`, and keep `openETQW/STATUS.md` honest about what was verified.
