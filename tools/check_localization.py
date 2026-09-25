"""Check X2Access's shared English/Russian vocabulary before building."""

from __future__ import annotations

from collections import Counter
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "mods/X2AccessCompanion"
CLASS = BASE / "Src/X2AccessCompanion/Classes/X2AccessVocab.uc"
TAG = re.compile(r"<XGParam:(?:IntValue[012]|StrValue0)/>|\{[01]\}")


def keys(path: Path, pattern: str) -> list[str]:
    return re.findall(pattern, path.read_text(encoding="utf-8"), re.M)


def localized(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(";") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if value.startswith('"') and value.endswith('"'):
            result[key] = value[1:-1]
    return result


def main() -> int:
    declared = keys(CLASS, r"^var localized string (\w+);")
    english_path = BASE / "Localization/X2AccessCompanion.int"
    russian_path = BASE / "Localization/X2AccessCompanion.rus"
    english_keys = keys(english_path, r"^(\w+)=")
    russian_keys = keys(russian_path, r"^(\w+)=")
    native_words = keys(ROOT / "src/vocab.h", r"^inline constexpr Word (\w+)\{")
    native_words += keys(ROOT / "src/vocab.h", r"^inline constexpr Word (\w+)\s*$")
    native_index = keys(ROOT / "src/vocab_words.inc", r'^\s*"(\w+)",')
    errors = []

    native_header = (ROOT / "src/vocab.h").read_text(encoding="utf-8")
    if re.search(r'inline constexpr Word \w+\{\s*"\w+"\s*,', native_header):
        errors.append("native words must contain only a localization key")
    for path in (BASE / "Src/X2AccessCompanion/Classes").glob("*.uc"):
        if path.name == "X2AccessTileProbe.uc":  # dev-server diagnostics, never speech
            continue
        for match in re.finditer(r'\b(?:NativeResult|AccessibilityData)\s*=\s*"([^"\r\n]+)"',
                                 path.read_text(encoding="utf-8")):
            errors.append(f"{path.name}: literal speech result {match.group(1)!r}")

    for label, values in (("class", declared), ("INT", english_keys), ("RUS", russian_keys),
                          ("native index", native_index)):
        duplicates = sorted(key for key, count in Counter(values).items() if count > 1)
        if duplicates:
            errors.append(f"{label}: duplicate keys {duplicates}")
    for label, values in (("INT", english_keys), ("RUS", russian_keys)):
        if set(values) != set(declared):
            errors.append(f"{label}: missing {sorted(set(declared) - set(values))}, "
                          f"undeclared {sorted(set(values) - set(declared))}")
    if set(native_words) != set(native_index):
        errors.append(f"native index: missing {sorted(set(native_words) - set(native_index))}, "
                      f"stale {sorted(set(native_index) - set(native_words))}")
    if not set(native_words) <= set(declared):
        errors.append(f"native words undeclared: {sorted(set(native_words) - set(declared))}")

    english = localized(english_path)
    russian = localized(russian_path)
    for key in native_index:
        if not english.get(key) or not russian.get(key):
            errors.append(f"native word {key}: INT and RUS must both be nonempty")
    for key in set(english) & set(russian):
        if not russian[key] and english[key] and not key.endswith("_Few"):
            errors.append(f"RUS: empty {key}")
        reference = english[key]
        if key.endswith("_Few") and not reference:
            reference = english.get(key[:-4] + "_Many", "")
        if russian[key] and Counter(TAG.findall(reference)) != Counter(TAG.findall(russian[key])):
            errors.append(f"RUS: placeholder mismatch in {key}")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Localization OK: {len(declared)} matching keys; {len(native_words)} native words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
