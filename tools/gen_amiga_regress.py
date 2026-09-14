#!/usr/bin/env python3
# Generates the AmigaDOS quick-regression script for the MiniGL test suite.

TESTS = [
    ("system.dispatch", "Validate ABI version, table size, and runtime dispatch table."),
    ("primitives.triangle", "Draw and sample a solid immediate-mode triangle."),
    ("depth.less", "Verify a near surface hides a later far surface."),
    ("blend.source-alpha", "Compare half-alpha, opaque, and background pixels."),
    ("matrices.translate", "Verify modelview translation moves triangle coverage."),
    ("arrays.draw-arrays", "Render two equal triangles through glDrawArrays."),
    ("3d.rotating-cubes", "Exercise perspective, modelview transforms, depth, and colored faces."),
    ("benchmark.triangles", "Measure 64 immediate triangles including presentation."),
    ("lighting.availability", "Report whether optional ABI v4 lighting is available."),
    ("raster.prim-shade", "Draw every filled primitive type in both shade models and compare each against its exact expected colour."),
    ("raster.point-size", "Render points at 1, 2, 4, 8 and 16 pixels and measure the rendered width of each."),
    ("blend.texture-alpha", "Modulated 1x1 alpha texture blended over known backgrounds, as used by Quake's screen flash."),
    ("texture.luminance", "Upload a 4-texel GL_LUMINANCE ramp and render it back."),
    ("texture.luminance-alpha", "Upload GL_LUMINANCE_ALPHA texels and blend them over black."),
    ("texture.alpha", "Upload GL_ALPHA texels, modulate with white, and blend over black."),
    ("texture.bgra", "Upload an MGL_UBYTE_BGRA texture with distinct bytes per channel."),
    ("texture.argb", "Upload an MGL_UBYTE_ARGB texture with distinct bytes per channel."),
    ("texture.subimage-formats", "Patch a resident texture with luminance and RGBA subimages."),
    ("texture.row-identity", "Upload distinct solid rows at 4x4, 8x2 and 16x2 and read one cell per row."),
    ("quake.alias-ogre", "Render the real Quake Ogre mesh with its original game skin through its strided alias-array layout, alternating float and integer paths."),
    ("quake.alias-ogre-x4-static", "Render four real Quake Ogre meshes at mixed depths with fixed transforms."),
    ("quake.alias-ogre-x4-spin", "Render four real Quake Ogre meshes spinning independently at mixed depths."),
    ("quake.alias-ogre-x4-advance", "Render four real Quake Ogre meshes moving toward and away from the viewer."),
    ("quake.world-stream", "Sustain one large merged world-style triangle list to exhaust and rotate streaming segments."),
    ("quake.lightmap-static", "Render Quake's two-pass ZERO/SRC_COLOR lightmap without texture updates."),
    ("quake.lightmap-update", "Replace a resident lightmap every frame and apply Quake's ZERO/SRC_COLOR pass."),
    ("quake.particles-mixed", "Mix TCL world triangles with 24 animated blended points orbiting between world draws."),
    ("quake.world-room-walk", "Walk the real Quake e1m1 entrance hall out and back: world geometry plus doors in texture-chained batches, depth-tested, no effects."),
    ("quake.fog-linear", "Render near, middle, and far surfaces with Quake-style linear vertex fog."),
    ("quake.pickup-flash", "Pulse Quake's textured device-coordinate MGL_FLATFAN blend: a green burst fading over 0.75 s every 3 s, plain scene between pulses."),
    ("present.stability", "Draw and present one identical frame repeatedly and check the result never drifts."),
]

def gen(tests, path):
    n_total = len(tests)
    L = []
    A = L.append
    A("; MiniGL quick regression package (AmigaDOS)")
    A("; Usage:  Execute Work:ibdraw/regress AUTO    - all tests, no questions")
    A(";         Execute Work:ibdraw/regress MANUAL - briefed, then Accept/Re-run/Finish/Reject")
    A("; Optional 2nd arg: frames per test (default 60 auto, 600 manual)")
    A("; Requires: Work:ibdraw/mgltestsuite + hostrun + hoststop deployed.")
    A("; Each test gets a fresh 68k host; logs land in Work:ibdraw/regress/.")
    A("; The shell is left in Work:ibdraw afterwards.")
    A("")
    A("FailAt 21")
    A("If NOT EXISTS Work:ibdraw/mgltestsuite")
    A('   Echo "Work:ibdraw/mgltestsuite missing - deploy the suite first."')
    A("   Quit")
    A("EndIf")
    A('Set mode "$1"')
    A('If "$mode" EQ "" Set mode "AUTO"')
    A('Set frames "60"')
    A('If "$mode" EQ "MANUAL" Set frames "600"')
    A('Set ovf "$2"')
    A('If "$ovf" NE "" Set frames "$ovf"')
    A('Set stop ""')
    A('Echo "*NMiniGL regression - mode=$mode frames=$frames*N"')
    A('Delete >NIL: Work:ibdraw/regress ALL QUIET')
    A("makedir >NIL: Work:ibdraw/regress")
    A("CD Work:ibdraw")
    A("; clear any orphan host from a previous crashed session")
    A("Work:ibdraw/hoststop --orphan >NIL:")
    A("")
    for i, (tid, desc) in enumerate(tests, 1):
        n = i
        A(f"; --- {n}/{n_total} {tid} ---")
        A(f'If "$stop" EQ "1" Skip >end')
        A(f'Set cur "{tid}"')
        A(f'Set retried ""')
        A(f'Echo "*N=== {n}/{n_total}: {tid} ==="')
        A(f'If "$mode" EQ "MANUAL" Echo "YOU WILL SEE: {desc}*NStarting in 3 s..."')
        A(f'If "$mode" EQ "MANUAL" Delay 150')
        A(f"Delay 40")
        A(f"Run >NIL: Execute Work:ibdraw/hostrun")
        A(f"Delay 150")
        A(f"Lab run_{n}")
        A(f"mgltestsuite --test {tid} --fullscreen --width 640 --height 480 --depth 16 --frames $frames >Work:ibdraw/regress/{tid}.log")
        A("If ERROR")
        A(f'   If "$retried" EQ ""')
        A(f'      Echo "   * setup error - fresh host, retrying once *"')
        A(f'      Set retried "1"')
        A(f"      Delay 50")
        A(f"      Run >NIL: Execute Work:ibdraw/hostrun")
        A(f"      Delay 150")
        A(f"      Skip >run_{n}")
        A("   EndIf")
        A('      Echo "   * SETUP FAILED - see log; cleaning orphan host *"')
        A("      Work:ibdraw/hoststop --orphan >NIL:")
        A("EndIf")
        A(f"Type Work:ibdraw/regress/{tid}.log")
        A('If "$mode" EQ "MANUAL"')
        A('   Ask "    Accept this result? (y/n)"')
        A("   If WARN")
        A('      Ask "    Run this test again? (y/n)"')
        A("      If WARN")
        A('         Ask "    Finish testing (skip all remaining)? (y/n)"')
        A("         If WARN")
        A('            Set stop "1"')
        A('            Echo "    REJECTED - moving to next test*N"')
        A("         Else")
        A('            Echo "    FINISH chosen - skipping remaining tests*N"')
        A("         EndIf")
        A("      Else")
        A('         Set retried "1"')
        A("         Delay 50")
        A("         Run >NIL: Execute Work:ibdraw/hostrun")
        A("         Delay 150")
        A(f"         Skip >run_{n}")
        A("      EndIf")
        A("   Else")
        A('      Echo "    accepted.*N"')
        A("   EndIf")
        A("EndIf")
        A("")
    A("Lab end")
    A('Echo "*N===== Regression run complete ====="')
    A('Echo "Logs: Work:ibdraw/regress/ (one per test)"')
    A('Echo "Machine-readable records: Type T:minigl-testsuite.log"')
    A("List Work:ibdraw/regress FILES")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print(f"{path}: {len(L)} lines, {n_total} tests")

gen(TESTS, "/tmp/regress")
gen([
    ("system.dispatch", "Validate ABI version, table size, and runtime dispatch table."),
    ("primitives.triangle", "Draw and sample a solid immediate-mode triangle."),
    ("depth.less", "Verify a near surface hides a later far surface."),
    ("present.stability", "Draw and present one identical frame repeatedly and check the result never drifts."),
], "/tmp/regress_smoke")
