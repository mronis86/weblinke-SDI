#!/usr/bin/env python3
"""Embed src/control/control_page.html into web_assets.h for MSVC."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from split_msvc_raw_strings import process_file

HTML = ROOT / "src" / "control" / "control_page.html"
HEADER = ROOT / "src" / "control" / "web_assets.h"

html = HTML.read_text(encoding="utf-8")
if ")WEBLINKED" in html:
    raise SystemExit("control_page.html contains the raw-string delimiter")

HEADER.write_text(
    """#pragma once

#include <string>

namespace weblinked::assets {

/// The control page, compiled in. Edit control_page.html, then run
/// tools/embed_control_page.py — there is still no page build step at
/// compile time; this header is what the binary serves.

inline constexpr const char* kControlPage = R"WEBLINKED("""
    + html
    + """)WEBLINKED";

}  // namespace weblinked::assets
""",
    encoding="utf-8",
    newline="\n",
)

process_file(HEADER)
print("wrote", HEADER)
