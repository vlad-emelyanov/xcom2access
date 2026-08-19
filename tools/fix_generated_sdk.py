#!/usr/bin/env python3
"""Repair CodeRed-Generator output so the full SDK compiles under MSVC.

Most of what this used to repair is now fixed IN THE GENERATOR (see
deps/CodeRed-Generator/dllmain.cpp), because a fix at generation time cannot be forgotten:

  - unterminated CONST_x literals    gone with m_useConstants = false (no SdkConstants.hpp)
  - dotted struct field names        '.' (and < > ~ space) added to m_unsafeChars
  - duplicate method declarations    UniquifyFunctionNames(), applied in all three passes
  - duplicate _Params structs        same fix (both names derive from the function's ValidName)
  - GameDefines.hpp pack region      now emitted by the generator, no longer applied by hand

What is left is one case, kept here because the root cause is not yet understood:

  - references to generator-suffixed enum names that were never defined
    (ELeaderboardSortType_4) -> uint8_t, which is right because UE3 enums are one byte.

The script stays wired into `just sdk-adopt` and is idempotent, so it also acts as the check
that the generator fixes are still doing their job: if a per-file line reappears in its output,
something regressed in the generator. It deliberately does NOT touch GameDefines.hpp.

Scope note: this repairs the HEADERS so their class layouts compile (that is what
sdk_layout_checks.cpp asserts against). It does not touch the *_classes.cpp function wrappers,
because those are unusable and are never compiled: all 29388 of them resolve their UFunction
from a CLASS index (GObjObjects()->at(IDX_CLASS_...)), and 2308 additionally clear FUNC_Native
around the call, which makes ProcessEvent run a native's 2-byte script stub and return a zeroed
param block. Outbound calls resolve functions by name at runtime instead (GameUI::
CallNamedFunction / CallStaticNative). Do not "fix" the wrappers; do not add them to the build.

Historical note — this used to say RE-APPLY BY HAND to GameDefines.hpp: a `#pragma pack(push, 0x4)` after its
standard includes and a matching `pop` at end of file. The generator omits it there (every
SDK_HEADERS/*.hpp gets one), which leaves FNameEntry/TArray/FName/UObject taking whatever pack
each including TU is in — an ODR violation that misplaces FNameEntry::Name and breaks GNames
lookup. src/sdk_layout_checks.cpp static_asserts the result, so a missed re-apply is a build
error rather than a silent runtime failure.

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

DEFAULT_ROOT = os.path.join(
    os.path.dirname(__file__), "..", "generated_sdk", "X2SDK", "SDK_HEADERS")


def resolve_root(argv):
    """Repair the installed SDK by default, or an explicit one given on the command line.

    Taking a path matters for the regeneration flow: a fresh dump is repaired in
    generated_sdk_new/ and only swapped in once it builds, so a bad regeneration never replaces
    a working SDK. Accepts either the X2SDK directory or its SDK_HEADERS subdirectory.
    """
    if len(argv) < 2:
        return DEFAULT_ROOT
    root = os.path.abspath(argv[1])
    headers = os.path.join(root, "SDK_HEADERS")
    return headers if os.path.isdir(headers) else root


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
    root = resolve_root(sys.argv)
    print(f"repairing {root}")
    files = sorted(glob.glob(os.path.join(root, "*.hpp")))
    if not files:
        print(f"no headers found under {root}", file=sys.stderr)
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
