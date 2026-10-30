#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Keep the sanitizer exclusions as narrow as the engine's decision 0005 allows.

Two vptr exclusions are allowed, nothing else:

* meson.build: UBSan's vptr check off for C++ on Linux (engine docs/decisions/0005,
  the owner, 2026-10-29), so the engine's unit tests can link this idLib;
* commit-validation.yml's instrumented module build: vptr off for the dynamically
  loaded module on every OS (since 2026-09-22; recorded in decision 0005), because
  the module cannot link the engine-side typeinfo.

This fails when a meson file, the meson options, a game source or a workflow turns
off another sanitizer check, makes one recoverable or excludes code from it, when
either vptr exclusion is reworded or moved, when the sanitizer legs stop building
with address,undefined, or when their ASan/UBSan failures stop being fatal. Game
twin of the engine's tests/test_sanitizer_scope.py, after openQ4's
tools/tests/linux_sanitizer_ci.py.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MESON_IF = "if host_system == 'linux' and cpp.has_argument('-fno-sanitize=vptr')"
MESON_ARG = "add_project_arguments('-fno-sanitize=vptr', language: 'cpp')"
MODULE_ARGS = 'args+=("-Dcpp_args=-fno-sanitize=vptr" "-Dcpp_link_args=-fno-sanitize=vptr")'
WEAKENING = re.compile(r"fno-sanitize|sanitize-recover|sanitize-blacklist|sanitize-ignorelist"
                       r"|no_sanitize|disable_sanitizer|sanitize=[^,'\"\s]*vptr")
SELF = "tools/ci/check_sanitizer_scope.py"

failures: list[str] = []


def tracked(*patterns: str) -> list[str]:
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "--", *patterns],
                         capture_output=True, text=True, check=False).stdout
    return [p for p in out.splitlines() if p and p != SELF]


def text(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def code_lines(rel: str):
    for n, line in enumerate(text(rel).splitlines(), 1):
        if not line.lstrip().startswith("#"):
            yield n, line


seen = {"meson": False, "module": False}
files = tracked("*meson.build", "meson_options.txt", "meson.options", ".github/*", "tools/ci/*",
                "src/*.c", "src/*.cpp", "src/*.h", "src/*.mm")
for rel in files:
    lines = text(rel).splitlines()
    for n, line in code_lines(rel):
        if not WEAKENING.search(line):
            continue
        stripped = line.strip()
        if rel == "meson.build" and stripped == MESON_IF:
            seen["meson"] = True
            continue
        if rel == "meson.build" and stripped == MESON_ARG and n >= 2 and lines[n - 2].strip() == MESON_IF:
            continue
        if rel == ".github/workflows/commit-validation.yml" and stripped == MODULE_ARGS:
            seen["module"] = True
            continue
        failures.append(f"{rel}:{n}: sanitizer exclusion outside the ones decision 0005 allows: {stripped}")
if not seen["meson"]:
    failures.append("meson.build: the Linux-only vptr exclusion is missing or reworded; change this check only with the decision")
if not seen["module"]:
    failures.append("commit-validation.yml: the module's vptr exclusion is missing or reworded; change this check only with the decision")


def require(rel: str, needle: str, why: str, count: int = 1) -> None:
    found = text(rel).count(needle)
    if found < count:
        failures.append(f"{rel}: {needle!r} found {found} time(s), want at least {count} ({why})")


CV = ".github/workflows/commit-validation.yml"
EC = ".github/workflows/engine-contract.yml"
for wf in (CV, EC):
    require(wf, "ASAN_OPTIONS: detect_leaks=0:halt_on_error=1:", "ASan reports fail the leg")
    require(wf, "UBSAN_OPTIONS: halt_on_error=1:", "UBSan reports fail the leg")
require(CV, '"-Db_sanitize=${CI_SANITIZE}"', "the planned sanitizers reach b_sanitize")
require(EC, '"-Db_sanitize=${CI_SANITIZE}"', "the planned sanitizers reach b_sanitize", 2)
plan = text("tools/ci/ci_plan.py")
values = re.findall(r'sanitize="([^"]*)"', plan)
if values.count("address,undefined") < 4 or any(v not in ("none", "address,undefined") for v in values):
    failures.append(f"tools/ci/ci_plan.py: sanitizer legs must use address,undefined (found {values})")

if failures:
    print("\n".join(failures))
    print("sanitizer-scope: FAILED")
    sys.exit(1)
print("sanitizer-scope: only the vptr exclusions decision 0005 allows")
