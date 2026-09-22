#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run a build command with a full log file and a readable console.

The SDK-era sources produce tens of thousands of compiler warnings, which
drown the GitHub log viewer.  This wrapper writes everything to ``--log``
and prints only ninja progress (every ``--progress`` percent), compiler
error lines and a closing summary; on failure it also prints the log tail.
The first compiler errors become GitHub annotations (GCC/Clang and MSVC
formats), with paths made relative to the repository.

  python3 tools/ci/run_logged.py --log .tmp/ci/compile.log --paths-from build -- meson compile -C build

Exit status is the wrapped command's.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

PROGRESS_RE = re.compile(r"^\[(\d+)/(\d+)\]")
GNU_ERROR_RE = re.compile(r"^(?P<path>[^:\s][^:]*):(?P<line>\d+):(?:(?P<col>\d+):)?\s*(?:fatal )?error:\s*(?P<msg>.*)$")
MSVC_ERROR_RE = re.compile(r"^(?P<path>.+?)\((?P<line>\d+)(?:,(?P<col>\d+))?\)\s*:\s*(?:fatal )?error (?P<code>[A-Z]+\d+):\s*(?P<msg>.*)$")
ERROR_LINE_RE = re.compile(
    r"(:\s*(?:fatal )?error\b|\berror [A-Z]+\d+:|^FAILED:|^ERROR:|undefined reference to|\bLNK\d{4}\b|"
    r"^(?:/usr/bin/)?ld(?:\.lld)?: |clang(?:\+\+)?: error|collect2: error)"
)
WARNING_RE = re.compile(r"(:\s*warning\b|\bwarning [A-Z]+\d+:)")
ANNOTATION_LIMIT = 20
ERROR_ECHO_LIMIT = 300


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--log", type=Path, required=True, help="full output log")
    parser.add_argument("--paths-from", type=Path, default=Path("."), help="directory compiler paths are relative to")
    parser.add_argument("--progress", type=int, default=10, help="print ninja progress every N percent")
    parser.add_argument("--tail", type=int, default=60, help="log lines to print on failure")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- command ...")
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("missing command after --")

    args.log.parent.mkdir(parents=True, exist_ok=True)
    root = Path.cwd().resolve()
    base = args.paths_from.resolve()
    annotate = os.environ.get("GITHUB_ACTIONS", "").lower() == "true"
    warnings = errors = annotations = echoed = 0
    seen_errors: set[str] = set()
    next_progress = args.progress
    tail: list[str] = []
    print(f"$ {' '.join(command)}   (full log: {args.log})", flush=True)
    with args.log.open("w", encoding="utf-8", errors="replace") as log:
        process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace"
        )
        assert process.stdout is not None
        for line in process.stdout:
            log.write(line)
            text = line.rstrip("\r\n")
            tail.append(text)
            if len(tail) > args.tail:
                tail.pop(0)
            progress = PROGRESS_RE.match(text)
            if progress:
                done, total = int(progress.group(1)), max(int(progress.group(2)), 1)
                percent = done * 100 // total
                if percent >= next_progress or done == total:
                    print(f"  progress {done}/{total} ({percent}%)", flush=True)
                    while next_progress <= percent:
                        next_progress += args.progress
                continue
            if WARNING_RE.search(text) and not ERROR_LINE_RE.search(text):
                warnings += 1
                continue
            match = GNU_ERROR_RE.match(text) or MSVC_ERROR_RE.match(text)
            if match or ERROR_LINE_RE.search(text):
                errors += 1
                if text.startswith("FAILED:"):
                    key = text
                elif match:
                    path = Path(os.path.normpath(base / match.group("path")))
                    try:
                        rel = path.relative_to(root).as_posix()
                    except ValueError:
                        rel = path.as_posix()
                    key = f"{rel}:{match.group('line')}: {match.group('msg')}"
                else:
                    key = text
                if key in seen_errors:
                    continue  # the same header error repeats for every translation unit
                seen_errors.add(key)
                if echoed < ERROR_ECHO_LIMIT:
                    print(text if not match else f"error: {key}", flush=True)
                    echoed += 1
                if match and annotate and annotations < ANNOTATION_LIMIT:
                    message = match.group("msg").replace("%", "%25")
                    print(f"::error file={rel},line={match.group('line')}::{message}", flush=True)
                    annotations += 1
        code = process.wait()

    print(
        f"exit status {code}; {warnings} warning line(s), {errors} error line(s) "
        f"({len(seen_errors)} distinct); full log: {args.log}"
    )
    if code != 0:
        print(f"----- last {len(tail)} log lines -----")
        print("\n".join(tail))
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(
                f"- `{' '.join(command)}`: exit {code}, {warnings} warning lines, {errors} error lines\n"
            )
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
