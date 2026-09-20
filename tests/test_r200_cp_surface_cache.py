"""CP surface-descriptor cache lifetime regression (plan 2026-09-20 §7 P0).

python3 tests/test_r200_cp_surface_cache.py -v

The real cache helpers and ResolveCpSurface are extracted from the production
frontend and compiled on the host with ASan/UBSan. Only PlatformCall (the
mailbox transport) is stubbed: it serves descriptors from a small token map so
the test can simulate a surface token being released and reallocated for a
different surface within one device epoch - the reuse hazard that made the
previous lifetime relaxation unsafe.

Scenarios:
- hit / miss accounting;
- same-epoch token reuse: without a drop the cache returns the stale
  descriptor (documents the hazard), with FrontendDropCachedSurface it
  returns the fresh one;
- lifecycle clear forces re-query;
- capacity overwrites the oldest entry and stays within MGLPPC_CP_SURFACE_SLOTS;
- FrontendDropCachedTextureSurface drops by texture mirror handle and ignores
  out-of-range names;
- source-level coverage: every texture-mutating command drops before send.

No production header or driver is modified; generated files stay in
/tmp/opencode and are removed.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
FRONTEND = ROOT / "backend_r200/library/minigl_r200_warpos_compile_probe.c"
INCLUDE = ROOT / "backend_r200/include"

SLOTS_RE = r"^#define\s+MGLPPC_CP_SURFACE_SLOTS\s+(\d+)UL\s*$"


def function(text, signature):
    """Return the full function whose text starts at signature (brace balanced)."""
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


def load():
    source = FRONTEND.read_text()
    match = re.search(SLOTS_RE, source, re.M)
    if not match:
        raise AssertionError("MGLPPC_CP_SURFACE_SLOTS define not found")
    slots = int(match.group(1))
    start = source.index("static ULONG CpSurfHits")
    end = source.index("static BOOL PlatformCall(ULONG command, ULONG contextId)")
    helpers = source[start:end]
    # W-10 added a timing wrapper around the resolver; extract both so the
    # wrapper's PerfClock/CpPhaseResolveTicks usage is compiled for real.
    resolve = (function(source, "static BOOL ResolveCpSurfaceInner(") + "\n" +
               function(source, "static BOOL ResolveCpSurface("))
    return slots, helpers, resolve, source


MOCKS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int BOOL;
typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define GL_NO_ERROR 0
/* Real command numbers from minigl_ppc_protocol.h; the drop switch is
 * extracted verbatim and must not be tested with renumbered constants. */
#define MGLPPC_CMD_SHUTDOWN 2UL
#define MGLPPC_CMD_DESTROY_CONTEXT 5UL
#define MGLPPC_CMD_DELETE_TEXTURES 8UL
#define MGLPPC_CMD_TEXTURE_UPLOAD_BEGIN 9UL
#define MGLPPC_CMD_TEXTURE_UPLOAD_END 11UL
#define MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS 19UL
#define MGLPPC_CMD_TEXTURE_UPLOAD_SHARED 22UL
#define MGLPPC_CMD_TEXTURE_DIRECT_BEGIN 23UL
#define MGLPPC_CMD_TEXTURE_DIRECT_COMMIT 24UL
#define MGLPPC_CMD_TEXTURE_DIRECT_ABORT 25UL
#define MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED 26UL
#define MGLPPC_CMD_CP_SURFACE 28UL
#define MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2 29UL
#define MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2 30UL
#define GL_TEXTURE_2D 174UL
#define MAX_TEXTURES 2048UL
#define MGLPPC_CP_SURFACE_SLOTS %(slots)dUL
#define CP_SLOTS MGLPPC_CP_SURFACE_SLOTS

struct MGLPPCEmitSurface {
    ULONG cpuAddress, gpuAddress, pitch, width, height, format;
};

struct CacheEntry {
    ULONG token, epoch;
    struct MGLPPCEmitSurface surface;
};

struct Control {
    ULONG command, contextId, value[8];
    LONG status;
};

static struct {
    BOOL cpEnabled, cpFailed;
    ULONG contextId, cpSurfaceCount;
    struct Control control;
    struct CacheEntry cpSurfaces[CP_SLOTS];
} FrontendPlatform;

struct TestTexture { struct { APTR Handle; } surface; };
static struct {
    ULONG deviceEpoch;
    ULONG activeTexture, boundTexture[2];
    struct TestTexture textures[MAX_TEXTURES];
} proofContext;

struct MiniGLR200Context { ULONG deviceEpoch; };
static struct MiniGLR200Context testContext;

/* Defined by the extracted production source below. */
static BOOL ResolveCpSurface(void *user, ULONG token,
                            struct MGLPPCEmitSurface *out);

/* Stand-in for the PPC timebase used by the W-10 resolve wrapper. */
static ULONG perfClockNow;
static ULONG PerfClock(void) { return ++perfClockNow; }

/* Token map: enough entries for every capacity scenario token. */
#define MAP_MAX (CP_SLOTS + 16)
static ULONG mapToken[MAP_MAX], mapAddress[MAP_MAX];
static ULONG mapCount, platformCalls;

static void mapSet(ULONG token, ULONG address) {
    ULONG i;
    for (i = 0; i < mapCount; ++i)
        if (mapToken[i] == token) { mapAddress[i] = address; return; }
    assert(mapCount < MAP_MAX);
    mapToken[mapCount] = token;
    mapAddress[mapCount] = address;
    ++mapCount;
}

static ULONG mapGet(ULONG token) {
    ULONG i;
    for (i = 0; i < mapCount; ++i)
        if (mapToken[i] == token) return mapAddress[i];
    assert(!"unknown token queried");
    return 0;
}

/* Stands in for the mailbox transport: answers MGLPPC_CMD_CP_SURFACE from
 * the token map, exactly like the host reply layout. */
static BOOL PlatformCall(ULONG command, ULONG contextId) {
    ULONG token;
    assert(command == MGLPPC_CMD_CP_SURFACE);
    assert(contextId == FrontendPlatform.contextId);
    ++platformCalls;
    token = FrontendPlatform.control.value[0];
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = GL_NO_ERROR;
    FrontendPlatform.control.value[1] = 0x10000000UL + mapGet(token);
    FrontendPlatform.control.value[2] = 0x800000UL + mapGet(token);
    FrontendPlatform.control.value[3] = 2560;
    FrontendPlatform.control.value[4] = 640;
    FrontendPlatform.control.value[5] = 480;
    FrontendPlatform.control.value[6] = 3;
    FrontendPlatform.control.value[7] = proofContext.deviceEpoch;
    return TRUE;
}

static ULONG resolveAddr(ULONG token) {
    struct MGLPPCEmitSurface surface;
    memset(&surface, 0, sizeof(surface));
    assert(ResolveCpSurface(&testContext, token, &surface));
    return surface.cpuAddress;
}
'''


def build_module(slots, helpers, resolve):
    return (MOCKS % {"slots": slots}) + "\n" + helpers + "\n" + resolve + "\n"


CHECKS = r'''
static void reset(void) {
    memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
    memset(&proofContext, 0, sizeof(proofContext));
    memset(&testContext, 0, sizeof(testContext));
    memset(mapToken, 0, sizeof(mapToken));
    memset(mapAddress, 0, sizeof(mapAddress));
    mapCount = platformCalls = 0;
    FrontendPlatform.cpEnabled = TRUE;
    FrontendPlatform.contextId = 42;
    proofContext.deviceEpoch = 7;
    testContext.deviceEpoch = 7;
}

/* 1. Hit accounting and the reuse hazard itself. */
static void reuse(void) {
    ULONG dropped;
    reset();
    dropped = CpSurfDropped;
    mapSet(0x1000, 0x111);
    assert(resolveAddr(0x1000) == 0x10000000UL + 0x111);
    assert(resolveAddr(0x1000) == 0x10000000UL + 0x111);
    assert(platformCalls == 1);              /* second resolve was a hit */
    /* Same epoch, same token, different surface: without a drop the cache
     * must (and does) return the stale descriptor. This is why W-01 drops
     * before every mutating command. */
    mapSet(0x1000, 0x222);
    assert(resolveAddr(0x1000) == 0x10000000UL + 0x111);
    assert(platformCalls == 1);
    /* After the drop the reused token resolves to the fresh descriptor. */
    FrontendDropCachedSurface(0x1000);
    assert(CpSurfDropped == dropped + 1);
    assert(resolveAddr(0x1000) == 0x10000000UL + 0x222);
    assert(platformCalls == 2);
}

/* 2. Lifecycle clear (UNREGISTER/DESTROY/SHUTDOWN) forces re-query. */
static void lifecycle(void) {
    reset();
    mapSet(0x2000, 0x333);
    assert(resolveAddr(0x2000) == 0x10000000UL + 0x333);
    assert(platformCalls == 1);
    FrontendPlatform.cpSurfaceCount = 0;     /* PlatformCall lifecycle branch */
    mapSet(0x2000, 0x444);
    assert(resolveAddr(0x2000) == 0x10000000UL + 0x444);
    assert(platformCalls == 2);
}

/* 3. Capacity: the ring overwrites the oldest entry, keeps the newest and
 *    never exceeds the configured slot count. */
static void capacity(void) {
    ULONG i, resets;
    reset();
    resets = CpSurfCapResets;
    for (i = 0; i < CP_SLOTS; ++i) {
        mapSet(0x3000 + i, i + 1);
        assert(resolveAddr(0x3000 + i) == 0x10000000UL + i + 1);
    }
    assert(FrontendPlatform.cpSurfaceCount == CP_SLOTS);
    assert(CpSurfCapResets == resets);
    mapSet(0x4000, 0x555);
    assert(resolveAddr(0x4000) == 0x10000000UL + 0x555);
    assert(FrontendPlatform.cpSurfaceCount == CP_SLOTS);
    assert(CpSurfCapResets == resets + 1);
    /* Entry 0 (token 0x3000) was overwritten, so it must re-query now. */
    mapSet(0x3000, 0x666);
    assert(resolveAddr(0x3000) == 0x10000000UL + 0x666);
    /* A recently cached token is still a hit. */
    assert(resolveAddr(0x3000) == 0x10000000UL + 0x666);
}

/* 4. Texture-mirror drop helper, including out-of-range names. */
static void textures(void) {
    ULONG dropped;
    reset();
    dropped = CpSurfDropped;
    proofContext.textures[7].surface.Handle = (APTR)0x5000UL;
    mapSet(0x5000, 0x777);
    assert(resolveAddr(0x5000) == 0x10000000UL + 0x777);
    assert(CpSurfDropped == dropped);
    FrontendDropCachedTextureSurface(7);
    assert(CpSurfDropped == dropped + 1);
    mapSet(0x5000, 0x888);
    assert(resolveAddr(0x5000) == 0x10000000UL + 0x888);
    /* Unknown names are ignored; a NULL mirror drops nothing. */
    FrontendDropCachedTextureSurface(0);
    FrontendDropCachedTextureSurface(MAX_TEXTURES);
    assert(CpSurfDropped == dropped + 1);
    proofContext.textures[8].surface.Handle = NULL;
    FrontendDropCachedTextureSurface(8);
    assert(CpSurfDropped == dropped + 1);
    /* Epoch mismatch is a miss (stale counter), then re-queries; the stale
     * slot is replaced in place, so the next resolve is a hit again. */
    proofContext.deviceEpoch = 8;
    testContext.deviceEpoch = 8;
    mapSet(0x5000, 0x999);
    assert(resolveAddr(0x5000) == 0x10000000UL + 0x999);
    assert(CpSurfEpochStale == 1);
    assert(resolveAddr(0x5000) == 0x10000000UL + 0x999);
}

/* 6. The real pre-command drop switch: value layouts must select the right
 *    texture. UPLOAD_SHARED carries the target enum (174) in value[0], not a
 *    texture name - reading it as a name would drop the wrong texture and
 *    leave the upload target exposed to handle reuse. */
static void command_drops(void) {
    ULONG dropped, invalidations;

    /* UPLOAD_SHARED: drop the bound texture of the active unit, never the
     * value[0] target enum. */
    reset();
    dropped = CpSurfDropped;
    proofContext.activeTexture = 0;
    proofContext.boundTexture[0] = 7;
    proofContext.textures[7].surface.Handle = (APTR)0x7000UL;
    proofContext.textures[174].surface.Handle = (APTR)0x7f00UL;
    mapSet(0x7000, 0x701);
    mapSet(0x7f00, 0x7f1);
    assert(resolveAddr(0x7000) == 0x10000000UL + 0x701);
    assert(resolveAddr(0x7f00) == 0x10000000UL + 0x7f1);
    FrontendPlatform.control.value[0] = GL_TEXTURE_2D;
    FrontendDropCachedSurfacesForCommand(MGLPPC_CMD_TEXTURE_UPLOAD_SHARED);
    assert(CpSurfDropped == dropped + 1);
    assert(resolveAddr(0x7000) == 0x10000000UL + 0x701);   /* re-query */
    assert(resolveAddr(0x7f00) == 0x10000000UL + 0x7f1);   /* still cached */
    assert(platformCalls == 3);

    /* SUBIMAGE_SHARED carries the texture name in value[0]. */
    reset();
    dropped = CpSurfDropped;
    proofContext.textures[8].surface.Handle = (APTR)0x8000UL;
    mapSet(0x8000, 0x801);
    assert(resolveAddr(0x8000) == 0x10000000UL + 0x801);
    FrontendPlatform.control.value[0] = 8;
    FrontendDropCachedSurfacesForCommand(MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED);
    assert(CpSurfDropped == dropped + 1);

    /* DELETE_TEXTURES drops every name in the chunk. */
    reset();
    dropped = CpSurfDropped;
    proofContext.textures[8].surface.Handle = (APTR)0x8000UL;
    proofContext.textures[9].surface.Handle = (APTR)0x9000UL;
    mapSet(0x8000, 0x801);
    mapSet(0x9000, 0x901);
    assert(resolveAddr(0x8000) == 0x10000000UL + 0x801);
    assert(resolveAddr(0x9000) == 0x10000000UL + 0x901);
    FrontendPlatform.control.value[0] = 2;
    FrontendPlatform.control.value[1] = 8;
    FrontendPlatform.control.value[2] = 9;
    FrontendDropCachedSurfacesForCommand(MGLPPC_CMD_DELETE_TEXTURES);
    assert(CpSurfDropped == dropped + 2);

    /* Two-tile direct commands use their per-tile name slots. */
    reset();
    dropped = CpSurfDropped;
    proofContext.textures[10].surface.Handle = (APTR)0xa000UL;
    proofContext.textures[11].surface.Handle = (APTR)0xb000UL;
    mapSet(0xa000, 0xa01);
    mapSet(0xb000, 0xb01);
    assert(resolveAddr(0xa000) == 0x10000000UL + 0xa01);
    assert(resolveAddr(0xb000) == 0x10000000UL + 0xb01);
    FrontendPlatform.control.value[0] = 10;
    FrontendPlatform.control.value[4] = 11;
    FrontendDropCachedSurfacesForCommand(MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2);
    assert(CpSurfDropped == dropped + 2);
    proofContext.textures[12].surface.Handle = (APTR)0xc000UL;
    proofContext.textures[13].surface.Handle = (APTR)0xd000UL;
    mapSet(0xc000, 0xc01);
    mapSet(0xd000, 0xd01);
    assert(resolveAddr(0xc000) == 0x10000000UL + 0xc01);
    assert(resolveAddr(0xd000) == 0x10000000UL + 0xd01);
    FrontendPlatform.control.value[0] = 12;
    FrontendPlatform.control.value[2] = 13;
    FrontendDropCachedSurfacesForCommand(MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2);
    assert(CpSurfDropped == dropped + 4);

    /* Lifecycle commands clear everything and count the invalidation. */
    reset();
    invalidations = CpSurfInvalidate;
    mapSet(0xe000, 0xe01);
    assert(resolveAddr(0xe000) == 0x10000000UL + 0xe01);
    assert(FrontendPlatform.cpSurfaceCount == 1);
    FrontendDropCachedSurfacesForCommand(MGLPPC_CMD_DESTROY_CONTEXT);
    assert(FrontendPlatform.cpSurfaceCount == 0);
    assert(CpSurfInvalidate == invalidations + 1);
}

/* 5. Legacy chunked uploads carry no texture name: the helper must drop the
 *    currently bound texture of the active unit. */
static void active_texture(void) {
    ULONG dropped;
    reset();
    dropped = CpSurfDropped;
    proofContext.activeTexture = 1;
    proofContext.boundTexture[1] = 9;
    proofContext.textures[9].surface.Handle = (APTR)0x6000UL;
    mapSet(0x6000, 0xaaa);
    assert(resolveAddr(0x6000) == 0x10000000UL + 0xaaa);
    assert(CpSurfDropped == dropped);
    FrontendDropCachedActiveTextureSurface();
    assert(CpSurfDropped == dropped + 1);
    mapSet(0x6000, 0xbbb);
    assert(resolveAddr(0x6000) == 0x10000000UL + 0xbbb);
    /* An out-of-range unit falls back to unit 0; a NULL bound name drops
     * nothing. */
    proofContext.activeTexture = 7;
    proofContext.boundTexture[0] = 0;
    FrontendDropCachedActiveTextureSurface();
    assert(CpSurfDropped == dropped + 1);
}

int main(void) {
    /* Silence the attribution counters this harness extracts but does not
     * exercise (their users live in PublishCpSlot / the teardown report /
     * the glHint reset, none of which are compiled here). Keep this list
     * in sync with the frontend's counter block: a stale name fails the
     * compile below, which is how the removed W-11 counters were caught. */
    (void)CpSurfInvalidate;  /* only written by the (stubbed) lifecycle branch */
    (void)CpPhasePublishes; (void)CpPhaseWaitTicks; (void)CpPhaseEmitTicks;
    (void)CpPhaseCopyTicks; (void)CpPhasePublishTicks;
    (void)CpWaitHostTicks; (void)CpWaitFenceTicks; (void)CpWaitOtherTicks;
    (void)CpWaitMaxBacklog; (void)CpWaitMaxPending;
    reuse();
    lifecycle();
    capacity();
    textures();
    active_texture();
    command_drops();
    printf("PASS surface-cache\n");
    return 0;
}
'''


class SurfaceCache(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        slots, helpers, resolve, source = load()
        cls.source = source
        temporary = tempfile.TemporaryDirectory(prefix="r200-cp-surface-",
                                                dir="/tmp/opencode")
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        source_file = cls.directory / "test.c"
        source_file.write_text(build_module(slots, helpers, resolve) + CHECKS)
        cls.binary = cls.directory / "test"
        result = subprocess.run(
            # Host is 64-bit: the production code casts APTR handles to ULONG
            # for 32-bit Amiga targets, which is correct there and too narrow
            # here. Keep every other warning fatal.
            ["cc", "-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-Wno-error=pointer-to-int-cast", "-Wno-pointer-to-int-cast",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             str(source_file), "-o", str(cls.binary)],
            capture_output=True, text=True, timeout=60, cwd=cls.directory)
        if result.returncode or result.stderr:
            raise AssertionError(f"compile failed:\n{result.stdout}{result.stderr}")

    def test_reuse_lifecycle_capacity_and_texture_drops(self):
        result = subprocess.run([str(self.binary)], capture_output=True,
            text=True, timeout=30, cwd=self.directory,
            env={**os.environ,
                 "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                 "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout, "PASS surface-cache\n")

    def test_every_texture_mutating_command_drops_before_send(self):
        """Source guard: the PlatformCall switch must drop for every command
        that can release or replace a native texture surface, with the
        two-tile and delete value indices matching the protocol layout."""
        switch = self.source[
            self.source.index("switch (command) {"):
            self.source.index("default:", self.source.index("switch (command) {"))]
        for command in ("MGLPPC_CMD_TEXTURE_UPLOAD_SHARED",
                        "MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED",
                        "MGLPPC_CMD_TEXTURE_DIRECT_BEGIN",
                        "MGLPPC_CMD_TEXTURE_DIRECT_COMMIT",
                        "MGLPPC_CMD_TEXTURE_DIRECT_ABORT",
                        "MGLPPC_CMD_TEXTURE_UPLOAD_BEGIN",
                        "MGLPPC_CMD_TEXTURE_UPLOAD_END",
                        "MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2",
                        "MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2",
                        "MGLPPC_CMD_DELETE_TEXTURES"):
            self.assertIn(command, switch, f"{command} missing from drop switch")
        begin2 = switch[switch.index("MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2"):
                        switch.index("MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2")]
        commit2 = switch[switch.index("MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2"):
                         switch.index("MGLPPC_CMD_DELETE_TEXTURES")]
        self.assertIn("value[4]", begin2)
        self.assertIn("value[2]", commit2)
        self.assertIn("value[dropIndex + 1UL]", switch)

    def test_subimage_drops_before_clearing_the_mirror(self):
        """The shared-subimage path clears the mirror handle before its
        command, which would hide the old token from the platform
        pre-command drop; the explicit drop must come first."""
        start = self.source.index("void R200PPCDispatchTexSubImage2D")
        body = self.source[start:
                           self.source.index("void R200PPCDispatchDeleteTextures", start)]
        drop = body.index("FrontendDropCachedSurface((ULONG)texture->surface.Handle);")
        clear = body.index("texture->surface.Handle = NULL;")
        self.assertLess(drop, clear)


if __name__ == "__main__":
    unittest.main()
