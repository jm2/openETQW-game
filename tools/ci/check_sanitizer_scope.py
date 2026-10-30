#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Keep the sanitizer exclusions as narrow as decision 0005 allows.

The owner allowed exactly one exclusion (the engine repository's docs/decisions/0005, 2026-10-29): UBSan's
vptr check, off on Linux, in meson.build. This fails when any build file or the
validation tool turns off another sanitizer check, turns vptr off elsewhere or
unconditionally, or when the exclusion stops being Linux-only. After openQ4's
tools/tests/linux_sanitizer_ci.py. Game twin of the engine's tests/test_sanitizer_scope.py.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ALLOWED = "if host_system == 'linux' and cpp.has_argument('-fno-sanitize=vptr')"
# what turns a check off or makes a failure non-fatal
WEAKENING = re.compile(r"-fno-sanitize[=-]|-fsanitize-recover|-fsanitize-blacklist|-fsanitize-ignorelist|no_sanitize")

failures = []


def scan(path: Path) -> list[tuple[int, str]]:
    return [(n, line) for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
            if WEAKENING.search(line) and not line.lstrip().startswith("#")]


files = [ROOT / "meson.build"]
files += sorted((ROOT / "src").rglob("meson.build"))
for path in files:
    if not path.is_file():
        continue
    hits = scan(path)
    rel = path.relative_to(ROOT).as_posix()
    if rel != "meson.build":
        failures += [f"{rel}:{n}: sanitizer exclusion outside the one decision 0005 allows: {line.strip()}" for n, line in hits]
        continue
    lines = path.read_text(encoding="utf-8").splitlines()
    for n, line in hits:
        stripped = line.strip()
        if stripped == "add_project_arguments('-fno-sanitize=vptr', language: 'cpp')" and n >= 2 and lines[n - 2].strip() == ALLOWED:
            continue
        if stripped == ALLOWED:
            continue
        failures.append(f"meson.build:{n}: sanitizer exclusion outside the one decision 0005 allows: {stripped}")
    if not any(line.strip() == ALLOWED for line in lines):
        failures.append("meson.build: the Linux-only vptr exclusion of decision 0005 is missing or reworded; update this test with the decision")

if failures:
    print("\n".join(failures))
    print("sanitizer-scope: FAILED")
    sys.exit(1)
print("sanitizer-scope: only the Linux UBSan vptr exclusion (decision 0005)")
