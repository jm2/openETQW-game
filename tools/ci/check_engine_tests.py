#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify the engine's asset-free unit tests ran against this game checkout.

Reads Meson's ``meson-logs/testlog.json`` from the engine build and checks
that every required test (default: the engine's asset-free ``unit`` suite
that exercises game-repo code: bitmsg, usercmd-delta, struct-layout,
usercmd-history, zero-sdk) was executed and passed.  A required test that is
missing, skipped, timed out or failed fails the check, so a renamed or
silently dropped test cannot turn the contract green.  ``--scan-sanitizer``
also fails when AddressSanitizer / UndefinedBehaviorSanitizer diagnostics
appear in any test output.

Meson's raw test logs embed the complete inherited environment of the test
run, so CI must not upload them; ``--report`` writes a sanitised JSON
(name, result, duration, return code, output tail) for artifacts instead.

Exit codes: 0 pass, 1 check failure, 2 usage error (log missing/unreadable).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

DEFAULT_REQUIRED = "bitmsg,usercmd-delta,struct-layout,usercmd-history,zero-sdk"
SANITIZER_RE = re.compile(
    r"ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:|SUMMARY: UndefinedBehaviorSanitizer|"
    r"SUMMARY: AddressSanitizer"
)
TAIL_CHARS = 4000


def short_name(name: str) -> str:
    # Meson formats names as "<suite> - <project>:<test>" (or "<project>:<test>").
    return name.rsplit(":", 1)[-1].strip() if ":" in name else name.strip()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("testlog", type=Path, help="path to meson-logs/testlog.json")
    parser.add_argument("--require", default=DEFAULT_REQUIRED, help=f"comma-separated test names (default: {DEFAULT_REQUIRED})")
    parser.add_argument("--scan-sanitizer", action="store_true", help="fail on ASan/UBSan diagnostics in test output")
    parser.add_argument("--report", type=Path, help="write a sanitised JSON report (no environment) here")
    parser.add_argument("--label", default="", help="label for the job summary heading")
    args = parser.parse_args(argv)

    if not args.testlog.is_file():
        print(f"error: {args.testlog} not found (did 'meson test' run?)", file=sys.stderr)
        return 2
    results: dict[str, dict] = {}
    try:
        for line in args.testlog.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.strip():
                continue
            entry = json.loads(line)
            output = (entry.get("stdout") or "") + (entry.get("stderr") or "")
            results[short_name(entry.get("name", "?"))] = {
                "name": entry.get("name"),
                "result": entry.get("result"),
                "returncode": entry.get("returncode"),
                "duration": round(float(entry.get("duration") or 0.0), 3),
                "outputTail": output[-TAIL_CHARS:],
                "sanitizerFindings": len(SANITIZER_RE.findall(output)),
            }
    except (OSError, ValueError) as exc:
        print(f"error: cannot parse {args.testlog}: {exc}", file=sys.stderr)
        return 2

    required = [name.strip() for name in args.require.split(",") if name.strip()]
    errors: list[str] = []
    for name in required:
        entry = results.get(name)
        if entry is None:
            errors.append(f"required test '{name}' did not run (renamed or not registered?)")
        elif entry["result"] != "OK":
            errors.append(f"required test '{name}' result {entry['result']} (return code {entry['returncode']})")
    for name, entry in sorted(results.items()):
        if name not in required and entry["result"] not in ("OK", "SKIP", "EXPECTEDFAIL"):
            errors.append(f"test '{name}' result {entry['result']}")
        if args.scan_sanitizer and entry["sanitizerFindings"]:
            errors.append(f"test '{name}' printed {entry['sanitizerFindings']} sanitizer diagnostic(s)")

    print(f"Engine unit tests ({args.testlog}):")
    for name, entry in sorted(results.items()):
        marker = "required" if name in required else "extra"
        print(f"  {name:<20} {entry['result']:<8} {entry['duration']:>7.2f}s  ({marker})")
    for message in errors:
        print(f"error: {message}", file=sys.stderr)
        if os.environ.get("GITHUB_ACTIONS", "").lower() == "true":
            print(f"::error title=Engine contract tests::{message}")

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps({"required": required, "errors": errors, "tests": results}, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            status = "failed" if errors else "passed"
            handle.write(f"### Engine unit tests{(' - ' + args.label) if args.label else ''}: {status}\n\n")
            handle.write("| test | result | duration | required |\n|---|---|---|---|\n")
            for name, entry in sorted(results.items()):
                handle.write(f"| {name} | {entry['result']} | {entry['duration']:.2f}s | {'yes' if name in required else 'no'} |\n")
            for name in required:
                if name not in results:
                    handle.write(f"| {name} | MISSING | - | yes |\n")
            handle.write("\n")

    if errors:
        print("FAILED: engine contract tests", file=sys.stderr)
        return 1
    print(f"PASSED: {len(required)} required engine unit tests")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
