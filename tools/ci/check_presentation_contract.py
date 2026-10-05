#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fail if the seat presentation (M5 step 14) breaks its contract with tick state.

sdPresentation (src/game/Presentation.{h,cpp}) draws allowed vehicle seats from tick
samples. The engine's docs/vehicle-responsiveness.md 3.1 lists what a render frame may
do: write only the presentation's own members, the drawn view and renderer defs through
copies that are restored after the draw; never call anything that changes game state.
This checks the presentation's source, with comments and strings removed:

* no call of Present, BecomeActive, UpdateVisuals, UpdateModelTransform, GetWorldOrigin,
  GetWorldAxis, LinkCombat, ClipView, ClampViewAngles, CalculateViewPos, Decay, Think,
  RunPhysics, SetOrigin, SetAxis, UpdateEntityDef or the game's random number generator
  (renderer defs go through idGameLocal::SubmitPresentationCopy, which restores them);
* no assignment to lastPushedOrigin/Axis, renderEntity, viewAngles, clientViewAngles,
  vehicleEye*, firstPersonView*, renderView, or any member of gameLocal or unlock;
* sdPresentation and the presentation member are referenced only from the listed hooks
  (Game_local.h, Game_local.cpp, Game_network.cpp, PlayerView.cpp).

Exit codes: 0 pass, 1 violations found, 2 usage / environment error.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FORBIDDEN_CALLS = (
    "Present", "BecomeActive", "UpdateVisuals", "UpdateModelTransform", "GetWorldOrigin",
    "GetWorldAxis", "LinkCombat", "ClipView", "ClampViewAngles", "CalculateViewPos", "Decay",
    "Think", "RunPhysics", "SetOrigin", "SetAxis", "UpdateEntityDef",
)
FORBIDDEN_TOKENS = ("gameLocal.random", "random.RandomInt", "random.RandomFloat")
FORBIDDEN_TARGETS = re.compile(
    r"(?<![\w.])(?:[\w>.\-]*(?:->|\.))?(lastPushedOrigin|lastPushedAxis|renderEntity|viewAngles|clientViewAngles"
    r"|vehicleEye\w*|firstPersonView\w*|renderView)\s*(?:\.\w+\s*)*(?:\[[^\]]*\]\s*)*=(?!=)")
GAME_STATE_WRITE = re.compile(r"\b(?:gameLocal|unlock)\s*\.\s*[\w.\[\]]+\s*(?:[+\-*/]?=)(?!=)")
HOOK_FILES = {"Game_local.h", "Game_local.cpp", "Game_network.cpp", "PlayerView.cpp", "Presentation.h", "Presentation.cpp"}
REFERENCE = re.compile(r"\bsdPresentation\b|\bpresentation\s*\.")


def strip_comments_and_strings(text: str) -> str:
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            out.append("\n" * text.count("\n", i, n if j < 0 else j))
            i = n if j < 0 else j + 2
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def check_presentation(path: Path) -> list[str]:
    problems = []
    code = strip_comments_and_strings(path.read_text(encoding="utf-8", errors="replace"))
    for lineno, line in enumerate(code.splitlines(), 1):
        for name in FORBIDDEN_CALLS:
            if re.search(rf"(?<![\w])(?:[\w>.\-]*(?:->|\.|::))?{name}\s*\(", line) and \
                    not re.search(rf"\b(?:void|bool|int|float)\s+[\w:]*{name}\s*\(", line):
                problems.append(f"{path.name}:{lineno}: calls {name}: {line.strip()}")
        for token in FORBIDDEN_TOKENS:
            if token in line:
                problems.append(f"{path.name}:{lineno}: uses {token}: {line.strip()}")
        m = FORBIDDEN_TARGETS.search(line)
        if m:
            problems.append(f"{path.name}:{lineno}: assigns {m.group(1)}: {line.strip()}")
        if GAME_STATE_WRITE.search(line):
            problems.append(f"{path.name}:{lineno}: writes game state: {line.strip()}")
    return problems


def check_references(game: Path) -> list[str]:
    problems = []
    for path in sorted(game.rglob("*")):
        if path.suffix not in (".cpp", ".h") or path.name in HOOK_FILES:
            continue
        code = strip_comments_and_strings(path.read_text(encoding="utf-8", errors="replace"))
        for lineno, line in enumerate(code.splitlines(), 1):
            if REFERENCE.search(line):
                problems.append(f"{path.relative_to(game)}:{lineno}: references the presentation outside its hooks: {line.strip()}")
    return problems


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[2]), help="the repository root")
    args = ap.parse_args(argv)
    game = Path(args.root) / "src" / "game"
    files = [game / "Presentation.cpp", game / "Presentation.h"]
    if not all(f.is_file() for f in files):
        print(f"check_presentation_contract: no src/game/Presentation.{{h,cpp}} under {args.root}", file=sys.stderr)
        return 2
    problems = []
    for f in files:
        problems += check_presentation(f)
    problems += check_references(game)
    for p in problems:
        print(p)
    print(f"check_presentation_contract: {len(problems)} violation(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
