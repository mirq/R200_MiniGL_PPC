/* Minimal WarpOS MiniGL dispatch client: pre-main/main-entry bisect probe.
 * Links exactly like texfmt_probe (same archive, same DLL override). */
#include <proto/minigl.h>
#include <stdio.h>

static void LogLine(const char *s)
{
    FILE *f = fopen("DH2:wosbuild/texfmt_diag.log", "a");
    if (f) {
        fprintf(f, "%s\n", s);
        fclose(f);
    }
}

int main(void)
{
    LogLine("main-enter");
    printf("MINITEST begin\n");
    if (MiniGLOpen() == 0) {
        LogLine("open-fail");
        printf("MINITEST open-failed\n");
        return 20;
    }
    LogLine("open-ok");
    printf("MINITEST opened\n");
    MiniGLClose();
    LogLine("closed");
    printf("MINITEST done\n");
    return 0;
}
