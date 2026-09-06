"""Exercise the production, shutdown-only pacing diagnostic without hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
host = (root / "backend_r200/minigl_ppc_host.c").read_text()
start = host.index("#if MGLPPC_PACING\n/* Full host session")
end = host.index("\n#endif", start) + len("\n#endif")

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
static ULONG TimerClockRate = 1000000, now, clockReads;
static int clockLive = 1;
static ULONG ProbeTicks(void) { assert(clockLive); ++clockReads; return now; }
static ULONG ProbeMicros(ULONG start, ULONG end) {
    return (ULONG)((uint64_t)(ULONG)(end - start) * 1000000 / TimerClockRate);
}
'''
code += host[start:end]
code += r'''
int main(void) {
    HOST_PACING_INIT();
#if MGLPPC_PACING
    static const ULONG gaps[] = {0,999,1000,4999,5000,9999,10000,19999,
                                20000,32999,33000,49999,50000,99999,100000,500000};
    now = UINT32_MAX - 500;
    HOST_PACING_PRESENT();
    for (ULONG i = 0; i < 16; ++i) {
        now += gaps[i];
        HOST_PACING_PRESENT();
    }
    assert(clockReads == 17 && HostPacingPresents == 17);
    for (ULONG i = 0; i < 8; ++i) assert(HostPacingBins[i] == 2);
    for (ULONG i = 1; i < 16; ++i)
        assert(HostPacingGaps[i - 1].ticks >= HostPacingGaps[i].ticks);
    assert(HostPacingGaps[0].frame == 17 && HostPacingGaps[0].ticks == 500000);
    HOST_PACING_PRESENT(); /* zero-length tie retains the older zero interval */
    assert(HostPacingGaps[15].frame == 2);
    now += 500000;
    HOST_PACING_PRESENT();
    assert(HostPacingGaps[0].frame == 17 && HostPacingGaps[1].frame == 19);
    clockLive = 0;
    HOST_PACING_REPORT(); /* reporting must not sample a closed timer */
    assert(clockReads == 19);
    TimerClockRate = 709379;
    HOST_PACING_INIT();
    for (ULONG i = 0; i < 7; ++i) {
        uint64_t scaled = (uint64_t)TimerClockRate * HostPacingLimitsMs[i];
        assert((uint64_t)HostPacingLimits[i] * 1000 >= scaled);
        assert((uint64_t)(HostPacingLimits[i] - 1) * 1000 < scaled);
    }
#else
    clockLive = 0;
    HOST_PACING_PRESENT();
    HOST_PACING_REPORT();
    assert(clockReads == 0);
#endif
    puts("Host pacing: bins, boundaries, wrap, ranking, no-clock teardown and disabled path passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="r200-pacing-", dir="/tmp/opencode") as directory:
    source = Path(directory) / "test.c"
    source.write_text(code)
    for enabled in (0, 1):
        binary = Path(directory) / f"test{enabled}"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-fsanitize=address,undefined",
                        "-fno-pie", "-no-pie", f"-DMGLPPC_PACING={enabled}",
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
