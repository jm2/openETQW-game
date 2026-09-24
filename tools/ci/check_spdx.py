#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""License / provenance header gate for openETQW-game.

Convention enforced (matches the tree as of the CI bring-up):

* New game-side files and build/CI infrastructure carry an
  ``SPDX-License-Identifier`` tag in their first few lines (MIT for new work).
* Imported ETQW SDK files (``LicenseRef-ETQW-SDK``, including the SDK's
  compiled-script runtime and game script text media under
  ``src/compiledscript``) and the reconstruction's compiled-script build files
  (MIT) are unmarked.  Their classification is
  recorded in ``tools/ci/spdx_allowlist.txt``, which was seeded from the
  engine repository's provenance manifest (``docs/provenance-manifest.json``
  in jm2/openETQW, entries with ``targetRepo == "openETQW-game"``).

Every in-scope tracked file must therefore either carry an SPDX tag whose
license identifiers are all in ``ALLOWED_LICENSES`` or be listed in the
allowlist.  The check fails on:

* new gaps: an in-scope file with no SPDX tag that is not allowlisted;
* disallowed licenses: a tag naming anything outside ``ALLOWED_LICENSES``
  (copyleft identifiers get a dedicated message, the game repo charter
  forbids GPL-only code);
* provenance conflicts: a file allowlisted as one license whose tag now
  claims another (for example an SDK file relabelled MIT);
* malformed allowlist lines, duplicate entries or unknown license ids.

Stale entries (paths no longer tracked) and redundant entries (the file now
has a matching tag) are warnings; ``--strict`` turns them into errors and
``--prune`` rewrites the allowlist without them.

Exit codes: 0 pass, 1 policy failure, 2 usage / environment error.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ALLOWLIST = Path("tools/ci/spdx_allowlist.txt")

# Paths that must be covered (prefixes end in "/"; the rest are exact paths).
SCOPE_PREFIXES = ("src/", "tools/", ".github/")
SCOPE_FILES = ("meson.build", "meson_options.txt", "meson.options")

# License identifiers acceptable in this repository.  LicenseRef-ETQW-SDK is
# the SDK EULA; MIT is the default for new work; the remainder are permissive
# licenses that could legitimately arrive with small vendored helpers.
# Extending this set is a legal decision, not a CI tweak.
ALLOWED_LICENSES = frozenset(
    {
        "LicenseRef-ETQW-SDK",
        "MIT",
        "MIT-0",
        "BSD-2-Clause",
        "BSD-3-Clause",
        "Zlib",
        "ISC",
        "BSL-1.0",
        "Apache-2.0",
        "CC0-1.0",
        "0BSD",
        "Unlicense",
    }
)
# Licenses the allowlist may assign to unmarked files (provenance classes).
ALLOWLIST_LICENSES = frozenset({"LicenseRef-ETQW-SDK", "MIT"})
COPYLEFT_PREFIXES = ("GPL", "LGPL", "AGPL")
EXPRESSION_OPERATORS = frozenset({"AND", "OR", "WITH"})

HEAD_BYTES = 8192
TAG_RE = re.compile(r"SPDX-License-Identifier:[ \t]*([^\r\n]*)")
TOKEN_RE = re.compile(r"[A-Za-z0-9.+:\-]+")
TRAILER_RE = re.compile(r"\*/|-->|\"|'|\}\}")

ANNOTATION_LIMIT = 40
SECTION_PREFIX = "# --- "


def github_actions() -> bool:
    return os.environ.get("GITHUB_ACTIONS", "").lower() == "true"


def annotate(level: str, path: str | None, message: str) -> None:
    if not github_actions():
        return
    message = message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    if path:
        print(f"::{level} file={path},line=1::{message}")
    else:
        print(f"::{level}::{message}")


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
        for raw in output.split(b"\0"):
            if raw:
                paths.add(raw.decode("utf-8", "surrogateescape"))
    # Deleted-but-still-indexed files are not on disk; they cannot carry tags.
    return sorted(p for p in paths if (root / p).is_file())


def in_scope(path: str) -> bool:
    return path in SCOPE_FILES or path.startswith(SCOPE_PREFIXES)


def decode_head(data: bytes) -> str:
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16", "replace")
    return data.decode("utf-8", "replace")


def read_tag(path: Path) -> str | None:
    with path.open("rb") as handle:
        head = decode_head(handle.read(HEAD_BYTES))
    match = TAG_RE.search(head)
    if not match:
        return None
    return TRAILER_RE.split(match.group(1), maxsplit=1)[0].strip()


def expression_licenses(expression: str) -> tuple[list[str], list[str]]:
    """Return (license ids, exception ids) named by an SPDX expression."""
    licenses: list[str] = []
    exceptions: list[str] = []
    expect_exception = False
    for token in TOKEN_RE.findall(expression):
        upper = token.upper()
        if upper in EXPRESSION_OPERATORS:
            expect_exception = upper == "WITH"
            continue
        if expect_exception:
            exceptions.append(token)
            expect_exception = False
        else:
            licenses.append(token)
    return licenses, exceptions


def is_copyleft(license_id: str) -> bool:
    return license_id.upper().startswith(COPYLEFT_PREFIXES)


class Allowlist:
    def __init__(self, path: Path, must_exist: bool = True):
        self.path = path
        self.entries: dict[str, str] = {}
        self.header: list[str] = []
        self.errors: list[str] = []
        if self.path.is_file():
            self._load()
        elif must_exist:
            self.errors.append(f"allowlist not found: {self.path}")

    def _load(self) -> None:
        seen_entry = False
        for number, raw in enumerate(self.path.read_text(encoding="utf-8").splitlines(), start=1):
            line = raw.strip()
            if not line or line.startswith("#"):
                if not seen_entry and not line.startswith(SECTION_PREFIX):
                    self.header.append(raw)
                continue
            seen_entry = True
            parts = line.split(None, 1)
            if len(parts) != 2:
                self.errors.append(f"{self.path.name}:{number}: expected '<license-id> <path>', got {raw!r}")
                continue
            license_id, rel = parts[0], parts[1].strip()
            if license_id not in ALLOWLIST_LICENSES:
                self.errors.append(
                    f"{self.path.name}:{number}: license {license_id!r} is not an allowlist class "
                    f"({', '.join(sorted(ALLOWLIST_LICENSES))})"
                )
                continue
            if rel.startswith("/") or ".." in Path(rel).parts or "\\" in rel:
                self.errors.append(f"{self.path.name}:{number}: path must be repo-relative POSIX: {rel!r}")
                continue
            if rel in self.entries:
                self.errors.append(f"{self.path.name}:{number}: duplicate entry for {rel}")
                continue
            self.entries[rel] = license_id

    def write(self, entries: dict[str, str]) -> None:
        header = list(self.header) or default_allowlist_header()
        while header and not header[-1].strip():
            header.pop()
        lines = header + [""]
        for license_id in sorted(set(entries.values())):
            lines.append(f"{SECTION_PREFIX}{license_id} ---")
            lines.extend(f"{license_id} {rel}" for rel in sorted(p for p, lic in entries.items() if lic == license_id))
            lines.append("")
        self.path.write_text("\n".join(lines).rstrip("\n") + "\n", encoding="utf-8")


def default_allowlist_header() -> list[str]:
    return [
        "# SPDX-License-Identifier: MIT",
        "#",
        "# openETQW-game license/provenance allowlist, consumed by tools/ci/check_spdx.py.",
        "#",
        "# Every tracked file under src/, tools/, .github/ and the top-level Meson files",
        "# must carry an SPDX-License-Identifier tag or be listed here with the license",
        "# that its provenance establishes. Entries were seeded from the engine's",
        "# docs/provenance-manifest.json (targetRepo == openETQW-game):",
        "#   SDK tier                     -> LicenseRef-ETQW-SDK",
        "#   Reconstruction_original tier -> MIT (src/compiledscript build files and code generator)",
        "#",
        "# Format: <license-id> <repo-relative path>   (one entry per line)",
        "# Do not add new files here: give new files an SPDX header instead. Adding an",
        "# entry is a provenance decision; record the justification in the commit.",
        "# Maintenance: python3 tools/ci/check_spdx.py --prune  (drops stale entries).",
    ]


def load_manifest(path: Path, repo_name: str) -> dict[str, str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    mapping: dict[str, str] = {}
    for entry in data.get("files", []):
        if entry.get("targetRepo") != repo_name:
            continue
        license_id = entry.get("license", "")
        if license_id not in ALLOWLIST_LICENSES:
            raise SystemExit(f"error: manifest assigns unsupported license {license_id!r} to {entry.get('path')}")
        mapping[entry["path"]] = license_id
    if not mapping:
        raise SystemExit(f"error: manifest {path} has no entries for targetRepo={repo_name!r}")
    return mapping


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="repository root (default: this checkout)")
    parser.add_argument("--allowlist", type=Path, default=None, help=f"allowlist file (default: <root>/{DEFAULT_ALLOWLIST})")
    parser.add_argument("--include-untracked", action="store_true", help="also check untracked, non-ignored files (pre-commit use)")
    parser.add_argument("--strict", action="store_true", help="treat stale and redundant allowlist entries as errors")
    parser.add_argument("--prune", action="store_true", help="rewrite the allowlist without stale/redundant entries, then check")
    parser.add_argument(
        "--seed-from-manifest",
        type=Path,
        metavar="MANIFEST",
        help="(re)generate the allowlist for every current gap from an engine provenance manifest JSON",
    )
    parser.add_argument("--manifest-repo", default="openETQW-game", help="targetRepo value to read from the manifest")
    parser.add_argument("--report", type=Path, help="write a JSON report here")
    parser.add_argument("--verbose", action="store_true", help="list every classified file")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    allowlist_path = (args.allowlist or root / DEFAULT_ALLOWLIST).resolve()
    files = [p for p in git_files(root, args.include_untracked) if in_scope(p)]

    tags: dict[str, str | None] = {}
    errors: list[tuple[str | None, str]] = []
    warnings: list[tuple[str | None, str]] = []
    for rel in files:
        try:
            tags[rel] = read_tag(root / rel)
        except OSError as exc:
            errors.append((rel, f"unreadable: {exc}"))

    if args.seed_from_manifest:
        manifest = load_manifest(args.seed_from_manifest, args.manifest_repo)
        gaps = [rel for rel, tag in tags.items() if tag is None]
        unclassified = [rel for rel in gaps if rel not in manifest]
        if unclassified:
            for rel in unclassified:
                print(f"error: untagged file has no manifest classification: {rel}", file=sys.stderr)
            return 1
        seeded = {rel: manifest[rel] for rel in gaps}
        Allowlist(allowlist_path, must_exist=False).write(seeded)
        print(f"Seeded {len(seeded)} allowlist entries into {allowlist_path}: {dict(Counter(seeded.values()))}")

    allowlist = Allowlist(allowlist_path)
    errors.extend((None, message) for message in allowlist.errors)

    tagged: Counter[str] = Counter()
    allowlisted: Counter[str] = Counter()
    gaps: list[str] = []
    for rel in files:
        if rel not in tags:
            continue
        tag = tags[rel]
        declared = allowlist.entries.get(rel)
        if tag is None:
            if declared:
                allowlisted[declared] += 1
                if args.verbose:
                    print(f"  allowlisted {declared:<22} {rel}")
            else:
                gaps.append(rel)
            continue
        licenses, exceptions = expression_licenses(tag)
        if not licenses:
            errors.append((rel, f"empty or unparsable SPDX expression: {tag!r}"))
            continue
        bad = [lic for lic in licenses if lic not in ALLOWED_LICENSES]
        copyleft = [lic for lic in licenses + exceptions if is_copyleft(lic)]
        if copyleft:
            errors.append((rel, f"copyleft license {', '.join(copyleft)} is forbidden in the game repository (charter: no GPL-only code)"))
        elif bad:
            errors.append((rel, f"license {', '.join(bad)} is not in the allowed set; extending it needs a legal decision"))
        elif exceptions:
            errors.append((rel, f"license exceptions are not expected here: {', '.join(exceptions)}"))
        tagged[tag] += 1
        if args.verbose:
            print(f"  tagged      {tag:<22} {rel}")
        if declared:
            if declared in licenses:
                warnings.append((rel, f"allowlist entry is redundant (file now carries SPDX tag {tag!r})"))
            else:
                errors.append((rel, f"provenance conflict: allowlist classifies it as {declared} but its tag says {tag!r}"))

    in_scope_set = set(tags)
    stale = sorted(rel for rel in allowlist.entries if rel not in in_scope_set)
    for rel in stale:
        warnings.append((rel, "stale allowlist entry (file is no longer tracked/in scope)"))
    for rel in gaps:
        errors.append((rel, "missing SPDX-License-Identifier and not covered by tools/ci/spdx_allowlist.txt (new files need an SPDX header, MIT for new game code)"))

    if args.prune:
        redundant = {rel for rel, message in warnings if message.startswith("allowlist entry is redundant")}
        kept = {rel: lic for rel, lic in allowlist.entries.items() if rel not in redundant and rel in in_scope_set}
        removed = len(allowlist.entries) - len(kept)
        allowlist.write(kept)
        print(f"Pruned {removed} allowlist entries ({len(kept)} remain).")
        warnings = [(rel, message) for rel, message in warnings if rel not in redundant and rel not in stale]

    if args.strict:
        errors.extend(warnings)
        warnings = []

    print(f"SPDX/provenance check: {len(files)} in-scope files under {root}")
    print(f"  tagged:      {sum(tagged.values()):5d}  {dict(sorted(tagged.items()))}")
    print(f"  allowlisted: {sum(allowlisted.values()):5d}  {dict(sorted(allowlisted.items()))}")
    print(f"  new gaps:    {len(gaps):5d}")
    print(f"  warnings:    {len(warnings):5d}")
    print(f"  errors:      {len(errors):5d}")
    for index, (rel, message) in enumerate(warnings):
        print(f"warning: {rel or '<allowlist>'}: {message}")
        if index < ANNOTATION_LIMIT:
            annotate("warning", rel, message)
    for index, (rel, message) in enumerate(errors):
        print(f"error: {rel or '<allowlist>'}: {message}", file=sys.stderr)
        if index < ANNOTATION_LIMIT:
            annotate("error", rel, message)

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(
                {
                    "root": root.as_posix(),
                    "inScope": len(files),
                    "tagged": dict(tagged),
                    "allowlisted": dict(allowlisted),
                    "gaps": gaps,
                    "stale": stale,
                    "warnings": [{"path": p, "message": m} for p, m in warnings],
                    "errors": [{"path": p, "message": m} for p, m in errors],
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            status = "failed" if errors else "passed"
            handle.write(
                f"### SPDX / provenance headers: {status}\n\n"
                f"| in scope | tagged | allowlisted | new gaps | warnings | errors |\n|---|---|---|---|---|---|\n"
                f"| {len(files)} | {sum(tagged.values())} | {sum(allowlisted.values())} | {len(gaps)} | {len(warnings)} | {len(errors)} |\n\n"
            )

    if errors:
        print("FAILED: license/provenance policy violations found.", file=sys.stderr)
        return 1
    print("PASSED: every in-scope file is SPDX-tagged or covered by the provenance allowlist.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
