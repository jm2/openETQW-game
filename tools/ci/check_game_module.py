#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate a built openETQW-game module (game_<arch>.{so,dylib,dll}).

Checks, all done by parsing the binary directly (no readelf/nm/dumpbin needed,
so the same script runs on every CI runner and on developer machines):

* file name: the engine loads ``game_<arch><ext>`` (arch x64 or arm64);
* container: ELF64 shared object / 64-bit thin Mach-O dylib / PE32+ DLL for
  the expected CPU;
* exports: exactly one public symbol, the ``GetGameAPI`` function (Linux
  dynsym GLOBAL/WEAK/UNIQUE with DEFAULT/PROTECTED visibility, the Mach-O
  export trie, the PE export directory including ordinal-only entries).
  ``--exports warn`` downgrades *extra* exports to a warning for platforms
  whose build does not restrict exports yet; a missing or malformed
  GetGameAPI always fails;
* macOS deployment floor (LC_BUILD_VERSION minos, default 11.0);
* hardening (Linux RELRO/BIND_NOW/non-executable stack/no TEXTREL, Windows
  DYNAMICBASE/NXCOMPAT/HIGH_ENTROPY_VA): ``--hardening warn`` (default)
  reports gaps as warnings, ``strict`` fails on them, ``off`` skips them;
* optional ``--expect-sanitizer``: the module references ASan/UBSan runtime
  symbols (proves the sanitizer build is really instrumented);
* optional ``--load-smoke``: load the module in a child process with ctypes
  and resolve GetGameAPI without calling it.  This runs every static
  initialiser and destructor of the module and needs no game data.

Exit codes: 0 pass, 1 check failure, 2 usage / environment error.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

EXPECTED_EXPORT = "GetGameAPI"
EXTENSIONS = {"linux": ".so", "macos": ".dylib", "windows": ".dll"}
ARCHES = ("x64", "arm64")
ANNOTATION_LIMIT = 20

ELF_MACHINES = {62: "x64", 183: "arm64"}
MACHO_CPUS = {0x01000007: "x64", 0x0100000C: "arm64"}
PE_MACHINES = {0x8664: "x64", 0xAA64: "arm64"}

SANITIZER_PREFIXES = ("__asan_", "__ubsan_", "___asan_", "___ubsan_")


def host_os() -> str:
    if sys.platform.startswith("linux"):
        return "linux"
    if sys.platform == "darwin":
        return "macos"
    if sys.platform in ("win32", "cygwin", "msys"):
        return "windows"
    return sys.platform


def normalize_arch(machine: str) -> str:
    value = machine.strip().lower()
    if value in ("x86_64", "amd64", "x64", "em64t"):
        return "x64"
    if value in ("arm64", "aarch64", "armv8", "arm64e"):
        return "arm64"
    return value


def host_arch() -> str:
    return normalize_arch(platform.machine())


@dataclass
class Findings:
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    info: dict[str, object] = field(default_factory=dict)

    def error(self, message: str) -> None:
        self.errors.append(message)

    def warn(self, message: str) -> None:
        self.warnings.append(message)


class FormatError(Exception):
    pass


class Reader:
    def __init__(self, data: bytes, little: bool = True):
        self.data = data
        self.prefix = "<" if little else ">"

    def unpack(self, fmt: str, offset: int) -> tuple:
        size = struct.calcsize(self.prefix + fmt)
        if offset < 0 or offset + size > len(self.data):
            raise FormatError(f"truncated read of {size} bytes at offset {offset:#x}")
        return struct.unpack_from(self.prefix + fmt, self.data, offset)

    def cstring(self, offset: int, limit: int = 4096) -> str:
        if offset < 0 or offset >= len(self.data):
            raise FormatError(f"string offset {offset:#x} out of range")
        end = self.data.find(b"\0", offset, min(len(self.data), offset + limit))
        if end < 0:
            raise FormatError(f"unterminated string at {offset:#x}")
        return self.data[offset:end].decode("utf-8", "replace")


# ----------------------------------------------------------------------------
# ELF
# ----------------------------------------------------------------------------
def inspect_elf(data: bytes, result: Findings) -> dict:
    if data[4] != 2:
        raise FormatError("not an ELF64 object (EI_CLASS != ELFCLASS64)")
    if data[5] != 1:
        raise FormatError("not a little-endian ELF object")
    r = Reader(data)
    e_type, e_machine = r.unpack("HH", 16)
    e_phoff, e_shoff = r.unpack("QQ", 32)
    e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = r.unpack("HHHHH", 54)
    if e_type != 3:
        result.error(f"ELF e_type is {e_type}, expected ET_DYN (3) for a shared object")

    sections = []
    for index in range(e_shnum):
        base = e_shoff + index * e_shentsize
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = r.unpack(
            "IIQQQQIIQQ", base
        )
        sections.append((sh_type, sh_offset, sh_size, sh_link, sh_entsize))

    exports: list[dict] = []
    undefined: list[str] = []
    dynsym = [s for s in sections if s[0] == 11]  # SHT_DYNSYM
    if not dynsym:
        raise FormatError("no .dynsym section")
    sh_type, sh_offset, sh_size, sh_link, sh_entsize = dynsym[0]
    str_offset = sections[sh_link][1]
    entsize = sh_entsize or 24
    for index in range(1, sh_size // entsize):
        st_name, st_info, st_other, st_shndx, st_value, st_size = r.unpack("IBBHQQ", sh_offset + index * entsize)
        name = r.cstring(str_offset + st_name) if st_name else ""
        bind, kind, visibility = st_info >> 4, st_info & 0xF, st_other & 0x3
        if st_shndx == 0:
            if name:
                undefined.append(name)
            continue
        if bind in (1, 2, 10) and visibility in (0, 3) and name:
            exports.append(
                {
                    "name": name,
                    "type": {0: "NOTYPE", 1: "OBJECT", 2: "FUNC", 6: "TLS", 10: "IFUNC"}.get(kind, str(kind)),
                    "bind": {1: "GLOBAL", 2: "WEAK", 10: "UNIQUE"}[bind],
                    "visibility": {0: "DEFAULT", 3: "PROTECTED"}[visibility],
                }
            )

    # Program headers: RELRO, executable stack.
    relro = False
    exec_stack = None
    for index in range(e_phnum):
        p_type, p_flags = r.unpack("II", e_phoff + index * e_phentsize)
        if p_type == 0x6474E552:
            relro = True
        elif p_type == 0x6474E551:
            exec_stack = bool(p_flags & 0x1)

    # Dynamic section: BIND_NOW, TEXTREL, DT_NEEDED.
    bind_now = False
    textrel = False
    needed: list[str] = []
    for sh_type, sh_offset, sh_size, sh_link, sh_entsize in sections:
        if sh_type != 6:  # SHT_DYNAMIC
            continue
        dynstr = sections[sh_link][1]
        for pos in range(sh_offset, sh_offset + sh_size, 16):
            d_tag, d_val = r.unpack("qQ", pos)
            if d_tag == 0:
                break
            if d_tag == 1:
                needed.append(r.cstring(dynstr + d_val))
            if d_tag == 24 or (d_tag == 30 and d_val & 0x8) or (d_tag == 0x6FFFFFFB and d_val & 0x1):
                bind_now = True
            if d_tag == 22 or (d_tag == 30 and d_val & 0x4):
                textrel = True

    hardening = {
        "relro": relro,
        "bindNow": bind_now,
        "nonExecStack": exec_stack is False,
        "noTextrel": not textrel,
    }
    return {
        "format": "ELF64",
        "arch": ELF_MACHINES.get(e_machine, f"e_machine={e_machine}"),
        "exports": exports,
        "undefined": undefined,
        "needed": needed,
        "hardening": hardening,
    }


# ----------------------------------------------------------------------------
# Mach-O
# ----------------------------------------------------------------------------
def read_uleb(data: bytes, offset: int, end: int) -> tuple[int, int]:
    value = shift = 0
    while True:
        if offset >= end:
            raise FormatError("truncated ULEB128 in export trie")
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, offset
        if shift > 63:
            raise FormatError("ULEB128 overflow in export trie")


def walk_export_trie(data: bytes, start: int, size: int) -> list[tuple[str, int]]:
    end = start + size
    names: list[tuple[str, int]] = []
    stack = [(start, "")]
    visited: set[int] = set()
    while stack:
        node, prefix = stack.pop()
        if node in visited or not start <= node < end:
            raise FormatError("malformed export trie (cycle or out-of-range node)")
        visited.add(node)
        terminal_size, cursor = read_uleb(data, node, end)
        if terminal_size:
            flags, _ = read_uleb(data, cursor, end)
            names.append((prefix, flags))
        cursor += terminal_size
        if cursor >= end:
            raise FormatError("truncated export trie node")
        children = data[cursor]
        cursor += 1
        for _ in range(children):
            label_end = data.find(b"\0", cursor, end)
            if label_end < 0:
                raise FormatError("unterminated export trie edge")
            label = data[cursor:label_end].decode("utf-8", "replace")
            child, cursor = read_uleb(data, label_end + 1, end)
            stack.append((start + child, prefix + label))
    return names


def inspect_macho(data: bytes, result: Findings) -> dict:
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic in (0xCAFEBABE, 0xCAFEBABF) or struct.unpack_from(">I", data, 0)[0] in (0xCAFEBABE, 0xCAFEBABF):
        raise FormatError("universal (fat) Mach-O; CI expects one thin slice per runner architecture")
    if magic != 0xFEEDFACF:
        raise FormatError(f"not a 64-bit little-endian Mach-O (magic {magic:#x})")
    r = Reader(data)
    _, cputype, _, filetype, ncmds, sizeofcmds, flags, _ = r.unpack("IiIIIIII", 0)
    if filetype != 6:
        result.error(f"Mach-O filetype is {filetype}, expected MH_DYLIB (6)")

    cursor = 32
    symtab = None
    trie = None
    minos = None
    install_name = None
    needed: list[str] = []
    for _ in range(ncmds):
        cmd, cmdsize = r.unpack("II", cursor)
        if cmdsize < 8:
            raise FormatError("malformed load command")
        if cmd == 0x2:  # LC_SYMTAB
            symtab = r.unpack("IIII", cursor + 8)
        elif cmd in (0x22, 0x80000022):  # LC_DYLD_INFO(_ONLY)
            export_off, export_size = r.unpack("II", cursor + 40)
            if export_size:
                trie = (export_off, export_size)
        elif cmd == 0x80000033:  # LC_DYLD_EXPORTS_TRIE
            trie = r.unpack("II", cursor + 8)
        elif cmd == 0x32:  # LC_BUILD_VERSION
            _, version = r.unpack("II", cursor + 8)
            minos = version
        elif cmd == 0x24 and minos is None:  # LC_VERSION_MIN_MACOSX
            minos = r.unpack("I", cursor + 8)[0]
        elif cmd == 0xD:  # LC_ID_DYLIB
            install_name = r.cstring(cursor + r.unpack("I", cursor + 8)[0])
        elif cmd in (0xC, 0x80000018, 0x8000001F):  # LC_LOAD_DYLIB / WEAK / REEXPORT
            needed.append(r.cstring(cursor + r.unpack("I", cursor + 8)[0]))
        cursor += cmdsize

    exports: list[dict] = []
    undefined: list[str] = []
    if symtab:
        symoff, nsyms, stroff, strsize = symtab
        for index in range(nsyms):
            n_strx, n_type, n_sect, n_desc, n_value = r.unpack("IBBHQ", symoff + index * 16)
            if n_type & 0xE0:  # N_STAB debugging entries
                continue
            name = r.cstring(stroff + n_strx) if n_strx else ""
            if n_type & 0x01 and (n_type & 0x0E) == 0x0 and name:
                undefined.append(name[1:] if name.startswith("_") else name)
    if trie:
        for name, trie_flags in walk_export_trie(data, trie[0], trie[1]):
            kind = {0: "REGULAR", 1: "THREAD_LOCAL", 2: "ABSOLUTE"}.get(trie_flags & 0x3, str(trie_flags & 0x3))
            if trie_flags & 0x8:
                kind = "REEXPORT"
            exports.append({"name": name[1:] if name.startswith("_") else name, "type": kind})
    elif symtab:
        symoff, nsyms, stroff, strsize = symtab
        for index in range(nsyms):
            n_strx, n_type, n_sect, n_desc, n_value = r.unpack("IBBHQ", symoff + index * 16)
            if n_type & 0xE0:
                continue
            if n_type & 0x01 and not n_type & 0x10 and (n_type & 0x0E) == 0x0E:
                name = r.cstring(stroff + n_strx)
                exports.append({"name": name[1:] if name.startswith("_") else name, "type": "SECT"})
    else:
        raise FormatError("no export trie and no symbol table")

    return {
        "format": "Mach-O 64",
        "arch": MACHO_CPUS.get(cputype, f"cputype={cputype:#x}"),
        "exports": exports,
        "undefined": undefined,
        "needed": needed,
        "installName": install_name,
        "minos": None if minos is None else f"{minos >> 16}.{(minos >> 8) & 0xFF}" + (f".{minos & 0xFF}" if minos & 0xFF else ""),
    }


# ----------------------------------------------------------------------------
# PE
# ----------------------------------------------------------------------------
def inspect_pe(data: bytes, result: Findings) -> dict:
    r = Reader(data)
    pe_offset = r.unpack("I", 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise FormatError("missing PE signature")
    coff = pe_offset + 4
    machine, nsections, _, _, _, opt_size, characteristics = r.unpack("HHIIIHH", coff)
    if not characteristics & 0x2000:
        result.error("PE image is not flagged IMAGE_FILE_DLL")
    opt = coff + 20
    opt_magic = r.unpack("H", opt)[0]
    if opt_magic != 0x20B:
        raise FormatError(f"not a PE32+ image (optional header magic {opt_magic:#x})")
    dll_characteristics = r.unpack("H", opt + 70)[0]
    num_dirs = r.unpack("I", opt + 108)[0]
    export_rva, export_size = r.unpack("II", opt + 112) if num_dirs > 0 else (0, 0)

    sections = []
    table = opt + opt_size
    for index in range(nsections):
        vsize, vaddr, raw_size, raw_ptr = r.unpack("IIII", table + index * 40 + 8)
        sections.append((vaddr, max(vsize, raw_size), raw_ptr))

    def rva_to_offset(rva: int) -> int:
        for vaddr, size, raw_ptr in sections:
            if vaddr <= rva < vaddr + size:
                return raw_ptr + (rva - vaddr)
        raise FormatError(f"RVA {rva:#x} is not inside any section")

    exports: list[dict] = []
    if export_rva and export_size:
        base = rva_to_offset(export_rva)
        (_, _, _, _, name_rva, ordinal_base, n_functions, n_names, functions_rva, names_rva, ordinals_rva) = r.unpack(
            "IIHHIIIIIII", base
        )
        named_ordinals: set[int] = set()
        for index in range(n_names):
            name_ptr = r.unpack("I", rva_to_offset(names_rva) + index * 4)[0]
            ordinal = r.unpack("H", rva_to_offset(ordinals_rva) + index * 2)[0]
            named_ordinals.add(ordinal)
            function_rva = r.unpack("I", rva_to_offset(functions_rva) + ordinal * 4)[0]
            forwarded = export_rva <= function_rva < export_rva + export_size
            exports.append({"name": r.cstring(rva_to_offset(name_ptr)), "type": "FORWARDER" if forwarded else "CODE"})
        for index in range(n_functions):
            function_rva = r.unpack("I", rva_to_offset(functions_rva) + index * 4)[0]
            if function_rva and index not in named_ordinals:
                exports.append({"name": f"#ordinal{ordinal_base + index}", "type": "ORDINAL_ONLY"})

    hardening = {
        "dynamicBase": bool(dll_characteristics & 0x40),
        "nxCompat": bool(dll_characteristics & 0x100),
        "highEntropyVA": bool(dll_characteristics & 0x20),
    }
    return {
        "format": "PE32+ DLL",
        "arch": PE_MACHINES.get(machine, f"machine={machine:#x}"),
        "exports": exports,
        "undefined": [],
        "needed": [],
        "hardening": hardening,
        "guardCF": bool(dll_characteristics & 0x4000),
    }


# ----------------------------------------------------------------------------
# Load smoke
# ----------------------------------------------------------------------------
LOAD_SMOKE = r"""
import ctypes, os, sys
path = sys.argv[1]
mode = getattr(os, "RTLD_NOW", 0) | getattr(os, "RTLD_LOCAL", 0)
lib = ctypes.CDLL(path, mode=mode) if os.name != "nt" else ctypes.CDLL(path)
entry = lib.GetGameAPI
address = ctypes.cast(entry, ctypes.c_void_p).value
if not address:
    sys.exit("GetGameAPI resolved to NULL")
print("GetGameAPI resolved at 0x%x" % address)
"""


def load_smoke(module: Path, timeout: int) -> tuple[bool, str]:
    try:
        completed = subprocess.run(
            [sys.executable, "-c", LOAD_SMOKE, str(module.resolve())],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return False, f"load smoke timed out after {timeout}s (static initialiser deadlock?)"
    output = completed.stdout.strip()
    if completed.returncode != 0:
        how = f"signal {-completed.returncode}" if completed.returncode < 0 else f"exit {completed.returncode}"
        return False, f"loading the module failed ({how}): {output[-2000:]}"
    return True, output


# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------
def github_actions() -> bool:
    return os.environ.get("GITHUB_ACTIONS", "").lower() == "true"


def annotate(level: str, message: str) -> None:
    if github_actions():
        print(f"::{level} title=Game module check::" + message.replace("%", "%25").replace("\n", "%0A"))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, default=Path("build"), help="Meson build directory (default: build)")
    parser.add_argument("--module", type=Path, help="module path (default: <build-dir>/game_<arch><ext>)")
    parser.add_argument("--os", dest="target_os", choices=("auto", "linux", "macos", "windows"), default="auto")
    parser.add_argument("--arch", choices=("auto",) + ARCHES, default="auto")
    parser.add_argument("--min-macos", default="11.0", help="required LC_BUILD_VERSION minos on macOS ('' to skip)")
    parser.add_argument("--exports", choices=("warn", "strict"), default="strict", help="policy for extra exports")
    parser.add_argument("--hardening", choices=("off", "warn", "strict"), default="warn")
    parser.add_argument("--expect-sanitizer", action="store_true", help="require ASan/UBSan runtime references")
    parser.add_argument("--load-smoke", action="store_true", help="dlopen/LoadLibrary the module and resolve GetGameAPI")
    parser.add_argument("--load-timeout", type=int, default=120)
    parser.add_argument("--report", type=Path, help="write a JSON report here")
    args = parser.parse_args(argv)

    target_os = host_os() if args.target_os == "auto" else args.target_os
    arch = host_arch() if args.arch == "auto" else args.arch
    if target_os not in EXTENSIONS:
        print(f"error: unsupported target OS {target_os!r}", file=sys.stderr)
        return 2
    if arch not in ARCHES:
        print(f"error: unsupported architecture {arch!r}", file=sys.stderr)
        return 2

    expected_name = f"game_{arch}{EXTENSIONS[target_os]}"
    module = args.module or args.build_dir / expected_name
    result = Findings()
    result.info.update({"module": module.as_posix(), "expectedName": expected_name, "os": target_os, "arch": arch})

    if module.name != expected_name:
        result.error(f"module is named {module.name!r}; the engine loads {expected_name!r}")
    if not module.is_file():
        others = sorted(p.name for p in module.parent.glob("game_*") if p.is_file()) if module.parent.is_dir() else []
        result.error(f"module not found: {module}" + (f" (found: {', '.join(others)})" if others else ""))
        return finish(result, args)
    stray = sorted(
        p.name
        for p in module.parent.glob("game_*" + EXTENSIONS[target_os])
        if p.is_file() and p.name != expected_name
    )
    if stray:
        result.warn(f"unexpected extra game modules next to {expected_name}: {', '.join(stray)}")

    data = module.read_bytes()
    result.info["bytes"] = len(data)
    try:
        if data[:4] == b"\x7fELF":
            details = inspect_elf(data, result)
            detected_os = "linux"
        elif data[:2] == b"MZ":
            details = inspect_pe(data, result)
            detected_os = "windows"
        elif data[:4] in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf"):
            details = inspect_macho(data, result)
            detected_os = "macos"
        else:
            raise FormatError(f"unrecognised binary format (magic {data[:4].hex()})")
    except (FormatError, struct.error, IndexError) as exc:
        result.error(f"cannot parse {module.name}: {exc}")
        return finish(result, args)

    result.info.update({k: v for k, v in details.items() if k not in ("exports", "undefined")})
    result.info["exports"] = details["exports"]
    result.info["undefinedCount"] = len(details["undefined"])

    if detected_os != target_os:
        result.error(f"{module.name} is a {details['format']} binary, expected a {target_os} module")
    if details["arch"] != arch:
        result.error(f"{module.name} targets {details['arch']}, expected {arch}")

    exports = details["exports"]
    api = [e for e in exports if e["name"] == EXPECTED_EXPORT]
    unexpected = [e for e in exports if e["name"] != EXPECTED_EXPORT]
    if len(api) != 1:
        result.error(f"expected exactly one {EXPECTED_EXPORT} export, found {len(api)}")
    elif detected_os == "linux" and (api[0]["type"], api[0]["bind"], api[0]["visibility"]) != ("FUNC", "GLOBAL", "DEFAULT"):
        result.error(f"{EXPECTED_EXPORT} must be a GLOBAL DEFAULT FUNC, found {api[0]}")
    elif detected_os == "windows" and api[0]["type"] != "CODE":
        result.error(f"{EXPECTED_EXPORT} must be exported as code, found {api[0]['type']}")
    result.info["unexpectedExports"] = len(unexpected)
    if unexpected:
        sample = ", ".join(e["name"] for e in unexpected[:15])
        (result.error if args.exports == "strict" else result.warn)(
            f"{len(unexpected)} unexpected public symbol(s) besides {EXPECTED_EXPORT} (first: {sample}); "
            "the game module must export only its entry point"
        )

    if detected_os == "macos" and args.min_macos:
        if details.get("minos") != args.min_macos:
            result.error(f"macOS deployment floor is {details.get('minos')}, expected {args.min_macos}")

    hardening = details.get("hardening") or {}
    missing = [name for name, ok in hardening.items() if not ok]
    if missing and args.hardening != "off":
        message = f"hardening gaps: {', '.join(missing)}"
        (result.error if args.hardening == "strict" else result.warn)(message)

    sanitizer_refs = sorted({s for s in details["undefined"] if s.startswith(SANITIZER_PREFIXES)})
    result.info["sanitizerRefs"] = len(sanitizer_refs)
    if args.expect_sanitizer and not sanitizer_refs:
        result.error("module has no ASan/UBSan runtime references; the sanitizer build is not instrumented")

    if args.load_smoke:
        py_arch = host_arch()
        if host_os() != target_os or py_arch != arch:
            result.warn(f"load smoke skipped: this Python is {host_os()}/{py_arch}, module is {target_os}/{arch}")
            result.info["loadSmoke"] = "skipped"
        else:
            ok, output = load_smoke(module, args.load_timeout)
            result.info["loadSmoke"] = "passed" if ok else "failed"
            result.info["loadSmokeOutput"] = output[-4000:]
            if not ok:
                result.error(output)

    return finish(result, args)


def finish(result: Findings, args: argparse.Namespace) -> int:
    info = result.info
    status = "FAILED" if result.errors else "PASSED"
    print(f"Game module check: {info.get('module')}")
    for key in ("format", "arch", "bytes", "installName", "minos", "needed", "hardening", "sanitizerRefs", "loadSmoke"):
        if key in info and info[key] not in (None, [], {}):
            print(f"  {key:<14} {info[key]}")
    if "exports" in info:
        names = [e["name"] for e in info["exports"]]
        more = f" (+{len(names) - 20} more)" if len(names) > 20 else ""
        print(f"  {'exports':<14} {names[:20]}{more}")
    if info.get("loadSmokeOutput"):
        print(f"  {'loadOutput':<14} {str(info['loadSmokeOutput']).splitlines()[-1] if str(info['loadSmokeOutput']).strip() else ''}")
    for index, message in enumerate(result.warnings):
        print(f"warning: {message}")
        if index < ANNOTATION_LIMIT:
            annotate("warning", message)
    for index, message in enumerate(result.errors):
        print(f"error: {message}", file=sys.stderr)
        if index < ANNOTATION_LIMIT:
            annotate("error", message)
    print(f"{status}: {info.get('expectedName')}")

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        payload = dict(info, status=status.lower(), errors=result.errors, warnings=result.warnings)
        args.report.write_text(json.dumps(payload, indent=2, sort_keys=True, default=str) + "\n", encoding="utf-8")

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        export_names = [e["name"] for e in info.get("exports", [])]
        exports = ", ".join(export_names[:3]) + (f" (+{len(export_names) - 3})" if len(export_names) > 3 else "") or "-"
        hardening = info.get("hardening") or {}
        gaps = ", ".join(k for k, v in hardening.items() if not v) or ("none" if hardening else "n/a")
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(
                f"### Game module `{info.get('expectedName')}`: {status.lower()}\n\n"
                "| format | arch | exports | minos | hardening gaps | load smoke |\n|---|---|---|---|---|---|\n"
                f"| {info.get('format', '-')} | {info.get('arch', '-')} | {exports} | {info.get('minos') or '-'} "
                f"| {gaps} | {info.get('loadSmoke', 'not run')} |\n\n"
            )
            for message in result.errors:
                handle.write(f"- :x: {message}\n")
            for message in result.warnings:
                handle.write(f"- :warning: {message}\n")
            handle.write("\n")
    return 1 if result.errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
