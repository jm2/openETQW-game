#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate or move the engine pin in tools/ci/engine.ref.

  python3 tools/ci/bump_engine.py --check                  # validate the pin file only
  python3 tools/ci/bump_engine.py <40-char-sha>            # pin an explicit commit
  python3 tools/ci/bump_engine.py --from-local ../openETQW # pin a local checkout's HEAD
  python3 tools/ci/bump_engine.py --remote main            # pin the remote tip of a branch/tag

Unless --no-verify is given, the new commit is confirmed to exist on the
engine remote with the same access policy as fetch_engine.py (anonymous
https first, then the optional OPENETQW_CROSS_REPO_TOKEN).  While the engine
repository is private and no token is available that verification is skipped
with a warning (the pin is still written), because the Engine Contract
workflow would skip anyway.  --from-local pins the committed HEAD (it warns
about uncommitted changes) and refuses a HEAD that is on no remote-tracking
branch unless --allow-unpushed is given, since CI can only fetch commits that
exist on the remote.

Exit codes: 0 ok (or nothing to change), 1 invalid pin / verification failed,
2 usage error.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fetch_engine as fe  # noqa: E402  (sibling module)


def git_local(repo: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repo), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    if completed.returncode != 0:
        raise fe.FetchError(f"git {' '.join(args)} failed in {repo}: {completed.stderr.strip()}")
    return completed.stdout.strip()


def from_local(repo: Path, allow_unpushed: bool) -> str:
    sha = git_local(repo, "rev-parse", "--verify", "HEAD")
    if git_local(repo, "status", "--porcelain", "--untracked-files=no"):
        print(f"warning: {repo} has uncommitted changes; pinning its committed HEAD {sha[:12]}")
    remote_branches = git_local(repo, "branch", "-r", "--contains", sha)
    if not remote_branches:
        message = f"{sha[:12]} is not contained in any remote-tracking branch of {repo} (push it first)"
        if not allow_unpushed:
            raise fe.FetchError(message + "; use --allow-unpushed to pin it anyway")
        print(f"warning: {message}")
    return sha


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    target = parser.add_mutually_exclusive_group()
    target.add_argument("sha", nargs="?", help="explicit full commit SHA to pin")
    target.add_argument("--from-local", type=Path, metavar="ENGINE_CHECKOUT", help="pin the HEAD of a local engine checkout")
    target.add_argument("--remote", metavar="REF", help="pin the current remote commit of a branch or tag")
    target.add_argument("--check", action="store_true", help="only validate the pin file")
    parser.add_argument("--pin-file", type=Path, default=fe.DEFAULT_PIN)
    parser.add_argument("--url", help="engine clone URL override (dry runs against a local mirror)")
    parser.add_argument("--no-verify", action="store_true", help="do not confirm the commit exists on the remote")
    parser.add_argument("--allow-unpushed", action="store_true", help="with --from-local: allow a HEAD not on any remote branch")
    parser.add_argument("--dry-run", action="store_true", help="show what would change without writing")
    args = parser.parse_args(argv)

    try:
        pin = fe.load_pin(args.pin_file)
    except fe.PinError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.check:
        print(f"OK: {args.pin_file} pins {pin.repository}@{pin.commit}")
        return 0
    if not (args.sha or args.from_local or args.remote):
        parser.error("give a SHA, --from-local, --remote or --check")

    url = args.url or f"{fe.GITHUB_HTTPS}{pin.repository}.git"
    git = fe.Git(url, os.environ.get(fe.TOKEN_ENV, "").strip(), verbose=False)
    try:
        if args.sha:
            new = args.sha.strip().lower()
            if not fe.SHA_RE.match(new):
                raise fe.FetchError(f"{args.sha!r} is not a full 40-character SHA")
        elif args.from_local:
            new = from_local(args.from_local.resolve(), args.allow_unpushed)
        else:
            access = fe.probe_access(git, retries=3)
            new, source = fe.resolve_ref(args.remote, access.refs)
            print(f"{args.remote} resolves to {new} ({source}) via {access.mode} https")

        if not args.no_verify:
            try:
                access = fe.probe_access(git, retries=3)
                with tempfile.TemporaryDirectory(prefix="engine-bump-") as scratch:
                    fe.fetch_commit(git, access.mode, new, Path(scratch) / "probe", retries=3, treeless=True)
                print(f"verified: {new[:12]} exists on {fe.safe_url(url)} ({access.mode})")
            except fe.NotAccessible as exc:
                print(f"warning: could not verify on the remote ({fe.SKIP_MESSAGE}): {exc}")
    except fe.NotAccessible as exc:
        print(f"error: {fe.SKIP_MESSAGE}; --remote needs access: {exc}", file=sys.stderr)
        return 1
    except (fe.FetchError, fe.PinError) as exc:
        print(f"error: {git.redact(str(exc))}", file=sys.stderr)
        return 1

    if new == pin.commit:
        print(f"engine pin already at {new}")
        return 0
    print(f"engine pin: {pin.commit} -> {new}")
    if args.from_local:
        try:
            log = git_local(args.from_local.resolve(), "log", "--oneline", "--no-decorate", f"{pin.commit}..{new}")
            if log:
                count = len(log.splitlines())
                print(f"{count} engine commit(s) since the previous pin:")
                print("\n".join(log.splitlines()[:30]) + ("\n..." if count > 30 else ""))
        except fe.FetchError:
            pass
    if args.dry_run:
        print("dry run: pin file not modified")
        return 0
    fe.write_pin(pin, new)
    print(f"updated {args.pin_file}")
    print(f"suggested commit message: ci: bump engine contract pin to {pin.repository}@{new[:12]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
