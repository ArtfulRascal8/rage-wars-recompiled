#!/usr/bin/env python3
"""Normalize N64Recomp overlay metadata for strict C++ compilers.

N64Recomp emits zero-length FuncEntry arrays for sections containing only
host-owned functions. GCC and Clang accept those as extensions, but MSVC does
not. This preserves the generated output and creates a build-local include
whose empty sections use nullptr plus a zero function count.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re


EMPTY_ARRAY_RE = re.compile(
    r"static FuncEntry (?P<name>[A-Za-z_][A-Za-z0-9_]*)_funcs\[\] = \{\r?\n\};"
)


def normalize(source: str) -> tuple[str, int]:
    names = [match.group("name") for match in EMPTY_ARRAY_RE.finditer(source)]
    if not names:
        raise ValueError("no empty FuncEntry arrays were found")

    normalized = EMPTY_ARRAY_RE.sub(
        lambda match: f"static FuncEntry {match.group('name')}_funcs[1] = {{}};",
        source,
    )

    for name in names:
        old = f".funcs = {name}_funcs, .num_funcs = ARRLEN({name}_funcs)"
        new = ".funcs = nullptr, .num_funcs = 0"
        count = normalized.count(old)
        if count != 1:
            raise ValueError(
                f"expected exactly one section-table reference for {name}, found {count}"
            )
        normalized = normalized.replace(old, new, 1)

    return normalized, len(names)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.input.read_text(encoding="utf-8")
    normalized, count = normalize(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(normalized, encoding="utf-8", newline="\n")
    print(f"normalized {count} empty overlay function arrays")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
