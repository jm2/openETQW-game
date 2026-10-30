#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fail if a Windows target's BUILD_STRING / CPUSTRING names the wrong architecture.

The version banner (SysCvar.cpp) prints BUILD_STRING, so an arm64 build that falls
into the x64 branch reports itself as win-x64 (game #61).  This takes the Windows
block of src/common/common.h, the one that starts at `#if !defined(_WIN64)`, runs
the C preprocessor over it with each target's predefined macros, and checks the
result.  A block the tool cannot find, or a compiler it cannot run, fails too.
"""
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src" / "common" / "common.h"

# predefined macros of each Windows target -> (BUILD_STRING, CPUSTRING)
TARGETS = {
    "win-x86": ([], ("win-x86", "x86")),
    "win-x64": (["_WIN64", "_M_X64", "_M_AMD64"], ("win-x64", "x64")),
    "win-arm64": (["_WIN64", "_M_ARM64"], ("win-arm64", "arm64")),
}


def windows_block(text):
    start = text.find("#if !defined(_WIN64)")
    if start < 0:
        return None
    depth = 0
    lines = text[start:].splitlines(keepends=True)
    for i, line in enumerate(lines):
        d = line.strip()
        if re.match(r"#\s*if", d):
            depth += 1
        elif re.match(r"#\s*endif", d):
            depth -= 1
            if depth == 0:
                return "".join(lines[: i + 1])
    return None


def main():
    block = windows_block(HEADER.read_text(encoding="utf-8", errors="replace"))
    if block is None:
        print(f"FAIL: no `#if !defined(_WIN64)` block found in {HEADER}")
        return 1
    # CC may hold a launcher and its compiler ("ccache gcc"); it must take gcc/clang flags
    cc = shlex.split(os.environ.get("CC", "")) or [c for c in (shutil.which(n) for n in ("cc", "gcc", "clang")) if c][:1]
    if not cc:
        print("FAIL: no C compiler found to run the preprocessor (set CC)")
        return 1
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "block.h"
        src.write_text(block + "\nBS=BUILD_STRING\nCS=CPUSTRING\n", encoding="utf-8")
        for name, (defs, want) in TARGETS.items():
            cmd = cc + ["-E", "-P", "-undef"] + [f"-D{d}" for d in defs] + [str(src)]
            try:
                out = subprocess.run(cmd, capture_output=True, text=True)
            except OSError as exc:
                print(f"FAIL {name}: cannot run {cmd[0]}: {exc}")
                failed += 1
                continue
            if out.returncode != 0:
                print(f"FAIL {name}: preprocessor exited {out.returncode}: {out.stderr.strip()}")
                failed += 1
                continue
            got = (re.search(r'^BS="([^"]*)"', out.stdout, re.M), re.search(r'^CS="([^"]*)"', out.stdout, re.M))
            got = tuple(m.group(1) if m else None for m in got)
            if got != want:
                print(f"FAIL {name}: BUILD_STRING/CPUSTRING {got}, want {want}")
                failed += 1
            else:
                print(f"ok   {name}: {got[0]} / {got[1]}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
