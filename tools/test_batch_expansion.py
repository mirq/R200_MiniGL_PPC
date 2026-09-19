#!/usr/bin/env python3
"""Compare actual driver CP counts with the frontend expansion budget."""
import argparse
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--emitter-root", type=pathlib.Path,
                    default=root.parent / "p96-driver")
args = parser.parse_args()
driver = args.emitter_root.resolve()

with tempfile.TemporaryDirectory(prefix="r200-expansion-") as directory:
    path = pathlib.Path(directory)
    (path / "exec").mkdir()
    (path / "exec/types.h").write_text(
        "#include <stdint.h>\n"
        "typedef uint32_t ULONG; typedef int32_t LONG; typedef void *APTR;\n"
        "typedef uint16_t UWORD; typedef int16_t WORD; typedef int16_t BOOL;\n"
        "typedef uint8_t UBYTE; typedef int8_t BYTE;\n"
        "#define TRUE 1\n#define FALSE 0\n")
    abi, count = re.subn(r"typedef char Radeon3D\w+SizeCheck\[[^\]]*\];", "",
                         (driver / "include/radeon3d.h").read_text())
    if count != 8:
        raise SystemExit("review changed target ABI size checks")
    (path / "radeon3d.h").write_text(abi)
    (path / "radeon3d_emit.h").write_text(
        (driver / "include/radeon3d_emit.h").read_text().replace(
            '"../src/radeon_regs.h"', '"radeon_regs.h"'))
    source = (root / "backend_r200/library/r200_submit.c").read_text()
    start = source.index("ULONG GeneratedHardwareVertexDwords(")
    end = source.index("ULONG GeneratedClearDwords(", start)
    (path / "generated_cost.inc").write_text(source[start:end])
    budget = (root / "backend_r200/library/r200_batch_budget.h").read_text()
    if budget.count("if (!lighting) {") != 1:
        raise SystemExit("review changed cached-expansion branch")
    old_budget = budget.replace("if (!lighting) {", "if (0 && !lighting) {", 1)
    for name, header, expected in (("cached", budget, 0), ("full-state", old_budget, 1)):
        (path / "r200_batch_budget.h").write_text(header)
        binary = path / name
        subprocess.run([
            "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-Wno-parentheses", "-Wno-sign-compare", "-fno-pie", "-no-pie",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I" + str(path), "-I" + str(root / "backend_r200/library"),
            "-I" + str(driver / "include"), "-I" + str(driver / "src"),
            "-I" + str(driver / "tools"),
            str(driver / "src/radeon3d_emit.c"),
            str(root / "tools/batch_expansion_check.c"), "-o", str(binary),
        ], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(name, result.stdout.strip() if name == "cached" else
              result.stdout.strip().splitlines()[-1:])
        if result.stderr or result.returncode != expected:
            raise SystemExit(result.stdout + result.stderr + "unexpected expansion result")
