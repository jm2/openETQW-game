#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Matrix planner for the openETQW-game workflows.

The tables below are the single place to add a CI leg or to mark one
experimental.  Experimental legs run their build/test steps with
``continue-on-error`` (the ``experimental`` matrix flag), so a failure is
reported as a warning and in the job summary but never blocks; every
experimental leg names the blocker in ``reason``.  Flip ``experimental`` to
False (and drop ``reason``) once the leg has been green.

Tables:

* ``build``      commit-validation.yml: idLib + game_<arch> per platform, then
                 tools/ci/check_game_module.py (name, format, exports, load).
* ``sanitizer``  commit-validation.yml: ASan+UBSan module load smoke.
* ``contract``   engine-contract.yml: this checkout's idLib + staged headers
                 against the pinned engine's asset-free unit tests.

Selection rules (identical for every table):

* Hosted ARM64 runners (ubuntu-24.04-arm, windows-11-arm) are only offered to
  public repositories.  While the repository is private those legs are
  skipped with a notice instead of queueing for a runner that never comes;
  they start running automatically once the repository is public.
  Repository variable OPENETQW_CI_ARM_RUNNERS = auto | on | off (default
  auto) overrides this.  The x64-hosted ARM64 cross build is not affected.
* workflow_dispatch input ``platforms``: ``default`` (or blank) = the normal
  set, ``all`` = everything incl. ARM64 regardless of visibility, or a comma
  list of leg ids / id prefixes (``linux``, ``macos-arm64``, ...).
* ``--experimental`` (repository variable OPENETQW_CI_EXPERIMENTAL for
  commit-validation, OPENETQW_CONTRACT_EXPERIMENTAL for engine-contract):
  comma list of leg ids that replaces the tables' experimental set without a
  commit (``none`` = nothing experimental).

Local dry run:
  CI_REPO_PRIVATE=true python3 tools/ci/ci_plan.py --tables build,sanitizer --pretty
  python3 tools/ci/ci_plan.py --list

Outputs (GitHub): ``<table>_matrix`` (JSON for ``fromJSON``) and
``<table>_count`` for every planned table.  Exit codes: 0 ok, 2 bad input.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import asdict, dataclass

ARM_RUNNERS = frozenset({"ubuntu-24.04-arm", "windows-11-arm"})
OSES = frozenset({"linux", "macos", "windows"})
ARCHES = frozenset({"x64", "arm64"})  # the engine loads game_<arch>


@dataclass(frozen=True)
class Leg:
    id: str
    name: str
    runner: str
    os: str
    arch: str  # target architecture (module name game_<arch>)
    compiler: str
    cc: str
    cxx: str
    cross: str = ""  # meson cross file in tools/ci/cross/ ('' = native build)
    python_arch: str = ""  # actions/setup-python 'architecture' ('' = runner default)
    buildtype: str = "release"
    sanitize: str = "none"  # meson b_sanitize value
    exports: str = "strict"  # check_game_module.py --exports (warn: extra exports are warnings)
    experimental: bool = False  # continue-on-error: reports, never blocks
    reason: str = ""  # why the leg is experimental (shown in the job summary)

    @property
    def arm_runner(self) -> bool:
        return self.runner in ARM_RUNNERS


# ----------------------------------------------------------------------------
# Known blockers (source fixes are tracked separately; see known gaps in the
# CI bring-up commit).  Keep these short: they are printed in job summaries.
# ----------------------------------------------------------------------------
LINUX_GAME = (
    "never built on Linux; src/common/common.h defines BUILD_STRING/CPUSTRING for Linux only on "
    "__i386__/__ppc__, so src/game/gamesys/SysCvar.cpp cannot compile for x86_64/aarch64"
)
LINUX_ARM64_GAME = (
    LINUX_GAME + "; src/idlib/Lib.cpp AssertFailed() uses x86 inline asm 'int $0x03' under __linux__"
)
MSVC_GAME = (
    "never built with MSVC; meson.build passes /permissive- (implies /Zc:strictStrings) and the SDK "
    "sources convert string literals to char* at ~476 sites (clang -Wwritable-strings), e.g. "
    "src/game/script/Script_Compiler.cpp, src/idlib/math/Simd.cpp, src/idlib/text/Lexer.cpp"
)
MSVC_ARM64_GAME = MSVC_GAME + "; Windows ARM64 never exercised"
LINUX_CONTRACT = "engine unit tests and idLib have never been built on Linux"
LINUX_ARM64_CONTRACT = (
    LINUX_CONTRACT + "; src/idlib/Lib.cpp AssertFailed() uses x86 inline asm 'int $0x03' under __linux__"
)
MSVC_CONTRACT = (
    "engine sources do not build on Windows yet (engine CI records the blockers) and idLib hits "
    "/Zc:strictStrings (string literal to char*) under the game's /permissive-"
)

TABLES: dict[str, list[Leg]] = {
    "build": [
        Leg("linux-x64-gcc", "Linux x64 (GCC)", "ubuntu-24.04", "linux", "x64", "gcc", "gcc", "g++",
            exports="warn", experimental=True, reason=LINUX_GAME),
        Leg("linux-x64-clang", "Linux x64 (Clang)", "ubuntu-24.04", "linux", "x64", "clang", "clang", "clang++",
            exports="warn", experimental=True, reason=LINUX_GAME),
        Leg("linux-arm64-gcc", "Linux ARM64 (GCC)", "ubuntu-24.04-arm", "linux", "arm64", "gcc", "gcc", "g++",
            exports="warn", experimental=True, reason=LINUX_ARM64_GAME),
        Leg("linux-arm64-clang", "Linux ARM64 (Clang)", "ubuntu-24.04-arm", "linux", "arm64", "clang", "clang", "clang++",
            exports="warn", experimental=True, reason=LINUX_ARM64_GAME),
        # Runs on an x64 runner, so it covers ARM64 while the hosted ARM64 runners are unavailable.
        Leg("linux-arm64-cross-gcc", "Linux ARM64 cross (GCC, x64 host)", "ubuntu-24.04", "linux", "arm64", "gcc",
            "gcc", "g++", cross="linux-arm64-gcc.ini", exports="warn", experimental=True, reason=LINUX_ARM64_GAME),
        Leg("macos-arm64", "macOS arm64 (Apple Clang)", "macos-15", "macos", "arm64", "apple-clang", "clang", "clang++"),
        Leg("macos-x64", "macOS Intel x64 (Apple Clang)", "macos-15-intel", "macos", "x64", "apple-clang", "clang", "clang++"),
        Leg("windows-x64", "Windows x64 (MSVC)", "windows-2025", "windows", "x64", "msvc", "cl", "cl",
            experimental=True, reason=MSVC_GAME),
        Leg("windows-arm64", "Windows ARM64 (MSVC)", "windows-11-arm", "windows", "arm64", "msvc", "cl", "cl",
            python_arch="arm64", experimental=True, reason=MSVC_ARM64_GAME),
    ],
    "sanitizer": [
        Leg("macos-arm64-asan-ubsan", "macOS arm64 ASan+UBSan (Apple Clang)", "macos-15", "macos", "arm64",
            "apple-clang", "clang", "clang++", buildtype="debugoptimized", sanitize="address,undefined"),
        Leg("linux-x64-clang-asan-ubsan", "Linux x64 ASan+UBSan (Clang)", "ubuntu-24.04", "linux", "x64", "clang",
            "clang", "clang++", buildtype="debugoptimized", sanitize="address,undefined", exports="warn",
            experimental=True, reason=LINUX_GAME),
    ],
    "contract": [
        Leg("linux-x64-gcc", "Linux x64 (GCC)", "ubuntu-24.04", "linux", "x64", "gcc", "gcc", "g++",
            experimental=True, reason=LINUX_CONTRACT),
        Leg("linux-x64-clang", "Linux x64 (Clang)", "ubuntu-24.04", "linux", "x64", "clang", "clang", "clang++",
            experimental=True, reason=LINUX_CONTRACT),
        Leg("linux-arm64-gcc", "Linux ARM64 (GCC)", "ubuntu-24.04-arm", "linux", "arm64", "gcc", "gcc", "g++",
            experimental=True, reason=LINUX_ARM64_CONTRACT),
        Leg("macos-arm64", "macOS arm64 (Apple Clang)", "macos-15", "macos", "arm64", "apple-clang", "clang", "clang++"),
        Leg("macos-x64", "macOS Intel x64 (Apple Clang)", "macos-15-intel", "macos", "x64", "apple-clang", "clang", "clang++"),
        Leg("windows-x64", "Windows x64 (MSVC)", "windows-2025", "windows", "x64", "msvc", "cl", "cl",
            experimental=True, reason=MSVC_CONTRACT),
        Leg("macos-arm64-asan-ubsan", "macOS arm64 ASan+UBSan (Apple Clang)", "macos-15", "macos", "arm64",
            "apple-clang", "clang", "clang++", buildtype="debugoptimized", sanitize="address,undefined"),
        Leg("linux-x64-clang-asan-ubsan", "Linux x64 ASan+UBSan (Clang)", "ubuntu-24.04", "linux", "x64", "clang",
            "clang", "clang++", buildtype="debugoptimized", sanitize="address,undefined",
            experimental=True, reason=LINUX_CONTRACT),
    ],
}


def validate_tables() -> list[str]:
    """Internal consistency of the tables (run by the static-checks job)."""
    problems: list[str] = []
    for table, legs in TABLES.items():
        seen: set[str] = set()
        for leg in legs:
            where = f"{table}/{leg.id}"
            if leg.id in seen:
                problems.append(f"{where}: duplicate id")
            seen.add(leg.id)
            if leg.os not in OSES:
                problems.append(f"{where}: unknown os {leg.os!r}")
            if leg.arch not in ARCHES:
                problems.append(f"{where}: unknown arch {leg.arch!r}")
            if leg.experimental and not leg.reason:
                problems.append(f"{where}: experimental legs must name their blocker in 'reason'")
            if not leg.experimental and leg.reason:
                problems.append(f"{where}: 'reason' set on a required leg (drop it when flipping to required)")
            if leg.exports not in ("strict", "warn"):
                problems.append(f"{where}: exports must be strict or warn")
            if leg.cross and not leg.cross.endswith(".ini"):
                problems.append(f"{where}: cross must name a meson cross file (*.ini) in tools/ci/cross/")
            if leg.cross and leg.os != "linux":
                problems.append(f"{where}: cross builds are only wired up for Linux")
            if leg.runner.startswith("windows") != (leg.os == "windows") or leg.runner.startswith("macos") != (leg.os == "macos"):
                problems.append(f"{where}: runner {leg.runner} does not match os {leg.os}")
    return problems


def parse_list(value: str) -> list[str]:
    return [item.strip() for item in value.replace(";", ",").split(",") if item.strip()]


def plan(
    tables: list[str], private: bool, selection: str, arm_policy: str, experimental_override: str
) -> dict[str, tuple[list[dict], list[tuple[Leg, str]]]]:
    selection = (selection or "default").strip().lower()
    arm_policy = (arm_policy or "auto").strip().lower()
    if arm_policy not in ("auto", "on", "off"):
        raise ValueError(f"OPENETQW_CI_ARM_RUNNERS must be auto, on or off (got {arm_policy!r})")
    unknown_tables = [t for t in tables if t not in TABLES]
    if unknown_tables or not tables:
        raise ValueError(f"unknown table(s) {', '.join(unknown_tables) or '<none>'}; known: {', '.join(TABLES)}")

    known = {leg.id for table in tables for leg in TABLES[table]}
    explicit: set[str] | None = None
    if selection not in ("default", "all"):
        explicit = set()
        for token in parse_list(selection):
            matches = {pid for pid in known if pid.startswith(token)}
            if not matches:
                raise ValueError(f"platform selection {token!r} matches nothing; known: {', '.join(sorted(known))}")
            explicit |= matches

    experimental_ids: set[str] | None = None
    if experimental_override.strip():
        tokens = parse_list(experimental_override)
        experimental_ids = set() if [t.lower() for t in tokens] == ["none"] else set(tokens)
        unknown = experimental_ids - known
        if unknown:
            raise ValueError(f"experimental override names unknown legs: {', '.join(sorted(unknown))}")

    result: dict[str, tuple[list[dict], list[tuple[Leg, str]]]] = {}
    for table in tables:
        selected: list[dict] = []
        skipped: list[tuple[Leg, str]] = []
        for leg in TABLES[table]:
            if explicit is not None and leg.id not in explicit:
                skipped.append((leg, "not selected by the workflow_dispatch 'platforms' input"))
                continue
            if leg.arm_runner:
                forced = selection == "all" or explicit is not None
                if arm_policy == "off":
                    skipped.append((leg, "hosted ARM64 runners disabled (OPENETQW_CI_ARM_RUNNERS=off)"))
                    continue
                if private and arm_policy == "auto" and not forced:
                    skipped.append(
                        (leg, "hosted ARM64 runners are only available to public repositories; runs automatically "
                         "once the repository is public (or set OPENETQW_CI_ARM_RUNNERS=on)")
                    )
                    continue
            entry = asdict(leg)
            entry["arm_runner"] = leg.arm_runner
            if experimental_ids is not None:
                entry["experimental"] = leg.id in experimental_ids
                if not entry["experimental"]:
                    entry["reason"] = ""
                elif not entry["reason"]:
                    entry["reason"] = "marked experimental by repository variable"
            entry["artifact"] = f"{'game' if table == 'build' else table}-{leg.id}"
            selected.append(entry)
        result[table] = (selected, skipped)
    return result


def write_summary(path: str, planned: dict[str, tuple[list[dict], list[tuple[Leg, str]]]]) -> None:
    with open(path, "a", encoding="utf-8") as handle:
        for table, (selected, skipped) in planned.items():
            handle.write(f"### CI plan: {table} ({len(selected)} of {len(TABLES[table])} legs)\n\n")
            handle.write("| leg | runner | status |\n|---|---|---|\n")
            for entry in selected:
                if entry["experimental"]:
                    status = f"runs, **experimental** (never blocks): {entry['reason']}"
                else:
                    status = "runs, required"
                if table == "build" and entry["exports"] != "strict":
                    status += "; extra exports reported as warnings"
                handle.write(f"| {entry['name']} | `{entry['runner']}` | {status} |\n")
            for leg, reason in skipped:
                handle.write(f"| {leg.name} | `{leg.runner}` | skipped: {reason} |\n")
            handle.write("\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tables", default="build", help=f"comma list of tables to plan ({', '.join(TABLES)})")
    parser.add_argument("--private", default=os.environ.get("CI_REPO_PRIVATE", "true"), help="'true' if the repository is private")
    parser.add_argument("--platforms", default=os.environ.get("CI_PLATFORMS", ""), help="dispatch selection")
    parser.add_argument("--arm-runners", default=os.environ.get("CI_ARM_RUNNERS", ""), help="auto | on | off")
    parser.add_argument("--experimental", default=os.environ.get("CI_EXPERIMENTAL", ""), help="override experimental ids")
    parser.add_argument("--pretty", action="store_true", help="human-readable dry run (no GitHub outputs/summary)")
    parser.add_argument("--list", action="store_true", help="list every table and exit")
    parser.add_argument("--check", action="store_true", help="validate the tables and exit")
    args = parser.parse_args(argv)

    problems = validate_tables()
    if problems or args.check:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        if not problems:
            print(f"OK: {sum(len(legs) for legs in TABLES.values())} legs in {len(TABLES)} tables are consistent")
        return 2 if problems else 0

    if args.list:
        for table, legs in TABLES.items():
            print(f"[{table}]")
            for leg in legs:
                flags = [f for f, on in (("experimental", leg.experimental), ("arm-runner", leg.arm_runner), ("cross", bool(leg.cross))) if on]
                print(f"  {leg.id:<28} {leg.runner:<18} {leg.name}" + (f"  [{', '.join(flags)}]" if flags else ""))
        return 0

    # Unknown visibility counts as private: never queue ARM64 legs that may not get a runner.
    private = str(args.private).strip().lower() not in ("false", "0", "no")
    try:
        planned = plan(parse_list(args.tables), private, args.platforms, args.arm_runners, args.experimental)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        if os.environ.get("GITHUB_ACTIONS", "").lower() == "true":
            print(f"::error title=CI plan::{exc}")
        return 2

    if args.pretty:
        print(f"repository private={private} selection={args.platforms or 'default'}")
        for table, (selected, skipped) in planned.items():
            print(f"[{table}] {len(selected)} leg(s)")
            for entry in selected:
                mode = "experimental" if entry["experimental"] else "required"
                print(f"  RUN   {entry['id']:<28} {entry['runner']:<18} {mode}")
            for leg, reason in skipped:
                print(f"  SKIP  {leg.id:<28} {reason}")
        return 0  # dry run: no GitHub outputs, notices or summary

    output = os.environ.get("GITHUB_OUTPUT")
    lines = []
    for table, (selected, _) in planned.items():
        lines.append(f"{table}_matrix={json.dumps(selected, separators=(',', ':'))}")
        lines.append(f"{table}_count={len(selected)}")
    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")
    else:
        print("\n".join(lines))

    if os.environ.get("GITHUB_ACTIONS", "").lower() == "true":
        noticed: set[str] = set()
        for table, (_, skipped) in planned.items():
            for leg, reason in skipped:
                if "public repositories" in reason and leg.id not in noticed:
                    noticed.add(leg.id)
                    print(f"::notice title=Skipped {leg.id}::{reason}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        write_summary(summary, planned)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
