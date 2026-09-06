"""Compile the production startup parser against a bounded GetVar stub."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
host = (root / "backend_r200/minigl_ppc_host.c").read_text()
start = host.index("static BOOL ReadHostPriority(")
parser = host[start:host.index("\nint main(", start)]
main = host[host.index("\nint main("):]
assert main.index("ReadHostPriority(") < main.index("MiniGLOpen()")
assert "SetTaskPri(FindTask(NULL), hostPriority)" in main

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef unsigned char UBYTE;
typedef const char *CONST_STRPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define GVF_GLOBAL_ONLY 0x100
static const char *environment;
static LONG GetVar(CONST_STRPTR name, UBYTE *buffer, LONG capacity, LONG flags) {
    assert(!strcmp(name, "MGLPPC_HOSTPRI") && flags == GVF_GLOBAL_ONLY);
    if (!environment) return -1;
    size_t length = strlen(environment);
    memcpy(buffer, environment, length < (size_t)capacity ? length : (size_t)capacity);
    /* Exercise the conservative copied-length convention, including NUL. */
    return (LONG)(length < (size_t)capacity ? length : (size_t)capacity - 1);
}
'''
code += parser
code += r'''
int main(void) {
    static const struct { const char *text; BOOL valid, specified; LONG priority; } cases[] = {
        {NULL, 1, 0, 123}, {"", 1, 0, 123},
        {"0", 1, 1, 0}, {"-1", 1, 1, -1}, {"-5", 1, 1, -5},
        {"-20", 1, 1, -20}, {"2", 1, 1, 2}, {"+2", 1, 1, 2},
        {"  -5", 1, 1, -5},
        {"-21", 0, 0, 123}, {"3", 0, 0, 123}, {"5", 0, 0, 123},
        {"20", 0, 0, 123}, {"45", 0, 0, 123}, {"53", 0, 0, 123},
        {"-", 0, 0, 123}, {"+", 0, 0, 123}, {" ", 0, 0, 123},
        {"foo", 0, 0, 123}, {"-5x", 0, 0, 123}, {"1.0", 0, 0, 123},
        {"2147483648", 0, 0, 123}, {"-4294967297", 0, 0, 123},
        {"00000000000002", 1, 1, 2},
        {"000000000000002", 0, 0, 123},
        {"0000000000000002", 0, 0, 123},
        {"9999999999999999999999999999999999", 0, 0, 123}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        LONG priority = 123;
        BOOL specified = TRUE;
        environment = cases[i].text;
        assert(ReadHostPriority(&priority, &specified) == cases[i].valid);
        assert(priority == cases[i].priority && specified == cases[i].specified);
    }
    puts("Host priority: 27 bounded parser cases passed; invalid settings fail before backend open");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="r200-priority-", dir="/tmp/opencode") as directory:
    source = Path(directory) / "test.c"
    binary = Path(directory) / "test"
    source.write_text(code)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
