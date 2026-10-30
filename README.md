<!-- SPDX-License-Identifier: MIT -->
# openETQW-game

The game module for [OpenETQW](https://github.com/jm2/openETQW), a native 64-bit engine for
*Enemy Territory: Quake Wars* (ET:QW) in the spirit of openQ4. OpenETQW runs the original
game from your own retail copy.

This repository holds the game code that the engine loads: id Software's ET:QW 1.5 SDK
code, which keeps the SDK's licence, with changes from jmarshall's reconstruction and new
files written for OpenETQW. The engine,
the build helpers and the tests are in [`openETQW`](https://github.com/jm2/openETQW); start
with its readme.

OpenETQW is an unofficial fan project. It is not affiliated with or endorsed by id Software,
Splash Damage, Activision or ZeniMax Media. Enemy Territory: Quake Wars, ET:QW, Quake and the
company names above are trademarks or registered trademarks of their respective owners.
OpenETQW does not use the game's logos or artwork.

## What you need

A retail copy of ET:QW that you own, patched to version 1.5. The engine checks the paks in
your install's `base/` folder at startup and won't run on unpatched or incomplete data (the
engine's readme lists the files). Neither repository contains any game data, executables or
media.

## Building

Clone this repository next to `openETQW` (the engine looks for it at `../openETQW-game`, or
wherever `OPENETQW_GAME_REPO` points). The engine's build helpers build both repositories
for you. To build only the game module:

```sh
meson setup build
meson compile -C build
```

This produces the SDK idLib (`build/libidLib.a`, which the engine links too), the decl
library and the game module itself (`build/game_<arch>.dylib`, `.so` or `.dll`). Build it
before configuring the engine.

## Checks

```sh
python3 tools/ci/check_spdx.py            # licence tags and the provenance allowlist
python3 tools/ci/check_no_gpl.py          # no GPL code in this repository
python3 tools/ci/check_build_string.py    # each Windows target names its own architecture
python3 tools/ci/check_event_handlers.py  # script event handlers match their definitions
python3 tools/ci/check_presentation_contract.py  # the vehicle presentation contract holds
python3 tools/ci/check_no_pii.py          # no private detail in tracked files
python3 tools/ci/check_sanitizer_scope.py # only the vptr exclusions engine decision 0005 allows
python3 tools/ci/selftest.py              # the checks above still catch what they exist to catch
actionlint                                # workflow lint
```

Add `--include-untracked` to `check_spdx.py` and `check_no_gpl.py` to check new files before
you commit them. New files carry an `SPDX-License-Identifier: MIT` line; the imported SDK
files carry no tag, and their class is recorded in `tools/ci/spdx_allowlist.txt` instead. The runtime tests for the game (maps, bots, matches, network play) are in
the engine repository.

## Layout

- `src/game/`: the game module.
- `src/idlib/`: the SDK idLib, built as a static library for the game module and the engine.
- `src/framework/`, `src/decllib/`, `src/bse/`, `src/sdnet/`, `src/renderer/`, `src/sound/`,
  `src/sys/`, `src/libs/`, `src/cm/`, `src/common/`: SDK interface headers and a few sources.
  The engine copies the headers it needs, plus `src/decllib/DeclType.cpp` and
  `src/idlib/BitMsg.cpp`, when it configures.
- `src/compiledscript/`: the compiled-script runtime from the reconstruction, and the game
  scripts in `scripts/`. None of it is part of the build. `scripts/events.script` is the
  same as retail 1.5's and is the reference for `check_event_handlers.py`, so keep it.
- `legal/`: the SDK EULA.
- `tools/ci/`: licence checks, CI helpers and the engine commit this repository is tested
  against (`engine.ref`).

## Licensing

- **SDK code**, modified or not, stays under the *Enemy Territory: Quake Wars SDK Limited
  Use License Agreement* (the SDK EULA): [`legal/EULA SDK ETQW.rtf`](legal/EULA%20SDK%20ETQW.rtf),
  with the same text in [`EULA_SDK_ETQW.txt`](EULA_SDK_ETQW.txt). Among other terms, it
  allows free, non-commercial distribution only, requires the full retail game, and forbids
  cheat programs. Read the EULA itself; this summary is not a substitute.
- **New files** written for this repository are under the MIT licence. [`LICENSE`](LICENSE)
  explains the split.
- **No GPL code** goes in this repository, because it can't be combined with the SDK code.
  `tools/ci/check_no_gpl.py` checks for it.
- **Early history.** OpenETQW began as a fork of jmarshall's ET:QW source reconstruction,
  and this repository's history starts with his commits. Code that came from his
  reconstruction keeps his authorship.

The engine's [`docs/provenance.md`](https://github.com/jm2/openETQW/blob/main/docs/provenance.md)
records where each file came from.

## Documents

- [`CONTRIBUTING.md`](CONTRIBUTING.md): how changes get in, and the checks to run first.
- [`LICENSE`](LICENSE): the licence split.
- In the engine repository: `STATUS.md` (what works today), `docs/compat-ledger.md` (where
  OpenETQW deliberately differs from retail 1.5) and `docs/provenance.md` (file origins and
  licences).
