#!/usr/bin/env python3
"""Check MGLDispatchTable against the upstream minigl.library 29.1 layout.

Usage:
    check_dispatch_layout.py <minigl_dispatch.h> [reference.txt]
    check_dispatch_layout.py --extract <upstream minigl_dispatch.h> > reference.txt

The table is positional, so a field inserted, dropped or retyped anywhere
silently rebinds every later slot. This compares our header field by field
with tools/reference/minigl_dispatch_v29_1.txt, a snapshot of the upstream
29.1 header (libraryroot/include/libraries/minigl_dispatch.h at upstream
d950a3a). The reference is vendored so the check does not need the upstream
tree to exist.

Checked:
  * slots 1..205 match upstream by name and by signature (parameter names
    are ignored; types and order are not);
  * the private PPC extension after slot 205 is exactly the FastPath trio;
  * MINIGL_DISPATCH_ABI_VERSION equals the reference ABI.
Prints the slot count and sizeof(MGLDispatchTable) on success.
"""

import re
import sys

HEADER_BYTES = 20  # abiVersion, structSize, backendFlags, reserved, currentContext
EXTENSION = ["mglBeginFastPath", "mglEndFastPath", "mglGetFastPathCapabilities"]
DEFAULT_REFERENCE = "tools/reference/minigl_dispatch_v29_1.txt"


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def split_top(text):
    parts, depth, cur = [], 0, ""
    for ch in text:
        depth += ch in "(["
        depth -= ch in ")]"
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


def normalize_param(param):
    """Drop the parameter name so only the type remains."""
    param = " ".join(param.replace("*", " * ").split())
    if param in ("void", "..."):
        return param
    # A function-pointer parameter: void (*func)(int x) -> void (*)(int)
    fp = re.match(r"(.*)\(\s*\*\s*\w*\s*\)\s*\((.*)\)$", param)
    if fp:
        inner = ",".join(normalize_param(p) for p in split_top(fp.group(2)))
        return f"{fp.group(1).strip()} (*)({inner})"
    words = param.split()
    if len(words) > 1 and re.match(r"^\w+$", words[-1]) and words[-1] not in (
            "int", "char", "long", "short", "unsigned", "float", "double"):
        words = words[:-1]
    return " ".join(words)


def parse(text):
    text = strip_comments(text)
    body = re.search(r"typedef struct MGLDispatchTable\s*\{(.*?)\}\s*MGLDispatchTable\s*;",
                     text, re.S)
    if not body:
        raise SystemExit("MGLDispatchTable declaration not found")
    fields = []
    for decl in body.group(1).split(";"):
        decl = " ".join(decl.split())
        if not decl or "(" not in decl:
            continue  # the five scalar header fields
        m = re.match(r"(.*?)\(\s*\*\s*(\w+)\s*\)\s*\((.*)\)$", decl)
        if not m:
            raise SystemExit(f"unparsed dispatch field: {decl!r}")
        ret, name, args = m.groups()
        params = ",".join(normalize_param(p) for p in split_top(args))
        fields.append((name, f"{' '.join(ret.replace('*', ' * ').split())}({params})"))
    abi = re.search(r"#define\s+MINIGL_DISPATCH_ABI_VERSION\s+(\d+)", text)
    return fields, int(abi.group(1)) if abi else None


def main(argv):
    if len(argv) == 3 and argv[1] == "--extract":
        with open(argv[2], encoding="latin-1") as f:
            fields, abi = parse(f.read())
        print(f"# MGLDispatchTable reference: upstream minigl.library 29.1, ABI {abi}")
        print(f"abi {abi}")
        for i, (name, sig) in enumerate(fields, 1):
            print(f"{i} {name} {sig}")
        return 0
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    with open(argv[1], encoding="latin-1") as f:
        ours, our_abi = parse(f.read())
    ref, ref_abi = [], None
    with open(argv[2] if len(argv) == 3 else DEFAULT_REFERENCE, encoding="ascii") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            if line.startswith("abi "):
                ref_abi = int(line.split()[1])
                continue
            _, name, sig = line.rstrip("\n").split(" ", 2)
            ref.append((name, sig))

    errors = []
    if our_abi != ref_abi:
        errors.append(f"MINIGL_DISPATCH_ABI_VERSION is {our_abi}, reference is {ref_abi}")
    for i, want in enumerate(ref, 1):
        if i > len(ours):
            errors.append(f"slot {i}: missing, expected {want[0]}")
            break
        have = ours[i - 1]
        if have[0] != want[0]:
            errors.append(f"slot {i}: {have[0]}, expected {want[0]}")
        elif have[1] != want[1]:
            errors.append(f"slot {i} {have[0]}: signature {have[1]}, expected {want[1]}")
    tail = [name for name, _ in ours[len(ref):]]
    if tail != EXTENSION:
        errors.append(f"extension after slot {len(ref)} is {tail}, expected {EXTENSION}")
    if errors:
        for e in errors[:20]:
            print(f"check_dispatch_layout: {e}", file=sys.stderr)
        return 1
    size = HEADER_BYTES + 4 * len(ours)
    print(f"check_dispatch_layout: OK, {len(ours)} slots, ABI {our_abi}, "
          f"sizeof(MGLDispatchTable) = {size:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
