"""Native read-only surface metadata, using the actual extracted query handler.

python3 tests/test_r200_surface_bridge.py -v

Compile with host cc and ASan/UBSan; no SDK, driver or hardware is loaded.
Only CurrentContext and Radeon3DGetInfo are mocked. TargetSurface and SetError
are extracted too. No allocation, residency, submission or recovery functions
are provided. Generated sources/binaries stay in /tmp/opencode and are removed.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "classic_68k/dev/MiniGL_Library_Source_Code"
STATE = (NATIVE / "library/r200_state.c").read_text()
CORE = (NATIVE / "library/minigl_r200.c").read_text()
GL = (NATIVE / "library/minigl_r200.h").read_text()
INTERNAL = (NATIVE / "library/minigl_r200_internal.h").read_text()
RADEON = (NATIVE / "third_party/radeon3d/include/radeon3d.h").read_text()
HEADERS = (
    ROOT / "backend_r200/include/libraries/minigl_r200_bridge.h",
    NATIVE / "include/libraries/minigl_r200_bridge.h",
    ROOT / "classic_68k/dev/include/libraries/minigl_r200_bridge.h",
)
FIELDS = (
    "version", "structSize", "surfaceToken", "cpuAddress", "gpuAddress",
    "pitch", "width", "height", "format", "deviceEpoch", "generation",
)


def section(text, start, end):
    start = text.index(start)
    return text[start:text.index(end, start)]


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


HANDLER = section(STATE, "    case MGL_R200_PRIVATE_SURFACE_V1_QUERY:",
                  "    case MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY:")

STUBS = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG, GLenum;
typedef int32_t GLint;
typedef void *APTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p, n) memset(p, 0, n)
'''

MOCKS = r'''
struct Radeon3DDevice { ULONG unused; };
struct TextureObject {
    APTR image;
    ULONG width, height, dirtyLevelMask, imageGeneration, residentGeneration;
    ULONG residentDeviceEpoch, surfaceGeneration;
    BOOL generated, resident, direct;
    struct Radeon3DSurface surface, directSurface;
};
struct DeferredSurface { struct Radeon3DSurface surface; };
struct MiniGLR200Context {
    struct Radeon3DDevice *device;
    struct Radeon3DSurface surfaces[3], depthSurface;
    struct TextureObject textures[MAX_TEXTURES];
    struct DeferredSurface *deferredSurfaces;
    ULONG width, height, deviceEpoch, radeonGeneration, radeonCaps;
    ULONG displayedBuffer, drawingBuffer, drawBuffer, semanticStateSerial;
    BOOL fullscreen;
    GLenum error;
};
static struct MiniGLR200Context c, foreign, *current;
static struct Radeon3DDevice device;
static struct Radeon3DInfo driverInfo;
static struct DeferredSurface deferred;
static BOOL getInfoOK;
static ULONG getInfoCalls;

static struct MiniGLR200Context *CurrentContext(void) { return current; }
static BOOL Radeon3DGetInfo(struct Radeon3DDevice *d, struct Radeon3DInfo *info) {
    struct Radeon3DInfo expected;
    memset(&expected, 0, sizeof(expected)); expected.Size = sizeof(expected);
    assert(d == &device && memcmp(info, &expected, sizeof(expected)) == 0);
    ++getInfoCalls;
    *info = driverInfo; /* Even a failing service may have written fields. */
    return getInfoOK;
}
'''

CHECKS = r'''
static struct Radeon3DSurface surface(ULONG token, ULONG format, ULONG width,
                                       ULONG height, ULONG pitch) {
    struct Radeon3DSurface s;
    memset(&s, 0, sizeof(s));
    s.Size = RADEON3D_SURFACE_V1_SIZE; s.Version = RADEON3D_SURFACE_VERSION;
    s.Generation = 19; s.Handle = (APTR)(uintptr_t)token;
    s.CpuAddress = (APTR)(uintptr_t)0x10000000;
    s.GpuAddress = 0x00800000; s.Pitch = pitch;
    s.Width = width; s.Height = height; s.Format = format;
    return s;
}
static void reset(void) {
    memset(&c, 0, sizeof(c)); memset(&foreign, 0, sizeof(foreign));
    memset(&driverInfo, 0, sizeof(driverInfo)); memset(&deferred, 0, sizeof(deferred));
    current = &c; c.device = &device; c.deviceEpoch = 7; c.radeonGeneration = 19;
    c.radeonCaps = RADEON3D_CAP_CP_READY; c.semanticStateSerial = 42;
    c.width = 800; c.height = 600; c.drawBuffer = GL_BACK;
    c.displayedBuffer = 1; c.drawingBuffer = 2;
    for (ULONG i = 0; i < 3; ++i)
        c.surfaces[i] = surface(0x100 + i, RADEON3D_FORMAT_B8G8R8A8, 800, 600, 3200);
    c.depthSurface = surface(0x200, RADEON3D_FORMAT_R5G6B5PC, 832, 600, 1664);
    c.textures[1].surface = surface(0x300, RADEON3D_FORMAT_B8G8R8A8, 65, 96, 320);
    c.textures[1].width = 64; c.textures[1].height = 64;
    c.textures[1].generated = c.textures[1].resident = TRUE;
    c.textures[1].residentDeviceEpoch = c.deviceEpoch;
    c.textures[1].surfaceGeneration = c.textures[1].surface.Generation;
    driverInfo.Size = sizeof(driverInfo); driverInfo.Generation = 19;
    driverInfo.Caps = RADEON3D_CAP_CP_READY;
    getInfoCalls = 0; getInfoOK = TRUE;
}

/* Every path checks the entire response, exact write bounds, fresh GetInfo
 * count, and unchanged native state (except the conventional sticky GL error). */
static void query(ULONG token, const struct Radeon3DSurface *wanted, ULONG calls) {
    struct { ULONG before; MGLR200PrivateSurfaceV1 value; ULONG after; } output;
    struct MiniGLR200Context before;
    struct Radeon3DInfo infoBefore;
    MGLR200PrivateSurfaceV1 expected;
    ULONG callsBefore = getInfoCalls;
    memcpy(&before, &c, sizeof(before)); memcpy(&infoBefore, &driverInfo, sizeof(infoBefore));
    memset(&output, 0xa5, sizeof(output)); output.value.surfaceToken = token;
    memset(&expected, 0, sizeof(expected));
    expected.version = 1; expected.structSize = 44; expected.surfaceToken = token;
    if (wanted) {
        expected.cpuAddress = (ULONG)(uintptr_t)wanted->CpuAddress;
        expected.gpuAddress = wanted->GpuAddress; expected.pitch = wanted->Pitch;
        expected.width = wanted->Width; expected.height = wanted->Height;
        expected.format = wanted->Format; expected.deviceEpoch = c.deviceEpoch;
        expected.generation = wanted->Generation;
    } else if (current && before.error == GL_NO_ERROR) {
        before.error = GL_INVALID_OPERATION;
    }
    Lib_glGetIntegerv(MGL_R200_PRIVATE_SURFACE_V1_QUERY, (GLint *)&output.value);
    assert(output.before == 0xa5a5a5a5 && output.after == 0xa5a5a5a5);
    assert(memcmp(&output.value, &expected, sizeof(expected)) == 0);
    assert(memcmp(&c, &before, sizeof(c)) == 0);
    assert(memcmp(&driverInfo, &infoBefore, sizeof(driverInfo)) == 0);
    assert(getInfoCalls == callsBefore + calls);
}

static void owned(void) {
    reset(); query(0x100, &c.surfaces[0], 1);
    c.fullscreen = TRUE; query(0x102, &c.surfaces[2], 1);
    c.drawBuffer = GL_FRONT; query(0x101, &c.surfaces[1], 1);
    query(0x200, &c.depthSurface, 1); /* Padded depth width, not context width. */
    query(0x300, &c.textures[1].surface, 1); /* Imported storage, not GL dimensions. */
    c.textures[MAX_TEXTURES - 1] = c.textures[1];
    c.textures[MAX_TEXTURES - 1].surface.Handle = (APTR)(uintptr_t)0x301;
    query(0x301, &c.textures[MAX_TEXTURES - 1].surface, 1);
    c.surfaces[1] = surface(0x101, RADEON3D_FORMAT_CLUT8, 800, 600, 832);
    query(0x101, &c.surfaces[1], 1);
}

static void handles(void) {
    reset(); query(0, NULL, 0); query(1, NULL, 0); query(0xffffffff, NULL, 0);
    query(0x101, NULL, 0); query(0x102, NULL, 0); /* Not current targets. */
    foreign.surfaces[0] = surface(0x400, RADEON3D_FORMAT_B8G8R8A8, 64, 64, 256);
    query(0x400, NULL, 0);
    deferred.surface = foreign.surfaces[0]; c.deferredSurfaces = &deferred;
    query(0x400, NULL, 0);
    c.textures[1].directSurface = foreign.surfaces[0]; c.textures[1].direct = TRUE;
    query(0x400, NULL, 0); /* Pending direct allocation is not committed. */
    c.textures[1].resident = FALSE; query(0x300, NULL, 0);
    c.textures[1].resident = TRUE; c.textures[1].generated = FALSE;
    query(0x300, NULL, 0);
    c.textures[1].generated = TRUE; --c.textures[1].residentDeviceEpoch;
    query(0x300, NULL, 0);
    reset(); c.surfaces[0].Handle = NULL; query(0x100, NULL, 0);
    c.depthSurface.Handle = NULL; query(0x200, NULL, 0);
    c.textures[1].surface.Handle = NULL; query(0x300, NULL, 0);
}

static void generations(void) {
    reset(); query(0x100, &c.surfaces[0], 1);
    /* The driver reset while native deviceEpoch and radeonGeneration stayed put. */
    ++driverInfo.Generation;
    query(0x100, NULL, 1); query(0x200, NULL, 1); query(0x300, NULL, 1);
    assert(c.deviceEpoch == 7 && c.radeonGeneration == 19);
    reset(); --c.surfaces[0].Generation; query(0x100, NULL, 1);
    reset(); driverInfo.Generation = 0; c.surfaces[0].Generation = 0;
    query(0x100, NULL, 1);
    reset(); c.surfaces[0].Generation = 0; query(0x100, NULL, 1);
}

static void session(void) {
    reset(); getInfoOK = FALSE; query(0x100, NULL, 1);
    reset(); driverInfo.Caps = 0; query(0x100, NULL, 1);
    assert(c.radeonCaps & RADEON3D_CAP_CP_READY); /* Cached CP cap is insufficient. */
    /* Nonzero unrelated live capabilities cannot substitute for CP readiness. */
    reset(); driverInfo.Caps = (ULONG)~RADEON3D_CAP_CP_READY; query(0x100, NULL, 1);
    reset(); c.radeonCaps = 0; query(0x100, &c.surfaces[0], 1); /* Live cap wins. */
    reset(); c.device = NULL; query(0x100, NULL, 0);
    reset(); c.deviceEpoch = 0; query(0x100, NULL, 0);
    reset(); current = NULL; query(0x100, NULL, 0);
    Lib_glGetIntegerv(MGL_R200_PRIVATE_SURFACE_V1_QUERY, NULL);
    assert(c.error == GL_NO_ERROR && !getInfoCalls);
    current = &c; Lib_glGetIntegerv(MGL_R200_PRIVATE_SURFACE_V1_QUERY, NULL);
    assert(c.error == GL_INVALID_VALUE && !getInfoCalls);
    query(1, NULL, 0); /* Preserve a previous error, not a replacement. */
    query(0x100, &c.surfaces[0], 1); /* Success must not clear that error either. */
}

static void extents(void) {
    for (ULONG fault = 0; fault < 19; ++fault) {
        struct Radeon3DSurface *s = &c.surfaces[0];
        reset();
        switch (fault) {
        case 0: s->Size = RADEON3D_SURFACE_V1_SIZE - 1; break;
        case 1: s->Version = 0; break;
        case 2: s->CpuAddress = NULL; break;
        case 3: s->Pitch = 0; break;
        case 4: ++s->Pitch; break;
        case 5: s->Width = 0; break;
        case 6: s->Height = 0; break;
        case 7: s->Width = 65537; break;
        case 8: s->Height = 65537; break;
        case 9: s->Width = 0x40000001; break; /* Width * 4 would wrap. */
        case 10: s->Pitch = 0x80000000; s->Height = 2; break;
        case 11: s->Pitch = 65536; s->Height = 65536; break;
        case 12: s->Pitch = 64; break; /* Row wider than pitch. */
        case 13: s->Format = 0; break;
        case 14: s->Format = 0xffffffff; break;
        case 15: s->CpuAddress = (APTR)(uintptr_t)0xfffffff0; break;
        case 16: s->GpuAddress = 0xfffffff0; break;
        case 17: ++s->GpuAddress; break;
        case 18: s->Pitch = 0xffffffc0; s->Height = 2; break; /* Row offset + bytes wraps. */
        }
        query(0x100, NULL, 1);
    }
}

static void boundaries(void) {
    reset(); c.surfaces[0].GpuAddress = 0; query(0x100, &c.surfaces[0], 1);
    c.surfaces[0].GpuAddress = 16; query(0x100, &c.surfaces[0], 1);
    /* Last occupied byte may be UINT32_MAX; unused final-row padding is not
     * part of the pixel extent. Arithmetic must not wrap or reject this. */
    c.surfaces[0] = surface(0x100, RADEON3D_FORMAT_CLUT8, 16, 1, 64);
    c.surfaces[0].GpuAddress = 0xfffffff0;
    c.surfaces[0].CpuAddress = (APTR)(uintptr_t)0xfffffff0;
    query(0x100, &c.surfaces[0], 1);
    c.surfaces[0].Width = 17; query(0x100, NULL, 1);
    reset(); c.surfaces[0] = surface(0x100, RADEON3D_FORMAT_CLUT8, 16, 2, 64);
    c.surfaces[0].GpuAddress = 0xffffffb0;
    c.surfaces[0].CpuAddress = (APTR)(uintptr_t)0xffffffb0;
    query(0x100, &c.surfaces[0], 1); /* Include preceding rows in the address bound. */
    c.surfaces[0].Width = 17; query(0x100, NULL, 1);
    reset(); c.surfaces[0].Width = 65536; c.surfaces[0].Height = 1;
    c.surfaces[0].Pitch = 65536 * 4; query(0x100, &c.surfaces[0], 1);
    reset(); c.surfaces[0].Width = 1; c.surfaces[0].Height = 65536;
    c.surfaces[0].Pitch = 64; query(0x100, &c.surfaces[0], 1);
}

static void readonly(void) {
    reset(); c.textures[1].direct = TRUE; /* Committed direct texture: no image. */
    c.textures[1].directSurface = surface(0x400, RADEON3D_FORMAT_R5G6B5PC, 64, 64, 128);
    query(0x300, &c.textures[1].surface, 1); query(0x400, NULL, 0);
    c.textures[1].direct = FALSE; c.textures[1].dirtyLevelMask = 1;
    c.textures[1].image = (APTR)(uintptr_t)1; /* Not safe to read/copy pixels. */
    c.textures[1].imageGeneration = 23; c.textures[1].residentGeneration = 22;
    query(0x300, &c.textures[1].surface, 1); /* Metadata must not sync dirt. */
    c.textures[1].resident = FALSE; c.textures[1].surface.Handle = NULL;
    query(0x300, NULL, 0); /* Never lazily resurrect residency. */
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "owned")) owned();
    else if (!strcmp(argv[1], "handles")) handles();
    else if (!strcmp(argv[1], "generations")) generations();
    else if (!strcmp(argv[1], "session")) session();
    else if (!strcmp(argv[1], "extents")) extents();
    else if (!strcmp(argv[1], "boundaries")) boundaries();
    else if (!strcmp(argv[1], "readonly")) readonly();
    else assert(0);
    return 0;
}
'''


class SurfaceBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="r200-surface-", dir="/tmp/opencode")
        cls.addClassCleanup(cls.temp.cleanup)
        source = Path(cls.temp.name) / "surface.c"
        cls.binary = Path(cls.temp.name) / "surface"
        code = STUBS + HEADERS[1].read_text().replace("#include <exec/types.h>", "")
        for text, names in (
            (GL, ("GL_NO_ERROR", "GL_INVALID_OPERATION", "GL_INVALID_VALUE", "GL_FRONT", "GL_BACK")),
            (INTERNAL, ("MAX_TEXTURES",)),
            (RADEON, ("RADEON3D_CAP_CP_READY", "RADEON3D_SURFACE_VERSION", "RADEON3D_SURFACE_V1_SIZE",
                      "RADEON3D_FORMAT_CLUT8", "RADEON3D_FORMAT_R5G6B5PC", "RADEON3D_FORMAT_B8G8R8A8")),
        ):
            for name in names:
                definition = re.search(rf"^#define {name}\s+[^\n]+", text, re.M)
                assert definition is not None, f"Missing production macro: {name}"
                code += definition[0] + "\n"
        for name in ("Radeon3DInfo", "Radeon3DSurface"):
            definition = re.search(rf"struct {name} \{{.*?\n\}};", RADEON, re.S)
            assert definition is not None, f"Missing production struct: {name}"
            code += definition[0] + "\n"
        for index, name in enumerate(FIELDS):
            code += (f'_Static_assert(offsetof(MGLR200PrivateSurfaceV1, {name}) == {index * 4}, '
                     f'"{name} ABI offset");\n')
        code += MOCKS
        code += function(CORE, "struct Radeon3DSurface *TargetSurface(")
        code += function(CORE, "void SetError(")
        code += section(STATE, "void Lib_glGetIntegerv(", "    switch (name) {")
        code += "    switch (name) {\n" + HANDLER + "    default: assert(0);\n    }\n}\n"
        source.write_text(code + CHECKS)
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-pointer-to-int-cast",
            "-fno-pie", "-no-pie", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", str(source), "-o", str(cls.binary),
        ], check=True)

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_header_abi_and_mirrors(self):
        definitions = []
        for path in HEADERS:
            definition = re.search(
                r"/\* Read-only metadata.*?MGLR200PrivateSurfaceV1SizeCheck\[[^;]+;",
                path.read_text(), re.S)
            assert definition is not None, f"Missing surface bridge definition: {path}"
            definitions.append(definition[0])
        self.assertEqual(definitions[0], definitions[1])
        self.assertEqual(definitions[0], definitions[2])
        self.assertIn("#define MGL_R200_PRIVATE_SURFACE_V1_QUERY 0x4d475013UL", definitions[0])
        self.assertIn("#define MGL_R200_PRIVATE_SURFACE_V1_VERSION 1UL", definitions[0])
        self.assertEqual(tuple(re.findall(r"ULONG (\w+);", definitions[0])), FIELDS)

    def test_handler_has_only_readonly_dependencies(self):
        code = re.sub(r"/\*.*?\*/", "", HANDLER, flags=re.S)
        calls = set(re.findall(r"\b(\w+)\s*\(", code))
        self.assertEqual(calls - {"if", "for", "switch", "sizeof"},
                         {"ZeroMemory", "TargetSurface", "Radeon3DGetInfo", "SetError"})
        indirect = section(STATE, "    case MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY:",
                           "    case GL_SHADE_MODEL:")
        self.assertNotIn("MGL_R200_PRIVATE_SURFACE_V1_QUERY", indirect)

    def test_color_depth_texture(self):
        self.run_case("owned")

    def test_invalid_foreign_inactive_pending_and_deleted_handles(self):
        self.run_case("handles")

    def test_driver_generation_even_with_unchanged_native_epoch(self):
        self.run_case("generations")

    def test_getinfo_failure_live_cp_cap_and_missing_session(self):
        self.run_case("session")

    def test_empty_invalid_and_overflowed_extents(self):
        self.run_case("extents")

    def test_zero_gpu_alignment_and_inclusive_extent_bounds(self):
        self.run_case("boundaries")

    def test_no_residency_allocation_upload_or_context_mutation(self):
        self.run_case("readonly")


if __name__ == "__main__":
    unittest.main()
