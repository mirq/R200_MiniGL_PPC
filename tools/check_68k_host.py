#!/usr/bin/env python3
"""Reject signatures that falsely classify the 68k host as PPC under Sonnet.

The legacy Sonnet LoadSeg patch scans raw segment bytes, including strings.
Use a conservative whole-file scan so new diagnostics cannot silently put the
68k helper on the PPC allocation path. This does not inspect/reset the target
file's cached classification bits; deployment must handle those separately.
"""
import argparse
from pathlib import Path


SIGNATURES = (b"RACE", b"powerpc.library", b"AmigaAMP")


def conflicts(data):
    return [(marker, data.find(marker)) for marker in SIGNATURES if marker in data]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    found = conflicts(args.binary.read_bytes())
    if found:
        for marker, offset in found:
            print(f"{args.binary}: Sonnet PPC classifier signature {marker!r} at 0x{offset:x}")
        raise SystemExit(1)
    print(f"{args.binary}: 68k host classifier check passed")


if __name__ == "__main__":
    main()
