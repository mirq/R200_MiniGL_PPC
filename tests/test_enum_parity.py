#!/usr/bin/env python3
"""Enum parity guard between the R200 backends and their client headers.

The backend's self-contained GL constant header must assign exactly the
same values the dispatch client compiles with. The WOS client header
(include/mgl/gl.h) re-anchored GL_TEXTURE0_ARB to the real GL spec values
and re-anchored GL_MAX_TEXTURE_UNITS_ARB to GL_FILL+1, which shifted its
tail constants by -4 relative to the classic client header
(classic_68k/dev/include/mgl/gl.h). A drifted backend constant is not
cosmetic: ValidPrimitive()/glHint/glColorTable validate raw client
values, and byte-order constants (BGRA/ARGB) silently swap channels when
they mismatch. This was the root cause of the 2026-09 GLQuake
"draw fails 0x58" and broken 565/BGRA uploads (MiniGL_R200_BUG_REPORT.md).

Checked pairs:
  1. backend_r200/library/minigl_r200.h            vs include/mgl/gl.h
  2. classic_68k/dev/MiniGL_Library_Source_Code/library/minigl_r200.h
                                                   vs classic_68k/dev/include/mgl/gl.h

Only constants that exist in BOTH headers are compared. Backend-only
constants (the native lighting tail, GL_POINT/GL_LINE, ...) and
client-only constants are ignored. Non-numeric backend defines
(bitmask macros like GLROT_*) are skipped.

Run:  python3 tests/test_enum_parity.py   (from the repository root)
Exit: 0 on parity, 1 with a mismatch report otherwise.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

PAIRS = [
    (
        REPO / "backend_r200" / "library" / "minigl_r200.h",
        REPO / "include" / "mgl" / "gl.h",
        "WOS frontend vs WOS client header",
    ),
    (
        REPO / "classic_68k" / "dev" / "MiniGL_Library_Source_Code"
        / "library" / "minigl_r200.h",
        REPO / "classic_68k" / "dev" / "include" / "mgl" / "gl.h",
        "native backend vs classic client header",
    ),
]

DEFINE_RE = re.compile(r"^#define\s+((?:GL|MGL)_[A-Za-z0-9_]+)\s+(\d+)U?\s*$")
IDENT = r"[A-Za-z_][A-Za-z0-9_]*"
ENTRY_RE = re.compile(rf"^\s*({IDENT})\s*(?:=\s*(.+?))?\s*,?\s*(?:/\*.*)?$")


def parse_backend_defines(path):
    values = {}
    for line in path.read_text().splitlines():
        m = DEFINE_RE.match(line)
        if m:
            values[m.group(1)] = int(m.group(2))
    return values


def parse_client_enum(path):
    """Evaluate the header's enum member(s), symbolic anchors included.

    Handles 'NAME', 'NAME = 0x84C0', 'NAME = OTHER' and
    'NAME = OTHER + 1'. Evaluation repeats until all members resolve,
    so anchors defined before their base (or after) work regardless of
    order.
    """
    text = path.read_text()
    entries = []
    for line in text.splitlines():
        stripped = line.split("/*")[0].strip()
        if not stripped:
            continue
        m = ENTRY_RE.match(stripped)
        if m and re.fullmatch(r"(?:GL|MGL)_[A-Za-z0-9_]*", m.group(1)):
            entries.append((m.group(1), m.group(2)))
    values = {}
    pending = list(entries)
    for _ in range(len(pending) + 2):
        if not pending:
            break
        progress = False
        last_value = None  # value of the immediately preceding entry
        still = []
        for name, expr in pending:
            if name in values:
                last_value = values[name]
                continue
            resolved = False
            if expr is None:
                if last_value is not None:
                    values[name] = last_value + 1
                    resolved = True
            else:
                simple = re.fullmatch(
                    r"(0[xX][0-9a-fA-F]+|\d+)", expr.strip())
                m = re.fullmatch(rf"({IDENT})\s*\+\s*(\d+)", expr.strip())
                if simple:
                    values[name] = int(simple.group(1), 0)
                    resolved = True
                elif m and m.group(1) in values:
                    values[name] = values[m.group(1)] + int(m.group(2))
                    resolved = True
                elif (re.fullmatch(IDENT, expr.strip())
                        and expr.strip() in values):
                    values[name] = values[expr.strip()]
                    resolved = True
            if resolved:
                last_value = values[name]
                progress = True
            else:
                still.append((name, expr))
                last_value = None  # sequential fill cannot cross unknowns
        pending = still
        if not progress or not pending:
            break
    for name, expr in pending:
        raise SystemExit(
            f"{path.name}: cannot evaluate enum expression for {name}: {expr}")
    return values


def check_pair(backend_path, client_path, label):
    backend = parse_backend_defines(backend_path)
    client = parse_client_enum(client_path)
    shared = sorted(set(backend) & set(client))
    mismatches = [
        (name, backend[name], client[name])
        for name in shared
        if backend[name] != client[name]
    ]
    print(f"[{label}] shared={len(shared)} mismatches={len(mismatches)}")
    for name, bval, cval in mismatches:
        print(f"  MISMATCH {name}: backend={bval} client={cval}")
    return mismatches


def main():
    all_mismatches = []
    for backend_path, client_path, label in PAIRS:
        if not backend_path.exists() or not client_path.exists():
            print(f"[{label}] SKIP (missing file)")
            continue
        all_mismatches += check_pair(backend_path, client_path, label)
    if all_mismatches:
        print(f"FAIL: {len(all_mismatches)} enum mismatches")
        return 1
    print("PASS: backend constants match their client headers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
