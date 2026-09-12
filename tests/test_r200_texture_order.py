"""Host-independent production source invariants: python3 tests/test_r200_texture_order.py.

These guard publication/control ordering, not cache coherency or GPU results.
The public texture update example supplies the pre-present pixel regression.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    text = (ROOT / path).read_text()
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    # Ignore only standalone profile blocks, never other/nested guards or else branches.
    return re.sub(r"^[ \t]*#if MGLPPC_PROFILE[ \t]*\n"
                  r"(?:(?![ \t]*#)[^\n]*\n)*[ \t]*#endif[ \t]*$",
                  "", text, flags=re.M)


class TextureOrder(unittest.TestCase):
    def test_direct_publication_and_failure_gate(self):
        frontend = source("backend_r200/library/minigl_r200_warpos_compile_probe.c")
        dispatch = frontend.split("void R200PPCDispatchTexImage2D(", 1)[1]
        direct = dispatch.split("if ((FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_DIRECT_V1)", 1)[1]
        gate, upload = direct.split("if (FrontendDirectTextureUpload(", 1)
        self.assertRegex(gate, r"if\s*\(\s*!PlatformMailboxReady\(\)\s*\|\|\s*"
                         r"FrontendPlatform.mailboxAbandoned\s*\|\|\s*"
                         r"!FlushScreenVertices\(context\)\s*\|\|\s*"
                         r"!Submit\(context\)\s*\|\|\s*"
                         r"\(FrontendPlatform.ring\s*&&\s*"
                         r"!R200PPCPlatformBarrier\(context, FALSE\)\)\)\s*\{\s*"
                         r"SetError\(context, GL_INVALID_OPERATION\);\s*return;\s*\}")
        self.assertNotIn("SubmitAndWait", gate)
        abandoned = upload.index("if (FrontendPlatform.mailboxAbandoned)")
        abort = upload.index("PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_ABORT")
        self.assertRegex(upload[abandoned:abort],
                         r"mailboxAbandoned\)\s*\{\s*SetError\(context, GL_INVALID_OPERATION\);\s*return;\s*\}")
        helper = frontend.split("static BOOL FrontendDirectTextureUpload(", 1)[1].split(
            "void R200PPCDispatchTexImage2D(", 1)[0]
        self.assertLess(helper.index("MGLPPC_CMD_TEXTURE_DIRECT_BEGIN"),
                        helper.index("MGLPPC_CMD_TEXTURE_DIRECT_COMMIT"))

    def test_current_host_requires_ring_consumption_before_begin(self):
        host = source("backend_r200/minigl_ppc_host.c")
        loop = host.split("while (state.running)", 1)[1]
        self.assertLess(loop.index("PollMailbox(&state)"), loop.index("RingPoll(&state)"))
        begin = host.split("case MGLPPC_CMD_TEXTURE_DIRECT_BEGIN:", 1)[1].split(
            "case MGLPPC_CMD_TEXTURE_DIRECT_COMMIT:", 1)[0]
        self.assertLess(begin.index("DrainSemanticFence(state)"),
                        begin.index("BeginBackendDirectTexture("))
        frontend = source("backend_r200/library/minigl_r200_warpos_compile_probe.c")
        barrier = frontend.rsplit("BOOL R200PPCPlatformBarrier(", 1)[1].split(
            "BOOL R200PPC", 1)[0]
        self.assertIn("while ((FrontendPlatform.cpEnabled ? control->retired : control->completed) != control->head)", barrier)
        self.assertIn("MGLPPCInvalidateLine32((APTR)&control->completed)", barrier)
        self.assertIn("MGLPPCInvalidateLine32((APTR)&control->retired)", barrier)
        self.assertNotIn("SubmitAndWait", barrier)

    def test_pixel_oracles_and_pre_present_order(self):
        example = source("examples/r200_texture_update_test.c")
        full = example.split('REQUIRE_ERROR("BASELINE RGBA FULL UPLOAD", GL_NO_ERROR);', 1)[1]
        full = full.split('printf("SMOKE CONTEXT=%lu FULL SAME SHAPE PIXEL ORDER', 1)[0]
        ordered = ["memcpy(before, image, sizeof(image))", "DrawPanel(-1, 0)",
                   "glTexImage2D(", "DrawPanel(0, 1)",
                   'VerifyPanel("A BEFORE FULL REDEFINITION", 0, before)',
                   'VerifyPanel("B AFTER FULL REDEFINITION", 160, image)',
                   "mglSwitchDisplay()"]
        positions = [full.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))
        between = full[positions[1]:positions[2]]
        for forbidden in ("glFlush(", "glFinish(", "glReadPixels(", "mglSwitchDisplay("):
            self.assertNotIn(forbidden, between)
        self.assertIn("for (pass = 0; pass < 2; ++pass)", example)


if __name__ == "__main__":
    unittest.main()
