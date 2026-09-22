#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fail if GPL-family license markers appear in openETQW-game.

The two-repository charter keeps GPL code in the engine (jm2/openETQW) and
forbids GPL-only code in this game repository, which ships under the ETQW SDK
EULA plus MIT.  This scan looks at every tracked text file (not just the
header) for GPL/LGPL/AGPL notices, SPDX tags naming them, GNU license URLs,
FSF copyright notices, and the boilerplate id Software put on its Doom 3 era
source releases.

Files that legitimately mention the GPL (for example LICENSE, which says the
SDK files are *not* GPL) are listed in tools/ci/gpl_marker_allowlist.txt, one
path per line followed by a mandatory "# reason" comment.

The marker patterns are assembled from fragments so this script does not flag
itself.  Exit codes: 0 pass, 1 markers found, 2 usage / environment error.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ALLOWLIST = Path("tools/ci/gpl_marker_allowlist.txt")

_GNU = "GN" + "U"
_GPL = "G" + "PL"
_SPDX_TAG = "SPDX-" + "License-Identifier:"

MARKERS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("gnu-license-text", re.compile(_GNU + r"\s+(?:Affero\s+|Lesser\s+|Library\s+)?General\s+Public\s+Licen[cs]e", re.I)),
    ("spdx-gpl-family", re.compile(re.escape(_SPDX_TAG) + r"[^\r\n]*\b(?:A|L)?" + _GPL, re.I)),
    ("gpl-source-code", re.compile(r"\b" + _GPL + r"\s+Source\s+Code\b", re.I)),
    ("gnu-licenses-url", re.compile(r"gnu\.org/(?:licenses|copyleft)", re.I)),
    ("fsf-notice", re.compile(r"Free\s+Software\s+Foundation\s*,?\s+Inc", re.I)),
    ("gpl-version", re.compile(r"\b(?:A|L)?" + _GPL + r"(?:v|-)[123](?:\.0)?(?:\+|-or-later|-only)?\b")),
)

ANNOTATION_LIMIT = 40


def git_files(root: Path, include_untracked: bool) -> list[str]:
    commands = [["git", "-C", str(root), "ls-files", "-z", "--cached"]]
    if include_untracked:
        commands.append(["git", "-C", str(root), "ls-files", "-z", "--others", "--exclude-standard"])
    paths: set[str] = set()
    for command in commands:
        try:
            output = subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        except (OSError, subprocess.CalledProcessError) as exc:
            raise SystemExit(f"error: cannot list files with git in {root}: {exc}") from exc
        paths.update(raw.decode("utf-8", "surrogateescape") for raw in output.split(b"\0") if raw)
    return sorted(p for p in paths if (root / p).is_file())


def decode(data: bytes) -> str | None:
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16", "replace")
    if b"\0" in data[:8192]:
        return None  # binary
    return data.decode("utf-8", "replace")


def load_allowlist(path: Path) -> tuple[dict[str, str], list[str]]:
    entries: dict[str, str] = {}
    errors: list[str] = []
    if not path.is_file():
        return entries, [f"allowlist not found: {path}"]
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        rel, _, reason = line.partition("#")
        rel, reason = rel.strip(), reason.strip()
        if not reason:
            errors.append(f"{path.name}:{number}: entry {rel!r} needs a '# reason' justification")
        elif rel in entries:
            errors.append(f"{path.name}:{number}: duplicate entry {rel!r}")
        else:
            entries[rel] = reason
    return entries, errors


def annotate(level: str, path: str | None, line: int, message: str) -> None:
    if os.environ.get("GITHUB_ACTIONS", "").lower() != "true":
        return
    message = message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    location = f" file={path},line={line}" if path else ""
    print(f"::{level}{location}::{message}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="repository root (default: this checkout)")
    parser.add_argument("--allowlist", type=Path, default=None, help=f"allowlist (default: <root>/{DEFAULT_ALLOWLIST})")
    parser.add_argument("--include-untracked", action="store_true", help="also scan untracked, non-ignored files")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    allowlist, errors = load_allowlist((args.allowlist or root / DEFAULT_ALLOWLIST).resolve())
    files = git_files(root, args.include_untracked)

    findings: list[tuple[str, int, str, str]] = []
    scanned = binary = 0
    for rel in files:
        if rel in allowlist:
            continue
        try:
            text = decode((root / rel).read_bytes())
        except OSError as exc:
            errors.append(f"{rel}: unreadable: {exc}")
            continue
        if text is None:
            binary += 1
            continue
        scanned += 1
        for marker, pattern in MARKERS:
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                snippet = text[match.start() : match.end()].replace("\n", " ")
                findings.append((rel, line, marker, snippet))

    tracked = set(files)
    for rel in sorted(allowlist):
        if rel not in tracked:
            print(f"warning: stale GPL-marker allowlist entry: {rel}")
            annotate("warning", None, 0, f"stale GPL-marker allowlist entry: {rel}")

    print(f"GPL-marker scan: {scanned} text files scanned, {binary} binary skipped, {len(allowlist)} allowlisted")
    for index, (rel, line, marker, snippet) in enumerate(findings):
        message = f"GPL-family marker [{marker}] {snippet!r}: the game repository must not contain GPL-only code"
        print(f"error: {rel}:{line}: {message}", file=sys.stderr)
        if index < ANNOTATION_LIMIT:
            annotate("error", rel, line, message)
    for message in errors:
        print(f"error: {message}", file=sys.stderr)
        annotate("error", None, 0, message)

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(
                f"### GPL-marker scan: {'failed' if findings or errors else 'passed'}\n\n"
                f"{scanned} text files scanned, {len(findings)} marker hits, {len(allowlist)} allowlisted paths.\n\n"
            )

    if findings or errors:
        print("FAILED: GPL-marker policy violations (see above).", file=sys.stderr)
        return 1
    print("PASSED: no GPL-family license markers in tracked files.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
