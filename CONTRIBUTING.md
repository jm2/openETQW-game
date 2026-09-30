<!-- SPDX-License-Identifier: MIT -->
# Contributing

`main` changes only through pull requests. Branch protection is not available on
these private repositories, so the merge gates below are a convention, not
enforced by GitHub. Keep to them anyway.

## Hosted CI is not running
Since 2026-09-24 the GitHub Actions minutes for these private repositories are used up.
Workflows do not start, so a missing check proves nothing either way. Until minutes are
available again, the local checks below replace CI. Do not make a repository public to
get CI back: both stay private until the owner decides to publish.

## Merge gates
1. **Local checks pass.** Run them from the repository root and put the result in the PR:
   - `python3 tools/ci/check_spdx.py` (SPDX tags and the provenance allowlist)
   - `python3 tools/ci/check_no_gpl.py` (no GPL-family license markers)
   - `python3 tools/ci/selftest.py` (the gates still fail on what they exist to catch)
   - `python3 tools/ci/check_event_handlers.py` (script event handlers match their event definitions, and every `events.script` declaration matches its native definition)
   - `actionlint` (workflow lint)

   Add `--include-untracked` to `check_spdx.py` and `check_no_gpl.py` to cover new files
   before they are committed.
2. **Retail-data suite for runtime changes.** Anything touching the game module, the engine
   or the network gets the engine's full Meson suite with retail data, run in the engine
   repository against this branch:
   `ETQW_DATA_PATH="$HOME/etqw_data" meson test -C build -j 1`
   (replace `$HOME/etqw_data` with the root of a full retail ET:QW install patched to 1.5:
   `base/` must hold `pak000`–`pak008.pk4`; the language paks, such as `zpak_english000`–`003`,
   are optional (they carry the dialogue); the unpatched DVD set is not supported). Check that
   the retail-data tests ran rather than skipped: a run that skips them does not meet this gate.
   Put the result in the PR with the host, OS, architecture, commit and data set. The tests use
   fixed UDP ports, so run one suite at a time on a host.
   **Vehicle or physics changes** (anything under `src/game/vehicles` or `src/game/physics` in this
   repository) must also pass `vehicle-physics-golden` (M5 step 5a; it is part of the full
   suite): the vehicle replay's `VEH_TRACE` must match the engine's `tests/golden/vehicle-physics/<toolchain>.txt`
   bit-exactly, or, on a toolchain with no golden, the reference golden at fixed precision (the
   test says which). A golden changes only on purpose, with the reason in its header:
   the engine's `tests/test_vehicle_physics_golden.py --regenerate --reason "..."`, and the PR says why.
3. **Independent review.** Someone other than the author (a person or a separate review
   session) reads the diff and the raw test output, not only the summary.
4. **CodeRabbit.** CodeRabbit reviews each PR. It currently allows about one review per
   hour: ask again with `@coderabbitai review` when the window opens, and say in the PR
   while it is out of quota. The ChatGPT Codex connector (`@codex review`) and Codacy, when
   they review, count the same way.
5. **Every finding is answered**, from the independent review and from the bots: fixed, or
   explained in the PR.
6. **Merge commit.** Branches are merged with a merge commit, which keeps the per-fix history.

When Actions minutes are available again, the **Commit Validation** and **Engine Contract**
workflows must also be green. Experimental legs report without blocking.

## Cross-repo changes
The engine builds against the game at the commit pinned in `openETQW/tools/ci/companion.ref`.
1. Merge the game PR first.
2. On the engine branch, pin the merged game commit with
   `python3 tools/ci/bump_companion.py --sha <full 40-character sha>`.
3. Merge the engine PR.

This repository pins the engine for the Engine Contract workflow in `tools/ci/engine.ref`.
Move it with `python3 tools/ci/bump_engine.py --from-local ../openETQW` or a full SHA
(`--remote <branch>` needs anonymous access to the engine repository, so it fails while the
repositories are private; add `--no-verify` to pin a SHA without fetching).

## History rewrite (2026-09-24)
Both repositories' histories were rewritten on 2026-09-24 to remove a retail file
(`src/sys/win32/rc/res/etqw_splash.bmp`, ET:QW cover art) from every commit. `main` and
`wip/stock-maps-m2-checkpoint` were force-pushed. Every commit ID changed: game `main`
is now `24f58bf` (was `285eaa1`), engine `main` is `84a224b` (was `2104090`).
- Re-clone any clone made before the rewrite, or hard-reset each branch to its new remote
  branch. Carry unpushed work over by cherry-picking it onto the new commits.
- Never push a branch or tag that still contains pre-rewrite commits: that would bring
  the file back.
- The historical `tools/ci/engine.ref` pins in this repository name engine commits that
  no longer exist. Engine commit IDs quoted in older engine docs are pre-rewrite IDs too.
- GitHub still keeps `refs/pull/1/head` in each repository, pointing at the old history
  (game `9b3a02b`, engine `53d0172`). Removing it needs GitHub Support or new
  repositories. The owner deferred it on 2026-09-25 and expects to recreate the repositories;
  when that happens, check that no ref reaches the purged bitmap.

## What never goes in
- Retail game data, retail executables, PDBs, raw disassembly or decompiler output, and
  packet captures. This holds for both repositories and their whole history.
- GPL code. It cannot be combined with the SDK-licensed code here.
- SDK-derived files labeled as anything but SDK. They keep their notices and stay under the
  SDK EULA (`LicenseRef-ETQW-SDK`), modified or not; correcting a wrong label to SDK, as
  on 2026-09-24 for `src/compiledscript`, is fine. New files carry
  `SPDX-License-Identifier: MIT`.

## Records
Record deliberate deviations from retail 1.5 in `openETQW/docs/compat-ledger.md` and file
origins in `openETQW/docs/provenance.md`. Keep `openETQW/STATUS.md` honest about what was
verified: each claim carries its evidence level (`inspected` < `configured` < `compiled` <
`unit-tested` < `launched` < `map-loaded` < `match-tested`), host, OS, architecture and commit.
