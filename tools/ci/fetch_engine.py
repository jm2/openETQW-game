#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fetch the companion engine repository (jm2/openETQW) at a pinned commit.

The pin lives in tools/ci/engine.ref (``repository`` and ``commit`` keys; see
bump_engine.py to move it).  A workflow_dispatch ``--ref`` override may name a
full commit SHA, a branch or a tag instead.

Access policy (the repositories are private today and will become public):

1. Anonymous https first.  Credential helpers, askpass programs and inherited
   http extra headers are disabled, so "anonymous" really means anonymous and
   git can never block on a prompt.
2. Only if that is denied and the optional secret OPENETQW_CROSS_REPO_TOKEN is
   non-empty, retry with the token.  The token is passed to git through
   GIT_CONFIG_* environment entries (never argv, never written to .git/config),
   only to https://github.com, and it is redacted from everything printed.
3. If the repository is still not reachable the companion is "not
   accessible": the script emits a ::notice::, a job-summary entry,
   ``available=false`` on $GITHUB_OUTPUT and exits 78.  Callers treat 78 as
   "skip cleanly", everything else non-zero as a real failure.  The day the
   engine repository is public, step 1 succeeds and nothing needs editing.

Exit codes:
  0   fetched (with --probe: reachable and the commit exists)
  78  companion repository not accessible -> SKIP, not a failure
  1   real error: invalid pin, commit missing on the remote, verification
      mismatch, network failure after retries
  2   usage error
"""

from __future__ import annotations

import argparse
import base64
import datetime as _dt
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path

EXIT_NOT_ACCESSIBLE = 78
TOKEN_ENV = "OPENETQW_CROSS_REPO_TOKEN"
DEFAULT_PIN = Path(__file__).resolve().parent / "engine.ref"
GITHUB_HTTPS = "https://github.com/"
SKIP_MESSAGE = "companion repo not accessible - skipped until repos are public"

SHA_RE = re.compile(r"^[0-9a-f]{40}$")
REPOSITORY_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
REF_RE = re.compile(r"^[A-Za-z0-9._/+-]{1,200}$")

NOT_ACCESSIBLE_PATTERNS = (
    "repository not found",
    "authentication failed",
    "could not read username",
    "could not read password",
    "terminal prompts disabled",
    "invalid username or password",
    "support for password authentication was removed",
    "requested url returned error: 401",
    "requested url returned error: 403",
    "requested url returned error: 404",
)
MISSING_OBJECT_PATTERNS = (
    "not our ref",
    "couldn't find remote ref",
    "no such remote ref",
    "unadvertised object",
    "is not a commit",
)
TRANSIENT_PATTERNS = (
    "could not resolve host",
    "timed out",
    "timeout",
    "connection reset",
    "connection refused",
    "connection was reset",
    "requested url returned error: 5",
    "requested url returned error: 429",
    "early eof",
    "rpc failed",
    "operation too slow",
    "remote end hung up unexpectedly",
    "gnutls",
    "ssl",
    "tls",
    "http/2 stream",
    "temporary failure",
)


class PinError(Exception):
    """engine.ref is malformed."""


class NotAccessible(Exception):
    """The companion repository cannot be read with the available credentials."""


class FetchError(Exception):
    """A real failure that must fail the job."""


# ----------------------------------------------------------------------------
# Pin file
# ----------------------------------------------------------------------------
@dataclass
class Pin:
    path: Path
    repository: str
    commit: str
    lines: list[str] = field(default_factory=list)


def load_pin(path: Path) -> Pin:
    if not path.is_file():
        raise PinError(f"pin file not found: {path}")
    values: dict[str, str] = {}
    lines = path.read_text(encoding="utf-8").splitlines()
    for number, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 2:
            raise PinError(f"{path.name}:{number}: expected '<key> <value>', got {raw!r}")
        key, value = parts
        if key not in ("repository", "commit"):
            raise PinError(f"{path.name}:{number}: unknown key {key!r} (allowed: repository, commit)")
        if key in values:
            raise PinError(f"{path.name}:{number}: duplicate key {key!r}")
        values[key] = value
    for key in ("repository", "commit"):
        if key not in values:
            raise PinError(f"{path.name}: missing '{key}' line")
    if not REPOSITORY_RE.match(values["repository"]):
        raise PinError(f"{path.name}: repository must be '<owner>/<name>', got {values['repository']!r}")
    if not SHA_RE.match(values["commit"]):
        raise PinError(f"{path.name}: commit must be a full 40-character lowercase SHA-1, got {values['commit']!r}")
    return Pin(path=path, repository=values["repository"], commit=values["commit"], lines=lines)


def write_pin(pin: Pin, commit: str) -> None:
    if not SHA_RE.match(commit):
        raise PinError(f"refusing to write non-SHA commit {commit!r}")
    out = []
    for raw in pin.lines:
        parts = raw.split()
        if len(parts) == 2 and parts[0] == "commit" and not raw.lstrip().startswith("#"):
            out.append(f"commit {commit}")
        else:
            out.append(raw)
    pin.path.write_text("\n".join(out) + "\n", encoding="utf-8")
    pin.commit = commit
    pin.lines = out


# ----------------------------------------------------------------------------
# Git with controlled credentials
# ----------------------------------------------------------------------------
@dataclass
class Outcome:
    ok: bool
    kind: str  # ok | denied | missing | transient | error
    stdout: str
    stderr: str


class Git:
    def __init__(self, url: str, token: str, verbose: bool = True):
        self.url = url
        self.token = token
        self.verbose = verbose
        self.header = ""
        if token:
            self.header = "AUTHORIZATION: basic " + base64.b64encode(f"x-access-token:{token}".encode()).decode()
            if os.environ.get("GITHUB_ACTIONS", "").lower() == "true":
                # The raw secret is masked by the runner; mask the derived header too.
                print(f"::add-mask::{self.header.split()[-1]}", flush=True)

    def redact(self, text: str) -> str:
        for secret in (self.token, self.header, self.header.split()[-1] if self.header else ""):
            if secret:
                text = text.replace(secret, "***")
        return text

    def _env(self, mode: str) -> dict[str, str]:
        env = os.environ.copy()
        env.pop(TOKEN_ENV, None)  # children never see the raw secret
        env.update(
            {
                "GIT_TERMINAL_PROMPT": "0",
                "GCM_INTERACTIVE": "never",
                "GIT_ASKPASS": "",
                "SSH_ASKPASS": "",
                "GIT_LFS_SKIP_SMUDGE": "1",
                "LC_ALL": "C",
            }
        )
        try:
            count = int(env.get("GIT_CONFIG_COUNT", "0") or "0")
        except ValueError:
            count = 0
        entries = [("credential.helper", ""), ("core.askPass", ""), ("http.extraHeader", "")]
        entries.append((f"http.{GITHUB_HTTPS}.extraHeader", ""))  # reset inherited per-URL headers
        if mode == "token":
            entries.append((f"http.{GITHUB_HTTPS}.extraHeader", self.header))
        for key, value in entries:
            env[f"GIT_CONFIG_KEY_{count}"] = key
            env[f"GIT_CONFIG_VALUE_{count}"] = value
            count += 1
        env["GIT_CONFIG_COUNT"] = str(count)
        return env

    def run(self, args: list[str], mode: str = "anonymous", cwd: Path | None = None, timeout: int = 600) -> Outcome:
        command = ["git", *args]
        if self.verbose:
            print(f"+ git {' '.join(self.redact(a) for a in args)}" + (f"   (auth: {mode})" if mode != "none" else ""), flush=True)
        try:
            completed = subprocess.run(
                command,
                cwd=cwd,
                env=self._env(mode),
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=timeout,
            )
        except FileNotFoundError as exc:
            raise FetchError("git is not installed or not on PATH") from exc
        except subprocess.TimeoutExpired:
            return Outcome(False, "transient", "", f"git {args[0]} timed out after {timeout}s")
        stdout = self.redact(completed.stdout or "")
        stderr = self.redact(completed.stderr or "")
        if completed.returncode == 0:
            return Outcome(True, "ok", stdout, stderr)
        lowered = stderr.lower()
        if any(p in lowered for p in NOT_ACCESSIBLE_PATTERNS):
            kind = "denied"
        elif any(p in lowered for p in MISSING_OBJECT_PATTERNS):
            kind = "missing"
        elif any(p in lowered for p in TRANSIENT_PATTERNS):
            kind = "transient"
        else:
            kind = "error"
        return Outcome(False, kind, stdout, stderr)

    def run_retrying(self, args: list[str], mode: str, cwd: Path | None = None, retries: int = 3, timeout: int = 600) -> Outcome:
        delay = 5
        for attempt in range(1, retries + 1):
            outcome = self.run(args, mode=mode, cwd=cwd, timeout=timeout)
            if outcome.ok or outcome.kind != "transient" or attempt == retries:
                return outcome
            print(f"transient git failure (attempt {attempt}/{retries}), retrying in {delay}s: {last_line(outcome.stderr)}", flush=True)
            time.sleep(delay)
            delay *= 3
        return outcome


def last_line(text: str) -> str:
    lines = [line for line in text.strip().splitlines() if line.strip()]
    return lines[-1] if lines else "(no output)"


# ----------------------------------------------------------------------------
# Access, resolution, fetch
# ----------------------------------------------------------------------------
@dataclass
class Access:
    mode: str
    refs: dict[str, str]
    attempts: list[str]


def probe_access(git: Git, retries: int) -> Access:
    """Find an auth mode that can list the remote's refs (anonymous first)."""
    modes = ["anonymous"]
    if git.token and git.url.startswith(GITHUB_HTTPS):
        modes.append("token")
    attempts: list[str] = []
    for mode in modes:
        outcome = git.run_retrying(["ls-remote", "--", git.url], mode=mode, retries=retries, timeout=120)
        if outcome.ok:
            refs = {}
            for line in outcome.stdout.splitlines():
                parts = line.split("\t")
                if len(parts) == 2:
                    refs[parts[1]] = parts[0]
            attempts.append(f"{mode}: ok")
            return Access(mode=mode, refs=refs, attempts=attempts)
        attempts.append(f"{mode}: {outcome.kind} ({last_line(outcome.stderr)})")
        if outcome.kind != "denied":
            raise FetchError(f"cannot reach {git.url} ({mode}): {outcome.stderr.strip() or outcome.kind}")
    if not git.token:
        attempts.append(f"token: {TOKEN_ENV} not set")
    elif not git.url.startswith(GITHUB_HTTPS):
        attempts.append("token: not sent to non-github.com URL")
    raise NotAccessible("; ".join(attempts))


def resolve_ref(ref: str, refs: dict[str, str]) -> tuple[str, str]:
    """Return (sha, description) for a SHA, branch, tag or full ref name."""
    if SHA_RE.match(ref.lower()):
        return ref.lower(), "commit"
    if re.fullmatch(r"[0-9a-fA-F]{4,39}", ref):
        raise FetchError(f"abbreviated SHA {ref!r} cannot be fetched; use the full 40-character SHA")
    if not REF_RE.match(ref) or ".." in ref:
        raise FetchError(f"invalid ref {ref!r}")
    candidates = [ref] if ref.startswith("refs/") else [f"refs/heads/{ref}", f"refs/tags/{ref}"]
    for name in candidates:
        peeled = refs.get(f"{name}^{{}}")  # annotated tag -> commit
        if peeled:
            return peeled, name
        if name in refs:
            return refs[name], name
    if ref == "HEAD" and "HEAD" in refs:
        return refs["HEAD"], "HEAD"
    raise FetchError(f"ref {ref!r} not found on the remote")


def fetch_commit(git: Git, mode: str, sha: str, dest: Path, retries: int, treeless: bool) -> None:
    for args in (["init", "-q", str(dest)], ["-C", str(dest), "remote", "add", "origin", git.url]):
        outcome = git.run(args, mode="none")
        if not outcome.ok:
            raise FetchError(f"git {args[0]} failed: {outcome.stderr.strip()}")
    fetch = ["-C", str(dest), "fetch", "--no-tags", "--depth", "1"]
    if treeless:
        fetch.append("--filter=tree:0")
    outcome = git.run_retrying([*fetch, "origin", sha], mode=mode, retries=retries)
    if not outcome.ok:
        if outcome.kind == "denied":
            raise NotAccessible(f"{mode}: denied during fetch ({last_line(outcome.stderr)})")
        if outcome.kind == "missing":
            raise FetchError(
                f"commit {sha} is not available from {git.url} (never pushed, or history was rewritten?): "
                f"{last_line(outcome.stderr)}"
            )
        raise FetchError(f"fetch of {sha} failed: {outcome.stderr.strip() or outcome.kind}")
    outcome = git.run(["-C", str(dest), "cat-file", "-t", sha], mode="none")
    if not outcome.ok or outcome.stdout.strip() != "commit":
        raise FetchError(f"{sha} is not a commit on the remote")
    if treeless:
        return
    outcome = git.run(["-C", str(dest), "-c", "advice.detachedHead=false", "checkout", "-q", "--detach", sha], mode="none")
    if not outcome.ok:
        raise FetchError(f"checkout of {sha} failed: {outcome.stderr.strip()}")
    head = git.run(["-C", str(dest), "rev-parse", "--verify", "HEAD"], mode="none").stdout.strip()
    if head != sha:
        raise FetchError(f"HEAD verification failed: checked out {head or '<none>'}, expected {sha}")
    dirty = git.run(["-C", str(dest), "status", "--porcelain", "--untracked-files=no"], mode="none")
    if not dirty.ok or dirty.stdout.strip():
        raise FetchError(f"fresh engine checkout is not clean: {dirty.stdout.strip()[:500]}")


# ----------------------------------------------------------------------------
# Reporting
# ----------------------------------------------------------------------------
def github_actions() -> bool:
    return os.environ.get("GITHUB_ACTIONS", "").lower() == "true"


def write_outputs(enabled: bool, values: dict[str, str]) -> None:
    target = os.environ.get("GITHUB_OUTPUT")
    if not enabled or not target:
        return
    with open(target, "a", encoding="utf-8") as handle:
        for key, value in values.items():
            handle.write(f"{key}={value.replace(chr(10), ' ')}\n")


def write_summary(text: str) -> None:
    target = os.environ.get("GITHUB_STEP_SUMMARY")
    if target:
        with open(target, "a", encoding="utf-8") as handle:
            handle.write(text + "\n")


def write_record(path: Path | None, record: dict) -> None:
    if path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def safe_url(url: str) -> str:
    return re.sub(r"//[^/@]+@", "//", url)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pin-file", type=Path, default=DEFAULT_PIN, help="engine pin (default: tools/ci/engine.ref)")
    parser.add_argument("--ref", default="", help="override: full SHA, branch or tag (blank = use the pin)")
    parser.add_argument("--dest", type=Path, help="checkout directory (required unless --probe)")
    parser.add_argument("--force", action="store_true", help="replace an existing, non-empty --dest")
    parser.add_argument("--probe", action="store_true", help="only check access and that the commit exists (treeless fetch)")
    parser.add_argument("--url", help="clone URL override (e.g. a local path for dry runs); tokens are only sent to github.com")
    parser.add_argument("--retries", type=int, default=3, help="attempts for transient network failures")
    parser.add_argument("--github-output", action="store_true", help="write available/sha/... to $GITHUB_OUTPUT")
    parser.add_argument("--record", type=Path, help="write a JSON record of what was fetched (no secrets)")
    parser.add_argument("--quiet", action="store_true", help="do not echo git commands")
    args = parser.parse_args(argv)

    if not args.probe and not args.dest:
        parser.error("--dest is required unless --probe is given")

    try:
        pin = load_pin(args.pin_file)
    except PinError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    url = args.url or f"{GITHUB_HTTPS}{pin.repository}.git"
    token = os.environ.get(TOKEN_ENV, "").strip()
    git = Git(url, token, verbose=not args.quiet)
    override = (args.ref or "").strip()
    record: dict = {
        "repository": pin.repository,
        "url": safe_url(url),
        "pinnedCommit": pin.commit,
        "requestedRef": override or None,
        "tokenConfigured": bool(token),
        "time": _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds"),
    }

    try:
        access = probe_access(git, args.retries)
        record["auth"] = access.mode
        if override:
            sha, source = resolve_ref(override, access.refs)
            source = f"override {source}"
        else:
            sha, source = pin.commit, "pin"
        record.update({"commit": sha, "source": source})
        print(f"Engine repository {pin.repository} reachable ({access.mode}); target {sha} ({source})", flush=True)

        if args.probe:
            with tempfile.TemporaryDirectory(prefix="engine-probe-") as scratch:
                fetch_commit(git, access.mode, sha, Path(scratch) / "probe", args.retries, treeless=True)
        else:
            dest = args.dest.resolve()
            if dest.exists() and any(dest.iterdir()):
                if not args.force:
                    raise FetchError(f"destination exists and is not empty: {dest} (use --force)")
                shutil.rmtree(dest)
            dest.parent.mkdir(parents=True, exist_ok=True)
            fetch_commit(git, access.mode, sha, dest, args.retries, treeless=False)
            record["path"] = dest.as_posix()
    except NotAccessible as exc:
        reason = str(exc)
        record.update({"available": False, "reason": reason})
        print(f"{SKIP_MESSAGE}: {pin.repository} ({reason})", flush=True)
        if github_actions():
            print(
                f"::notice title=Engine contract skipped::{SKIP_MESSAGE} "
                f"({pin.repository}: anonymous https denied; {TOKEN_ENV} {'set but denied' if token else 'not set'})",
                flush=True,
            )
        write_summary(
            "### :information_source: Engine contract skipped\n\n"
            f"**{SKIP_MESSAGE}.**\n\n"
            f"- companion: `{pin.repository}` at pinned commit `{pin.commit[:12]}`"
            + (f" (requested `{override}`)" if override else "")
            + f"\n- access attempts: {reason}\n"
            "- This is expected while the repositories are private. It starts working automatically once the "
            f"engine repository is public (anonymous https), or earlier if the optional `{TOKEN_ENV}` secret is set.\n"
        )
        write_outputs(args.github_output, {"available": "false", "reason": reason, "sha": "", "auth": "none"})
        write_record(args.record, record)
        return EXIT_NOT_ACCESSIBLE
    except (FetchError, PinError) as exc:
        message = git.redact(str(exc))
        print(f"error: {message}", file=sys.stderr)
        if github_actions():
            print(f"::error title=Engine fetch failed::{message.splitlines()[0] if message else 'unknown error'}")
        record.update({"available": False, "error": message})
        write_record(args.record, record)
        return 1

    record["available"] = True
    write_outputs(
        args.github_output,
        {
            "available": "true",
            "sha": record["commit"],
            "short_sha": record["commit"][:12],
            "source": record["source"],
            "auth": record["auth"],
            "repository": pin.repository,
            "path": record.get("path", ""),
        },
    )
    write_record(args.record, record)
    verb = "reachable" if args.probe else f"checked out at {record.get('path')}"
    write_summary(
        f"### Engine contract input: `{pin.repository}` @ `{record['commit'][:12]}`\n\n"
        f"{verb} via {record['auth']} https ({record['source']}).\n"
    )
    print(f"OK: {pin.repository}@{record['commit']} {verb}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
