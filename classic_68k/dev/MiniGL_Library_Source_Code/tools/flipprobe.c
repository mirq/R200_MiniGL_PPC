/* ChangeScreenBuffer latency probe: times raw screen-buffer flips on a
 * P96 screen, no GPU, no MiniGL. Answers: is ChangeScreenBuffer synchronous
 * (one vblank per call) or async? */
#include <exec/types.h>
#include <devices/timer.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase;
struct MsgPort *timerPort;
struct timerequest *timerReq;

static ULONG Clock(void)
{
    struct EClockVal v;
    ReadEClock(&v);
    return v.ev_lo;
}

static ULONG Ms100(ULONG ticks, ULONG iters)   /* returns ms*100 */
{
    if (!iters) return 0;
    return (ULONG)(((unsigned long long)ticks * 100000ULL / 709379ULL) / iters);
}

int main(void)
{
    struct Screen *screen;
    struct ScreenBuffer *sb[3];
    ULONG t0, t1, i, nbuf = 0;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) { printf("no libs\n"); return 20; }

    timerPort = CreateMsgPort();
    timerReq = (struct timerequest *)CreateIORequest(timerPort, sizeof(struct timerequest));
    if (!timerReq || OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)timerReq, 0)) {
        printf("no timer\n"); return 20;
    }
    TimerBase = (struct Device *)timerReq->tr_node.io_Device;

    screen = OpenScreenTags(NULL, SA_Width, 800, SA_Height, 600, SA_Depth, 32,
                            SA_Title, (ULONG)"flipprobe", SA_Quiet, TRUE,
                            SA_ShowTitle, FALSE, TAG_DONE);
    if (!screen) { printf("no screen\n"); return 20; }
    printf("screen %dx%dx%d\n", screen->Width, screen->Height,
           screen->RastPort.BitMap->Depth);

    for (i = 0; i < 3; ++i) {
        sb[i] = AllocScreenBuffer(screen, NULL, SB_COPY_BITMAP);
        if (!sb[i]) break;
        nbuf++;
    }
    printf("buffers=%d\n", nbuf);
    if (nbuf < 2) { CloseScreen(screen); return 20; }

    /* warm up one flip */
    ChangeScreenBuffer(screen, sb[0]);
    WaitTOF(); WaitTOF();

    /* raw loop: flip between two buffers as fast as the call allows */
    t0 = Clock();
    for (i = 0; i < 50; ++i)
        ChangeScreenBuffer(screen, sb[i & 1]);
    t1 = Clock();
    printf("raw 2buf flip avg = %ld.%02ld ms/call\n", Ms100(t1 - t0, 50) / 100,
           Ms100(t1 - t0, 50) % 100);

    /* triple-buffer burst */
    if (nbuf == 3) {
        t0 = Clock();
        for (i = 0; i < 60; ++i)
            ChangeScreenBuffer(screen, sb[i % 3]);
        t1 = Clock();
        printf("3buf flip avg = %ld.%02ld ms/call\n", Ms100(t1 - t0, 60) / 100,
               Ms100(t1 - t0, 60) % 100);
    }

    /* flip + explicit TOF: ~one vblank per iteration if the call is async */
    t0 = Clock();
    for (i = 0; i < 20; ++i) {
        ChangeScreenBuffer(screen, sb[i & 1]);
        WaitTOF();
    }
    t1 = Clock();
    printf("flip+WaitTOF avg = %ld.%02ld ms/call\n", Ms100(t1 - t0, 20) / 100,
           Ms100(t1 - t0, 20) % 100);

    for (i = 0; i < 3; ++i)
        if (sb[i]) FreeScreenBuffer(screen, sb[i]);
    CloseScreen(screen);
    CloseDevice((struct IORequest *)timerReq);
    DeleteIORequest((struct IORequest *)timerReq);
    DeleteMsgPort(timerPort);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    printf("probe done\n");
    return 0;
}
