#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fail if a script event handler's parameters do not match its idEventDef.

idClass::ProcessEventArgPtr calls an event handler through a function pointer
type chosen from the event definition's argument format (Callbacks.cpp: the
argument count and which arguments are floats).  On arm64 and x86-64 floats and
integers travel in different registers, so a handler declared with a float
where the definition says 'd' (or the reverse) reads another register, and a
pointer of the wrong kind is used as whatever it points at.  The compiler does
not see this: EVENT() casts the member function pointer.

This checks every EVENT( EV_x, Class::Handler ) binding in src/game against the
format of EV_x: each handler parameter must have the kind the definition
passes (d/b/h integer, f float, s char*, w wchar_t*, v idVec3/idAngles,
e/E idEntity*, o idScriptObject*).  A handler may take fewer parameters than
the definition (the rest are ignored), not more.  Integer parameters are the
built-in integer types, enums and typedefs of them found under src/; any other
type is a mismatch.  A binding the tool cannot resolve (definition or handler
not found, format not readable) fails too, so the gate cannot quietly weaken.

It also checks every declaration in src/compiledscript/scripts/events.script
(the retail 1.5 events.script) against the native definition of that name, with
the rule sdEventDefHelper::ReconcileEventsWithScript applies at runtime, made
strict: the script must declare exactly the definition's arguments, each reaching
the handler as the same kind (script float may feed an integer argument, entity a
nullable entity), and the return types must agree (script float for a native
float or integer, boolean or handle for a native boolean or handle, entity or
object for an entity or object).  The runtime only warns about fewer arguments;
the retail 1.5 events.script has no declaration with more, and a definition that
drops an argument the script passes loses it (setPlayerPickupRequestTime on the
1.0 DVD scripts).  A declaration without a native definition fails too, and so
does a missing events.script unless --root names another tree (the selftests).  Together the two checks cover script ->
definition -> handler without retail data; on the 1.0 DVD scripts they would
have caught sendNetworkEvent, broadcastToolTip and setPlayerPickupRequestTime.

Exit codes: 0 pass, 1 mismatches found, 2 usage / environment error.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

STR = r'"(?:[^"\\]|\\.)*"'
DEF_RE = re.compile(r'idEventDef(?:Internal)?\s+(\w+)\s*\(\s*(' + STR + r')\s*(?:,(.*?))?\)\s*;', re.S)
NEW_STYLE_RE = re.compile(r"\s*'(\\?.)'\s*,\s*(?:DOC_TEXT\(\s*(?:" + STR + r"\s*)+\)|(?:" + STR + r"\s*)+)\s*,\s*(\d+)\s*,(.*)", re.S)
BLOCK_RE = re.compile(r'(?:CLASS|ABSTRACT)_DECLARATION\s*\(\s*\w+\s*,\s*(\w+)\s*\)(.*?)END_CLASS', re.S)
EVENT_RE = re.compile(r'EVENT\s*\(\s*(\w+)\s*,\s*(\w+)::(\w+)\s*\)')
COMMENT_RE = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)

RETURN_RE = re.compile(r"\s*,\s*'(\\?.)'")
# script return type -> the native return characters a handler may use for it
RETURN_FITS = {'f': 'fd', 'b': 'bh', 'h': 'bh', 'e': 'eEo', 'o': 'eEo', 's': 's', 'w': 'w', 'v': 'v', '\0': '\0'}
SCRIPT_EVENT_RE = re.compile(r'\bscriptEvent\s+(\w+)\s+(\w+)\s*\(([^)]*)\)\s*;')
# ReconcileEventsWithScript's format character for each script type ('o' for any other)
SCRIPT_FORMAT = {'float': 'f', 'boolean': 'b', 'handle': 'h', 'vector': 'v', 'string': 's',
                 'wstring': 'w', 'entity': 'e', 'object': 'o'}
# sdEventDefHelper::ArgKind: what a native handler receives for a format character
ARG_KIND = {'d': 'd', 'b': 'd', 'h': 'd', 'e': 'e', 'E': 'e'}

# what the dispatcher passes for each format character
FORMAT_KIND = {'d': 'int', 'b': 'int', 'h': 'int', 'f': 'float', 's': 'char*', 'w': 'wchar_t*',
               'v': 'vector', 'e': 'idEntity*', 'E': 'idEntity*', 'o': 'idScriptObject*'}


def parse_format(rest: str | None) -> str | None:
    """The argument format of an idEventDef from the text after its name."""
    rest = (rest or '').strip()
    if rest == '' or rest.startswith('NULL'):
        return ''
    if rest.startswith('"'):
        return re.match(STR, rest).group(0)[1:-1]
    m = NEW_STYLE_RE.match(rest)
    if not m:
        return None
    count, tail = int(m.group(2)), m.group(3)
    literals = [s[1:-1] for s in re.findall(STR, tail)]
    if len(literals) < 3 * count:
        return None
    return ''.join(literals[len(literals) - 3 * count + 3 * i] for i in range(count))


INT_TYPES = {'int', 'unsigned', 'signed', 'short', 'long', 'bool', 'char', 'byte', 'size_t',
             'int8_t', 'int16_t', 'int32_t', 'uint8_t', 'uint16_t', 'uint32_t'}


def integer_types(texts: list[str]) -> set[str]:
    """Built-in integer types plus every enum and typedef of an integer type."""
    names = set(INT_TYPES)
    for text in texts:
        names.update(re.findall(r'\benum\s+(?:class\s+)?(\w+)\s*(?::\s*\w+\s*)?\{', text))
        names.update(re.findall(r'\}\s*(\w+)\s*;', ''.join(re.findall(r'typedef\s+enum\b[^;]*?\}\s*\w+\s*;', text, re.S))))
    typedefs = [m for text in texts for m in re.findall(r'\btypedef\s+((?:unsigned\s+|signed\s+)?\w+)\s+(\w+)\s*;', text)]
    # sdHandle< integer, invalid > (idlib/Handle.h) is a trivially copyable wrapper of
    # one integer, so it is passed in an integer register like the integer itself
    typedefs += [m for text in texts for m in re.findall(r'\btypedef\s+sdHandle\s*<\s*((?:unsigned\s+|signed\s+)?\w+)\s*,[^;>]*>\s*(\w+)\s*;', text)]
    changed = True
    while changed:
        changed = False
        for base, name in typedefs:
            if name not in names and base.split()[-1] in names:
                names.add(name)
                changed = True
    return names


def parameter_kind(param: str, ints: set[str] = INT_TYPES) -> str | None:
    """The kind of value a C++ parameter declaration receives, None for 'void'."""
    param = re.sub(r'=.*$', '', param).strip()
    if param in ('', 'void'):
        return None
    words = param.replace('const ', ' ').replace('&', ' & ').replace('*', ' * ').split()
    if '*' in words or '&' in words:
        base = words[0]
        if base == 'char':
            return 'char*'
        if base == 'wchar_t':
            return 'wchar_t*'
        if base in ('idVec3', 'idAngles'):
            return 'vector'
        if base == 'idScriptObject':
            return 'idScriptObject*'
        if base == 'idEntity':
            return 'idEntity*'
        return f'{base}*'
    if words[0] == 'float':
        return 'float'
    if words[0] in ('idVec3', 'idAngles'):
        return 'vector by value'
    words = [w for w in words if w not in ('unsigned', 'signed')] or ['int']
    if words[0] in ints or words[0].split('::')[-1] in ints:
        return 'int'
    return words[0]


class Source:
    """All sources as one text, with an index of out-of-class member definitions."""

    def __init__(self, everything: str) -> None:
        self.everything = everything
        self.definitions: dict[tuple[str, str], str] = {}
        for m in re.finditer(r'\b(\w+)::(\w+)\s*\(([^)]*)\)\s*(?:const\s*)?\{', everything):
            self.definitions.setdefault((m.group(1), m.group(2)), m.group(3))
        self.bodies: dict[str, tuple[str, str | None] | None] = {}

    def class_body(self, cls: str) -> tuple[str, str | None] | None:
        if cls not in self.bodies:
            self.bodies[cls] = class_body(self.everything, cls)
        return self.bodies[cls]


def class_body(everything: str, cls: str) -> tuple[str, str | None] | None:
    """The body of 'class cls { ... }' and its first public base class."""
    m = re.search(r'\bclass\s+' + cls + r'\b([^;{]*)\{', everything)
    if not m:
        return None
    depth, i = 1, m.end()
    while depth and i < len(everything):
        depth += {'{': 1, '}': -1}.get(everything[i], 0)
        i += 1
    base = re.search(r'public\s+(\w+)', m.group(1))
    return everything[m.end():i], base.group(1) if base else None


def handler_parameters(source: Source, cls: str, method: str) -> str | None:
    """The parameter list of cls::method: its out-of-class definition, else its
    declaration in the class (inline definitions), else the base class's."""
    for _ in range(16):
        if (cls, method) in source.definitions:
            return source.definitions[(cls, method)]
        body = source.class_body(cls)
        if body is None:
            return None
        d = re.search(r'\b' + method + r'\s*\(([^)]*)\)\s*(?:const\s*)?[;{]', body[0])
        if d:
            return d.group(1)
        if body[1] is None:
            return None
        cls = body[1]
    return None


def parse_return(rest: str | None) -> str | None:
    """The return character of an idEventDef from the text after its name ('\\0' for void)."""
    rest = (rest or '').strip()
    if rest == '':
        return '\0'
    if rest.startswith('"') or rest.startswith('NULL'):
        # ( "name", "format" or NULL [, 'r'] )
        after = rest[len(re.match(STR, rest).group(0)):] if rest.startswith('"') else rest[len('NULL'):]
        m = RETURN_RE.match(after)
        return ('\0' if m.group(1) == '\\0' else m.group(1)) if m else '\0'
    m = NEW_STYLE_RE.match(rest)
    if not m:
        return None
    return '\0' if m.group(1) == '\\0' else m.group(1)


def reconcile_mismatch(declared: str, native: str) -> str | None:
    """Why a script declaration does not fit the native definition, or None: the
    rule of sdEventDefHelper::ReconcileEventsWithScript, but also failing when the
    script passes more arguments than the definition takes."""
    if len(declared) != len(native):
        return f'declares {len(declared)} argument(s), the native definition takes {len(native)}'
    for i, (script, old) in enumerate(zip(declared, native)):
        if script == 'f':
            new = 'd' if old in 'db' else 'f'
        elif script == 'e' and old == 'E':
            new = 'E'
        else:
            new = script
        if ARG_KIND.get(old, old) != ARG_KIND.get(new, new):
            return f'argument {i + 1} is \'{new}\', the native definition passes \'{old}\''
    return None


def check_script(root: Path, formats: dict[Path, dict[str, tuple[str, str]]], returns: dict[str, set[str]],
                 required: bool) -> tuple[list[str], list[str], int]:
    """Every events.script declaration against the native definition of that event name."""
    events = root / 'src' / 'compiledscript' / 'scripts' / 'events.script'
    if not events.is_file():
        if required or (root / 'src' / 'compiledscript').is_dir():
            return [], [f'{events.relative_to(root)} not found'], 0
        return [], [], 0
    text = COMMENT_RE.sub(lambda m: '\n' * m.group(0).count('\n'), events.read_text(errors='replace'))
    native: dict[str, set[str]] = {}
    for defs in formats.values():
        for name, fmt in defs.values():
            native.setdefault(name, set()).add(fmt)
    problems: list[str] = []
    notes: list[str] = []
    declarations = 0
    for m in SCRIPT_EVENT_RE.finditer(text):
        declarations += 1
        where = f'{events.relative_to(root)}:{text.count(chr(10), 0, m.start()) + 1}'
        name = m.group(2)
        params = [p.split() for p in m.group(3).split(',') if p.strip()]
        declared = ''.join(SCRIPT_FORMAT.get(p[0], 'o') for p in params)
        declared_return = '\0' if m.group(1) == 'void' else SCRIPT_FORMAT.get(m.group(1), 'o')
        fmts = native.get(name)
        if not fmts:
            problems.append(f'{where}: \'{name}\' ( {declared} ) has no native definition')
            continue
        if len(fmts) > 1:
            notes.append(f'{where}: \'{name}\' has more than one different native definition ({", ".join(sorted(fmts))})')
            continue
        fmt = next(iter(fmts))
        why = reconcile_mismatch(declared, fmt)
        if why:
            problems.append(f'{where}: \'{name}\' ( {declared} ) does not match its native definition ( {fmt} ): {why}')
        rets = returns.get(name, set())
        if len(rets) == 1 and next(iter(rets)) not in RETURN_FITS[declared_return]:
            native_return = next(iter(rets))
            problems.append(f'{where}: \'{name}\' returns {m.group(1)}, but its native definition returns '
                            f'\'{native_return if native_return != chr(0) else "void"}\'')
        elif len(rets) != 1:
            notes.append(f'{where}: cannot read the native return type of \'{name}\'')
    if declarations == 0:
        notes.append(f'{events.relative_to(root)}: no scriptEvent declarations found')
    return problems, notes, declarations


def check(root: Path, script_required: bool = False) -> tuple[list[str], list[str], int, int]:
    sources = sorted(p for p in (root / 'src' / 'game').rglob('*') if p.suffix in ('.cpp', '.h'))
    texts = {p: COMMENT_RE.sub(lambda m: '\n' * m.group(0).count('\n'), p.read_text(errors='replace')) for p in sources}

    type_sources = [p.read_text(errors='replace') for p in (root / 'src').rglob('*.h')]
    ints = integer_types(type_sources + list(texts.values()))

    # namespace-scope definitions have internal linkage: two files can use the same
    # variable name for different events (EV_GetState), so look in the binding's file first
    returns: dict[str, set[str | None]] = {}
    formats: dict[Path, dict[str, tuple[str, str]]] = {}
    notes: list[str] = []
    for path, text in texts.items():
        for m in DEF_RE.finditer(text):
            fmt = parse_format(m.group(3))
            if fmt is None:
                notes.append(f'{path.relative_to(root)}: cannot read the format of {m.group(1)}')
                continue
            formats.setdefault(path, {})[m.group(1)] = (m.group(2)[1:-1], fmt)
            returns.setdefault(m.group(2)[1:-1], set()).add(parse_return(m.group(3)))

    def find_format(path: Path, var: str) -> tuple[str, str] | None | str:
        if var in formats.get(path, {}):
            return formats[path][var]
        found = {v[var] for v in formats.values() if var in v}
        if len(found) > 1:
            return 'ambiguous'
        return found.pop() if found else None

    source = Source('\n'.join(texts.values()))
    problems: list[str] = []
    bindings = 0
    for path, text in texts.items():
        for block in BLOCK_RE.finditer(text):
            for m in EVENT_RE.finditer(block.group(2)):
                var, cls, method = m.groups()
                bindings += 1
                where = f'{path.relative_to(root)}:{text.count(chr(10), 0, block.start(2) + m.start()) + 1}'
                found = find_format(path, var)
                if found is None or found == 'ambiguous':
                    notes.append(f'{where}: {"no" if found is None else "more than one different"} definition found for {var}')
                    continue
                name, fmt = found
                params = handler_parameters(source, cls, method)
                if params is None:
                    notes.append(f'{where}: no declaration found for {cls}::{method}')
                    continue
                kinds = [k for k in (parameter_kind(p, ints) for p in params.split(',')) if k is not None]
                if len(kinds) > len(fmt):
                    problems.append(f'{where}: {cls}::{method} takes {len(kinds)} parameters, but {var} '
                                    f'("{name}", "{fmt}") passes {len(fmt)}')
                    continue
                for i, kind in enumerate(kinds):
                    if kind != FORMAT_KIND.get(fmt[i]):
                        problems.append(f'{where}: {cls}::{method} parameter {i + 1} is {kind}, but {var} '
                                        f'("{name}", "{fmt}") passes {FORMAT_KIND.get(fmt[i], fmt[i])}')
    script_problems, script_notes, declarations = check_script(root, formats, returns, script_required)
    return problems + script_problems, notes + script_notes, bindings, declarations


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', type=Path, default=REPO_ROOT, help='repository root (default: this checkout)')
    args = ap.parse_args(argv)
    if not (args.root / 'src' / 'game').is_dir():
        print(f'check_event_handlers: no src/game under {args.root}', file=sys.stderr)
        return 2
    # the real tree must have events.script; selftest trees (--root) may leave it out
    problems, notes, bindings, declarations = check(args.root, script_required=args.root.resolve() == REPO_ROOT)
    for p in problems:
        print(p)
    for n in notes:
        print(f'not checked: {n}')
    print(f'check_event_handlers: {bindings} bindings, {declarations} script declarations, '
          f'{len(problems)} mismatch(es), {len(notes)} not checked')
    return 1 if problems or notes else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
