#!/usr/bin/env python3
"""Repair CodeRed-Generator output so the full SDK compiles under MSVC.

The generator emits five classes of invalid C++ in SDK_HEADERS/*.hpp; this script fixes all of
them in place and is idempotent. Rerun it after regenerating the SDK. It deliberately does NOT
touch GameDefines.hpp — that file is hand-maintained CodeRed boilerplate and compiles as-is
(a previous automated pass corrupted its TArray layout; keep it excluded).

Fixes:
1. `#define CONST_x "` — string value lost by the generator, unterminated literal → close it.
2. `#define CONST_x '` — same for name literals → make a valid unused char literal.
3. Struct fields named with dots (`OverriddenTangentVector.X`) → dots become underscores.
4. References to generator-suffixed enum names that were never defined
   (`ELeaderboardSortType_4`) → `uint8_t` (UE3 enums are one byte).
5. Duplicate declarations from name-collapsed UnrealScript functions (ten digit-key
   `bool Key(int32_t)` overloads) and their duplicated `_Params` structs → uniquified
   with a `_dupN` suffix (never referenced by our code).
"""

import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..", "generated_sdk", "X2SDK", "SDK_HEADERS")


def load(path):
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        return f.read().split("\n")


def save(path, lines):
    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
        f.write("\n".join(lines))


def fix_unterminated_literals(lines):
    fixed = 0
    for i, line in enumerate(lines):
        if not line.lstrip().startswith("#define"):
            continue
        if line.rstrip().endswith('"') and line.count('"') % 2 == 1:
            lines[i] = line + '"'
            fixed += 1
        elif line.rstrip().endswith("'") and line.count("'") % 2 == 1:
            lines[i] = line + "?'"
            fixed += 1
    return fixed


FIELD_DOT_RE = re.compile(r"([A-Za-z_]\w*)((?:\.[A-Za-z_]\w*)+)(\s*(?:\[[^\]]*\])?\s*;)")


def fix_dotted_fields(lines):
    fixed = 0
    for i, line in enumerate(lines):
        code, sep, comment = line.partition("//")
        ls = code.lstrip()
        if not ls or ls.startswith("#") or "(" in code:
            continue
        new, n = FIELD_DOT_RE.subn(
            lambda m: m.group(1) + m.group(2).replace(".", "_") + m.group(3), code)
        if n:
            lines[i] = new + sep + comment
            fixed += n
    return fixed


def collect_defined_enums(files):
    defined = set()
    enum_re = re.compile(r"^\s*enum\s+(?:class\s+)?([A-Za-z_]\w*)")
    for path in files:
        for line in load(path):
            m = enum_re.match(line)
            if m:
                defined.add(m.group(1))
    return defined


SUFFIXED_ENUM_RE = re.compile(r"\bE[A-Za-z]\w*_\d+\b")


def fix_undefined_enums(files):
    defined = collect_defined_enums(files)
    undefined = set()
    for path in files:
        for name in SUFFIXED_ENUM_RE.findall("\n".join(load(path))):
            if name not in defined:
                undefined.add(name)
    total = 0
    for path in files:
        text = "\n".join(load(path))
        orig = text
        for name in undefined:
            text = re.sub(r"\b" + re.escape(name) + r"\b", "uint8_t", text)
        if text != orig:
            save(path, text.split("\n"))
            total += 1
    return sorted(undefined), total


DECL_RE = re.compile(r"^(\s*(?:[\w:<>,\s*&]+?)\s+)([A-Za-z_]\w*)(\s*\()")


def fix_duplicate_method_decls(lines):
    """Within each class scope, uniquify textually identical method DECLARATIONS.

    Only single-line `...);`-terminated declarations count; inline bodies (lines ending in
    `{` or plain statements) are never touched — scope resets on class/struct/`};` lines.
    """
    seen = {}
    fixed = 0
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith("class ") or s.startswith("struct ") or s.startswith("template") or s == "};":
            seen = {}
            continue
        # A declaration: starts at one tab depth, ends with ");", is not a statement.
        if not (line.startswith("\t") and not line.startswith("\t\t")):
            continue
        if not s.endswith(");") or s.endswith("= 0;"):
            continue
        if s in seen:
            m = DECL_RE.match(line)
            if m:
                seen[s] += 1
                lines[i] = line.replace(m.group(2) + "(", f"{m.group(2)}_dup{seen[s]}(", 1)
                fixed += 1
        else:
            seen[s] = 0
    return fixed


STRUCT_RE = re.compile(r"^struct ([A-Za-z_]\w*)$")


def fix_duplicate_param_structs(lines):
    seen = {}
    fixed = 0
    for i, line in enumerate(lines):
        m = STRUCT_RE.match(line.strip())
        if not m:
            continue
        name = m.group(1)
        if name in seen:
            seen[name] += 1
            lines[i] = line.replace(name, f"{name}_dup{seen[name]}")
            fixed += 1
        else:
            seen[name] = 0
    return fixed


def main():
    files = sorted(glob.glob(os.path.join(ROOT, "*.hpp")))
    if not files:
        print(f"no headers found under {ROOT}", file=sys.stderr)
        return 1

    for path in files:
        lines = load(path)
        literals = fix_unterminated_literals(lines)
        dots = fix_dotted_fields(lines)
        decls = fix_duplicate_method_decls(lines) if path.endswith("_classes.hpp") else 0
        params = fix_duplicate_param_structs(lines) if path.endswith("_parameters.hpp") else 0
        if literals or dots or decls or params:
            save(path, lines)
            print(f"{os.path.basename(path)}: {literals} literals, {dots} dotted fields, "
                  f"{decls} dup decls, {params} dup param structs")

    undefined, touched = fix_undefined_enums(files)
    if undefined:
        print(f"undefined suffixed enums -> uint8_t: {', '.join(undefined)} ({touched} files)")
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
