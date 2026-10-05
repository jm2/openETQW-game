<!-- SPDX-License-Identifier: MIT -->
# openETQW-game

The game module for OpenETQW. OpenETQW is a native 64-bit replacement engine for *Enemy Territory: Quake Wars* (ET:QW), in the mold of openQ4. It runs the original game from retail game data that you supply.

This repository holds the game code that the engine loads, and every file that falls under the ET:QW SDK license. The engine is in the companion repository `openETQW`.

OpenETQW is an unofficial project. It is not affiliated with or endorsed by id Software, Splash Damage, Activision or ZeniMax Media. Enemy Territory: Quake Wars, ET:QW, Quake and the company names above are trademarks or registered trademarks of their respective owners. OpenETQW does not use the game's logos or artwork.

**Both repositories are private.** They stay private until the release gate is cleared and the owner decides to publish (see [Licensing](#licensing)). Nothing here is a release.

## Two repositories

| Repository | Contents | License |
|---|---|---|
| `openETQW` | Engine, platform layer, renderer, networking, tools, tests, docs, `STATUS.md` | GNU GPL, version 3 or later |
| `openETQW-game` (this one) | Game module source, the SDK idLib, SDK interface headers, the compiled-script tree | ET:QW SDK EULA for SDK-derived files; MIT for new files |

The layout follows openQ4 and openQ4-game. The engine and the game module are built together from the two repositories. Compatibility with the retail game module ABI is not a goal.

## Licensing

- SDK-derived files, modified or not, stay under the *Enemy Territory: Quake Wars* SDK Limited Use License Agreement (the SDK EULA). It is shipped verbatim in [`legal/EULA SDK ETQW.rtf`](legal/EULA%20SDK%20ETQW.rtf); [`EULA_SDK_ETQW.txt`](EULA_SDK_ETQW.txt) has the same text as plain text. The SPDX identifier used for these files is `LicenseRef-ETQW-SDK`. Among other terms, the EULA allows free, non-commercial distribution only, requires the full retail game, and forbids cheat programs. Read the EULA itself; this summary is not a substitute.
- New files written for this repository are MIT. [`LICENSE`](LICENSE) explains the split.
- No GPL code goes in this repository, because it cannot be combined with the SDK-licensed code. `tools/ci/check_no_gpl.py` checks for it.
- Imported SDK files carry no SPDX tag yet. Their license class is recorded in `tools/ci/spdx_allowlist.txt`, and `tools/ci/check_spdx.py` checks that every file under `src/`, `tools/`, `.github/` and the Meson files is tagged or listed. The charter asks for a `LicenseRef-ETQW-SDK` tag on SDK-derived files; the allowlist stands in for it for now.
- The `.script` files in `src/compiledscript/scripts/` are SDK text files (the SDK installer ships the same files; a few have edits), so they are under the SDK EULA, like the compiledscript runtime files that match SDK files.
- This repository's history starts from jmarshall's ET:QW source reconstruction (jmarshall/etqw), which has no license file. Whether his changes can be redistributed is unresolved; until it is (a licence from him or replacement code), release gate item 1 stays blocking.
- Whether the SDK EULA covers a module built for OpenETQW (it runs with the retail data, not the retail executable) is untested. The project accepts that risk, as openQ4 does for Quake 4, with two safeguards, both in place: the engine refuses to start without the retail 1.5 data (the engine's `fs_validateOfficialPaks`), and every package carries the licence texts (`meson install` puts `LICENSE`, `EULA_SDK_ETQW.txt` and `legal/EULA SDK ETQW.rtf` in `licenses/openETQW-game`; the engine's install set carries the EULA too). See the engine's `docs/decisions/0002-sdk-eula-risk-and-retail-install-check.md`.

The release gate, and which of its items are still open, is described in the engine repository's `readme.md` and `STATUS.md`. Item 2 (SDK code in the engine binaries) is approved as tracked exceptions, on openQ4's precedent, on condition that engine packages carry the SDK EULA, which the engine's install set does; the item stays open for the SDK files in the engine's early history (SDK EULA §5), and the other items are open.

## Retail game data is required

The game module needs the data from a legitimately owned retail ET:QW install, patched to version 1.5. You must own the game. Both the base game's paks and the 1.5 patch's paks are required in `base/`:

| Set | Files | Checked at startup |
|---|---|---|
| Base game | `pak000.pk4`–`pak004.pk4` | yes, by checksum |
| Official 1.5 patch | `pak005.pk4`–`pak008.pk4` | yes, by checksum |
| Language (base game) | `zpak_<language>000.pk4` | no |
| Language (1.5 patch) | `zpak_<language>001.pk4`–`003.pk4` | no |

This split follows the file list in the official 1.5 patch README. Unpatched data is not supported. The script-event definitions are the SDK's, which the retail 1.5 scripts and game module use. Neither repository contains retail game data, executables or media, and none may be added. The engine refuses to start unless `base/` holds `pak000`–`pak008` with their retail checksums (openQ4's retail-install check; the engine's `docs/official-pk4-checksums.md`).

## Network play: OpenETQW only

OpenETQW speaks its own network protocol, 20293.5 (defined in the engine's `src/framework/async/AsyncNetwork.h`). It cannot play with retail ET:QW 1.5 clients or servers; the server refuses a retail client at connect (tested with a simulated retail peer; no real retail 1.5 client has been tried). This follows openQ4: retail 1.5 network compatibility is not a goal. Retail 1.5 game data stays required and supported.

## Status

The game module and idLib build on macOS arm64 (Apple Clang), Linux x86_64 (GCC) and Windows x64 (MSVC), and the game runs on all three through the engine's test suite (maps, bots, network play). The engine repository's `STATUS.md` has the evidence, the known failures and the open items.

## Build on macOS

Use Apple Clang from the Xcode command line tools, Python 3, Meson and Ninja. Clone this repository next to `openETQW`: the engine looks for it at `../openETQW-game`, or at `$OPENETQW_GAME_REPO` when that is set. Build it before configuring the engine.

```sh
meson setup build
meson compile -C build
```

On macOS arm64 this produces `build/libidLib.a` (the SDK idLib, which the engine links too), `build/liblibGameDecl.a` and `build/game_arm64.dylib`. The engine's `readme.md` covers the engine build and how the engine finds the module.

## Checks

```sh
python3 tools/ci/check_spdx.py            # SPDX tags and the provenance allowlist
python3 tools/ci/check_no_gpl.py          # no GPL-family license markers
python3 tools/ci/check_event_handlers.py  # event handlers match their definitions, and events.script matches the definitions
python3 tools/ci/selftest.py              # the checks above still fail on what they exist to catch
actionlint                                # workflow lint
```

Add `--include-untracked` to `check_spdx.py` and `check_no_gpl.py` to check new files before they are committed. The runtime tests for the game module (maps, bots, matches, network play) are in the engine repository.

## Layout

- `src/game/`: the game module.
- `src/idlib/`: the SDK idLib, built as a static library for the game module and the engine. The engine is GPL; linking and compiling this SDK code into it is an owner-approved exception (the engine's `docs/decisions/0001-sdk-code-in-the-engine-build.md`), so engine packages ship the SDK EULA too.
- `src/framework/`, `src/decllib/`, `src/bse/`, `src/sdnet/`, `src/renderer/`, `src/sound/`, `src/sys/`, `src/libs/`, `src/cm/`, `src/common/`: SDK interface headers, and a few sources. When the engine configures, it copies the headers it needs, plus `src/decllib/DeclType.cpp` and `src/idlib/BitMsg.cpp`.
- `src/compiledscript/`: the compiled-script tree from the reconstruction, with the game scripts in `scripts/`. It is not part of the Meson build. `scripts/events.script` equals the retail 1.5 file and is the reference for `tools/ci/check_event_handlers.py`: keep it.
- `legal/`: the SDK EULA.
- `tools/ci/`: license checks, CI helpers and the engine pin (`engine.ref`).

## Documents

- [`CONTRIBUTING.md`](CONTRIBUTING.md): merge gates and local checks.
- [`LICENSE`](LICENSE): the license split.
- In the engine repository: `STATUS.md`, `docs/compat-ledger.md` (deliberate deviations from retail 1.5) and `docs/provenance.md` (file origins and licenses).
