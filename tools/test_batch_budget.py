#!/usr/bin/env python3
"""Sanitized CPU tests of the real ring-size and expansion-cost helpers."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="r200-budget-") as path:
    output = str(pathlib.Path(path) / "check")
    subprocess.run([
        "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-fno-pie", "-no-pie", "-I" + str(root / "backend_r200/library"),
        str(root / "tools/batch_budget_check.c"), "-o", output,
    ], check=True)
    subprocess.run([output], check=True)
