#!/usr/bin/env python3
"""Test the real screen clipper, including PPC-style fused interpolation."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
text = (root / "backend_r200/library/r200_geometry.c").read_text()
start = text.index("static GLfloat ScreenPlaneDistance(")
end = text.index("static BOOL EmitClippedScreenTriangle(", start)
clip = text[start:end]
old, changed = re.subn(
    r"                /\* Intersections lie.*?\n                }\n", "", clip,
    flags=re.S)
assert changed == 1
start = old.index("    /* A later plane's interpolation")
end = old.index("    return count;", start)
old = old[:start] + old[end:]
old = old.replace("ULONG plane, vertexIndex;", "ULONG plane;")
prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <math.h>
typedef uint32_t ULONG;
typedef float GLfloat;
typedef int BOOL;
struct MiniGLR200Context { ULONG width, height; };
struct ScreenVertex {
    GLfloat x,y,z,rhw,nx,ny,nz;
    ULONG color;
    GLfloat s,t,s1,t1,fog;
};
'''
test = r'''
int main(void)
{
    struct MiniGLR200Context context = {800,600};
    unsigned bad=0, checks=0;
    int top, reverse, perspective;
    /* Includes the exact failed console triangle (top=-90, bottom=510). */
    for (top=-599; top<0; ++top)
        for (reverse=0; reverse<2; ++reverse)
            for (perspective=0; perspective<2; ++perspective) {
                struct ScreenVertex v[12] = {{0}};
                ULONG n, i;
                v[0].x=0; v[0].y=(float)top;
                v[1].x=800; v[1].y=(float)top;
                v[2].x=800; v[2].y=(float)(top+600);
                for (i=0; i<3; ++i) {
                    if (reverse) v[i].y=600-v[i].y;
                    v[i].s=v[i].x/800; v[i].t=v[i].y/600;
                    v[i].rhw=1; v[i].color=0xffffffff;
                }
                n=ClipScreenPolygon(&context,v,3,perspective);
                for (i=0; i<n; ++i) {
                    ++checks;
                    if (!(v[i].x>=0 && v[i].x<=800 && v[i].y>=0 && v[i].y<=600))
                        ++bad;
                    if (fabsf(v[i].s-v[i].x/800)>0.000002f ||
                        fabsf(v[i].t-v[i].y/600)>0.000002f)
                        return 2; /* position repair must preserve attributes */
                }
            }
    printf("SCREEN_CLIP checks=%u outside=%u\n",checks,bad);
    return bad ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="r200-clip-") as directory:
    path = pathlib.Path(directory)
    for name, body, expect in (("fixed", clip, 0), ("old", old, 1)):
        # fmadds is available on target PPC; use fmaf to reproduce its single
        # rounding on hosts regardless of their compiler's contraction policy.
        expression = "a->field + (b->field - a->field) * amount"
        assert body.count(expression) == 1
        body = body.replace(expression, "fmaf(b->field - a->field, amount, a->field)")
        source, binary = path / (name + ".c"), path / name
        source.write_text(prefix + body + test)
        subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-pie", "-no-pie", str(source), "-lm", "-o", str(binary)],
                       check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(name, result.stdout.strip())
        if result.returncode != expect or result.stderr:
            raise SystemExit(result.stderr or "unexpected clipper result")
