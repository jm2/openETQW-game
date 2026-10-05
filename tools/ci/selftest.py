#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Self-test for the CI gates in tools/ci (runs in the static-checks job).

The gates pass on today's tree; this proves they still *fail* on the things
they exist to catch, using throw-away git repositories, so a refactor cannot
silently turn a gate into a no-op:

* check_spdx.py: new untagged file, copyleft tag, unknown license,
  provenance conflict (allowlisted SDK file relabelled), malformed allowlist;
  and passes for tagged / allowlisted files (stale entries only warn).
* check_no_gpl.py: license boilerplate, SPDX tag and license URL in a
  tracked file; allowlisted files are exempt.
* ci_plan.py: table consistency, ARM64 runner gating by repository
  visibility, dispatch selection and the experimental override.
* fetch_engine.py: pin parsing, ref resolution and an offline fetch from a
  local repository (checkout verification, missing commit is a hard error).
* check_engine_tests.py: missing / skipped required tests and sanitizer
  output fail; run_logged.py propagates the exit status and keeps the log;
  check_game_module.py rejects missing, malformed and mis-typed modules.
* check_event_handlers.py: a handler taking a float (or more parameters,
  or a type it does not know as an integer) where its event definition
  passes an integer fails, as does a binding it cannot resolve; each binding
  uses the definition in its own file; an inherited or inline handler
  matching its definition passes. An events.script declaration with another
  number of arguments, an argument of another kind or another return type
  than its native definition fails, as does one with no native definition or
  a missing events.script.
* check_presentation_contract.py: the seat presentation calling a function
  that changes game state, assigning a view or game field, or being referenced
  outside its hooks fails; the clean source passes.

The copyleft marker strings are assembled from fragments so this file does
not trip check_no_gpl.py itself.  Exit codes: 0 pass, 1 a case failed.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ci_plan  # noqa: E402  (sibling module)
import fetch_engine  # noqa: E402  (sibling module)

_G = "G" + "PL"
_GNU = "GN" + "U"
SPDX = "SPDX-" + "License-Identifier:"

failures: list[str] = []


def check(condition: bool, label: str, detail: str = "") -> None:
    print(f"  {'ok  ' if condition else 'FAIL'} {label}")
    if not condition:
        failures.append(label + (f": {detail}" if detail else ""))


def git(repo: Path, *args: str) -> str:
    env = dict(os.environ, GIT_AUTHOR_NAME="ci", GIT_AUTHOR_EMAIL="ci@invalid", GIT_COMMITTER_NAME="ci",
               GIT_COMMITTER_EMAIL="ci@invalid", GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    return subprocess.run(["git", "-C", str(repo), *args], check=True, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True).stdout.strip()


def make_repo(root: Path, files: dict[str, str]) -> Path:
    root.mkdir(parents=True)
    git(root, "init", "-q")
    for rel, text in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    git(root, "add", "-A")
    return root


def run_tool(script: str, *args: str) -> tuple[int, str]:
    env = {k: v for k, v in os.environ.items() if k not in ("GITHUB_ACTIONS", "GITHUB_STEP_SUMMARY", "GITHUB_OUTPUT")}
    completed = subprocess.run([sys.executable, str(HERE / script), *args], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    return completed.returncode, completed.stdout


# ----------------------------------------------------------------------------
def test_spdx(scratch: Path) -> None:
    print("check_spdx.py")
    allowlist = f"# {SPDX} MIT\nLicenseRef-ETQW-SDK src/game/Sdk.cpp\nMIT src/compiledscript/Gen.cpp\nMIT src/gone.cpp\n"
    base = {
        "tools/ci/spdx_allowlist.txt": allowlist,
        "src/game/Sdk.cpp": "// Copyright (C) 2007 Id Software, Inc.\n",
        "src/compiledscript/Gen.cpp": "// generated\n",
        "src/game/New.cpp": f"// {SPDX} MIT\n",
        "tools/x.py": f"# {SPDX} Apache-2.0\n",
        "docs/untracked-scope.md": "outside the checked prefixes\n",
    }

    def spdx(name: str, extra: dict[str, str], *flags: str) -> tuple[int, str]:
        repo = make_repo(scratch / f"spdx-{name}", {**base, **extra})
        return run_tool("check_spdx.py", "--root", str(repo), *flags)

    code, out = spdx("baseline", {})
    check(code == 0 and "stale allowlist entry" in out, "tagged + allowlisted files pass; stale entry only warns", out)
    code, out = spdx("strict", {}, "--strict")
    check(code == 1, "--strict turns the stale entry into an error", out)
    code, out = spdx("gap", {"src/game/Brand.cpp": "int x;\n"})
    check(code == 1 and "src/game/Brand.cpp" in out and "missing SPDX" in out, "new untagged file fails", out)
    code, out = spdx("gap-yml", {".github/workflows/new.yml": "name: x\n"})
    check(code == 1 and "new.yml" in out, "new untagged workflow fails", out)
    code, out = spdx("copyleft", {"src/game/Bad.cpp": f"// {SPDX} {_G}-2.0-only\n"})
    check(code == 1 and "copyleft" in out, "copyleft SPDX tag fails", out)
    code, out = spdx("unknown", {"src/game/Odd.cpp": f"// {SPDX} LicenseRef-Proprietary\n"})
    check(code == 1 and "not in the allowed set" in out, "unknown license id fails", out)
    code, out = spdx("relabel", {"src/game/Sdk.cpp": f"// {SPDX} MIT\n"})
    check(code == 1 and "provenance conflict" in out, "allowlisted SDK file relabelled MIT fails", out)
    code, out = spdx("badlist", {"tools/ci/spdx_allowlist.txt": allowlist + "Apache-2.0 src/other.cpp\n"})
    check(code == 1 and "not an allowlist class" in out, "allowlist with a non-provenance license fails", out)


def test_no_gpl(scratch: Path) -> None:
    print("check_no_gpl.py")
    allow = "LICENSE  # licensing notice\n"
    base = {"tools/ci/gpl_marker_allowlist.txt": allow, "src/ok.cpp": f"// {SPDX} MIT\n",
            "LICENSE": f"These files are not under the {_GNU} General Public License.\n"}
    cases = {
        "boilerplate": ("src/a.cpp", f"This program is free software under the {_GNU} General Public License\n"),
        "spdx": ("src/b.cpp", f"// {SPDX} {_G}-3.0-or-later\n"),
        "url": ("src/c.cpp", "// see https://www.gnu.org/" + "licenses/\n"),
        "doom3": ("src/d.cpp", f"Doom 3 {_G} Source Code\n"),
    }
    repo = make_repo(scratch / "gpl-clean", base)
    code, out = run_tool("check_no_gpl.py", "--root", str(repo))
    check(code == 0, "clean tree passes (allowlisted LICENSE is exempt)", out)
    for name, (rel, text) in cases.items():
        repo = make_repo(scratch / f"gpl-{name}", {**base, rel: text})
        code, out = run_tool("check_no_gpl.py", "--root", str(repo))
        check(code == 1 and rel in out, f"{name} marker fails", out)
    repo = make_repo(scratch / "gpl-noreason", {**base, "tools/ci/gpl_marker_allowlist.txt": "LICENSE\n"})
    code, out = run_tool("check_no_gpl.py", "--root", str(repo))
    check(code == 1 and "justification" in out, "allowlist entry without a reason fails", out)


def test_plan() -> None:
    print("ci_plan.py")
    check(not ci_plan.validate_tables(), "tables are consistent", "; ".join(ci_plan.validate_tables()))
    ids = lambda planned, table: {e["id"] for e in planned[table][0]}  # noqa: E731
    private = ci_plan.plan(["build", "sanitizer"], True, "", "auto", "")
    public = ci_plan.plan(["build", "sanitizer"], False, "", "auto", "")
    arm = {leg.id for leg in ci_plan.TABLES["build"] if leg.arm_runner}
    check(bool(arm) and not (ids(private, "build") & arm), "private repo: hosted ARM64 legs skipped")
    check(arm <= ids(public, "build"), "public repo: hosted ARM64 legs run")
    check("linux-arm64-cross-gcc" in ids(private, "build"), "private repo: x64-hosted ARM64 cross leg still runs")
    forced = ci_plan.plan(["build"], True, "", "on", "")
    check(arm <= ids(forced, "build"), "OPENETQW_CI_ARM_RUNNERS=on runs ARM64 legs in a private repo")
    selected = ci_plan.plan(["build", "sanitizer"], True, "macos", "auto", "")
    check(ids(selected, "build") == {"macos-arm64"} and ids(selected, "sanitizer") == {"macos-arm64-asan-ubsan"},
          "dispatch prefix selection spans tables")
    none = ci_plan.plan(["build"], False, "", "auto", "none")
    check(not any(e["experimental"] for e in none["build"][0]), "experimental override 'none' makes every leg required")
    only = ci_plan.plan(["build"], False, "", "auto", "windows-x64")
    check([e["id"] for e in only["build"][0] if e["experimental"]] == ["windows-x64"], "experimental override replaces the set")
    for bad in (("build", "nothing-matches", ""), ("build", "", "no-such-leg"), ("nope", "", "")):
        try:
            ci_plan.plan([bad[0]], False, bad[1], "auto", bad[2])
            check(False, f"invalid input rejected {bad}")
        except ValueError:
            check(True, f"invalid input rejected {bad}")
    for table, legs in ci_plan.TABLES.items():
        for leg in legs:
            if leg.cross:
                check((HERE / "cross" / leg.cross).is_file(), f"{table}/{leg.id}: cross file exists")


def test_fetch(scratch: Path) -> None:
    print("fetch_engine.py")
    pin = scratch / "pin.ref"
    for text, label in (("repository a/b\n", "missing commit"), ("repository a/b\ncommit 1234\n", "short sha"),
                        ("repository a b\ncommit " + "0" * 40 + "\n", "bad line"), ("repo a/b\n", "unknown key")):
        pin.write_text(text, encoding="utf-8")
        try:
            fetch_engine.load_pin(pin)
            check(False, f"pin rejected: {label}")
        except fetch_engine.PinError:
            check(True, f"pin rejected: {label}")
    refs = {"refs/heads/main": "a" * 40, "refs/tags/v1": "b" * 40, "refs/tags/v1^{}": "c" * 40}
    check(fetch_engine.resolve_ref("main", refs)[0] == "a" * 40, "branch resolves")
    check(fetch_engine.resolve_ref("v1", refs)[0] == "c" * 40, "annotated tag peels to its commit")
    try:
        fetch_engine.resolve_ref("abc1234", refs)
        check(False, "abbreviated SHA rejected")
    except fetch_engine.FetchError:
        check(True, "abbreviated SHA rejected")

    # Offline fetch from a local "engine" repository (file transport).
    engine = make_repo(scratch / "engine-src", {"README": "engine\n"})
    git(engine, "commit", "-q", "-m", "one")
    sha = git(engine, "rev-parse", "HEAD")
    pin.write_text(f"repository jm2/openETQW\ncommit {sha}\n", encoding="utf-8")
    dest = scratch / "engine-dest"
    code, out = run_tool("fetch_engine.py", "--pin-file", str(pin), "--url", str(engine), "--dest", str(dest), "--quiet")
    check(code == 0 and (dest / "README").is_file() and git(dest, "rev-parse", "HEAD") == sha,
          "fetch checks out the pinned commit", out)
    code, out = run_tool("fetch_engine.py", "--pin-file", str(pin), "--url", str(engine), "--dest", str(dest), "--quiet")
    check(code == 1 and "not empty" in out, "refuses a non-empty destination without --force", out)
    pin.write_text(f"repository jm2/openETQW\ncommit {'d' * 40}\n", encoding="utf-8")
    code, out = run_tool("fetch_engine.py", "--pin-file", str(pin), "--url", str(engine), "--probe", "--quiet")
    check(code == 1, "missing commit on an accessible remote is a hard error (exit 1, not a skip)", out)


def test_engine_tests(scratch: Path) -> None:
    print("check_engine_tests.py")
    required = "bitmsg,usercmd-delta,struct-layout,usercmd-history,zero-sdk"

    def testlog(name: str, results: dict[str, tuple[str, str]]) -> Path:
        path = scratch / f"testlog-{name}.json"
        lines = [json.dumps({"name": f"unit - openETQW:{test}", "result": result, "returncode": 0 if result == "OK" else 1,
                             "duration": 0.1, "stdout": output, "stderr": ""})
                 for test, (result, output) in results.items()]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return path

    ok = {test: ("OK", "") for test in required.split(",")}
    code, out = run_tool("check_engine_tests.py", str(testlog("ok", ok)), "--require", required)
    check(code == 0, "all required tests OK passes", out)
    missing = {k: v for k, v in ok.items() if k != "struct-layout"}
    code, out = run_tool("check_engine_tests.py", str(testlog("missing", missing)), "--require", required)
    check(code == 1 and "struct-layout" in out, "a required test that did not run fails", out)
    code, out = run_tool("check_engine_tests.py", str(testlog("skip", {**ok, "bitmsg": ("SKIP", "")})), "--require", required)
    check(code == 1 and "bitmsg" in out, "a skipped required test fails", out)
    asan = {**ok, "bitmsg": ("OK", "==1==ERROR: AddressSanitizer: heap-buffer-overflow")}
    code, out = run_tool("check_engine_tests.py", str(testlog("asan", asan)), "--require", required, "--scan-sanitizer")
    check(code == 1 and "sanitizer" in out, "--scan-sanitizer fails on ASan output of a passing test", out)
    code, out = run_tool("check_engine_tests.py", str(scratch / "absent.json"))
    check(code == 2, "missing testlog is a usage error", out)


def test_run_logged(scratch: Path) -> None:
    print("run_logged.py")
    log = scratch / "logs" / "build.log"
    script = "import sys; print('[1/2] cc a.o'); print('src/a.cpp:3:5: error: boom'); print('[2/2] link'); sys.exit(3)"
    code, out = run_tool("run_logged.py", "--log", str(log), "--", sys.executable, "-c", script)
    check(code == 3, "wrapped command's exit status is propagated", out)
    check("src/a.cpp:3: boom" in out and log.is_file() and "error: boom" in log.read_text(encoding="utf-8"),
          "compiler errors are echoed and the full log is written", out)


def test_module_check(scratch: Path) -> None:
    print("check_game_module.py")
    build = scratch / "module-build"
    build.mkdir()
    code, out = run_tool("check_game_module.py", "--build-dir", str(build), "--os", "linux", "--arch", "x64")
    check(code == 1 and "module not found" in out, "missing module fails", out)
    (build / "game_x64.so").write_bytes(b"\x7fELF" + b"\x01" * 60)
    code, out = run_tool("check_game_module.py", "--build-dir", str(build), "--os", "linux", "--arch", "x64")
    check(code == 1 and "ELF64" in out, "32-bit/garbage ELF fails", out)
    (build / "game_x64.so").write_bytes(b"MZ" + b"\x00" * 200)
    code, out = run_tool("check_game_module.py", "--build-dir", str(build), "--os", "linux", "--arch", "x64")
    check(code == 1, "a PE file named like a Linux module fails", out)


def test_event_handlers(scratch: Path) -> None:
    print("check_event_handlers.py")
    header = ("class idClass {\npublic:\n\tvoid Event_Remove( void );\n};\n"
              "class idThing : public idClass {\npublic:\n\tvoid Event_Inline( bool on ) { }\n};\n")

    def tree(name: str, fmt: str, params: str) -> tuple[int, str]:
        root = scratch / f"events-{name}"
        (root / "src" / "game").mkdir(parents=True)
        (root / "src" / "game" / "Thing.h").write_text(header, encoding="utf-8")
        (root / "src" / "game" / "Thing.cpp").write_text(
            'const idEventDef EV_Set( "set", \'\\0\', DOC_TEXT( "Sets." ), %d, NULL%s );\n'
            'const idEventDef EV_Remove( "remove", "" );\n'
            'const idEventDef EV_Inline( "inline", "b" );\n'
            'CLASS_DECLARATION( idClass, idThing )\n'
            '\tEVENT( EV_Set, idThing::Event_Set )\n'
            '\tEVENT( EV_Remove, idThing::Event_Remove )\n'
            '\tEVENT( EV_Inline, idThing::Event_Inline )\n'
            'END_CLASS\n'
            'void idThing::Event_Set( %s ) {\n}\n'
            % (len(fmt), "".join(f', "{c}", "a{i}", "Argument."' for i, c in enumerate(fmt)), params),
            encoding="utf-8")
        return run_tool("check_event_handlers.py", "--root", str(root))

    code, out = tree("ok", "sf", "const char* name, float scale")
    check(code == 0 and "0 mismatch(es), 0 not checked" in out, "matching, inherited and inline handlers pass", out)
    code, out = tree("float", "sd", "const char* name, float scale")
    check(code == 1 and "parameter 2 is float" in out, "float handler for an integer argument fails", out)
    code, out = tree("extra", "s", "const char* name, float scale")
    check(code == 1 and "takes 2 parameters" in out, "handler taking more parameters than passed fails", out)
    code, out = tree("unknown", "sd", "const char* name, idStr scale")
    check(code == 1 and "parameter 2 is idStr" in out, "a type not known as an integer fails", out)

    root = scratch / "events-files"
    (root / "src" / "game").mkdir(parents=True)
    for name, fmt in (("A", "d"), ("B", "f")):
        (root / "src" / "game" / f"{name}.cpp").write_text(
            f'const idEventDef EV_Get( "get{name}", "{fmt}" );\n'
            f'CLASS_DECLARATION( idClass, id{name} )\n\tEVENT( EV_Get, id{name}::Event_Get )\nEND_CLASS\n'
            f'void id{name}::Event_Get( float value ) {{\n}}\n', encoding="utf-8")
    (root / "src" / "game" / "C.cpp").write_text(
        'CLASS_DECLARATION( idClass, idC )\n\tEVENT( EV_Missing, idC::Event_Missing )\nEND_CLASS\n', encoding="utf-8")
    code, out = run_tool("check_event_handlers.py", "--root", str(root))
    check(code == 1 and "idA::Event_Get parameter 1 is float" in out and "idB::Event_Get" not in out,
          "each binding is checked against the definition in its own file", out)
    check("not checked: " in out and "EV_Missing" in out, "a binding without a definition fails", out)

    def script_tree(name: str, declaration: str) -> tuple[int, str]:
        root = scratch / f"events-script-{name}"
        (root / "src" / "game").mkdir(parents=True)
        (root / "src" / "game" / "Thing.cpp").write_text(
            'const idEventDef EV_Send( "send", "dbs" );\n'
            'const idEventDef EV_Tip( "tip", "des" );\n'
            'const idEventDef EV_Count( "count", NULL, \'f\' );\n'
            'const idEventDef EV_Pick( "pick", "" );\n', encoding="utf-8")
        (root / "src" / "compiledscript" / "scripts").mkdir(parents=True)
        (root / "src" / "compiledscript" / "scripts" / "events.script").write_text(
            "// events\nscriptEvent void send( float client, boolean repeater, string message );\n" + declaration,
            encoding="utf-8")
        return run_tool("check_event_handlers.py", "--root", str(root))

    code, out = script_tree("ok", "scriptEvent void tip( float index, entity p, string text );\n"
                            "scriptEvent float count();\n")
    check(code == 0 and "3 script declarations, 0 mismatch(es)" in out,
          "script declarations that fit the native definitions pass (float -> integer; NULL format with a return)", out)
    code, out = script_tree("more", "scriptEvent void pick( entity target );\n")
    check(code == 1 and "'pick' ( e ) does not match" in out and "declares 1 argument(s)" in out,
          "a native definition that drops an argument the script passes fails", out)
    code, out = script_tree("return", "scriptEvent vector count();\n")
    check(code == 1 and "'count' returns vector" in out, "a return type other than the native one fails", out)
    code, out = script_tree("kind", "scriptEvent void tip( float index, entity p, wstring text );\n")
    check(code == 1 and "'tip' ( few ) does not match" in out, "a wstring for a native string fails", out)
    code, out = script_tree("fewer", "scriptEvent void tip( float index, entity p );\n")
    check(code == 1 and "declares 2 argument(s)" in out, "fewer script arguments than the native definition fails", out)
    code, out = script_tree("entity", "scriptEvent void tip( entity p, entity q, string text );\n")
    check(code == 1 and "argument 1 is 'e'" in out, "an entity for a native integer fails", out)
    code, out = script_tree("missing", "scriptEvent void tip( float index, entity p, string text );\n"
                            "scriptEvent float dvdOnly();\n")
    check(code == 1 and "'dvdOnly' (  ) has no native definition" in out, "a declaration without a native definition fails", out)
    empty = scratch / "events-script-none"
    (empty / "src" / "game").mkdir(parents=True)
    (empty / "src" / "compiledscript").mkdir(parents=True)
    code, out = run_tool("check_event_handlers.py", "--root", str(empty))
    check(code == 1 and "events.script not found" in out, "a script tree without events.script fails", out)


def test_presentation_contract(scratch: Path) -> None:
    print("check_presentation_contract.py")
    clean = ("void sdPresentation::SampleTick( idPlayer* viewPlayer ) {\n"
             "\tconst renderEntity_t* hull = transport->GetRenderEntity(); // Present() in a comment\n"
             "\ts.hullOrigin = hull->origin;\n}\n")

    def tree(name: str, body: str, other: str = "") -> tuple[int, str]:
        root = scratch / f"presentation-{name}"
        (root / "src" / "game").mkdir(parents=True)
        (root / "src" / "game" / "Presentation.h").write_text("class sdPresentation { };\n", encoding="utf-8")
        (root / "src" / "game" / "Presentation.cpp").write_text(clean + body, encoding="utf-8")
        if other:
            (root / "src" / "game" / "Player.cpp").write_text(other, encoding="utf-8")
        return run_tool("check_presentation_contract.py", "--root", str(root))

    code, out = tree("ok", "")
    check(code == 0 and "0 violation(s)" in out, "the clean presentation passes", out)
    code, out = tree("call", "void f() { transport->Present(); }\n")
    check(code == 1 and "calls Present" in out, "a Present call fails", out)
    code, out = tree("view", "void f() { viewPlayer->renderView.vieworg = o; }\n")
    check(code == 1 and "assigns renderView" in out, "assigning the player's renderView fails", out)
    code, out = tree("state", "void f() { gameLocal.unlock.seatAllowed = false; }\n")
    check(code == 1 and "writes game state" in out, "writing a gameLocal field fails", out)
    code, out = tree("ref", "", "void f() { gameLocal.presentation.Clear(); }\n")
    check(code == 1 and "outside its hooks" in out, "a reference outside the hooks fails", out)


def main() -> int:
    scratch = Path(tempfile.mkdtemp(prefix="openetqw-ci-selftest-"))
    try:
        test_spdx(scratch)
        test_no_gpl(scratch)
        test_plan()
        test_fetch(scratch)
        test_engine_tests(scratch)
        test_run_logged(scratch)
        test_module_check(scratch)
        test_event_handlers(scratch)
        test_presentation_contract(scratch)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    if failures:
        print(f"\nFAILED: {len(failures)} self-test case(s):", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("\nPASSED: CI gate self-test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
