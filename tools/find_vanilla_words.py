"""Show exact English matches for native vocabulary in installed XCOM 2 localization."""

from __future__ import annotations

import argparse
import collections
import pathlib
import re


def entries(path: pathlib.Path) -> dict[tuple[str, str], str]:
    section = ""
    result: dict[tuple[str, str], str] = {}
    for line in path.read_text(encoding="utf-16").splitlines():
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif not line.startswith(";") and "=" in line:
            key, value = line.split("=", 1)
            if value.startswith('"') and value.endswith('"'):
                result[(section, key)] = value[1:-1]
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("game_loc", type=pathlib.Path)
    args = parser.parse_args()
    english = entries(args.game_loc / "INT" / "XComGame.int")
    russian = entries(args.game_loc / "RUS" / "XComGame.rus")
    matches: dict[str, list[tuple[str, str, str]]] = collections.defaultdict(list)
    for ident, word in english.items():
        translated = russian.get(ident)
        if translated:
            matches[word.casefold()].append((*ident, translated))

    source = pathlib.Path("src/vocab.h").read_text(encoding="utf-8")
    pattern = re.compile(r'^inline constexpr Word (\w+)\{"\w+", "([^"]*)"\};', re.M)
    for name, word in pattern.findall(source):
        if word.casefold() in matches:
            print(f"{name}: {word}")
            for section, key, translated in matches[word.casefold()]:
                print(f"  [{section}] {key}: {translated}")


if __name__ == "__main__":
    main()
