#!/usr/bin/env python3
"""Rewrite oversized asset headers for MSVC 2019.

MSVC before VS 2022 rejects *concatenated* string literals over ~65535 bytes
(C2026), so splitting into adjacent R\"...\" pieces does not help. Emit separate
char arrays and a small helper that appends them into a std::string at runtime.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

LIMIT = 8000  # keep each array well under the MSVC literal limit

RAW_RE = re.compile(
    r"(inline\s+constexpr\s+const\s+char\*\s+)(\w+)(\s*=\s*)R\"(\w+)\((.*?)\)\4\"\s*;",
    re.S,
)


def emit_chunks(name: str, delim: str, content: str) -> str:
    chunks: list[str] = []
    i = 0
    n = len(content)
    idx = 0
    while i < n:
        j = min(i + LIMIT, n)
        if j < n:
            nl = content.rfind("\n", i, j)
            if nl > i + LIMIT // 2:
                j = nl + 1
        chunks.append(
            f"inline constexpr char {name}_{idx}[] = R\"{delim}({content[i:j]}){delim}\";"
        )
        idx += 1
        i = j

    parts = "\n".join(chunks)
    appends = "\n".join(
        f"  out.append(detail::{name}_{i}, sizeof(detail::{name}_{i}) - 1);"
        for i in range(idx)
    )
    sizes = " + ".join(f"(sizeof(detail::{name}_{i}) - 1)" for i in range(idx))
    return f"""namespace detail {{
{parts}
}}  // namespace detail

inline std::string {name}() {{
  std::string out;
  out.reserve({sizes});
{appends}
  return out;
}}"""


def process_file(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    if "#include <string>" not in text:
        # Insert after #pragma once
        text = text.replace("#pragma once\n", "#pragma once\n\n#include <string>\n", 1)

    changed = False

    def repl(match: re.Match[str]) -> str:
        nonlocal changed
        _prefix, name, _eq, delim, content = match.groups()
        changed = True
        print(f"{path.name}: rewrite {name} ({len(content)} chars)")
        return emit_chunks(name, delim, content)

    new_text = RAW_RE.sub(repl, text)
    # If already converted to functions, leave alone.
    if not changed:
        print(f"{path.name}: no pointer-style raw strings to rewrite")
        return False
    path.write_text(new_text, encoding="utf-8", newline="\n")
    return True


def main() -> int:
    files = [Path(p) for p in sys.argv[1:]]
    if not files:
        return 1
    for f in files:
        if not f.exists():
            print(f"missing: {f}", file=sys.stderr)
            return 1
        process_file(f)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
