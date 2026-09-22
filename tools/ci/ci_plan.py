#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build-matrix planner for .github/workflows/commit-validation.yml.

The platform table below is the single place to add a platform or to mark
one experimental (experimental legs run with continue-on-error, so they
report but never block).  The planner filters the table and prints the
matrix as JSON for ``strategy.matrix.include: ${{ fromJSON(...) }}``.

Selection rules:

* Hosted ARM64 runners (ubuntu-24.04-arm, windows-11-arm) are only offered to
  public repositories.  While the repository is private those legs are
  skipped with a notice instead of queueing forever; they start running
  automatically once the repository is public.  Repository variable
  OPENETQW_CI_ARM_RUNNERS = on | off | auto (default auto) overrides this,
  e.g. ``on`` for a private repo on a plan that has ARM64 runners.
* workflow_dispatch input ``platforms``: ``default`` (or blank) = the normal
  set, ``all`` = everything incl. ARM64 regardless of visibility, or a comma
  list of platform ids / prefixes (``linux``, ``macos-arm64``, ...).
* Repository variable OPENETQW_CI_EXPERIMENTAL (optional): comma list of
  platform ids that replaces the table's experimental set without a commit
  (``none`` = nothing experimental).

Local dry run:
  CI_REPO_PRIVATE=true python3 tools/ci/ci_plan.py --pretty

Exit codes: 0 ok, 2 bad selection.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import asdict, dataclass


@dataclass
class Platform:
    id: str
    name: str
    runner: str
    os: str  # linux | macos | windows
    arch: str  # x64 | arm64 (matches the engine's game_<arch> naming)
    compiler: str
    cc: str
    cxx: str
    # --- easy flags -------------------------------------------------------
    experimental: bool = False  # continue-on-error: reports, never blocks
    exports: str = "strict"  # 'warn' until the platform's build restricts exports
    arm_runner: bool = False  # hosted ARM64 runner (public repositories only)


# Linux exports are 'warn' until meson.build gives the Linux module a version
# script (or -fvisibility=hidden + --exclude-libs); today every non-static
# symbol of the game and idLib is exported.  Flip to 'strict' with that fix.
PLATFORMS = [
    Platform("linux-x64-gcc", "Linux x64 (GCC)", "ubuntu-24.04", "linux", "x64", "gcc", "gcc", "g++", exports="warn"),
    Platform("linux-x64-clang", "Linux x64 (Clang)", "ubuntu-24.04", "linux", "x64", "clang", "clang", "clang++", exports="warn"),
    Platform("linux-arm64-gcc", "Linux ARM64 (GCC)", "ubuntu-24.04-arm", "linux", "arm64", "gcc", "gcc", "g++", exports="warn", arm_runner=True),
    Platform("linux-arm64-clang", "Linux ARM64 (Clang)", "ubuntu-24.04-arm", "linux", "arm64", "clang", "clang", "clang++", exports="warn", arm_runner=True),
    Platform("macos-arm64", "macOS arm64 (Apple Clang)", "macos-15", "macos", "arm64", "apple-clang", "clang", "clang++"),
    Platform("macos-x64", "macOS Intel x64 (Apple Clang)", "macos-15-intel", "macos", "x64", "apple-clang", "clang", "clang++"),
    Platform("windows-x64", "Windows x64 (MSVC)", "windows-2025", "windows", "x64", "msvc", "cl", "cl"),
    Platform("windows-arm64", "Windows ARM64 (MSVC)", "windows-11-arm", "windows", "arm64", "msvc", "cl", "cl", experimental=True, arm_runner=True),
]


def parse_list(value: str) -> list[str]:
    return [item.strip() for item in value.replace(";", ",").split(",") if item.strip()]


def plan(event: str, private: bool, selection: str, arm_policy: str, experimental_override: str) -> tuple[list[dict], list[tuple[str, str]]]:
    selection = (selection or "default").strip().lower()
    arm_policy = (arm_policy or "auto").strip().lower()
    if arm_policy not in ("auto", "on", "off"):
        raise ValueError(f"OPENETQW_CI_ARM_RUNNERS must be auto, on or off (got {arm_policy!r})")

    known = {p.id for p in PLATFORMS}
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
        experimental_ids = set() if tokens == ["none"] else set(tokens)
        unknown = experimental_ids - known
        if unknown:
            raise ValueError(f"OPENETQW_CI_EXPERIMENTAL names unknown platforms: {', '.join(sorted(unknown))}")

    selected: list[dict] = []
    skipped: list[tuple[str, str]] = []
    for platform in PLATFORMS:
        if explicit is not None and platform.id not in explicit:
            skipped.append((platform.id, "not selected by the workflow_dispatch 'platforms' input"))
            continue
        if platform.arm_runner:
            forced = selection == "all" or explicit is not None
            if arm_policy == "off":
                skipped.append((platform.id, "hosted ARM64 runners disabled (OPENETQW_CI_ARM_RUNNERS=off)"))
                continue
            if private and arm_policy == "auto" and not forced:
                skipped.append(
                    (platform.id, "hosted ARM64 runners are only available to public repositories; "
                     "runs automatically once the repository is public (or set OPENETQW_CI_ARM_RUNNERS=on)")
                )
                continue
        entry = asdict(platform)
        if experimental_ids is not None:
            entry["experimental"] = platform.id in experimental_ids
        entry["artifact"] = f"game-{platform.id}"
        selected.append(entry)
    return selected, skipped


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--event", default=os.environ.get("CI_EVENT", "workflow_dispatch"))
    parser.add_argument("--private", default=os.environ.get("CI_REPO_PRIVATE", "true"), help="'true' if the repository is private")
    parser.add_argument("--platforms", default=os.environ.get("CI_PLATFORMS", ""), help="dispatch selection")
    parser.add_argument("--arm-runners", default=os.environ.get("CI_ARM_RUNNERS", ""), help="auto | on | off")
    parser.add_argument("--experimental", default=os.environ.get("CI_EXPERIMENTAL", ""), help="override experimental ids")
    parser.add_argument("--pretty", action="store_true", help="human-readable dry run (no GitHub outputs/summary)")
    parser.add_argument("--list", action="store_true", help="list known platform ids and exit")
    args = parser.parse_args(argv)

    if args.list:
        for platform in PLATFORMS:
            flags = [f for f, on in (("experimental", platform.experimental), ("arm-runner", platform.arm_runner)) if on]
            print(f"{platform.id:<20} {platform.runner:<18} {platform.name}" + (f"  [{', '.join(flags)}]" if flags else ""))
        return 0

    # Unknown visibility counts as private: never queue ARM64 legs that may not get a runner.
    private = str(args.private).strip().lower() not in ("false", "0", "no")
    try:
        selected, skipped = plan(args.event, private, args.platforms, args.arm_runners, args.experimental)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        if os.environ.get("GITHUB_ACTIONS", "").lower() == "true":
            print(f"::error title=Build plan::{exc}")
        return 2

    matrix = json.dumps(selected, separators=(",", ":"))
    if args.pretty:
        print(f"repository private={private} event={args.event} selection={args.platforms or 'default'}")
        for entry in selected:
            print(f"  RUN   {entry['id']:<20} {entry['runner']:<18} experimental={entry['experimental']} exports={entry['exports']}")
        for pid, reason in skipped:
            print(f"  SKIP  {pid:<20} {reason}")
    if args.pretty:
        return 0  # dry run: no GitHub outputs, notices or summary

    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write(f"matrix={matrix}\ncount={len(selected)}\n")
    if not output:
        print(matrix)

    if os.environ.get("GITHUB_ACTIONS", "").lower() == "true":
        for pid, reason in skipped:
            if "public repositories" in reason:
                print(f"::notice title=Skipped {pid}::{reason}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(f"### Build plan ({len(selected)} of {len(PLATFORMS)} platforms)\n\n")
            handle.write("| platform | runner | status |\n|---|---|---|\n")
            for entry in selected:
                status = "runs (experimental: reports, never blocks)" if entry["experimental"] else "runs (required)"
                if entry["exports"] != "strict":
                    status += "; extra exports reported as warnings"
                handle.write(f"| {entry['name']} | `{entry['runner']}` | {status} |\n")
            names = {p.id: p for p in PLATFORMS}
            for pid, reason in skipped:
                handle.write(f"| {names[pid].name} | `{names[pid].runner}` | skipped: {reason} |\n")
            handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
