#!/usr/bin/env python3
"""Build narrowly scoped HELLO diagnostics; never deploy or launch them.

These ABI-4 hosts are for the recovered static protocol-14 client only.
Keep the source transformation and artifact identity reproducible across
sessions instead of relying on temporary, hand-edited host copies.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zlib


ROOT = Path(__file__).resolve().parents[2]
BASELINE_SHA256 = "0f5d56715d4167b48af8cfa3eb42b7ca1f135e2c9ba05c12f5ad6dd915fdc872"


def hello(source):
    start = source.index("    case MGLPPC_CMD_HELLO:")
    end = source.index("    case MGLPPC_CMD_SHUTDOWN:", start)
    return start, end


def caps(source):
    start, end = hello(source)
    start = source.index("        message->value[3] = MGLPPC_CAP_REPLAY_V9", start, end)
    end = source.index("        break;", start, end)
    return start, end


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("variant", choices=("K", "L", "M", "N", "O", "P", "S"))
    parser.add_argument("--legacy", type=Path, default=Path("/home/mirek/minigl_ppc"))
    parser.add_argument("--source", type=Path,
                        help="Pre-fix host source snapshot; required after the production host changes")
    parser.add_argument("--out", type=Path, default=Path("/tmp/opencode/hello-probes"))
    args = parser.parse_args()
    current_path = args.source or ROOT / "backend_r200/minigl_ppc_host.c"
    old_path = args.legacy / "examples/minigl_ppc_host.c"
    current = current_path.read_text()
    if hashlib.sha256(current.encode()).hexdigest() != BASELINE_SHA256:
        parser.error(f"This historical probe needs pre-fix source SHA256 {BASELINE_SHA256}; use --source")
    old = old_path.read_text()
    block = ""
    if args.variant == "K":
        start, end = caps(current)
        old_start, old_end = caps(old)
        replacement = old[old_start:old_end]
        description = "Only HELLO capability assignment uses the old mask."
    else:
        first, last = hello(current)
        start = current.index("        {\n        UBYTE nodirect[8]", first, last)
        end = caps(current)[0]
        block = current[start:end]
        assert block.count("{") == block.count("}") == 2
        assert block.count("GetVar(") == 4
        replacement = "" if args.variant == "L" else block.replace("GetVar(", "ProbeEnv(")
        description = ("Only HELLO environment-read block omitted; flags remain zero-initialized."
                       if args.variant == "L" else
                       "Trace four HELLO GetVar results and cache flags before context creation.")
        if args.variant in ("N", "P", "S"):
            replacement = block.replace("UBYTE nodirect[8], trace[8], nomailbox[8];",
                "static UBYTE nodirect[8], trace[8], nomailbox[8];")
            replacement = replacement.replace("UBYTE ibdry[8];", "static UBYTE ibdry[8];")
            description = "Only move HELLO GetVar buffers off the stack; all reads and flag logic retained."
            if args.variant == "P":
                replacement = replacement.replace("GVF_GLOBAL_ONLY", "GVF_LOCAL_ONLY")
                description = "Static-buffer N control with only four HELLO GetVar flags changed to LOCAL_ONLY."
        if args.variant == "O":
            replacement = ""
            description = "Move the unchanged environment reads to host startup, before exposing the port."
    modified = current[:start] + replacement + current[end:]
    assert modified[:start] == current[:start]
    assert modified[start + len(replacement):] == current[end:]
    for signature in ("static ULONG RingPoll(", "static BOOL RegisterSemanticRing(",
                      "static void PollQueuedFence(", "static BOOL DiscardSemantic("):
        assert modified.count(signature) == current.count(signature) == 1
    if args.variant == "O":
        marker = "    state.running = 1;\n"
        assert modified.count(marker) == 1
        modified = modified.replace(marker, marker + block.replace("state->", "state."), 1)
    if args.variant == "M":
        helper = r'''
static LONG ProbeEnv(CONST_STRPTR name, UBYTE *buffer, LONG bytes, ULONG flags)
{
    struct Task *task = FindTask(NULL);
    LONG priorityBefore = task->tc_Node.ln_Pri;
    ULONG flagsBefore = task->tc_Flags;
    ULONG signalsBefore = task->tc_SigRecvd;
    ULONG before = CacheControl(0, 0), after;
    UBYTE first = buffer[0];
    LONG result = GetVar(name, buffer, bytes, flags);
    LONG error = IoErr();
    after = CacheControl(0, 0);
    printf("HELLO_ENV name=%s result=%ld ioerr=%ld first=%02lx->%02lx cache=%08lx->%08lx priority=%ld->%ld flags=%02lx->%02lx signals=%08lx->%08lx\n",
           (const char *)name, (long)result, (long)error,
           (unsigned long)first, (unsigned long)buffer[0],
           (unsigned long)before, (unsigned long)after,
           (long)priorityBefore, (long)task->tc_Node.ln_Pri,
           (unsigned long)flagsBefore, (unsigned long)task->tc_Flags,
           (unsigned long)signalsBefore, (unsigned long)task->tc_SigRecvd);
    return result;
}

'''
        modified = modified.replace("static void HandleMessage(", helper + "static void HandleMessage(", 1)
    args.out.mkdir(parents=True, exist_ok=True)
    name = {"K": "host-K-oldcaps", "L": "host-L-noenv", "M": "host-M-envtrace",
            "N": "host-N-static", "O": "host-O-startup", "P": "host-P-local",
            "S": "host-S-nocall"}[args.variant]
    source = args.out / (name + ".c")
    binary = args.out / name
    source.write_text(modified)
    command = [
        "/opt/amiga/bin/m68k-amigaos-gcc", "-std=gnu99", "-O2", "-mcpu=68020",
        "-noixemul", "-Wall", "-Werror", "-DMGLPPC_PROFILE=1", "-DMGLPPC_PACING=0",
        "-DMGLPPC_IDLE_DIAG=0", "-DMGLPPC_HOST_TRACE=0", "-DMGLPPC_HOST_TARGET_CACHE=1",
        "-I" + str(ROOT / "backend_r200/include"),
        "-I" + str(args.legacy / "include"),
        "-I" + str(args.legacy / "third_party/warp3d/include"),
        "-I" + str(ROOT / "backend_r200/third_party/radeon3d/include"),
        str(source), str(args.legacy / "lib/libminigl_dispatch.a"), "-o", str(binary),
    ]
    subprocess.run(command, check=True)
    symbols = subprocess.check_output(["/opt/amiga/bin/m68k-amigaos-nm", "-u", str(binary)], text=True)
    assert not symbols.strip(), symbols
    data = binary.read_bytes()
    if args.variant == "S":
        assert zlib.crc32(data) == 0x44D885CA, "N binary must match before instruction substitution"
        image = bytearray(data)
        for pc in (0x4ae4, 0x4b04, 0x4b24, 0x4b4a):
            offset = pc + 0x28
            assert image[offset:offset + 4] == bytes.fromhex("4eaefc76")
            image[offset:offset + 4] = bytes.fromhex("70ff4e71")
        data = bytes(image)
        binary.write_bytes(data)
        description = "Layout-preserving N diagnostic: four GetVar LVO calls replaced with return-minus-one/NOP."
    if args.variant == "P":
        reference = (args.out / "host-N-static").read_bytes()
        assert zlib.crc32(reference) == 0x44D885CA
        changed = [i for i, (a, b) in enumerate(zip(reference, data)) if a != b]
        assert len(reference) == len(data) and changed == [0x4b0a], changed
        assert reference[0x4b0a] == 1 and data[0x4b0a] == 2
    manifest = {
        "variant": args.variant, "scope": description,
        "restriction": "Diagnostic ABI4 / old static v14 client; not a universal host",
        "source_sha256": hashlib.sha256(current.encode()).hexdigest(),
        "legacy_source_sha256": hashlib.sha256(old.encode()).hexdigest(),
        "command": command, "binary": str(binary), "bytes": len(data),
        "crc32": f"{zlib.crc32(data):08X}",
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    (args.out / (name + ".json")).write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
