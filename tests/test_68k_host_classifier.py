"""Regression for the Sonnet loader's RACE-in-TRACE false positive.

Runs native CPU tests only. The makefile additionally scans the real linked
Amiga host, because a source-level check cannot constrain compiler constants.
"""
import runpy
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
check = runpy.run_path(str(ROOT / "tools/check_68k_host.py"))
conflicts = check["conflicts"]
SIGNATURES = check["SIGNATURES"]


class HostClassifier(unittest.TestCase):
    def test_embedded_option_triggers_classifier(self):
        self.assertEqual(conflicts(b"MGLPPC_UPLOAD_TRACE\0"), [(b"RACE", 15)])
        for marker in SIGNATURES:
            self.assertEqual(conflicts(b"prefix" + marker), [(marker, 6)])

    def test_construction_template_is_not_a_signature(self):
        self.assertEqual(conflicts(b"MGLPPC_UPLOAD_T?ACE\0"), [])
        self.assertEqual(conflicts(b"ReadUploadTraceOption\0"), [])

    def test_production_helper_preserves_name_and_result(self):
        source = (ROOT / "backend_r200/minigl_ppc_host.c").read_text()
        start = source.index("static LONG ReadUploadTraceOption(")
        end = source.index("\n}\n", start) + 3
        helper = source[start:end]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t UBYTE;
typedef int32_t LONG;
typedef const UBYTE *CONST_STRPTR;
#define GVF_GLOBAL_ONLY 0x100
static LONG nextResult;
static unsigned calls;
static LONG GetVar(CONST_STRPTR name, UBYTE *value, LONG bytes, unsigned flags) {
    assert(!strcmp((const char *)name, "MGLPPC_UPLOAD_TRACE"));
    assert(bytes == 8 && flags == GVF_GLOBAL_ONLY);
    ++calls;
    if (nextResult > 0) value[0] = '1';
    return nextResult;
}
'''
        program += helper
        program += r'''
int main(void) {
    UBYTE value[8] = {0};
    nextResult = -1;
    assert(ReadUploadTraceOption(value, sizeof(value)) == -1 && value[0] == 0);
    nextResult = 1;
    assert(ReadUploadTraceOption(value, sizeof(value)) == 1 && value[0] == '1');
    assert(calls == 2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="host-classifier-", dir="/tmp/opencode") as path:
            path = Path(path)
            (path / "helper.c").write_text(program)
            subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fno-pie", "-no-pie", "-fsanitize=address,undefined",
                            str(path / "helper.c"), "-o", str(path / "helper")], check=True)
            subprocess.run([str(path / "helper")], check=True)


if __name__ == "__main__":
    unittest.main()
