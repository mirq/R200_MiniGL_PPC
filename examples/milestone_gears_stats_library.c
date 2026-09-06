/* PPC DLL gears client. Interactive runs retain keyboard input and a title
 * readout; -frames N uses a quiet fixed loop, timed once including its final
 * drain but excluding setup and teardown. MGL_GEARS_PRECALC records the same
 * immediate-mode geometry before timing and replays it without repeated trig.
 */

#include <proto/minigl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <intuition/intuition.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <dos/dos.h>

/* The WarpOS runtime provides GfxBase but not IntuitionBase; the title
 * FPS readout needs it. */
struct IntuitionBase *IntuitionBase;
#include <proto/exec.h>

#ifndef D
#define D(x) do { printf x; } while (0)
#endif

/* Diagnostic output gate (default off): per-line teardown marker log on
 * DH2: and the console FPS fallback both cost console/FFS writes that
 * dominate frame time. Define 1 to re-enable outside the measured loop. */
#ifndef MGL_GEARS_DIAG
#define MGL_GEARS_DIAG 0
#endif


#ifndef M_PI
#define M_PI 3.14159265
#endif

#define fcos  cos
#define fsin  sin
#define fsqrt sqrt

//#ifndef __GNUC__
struct GfxBase *GfxBase = NULL;
//#endif

/* stricmp isn't linked in this toolchain (undefined symbol at link time,
 * confirmed) -- see milestone_gears.c's own comment on this exact
 * function. */
static int local_stricmp(const char *a, const char *b)
{
	while (*a && *b)
	{
		char ca = (*a >= 'A' && *a <= 'Z') ? *a + ('a' - 'A') : *a;
		char cb = (*b >= 'A' && *b <= 'Z') ? *b + ('a' - 'A') : *b;
		if (ca != cb)
			return (int)ca - (int)cb;
		a++; b++;
	}
	return (int)*a - (int)*b;
}

static int show = 1;
/* The readout lives in the window title (SetWindowTitles, one 68k
 * transition per FPS_SAMPLE_FRAMES) -- cheap enough to leave on. 'f'
 * still toggles it. */
static int bShowFPS = 1;
/* A positive frame limit selects the input/overlay-free benchmark loop. */
static int frameLimit = 0;
static int framesDone = 0;
static GLboolean bEnvMap = GL_TRUE;

#define MILESTONE_WIDTH 640
#define MILESTONE_HEIGHT 480

/* ================= Real gear geometry, verbatim from gears.c ================= */

typedef struct {
  float rad, wid;
} Profile;

int circle_subdiv;
#define MIN_SUBDIV 30

#ifdef MGL_GEARS_PRECALC
enum GearCommandType { GEAR_BEGIN, GEAR_NORMAL, GEAR_VERTEX, GEAR_END };
struct GearCommand {
	unsigned int type;
	GLenum mode;
	GLfloat x, y, z;
};
struct GearCommands {
	struct GearCommand *commands;
	unsigned int count, capacity;
};
static struct GearCommands precalculatedGears[2];
static struct GearCommands *recordingGear;
static int gearRecordingFailed;

static void RecordGearCommand(unsigned int type, GLenum mode,
                              GLfloat x, GLfloat y, GLfloat z)
{
	struct GearCommand *command;
	if (gearRecordingFailed)
		return;
	if (recordingGear->count == recordingGear->capacity) {
		unsigned int capacity = recordingGear->capacity ?
			recordingGear->capacity * 2U : 1024U;
		struct GearCommand *commands = realloc(recordingGear->commands,
			capacity * sizeof(*commands));
		if (!commands) {
			gearRecordingFailed = 1;
			return;
		}
		recordingGear->commands = commands;
		recordingGear->capacity = capacity;
	}
	command = &recordingGear->commands[recordingGear->count++];
	command->type = type;
	command->mode = mode;
	command->x = x;
	command->y = y;
	command->z = z;
}

/* Record this client's geometry, not the differently rounded static variant. */
#undef glBegin
#undef glNormal3f
#undef glVertex3f
#undef glEnd
#define glBegin(mode) RecordGearCommand(GEAR_BEGIN, (mode), 0, 0, 0)
#define glNormal3f(x,y,z) RecordGearCommand(GEAR_NORMAL, 0, (x), (y), (z))
#define glVertex3f(x,y,z) RecordGearCommand(GEAR_VERTEX, 0, (x), (y), (z))
#define glEnd() RecordGearCommand(GEAR_END, 0, 0, 0, 0)
#endif

void
flat_face(float ir, float or, float wd)
{
  int i;
  float w;

  if (wd == 0.0)
	return;
  for (w = wd / 2; w > -wd; w -= wd) {
	if (w > 0.0)
	  glNormal3f(0.0, 0.0, 1.0);
	else
	  glNormal3f(0.0, 0.0, -1.0);

	if (ir == 0.0) {
	  glBegin(GL_TRIANGLE_FAN);
	  glVertex3f(0.0, 0.0, w);
	  glVertex3f(or, 0.0, w);
	  for (i = 1; i < circle_subdiv; i++) {
		glVertex3f(fcos(2.0 * M_PI * i / (float)circle_subdiv) * or,
		  fsin(2.0 * M_PI * i / (float)circle_subdiv) * or,
		  w);
	  }
	  glVertex3f(or, 0.0, w);
	  glEnd();
	} else {
	  glBegin(GL_TRIANGLE_STRIP);
	  glVertex3f(or, 0.0, w);
	  glVertex3f(ir, 0.0, w);
	  for (i = 1; i < circle_subdiv; i++) {
		glVertex3f(fcos(2.0 * M_PI * i / (float)circle_subdiv) * or,
		  fsin(2.0 * M_PI * i / (float)circle_subdiv) * or,
		  w);
		glVertex3f(fcos(2.0 * M_PI * i / (float)circle_subdiv) * ir,
		  fsin(2.0 * M_PI * i / (float)circle_subdiv) * ir,
		  w);
	  }
	  glVertex3f(or, 0.0, w);
	  glVertex3f(ir, 0.0, w);
	  glEnd();
	}
  }
}

void
draw_inside(float w1, float w2, float rad)
{
  int i, j;
  float c, s;
  if (w1 == w2)
	return;

  w1 = w1 / 2;
  w2 = w2 / 2;
  for (j = 0; j < 2; j++) {
	if (j == 1) {
	  w1 = -w1;
	  w2 = -w2;
	}
	glBegin(GL_TRIANGLE_STRIP);
	glNormal3f(-1.0, 0.0, 0.0);
	glVertex3f(rad, 0.0, w1);
	glVertex3f(rad, 0.0, w2);
	for (i = 1; i < circle_subdiv; i++) {
	  c = fcos(2.0 * M_PI * i / circle_subdiv);
	  s = fsin(2.0 * M_PI * i / circle_subdiv);
	  glNormal3f(-c, -s, 0.0);
	  glVertex3f(c * rad, s * rad, w1);
	  glVertex3f(c * rad, s * rad, w2);
	}
	glNormal3f(-1.0, 0.0, 0.0);
	glVertex3f(rad, 0.0, w1);
	glVertex3f(rad, 0.0, w2);
	glEnd();
  }
}

void
draw_outside(float w1, float w2, float rad)
{
  int i, j;
  float c, s;
  if (w1 == w2)
	return;

  w1 = w1 / 2;
  w2 = w2 / 2;
  for (j = 0; j < 2; j++) {
	if (j == 1) {
	  w1 = -w1;
	  w2 = -w2;
	}
	glBegin(GL_TRIANGLE_STRIP);
	glNormal3f(1.0, 0.0, 0.0);
	glVertex3f(rad, 0.0, w1);
	glVertex3f(rad, 0.0, w2);
	for (i = 1; i < circle_subdiv; i++) {
	  c = fcos(2.0 * M_PI * i / circle_subdiv);
	  s = fsin(2.0 * M_PI * i / circle_subdiv);
	  glNormal3f(c, s, 0.0);
	  glVertex3f(c * rad, s * rad, w1);
	  glVertex3f(c * rad, s * rad, w2);
	}
	glNormal3f(1.0, 0.0, 0.0);
	glVertex3f(rad, 0.0, w1);
	glVertex3f(rad, 0.0, w2);
	glEnd();
  }
}

void
tooth_side(int nt, float ir, float or, float tp, float tip, float wd)
{
  float i;
  float end = 2.0 * M_PI / nt;
  float x[6], y[6];
  float s[3], c[3];

  or = or * ir;
  for (i = 0; i < 2.0 * M_PI - end / 4.0; i += end) {

	c[0] = fcos(i);
	s[0] = fsin(i);
	c[1] = fcos(i + end * (0.5 - tip / 2));
	s[1] = fsin(i + end * (0.5 - tip / 2));
	c[2] = fcos(i + end * (0.5 + tp / 2));
	s[2] = fsin(i + end * (0.5 + tp / 2));

	x[0] = ir * c[0];
	y[0] = ir * s[0];
	x[5] = ir * fcos(i + end);
	y[5] = ir * fsin(i + end);
	x[1] = x[0] + (x[5] - x[0]) * (0.5 - tp / 2);
	y[1] = y[0] + (y[5] - y[0]) * (0.5 - tp / 2);
	x[4] = x[0] + (x[5] - x[0]) * (0.5 + tp / 2);
	y[4] = y[0] + (y[5] - y[0]) * (0.5 + tp / 2);
	x[2] = or * fcos(i + end * (0.5 - tip / 2));
	y[2] = or * fsin(i + end * (0.5 - tip / 2));
	x[3] = or * fcos(i + end * (0.5 + tip / 2));
	y[3] = or * fsin(i + end * (0.5 + tip / 2));

	glNormal3f(0.0, 0.0, 1.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[2], y[2], wd / 2);
	glVertex3f(x[1], y[1], wd / 2);
	glVertex3f(x[3], y[3], wd / 2);
	glVertex3f(x[4], y[4], wd / 2);
	glEnd();

	glNormal3f(0.0, 0.0, -1.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[2], y[2], -wd / 2);
	glVertex3f(x[1], y[1], -wd / 2);
	glVertex3f(x[3], y[3], -wd / 2);
	glVertex3f(x[4], y[4], -wd / 2);
	glEnd();

	glNormal3f(c[0], s[0], 0.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[0], y[0], -wd / 2);
	glVertex3f(x[1], y[1], -wd / 2);
	glVertex3f(x[0], y[0], wd / 2);
	glVertex3f(x[1], y[1], wd / 2);
	glEnd();

	{
	  float a, b, n;
	  a = x[2] - x[1];
	  b = y[2] - y[1];
	  n = 1.0 / fsqrt(a * a + b * b);
	  a = a * n;
	  b = b * n;
	  glNormal3f(b, -a, 0.0);
	}
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[1], y[1], -wd / 2);
	glVertex3f(x[2], y[2], -wd / 2);
	glVertex3f(x[1], y[1], wd / 2);
	glVertex3f(x[2], y[2], wd / 2);
	glEnd();

	glNormal3f(c[1], s[1], 0.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[2], y[2], -wd / 2);
	glVertex3f(x[3], y[3], -wd / 2);
	glVertex3f(x[2], y[2], wd / 2);
	glVertex3f(x[3], y[3], wd / 2);
	glEnd();

	{
	  float a, b, cc;
	  a = x[4] - x[3];
	  b = y[4] - y[3];
	  cc = 1.0 / fsqrt(a * a + b * b);
	  a = a * cc;
	  b = b * cc;
	  glNormal3f(b, -a, 0.0);
	}
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[3], y[3], -wd / 2);
	glVertex3f(x[4], y[4], -wd / 2);
	glVertex3f(x[3], y[3], wd / 2);
	glVertex3f(x[4], y[4], wd / 2);
	glEnd();

	glNormal3f(c[2], s[2], 0.0);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(x[4], y[4], -wd / 2);
	glVertex3f(x[5], y[5], -wd / 2);
	glVertex3f(x[4], y[4], wd / 2);
	glVertex3f(x[5], y[5], wd / 2);
	glEnd();
  }
}

void
gear(int nt, float wd, float ir, float or, float tp, float tip, int ns, Profile * ip)
{
  float prev;
  int k, t;

  if (nt <= 0)
	circle_subdiv = MIN_SUBDIV;
  else {
	circle_subdiv = nt;
	while (circle_subdiv < MIN_SUBDIV)
	  circle_subdiv += nt;
  }

  if (ns <= 0) {
	flat_face(0.0, ir, wd);
  } else {
	if (ip[0].rad > 0.0) {
	  flat_face(0.0, ip[0].rad * ir, wd);
	  prev = wd;
	  t = 0;
	} else {
	  flat_face(0.0, ip[1].rad * ir, ip[0].wid * wd);
	  prev = ip[0].wid;
	  t = 1;
	}
	for (k = t; k < ns; k++) {
	  if (prev < ip[k].wid) {
		draw_inside(prev * wd, ip[k].wid * wd, ip[k].rad * ir);
	  } else {
		draw_outside(prev * wd, ip[k].wid * wd, ip[k].rad * ir);
	  }
	  prev = ip[k].wid;
	  if (k == ns - 1) {
		flat_face(ip[k].rad * ir, ir, ip[k].wid * wd);
		if (ip[k].wid < 1.0) {
		  draw_inside(ip[k].wid * wd, wd, ir);
		} else {
		  draw_outside(ip[k].wid * wd, wd, ir);
		}
	  } else {
		flat_face(ip[k].rad * ir, ip[k + 1].rad * ir, ip[k].wid * wd);
	  }
	}
  }

  tooth_side(nt, ir, or, tp, tip, wd);
}

#ifdef MGL_GEARS_PRECALC
#undef glBegin
#undef glNormal3f
#undef glVertex3f
#undef glEnd
#define glBegin MGLD_glBegin
#define glNormal3f MGLD_glNormal3f
#define glVertex3f MGLD_glVertex3f
#define glEnd MGLD_glEnd

static void DrawPrecalculatedGear(unsigned int gearIndex)
{
	unsigned int index;
	const struct GearCommands *buffer = &precalculatedGears[gearIndex];
	for (index = 0; index < buffer->count; ++index) {
		const struct GearCommand *command = &buffer->commands[index];
		switch (command->type) {
		case GEAR_BEGIN: glBegin(command->mode); break;
		case GEAR_NORMAL: glNormal3f(command->x, command->y, command->z); break;
		case GEAR_VERTEX: glVertex3f(command->x, command->y, command->z); break;
		case GEAR_END: glEnd(); break;
		}
	}
}
#endif

Profile gear_profile[] =
{ {0.000, 0.0},
  {0.300, 7.0},
  {0.340, 0.4},
  {0.550, 0.64},
  {0.600, 0.4},
  {0.950, 1.0}
};

/* ================= Demo-specific setup (not from the original) ================= */

/* Real external PPM (P6) loader -- verbatim port of the original gears.c's
 * own LoadPPM/TexInit, matching milestone_gears.c's own copy exactly. */
static GLubyte *LoadPPM(char *name, GLint *w, GLint *h)
{
	int i;
	unsigned long x, y, maxval;
	FILE *f;
	GLubyte *where;

	f = fopen(name, "r");
	if (!f)
	{
		*w = 0; *h = 0;
		return NULL;
	}

	/* Two real header-parsing bugs fixed here -- see milestone_gears.c's
	 * own copy of this function for the full writeup: (1) maxval is now
	 * read as a real number and verified in code instead of a fragile
	 * literal "255" match in the scanf format, which could silently leave
	 * the file position misaligned if a real deployed file's header ever
	 * differs even slightly from what was tested during porting; (2) the
	 * exactly-one required separator byte before binary pixel data is now
	 * consumed explicitly via fgetc(), not a greedy `\n` scanf directive
	 * that could over-consume into real (dark, low-byte-value) pixel
	 * data. */
	i = fscanf(f, "P6\n%lu %lu\n%lu", &x, &y, &maxval);
	if (i != 3 || maxval != 255)
	{
		D(("milestone_gears_stats_library: LoadPPM: error scanning PPM header in %s (i=%ld maxval=%lu)\n", name, (LONG)i, maxval));
		fclose(f);
		*w = 0; *h = 0;
		return NULL;
	}
	fgetc(f); /* the exactly-one required separator byte before binary data */

	*w = x;
	*h = y;

	where = malloc(x * y * 3);
	if (!where)
	{
		D(("milestone_gears_stats_library: LoadPPM: out of memory loading %s\n", name));
		fclose(f);
		*w = 0; *h = 0;
		return NULL;
	}

	i = fread(where, 1, x * y * 3, f);
	fclose(f);

	if ((unsigned long)i != x * y * 3)
	{
		D(("milestone_gears_stats_library: LoadPPM: short read on %s\n", name));
		free(where);
		*w = 0; *h = 0;
		return NULL;
	}

	return where;
}

static float a1 = 27.0f, a2 = 67.0f, a3 = 47.0f, a4 = 87.0f;
static const float i1 = 1.2f, i2 = 3.1f, i3 = 2.3f, i4 = 1.1f;

/* Live, on-screen FPS overlay -- the original gears.c's own bShowFPS
 * mechanism, ported to 68k instead of the PPC-only GetSysTimePPC/
 * SubTimePPC it used. DateStamp()'s own resolution is 1/50th sec (one
 * "tick") -- too coarse to measure a single frame meaningfully (a fast
 * frame would read back 0 elapsed ticks). Instead accumulate a small
 * window of frames (FPS_SAMPLE_FRAMES) and recompute/redraw only every
 * Nth frame, giving a stable reading updated periodically throughout
 * the run rather than a single end-of-run number. */
#define FPS_SAMPLE_FRAMES 30
static struct DateStamp g_fps_ds;
static struct DateStamp g_run_ds;
static int g_fps_frame_count = 0;
static char g_fps_text[40] = "fps: --";

static void UpdateFPSOverlay(void)
{
	struct DateStamp now_ds;
	LONG elapsed_ticks;
	struct Window *win;

	g_fps_frame_count++;
	if (g_fps_frame_count < FPS_SAMPLE_FRAMES)
		return;

	DateStamp(&now_ds);
	elapsed_ticks = (now_ds.ds_Days   - g_fps_ds.ds_Days)   * 1440L * 3000L
	              + (now_ds.ds_Minute - g_fps_ds.ds_Minute) * 3000L
	              + (now_ds.ds_Tick   - g_fps_ds.ds_Tick);

	if (elapsed_ticks > 0)
	{
		/* fps = frames * 50 ticks/sec / elapsed_ticks -- integer math,
		 * one decimal place (fps*10 first, then split). */
		LONG fps_x10 = ((LONG)FPS_SAMPLE_FRAMES * 500L) / elapsed_ticks;
		sprintf(g_fps_text, "fps: %ld.%ld", fps_x10 / 10L, fps_x10 % 10L);
	}

	g_fps_ds = now_ds;
	g_fps_frame_count = 0;
	win = (struct Window *)mglGetWindowHandle();

	if (win)
	{
		SetWindowTitles(win, g_fps_text, (UBYTE *)~0UL);
	}
#if MGL_GEARS_DIAG
	else
	{
		printf("\r%s   ", g_fps_text);
		fflush(stdout);
	}
#endif
}

static void oneFrame(void)
{
	/* Required by the bundled Warp3D backend; a local context check on R200. */
	mglLockDisplay();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (show & 1)
	{
		glPushMatrix();
			glTranslatef(0.0f, 0.0f, -4.0f);
			glRotatef(a3, 1.0f, 1.0f, 1.0f);
			glRotatef(a4, 0.0f, 0.0f, -1.0f);
			glTranslatef(0.14f, 0.2f, 0.0f);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#ifdef MGL_GEARS_PRECALC
			DrawPrecalculatedGear(0);
#else
			gear(40, 0.4f, 2.0f, 1.1f, 0.8f, 0.4f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
#endif
		glPopMatrix();
	}

	if (show & 2)
	{
		glPushMatrix();
			glTranslatef(0.1f, 0.2f, -3.8f);
			glRotatef(a2, -4.0f, 2.0f, -1.0f);
			glRotatef(a1, 1.0f, -3.0f, 1.0f);
			glTranslatef(0.0f, -0.2f, 0.0f);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor3f(1.0f, 0.8f, 0.0f);
#ifdef MGL_GEARS_PRECALC
			DrawPrecalculatedGear(1);
#else
			gear(36, 0.4f, 2.0f, 1.1f, 0.7f, 0.2f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
#endif
		glPopMatrix();
	}

	a1 += i1; if (a1 > 360.0f) a1 -= 360.0f;
	a2 += i2; if (a2 > 360.0f) a2 -= 360.0f;
	a3 += i3; if (a3 > 360.0f) a3 -= 360.0f;
	a4 += i4; if (a4 > 360.0f) a4 -= 360.0f;

	mglSwitchDisplay();
}


static void interactiveFrame(void)
{
	oneFrame();
	if (bShowFPS)
		UpdateFPSOverlay();
}

/* Real keyboard handler -- see milestone_gears.c's own comment on the
 * identical function. 'f' has a real effect HERE (gates whether oneFrame
 * calls Update/DrawFPSOverlay above), unlike milestone_gears.c where
 * there's no overlay code to gate at all. */

/* Per-line marker log on DH2: a wedge in teardown loses buffered stdio,
 * and this is the only way to see which of mglMainLoop/mglDeleteContext/
 * MiniGLClose never returned. */
static void MarkLog(const char *line)
{
#if MGL_GEARS_DIAG
	BPTR fh = Open((STRPTR)"DH2:wosbuild/gears_client.log", MODE_OLDFILE);
	if (!fh)
		return;
	Seek(fh, 0, OFFSET_END);
	Write(fh, (APTR)line, (LONG)strlen(line));
	Close(fh);
#else
	(void)line;
#endif
}

static void keys(char c)
{
	if (c == 27)
	{
		D(("milestone_gears_stats_library: ESC pressed, exiting\n"));
		mglExit();
	}

	if (c == '1') show = 1;
	if (c == '2') show = 2;
	if (c == '3') show = 3;

	if (c == 'f') bShowFPS = !bShowFPS;

	if (c == 'e')
	{
		if (bEnvMap == GL_TRUE)
		{
			bEnvMap = GL_FALSE;
			glDisable(GL_TEXTURE_GEN_S);
			glDisable(GL_TEXTURE_GEN_T);
		}
		else
		{
			bEnvMap = GL_TRUE;
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);
		}
	}
}

int main(int argc, char *argv[])
{
	GLint width = MILESTONE_WIDTH, height = MILESTONE_HEIGHT;
	int depth = 32;
	int buffers = 2;
	int noSync = 0;
	int i, result = 20, opened = 0;
	char *name = 0;
	GLuint texname;
	double d_near = 1.0, d_far = 2000.0;
	GLboolean windowed = GL_FALSE;
	GLenum error;

	for (i = 1; i < argc; i++)
	{
		const char *option = argv[i];
		if (0 == local_stricmp(option, "-window")) {
			windowed = GL_TRUE;
			continue;
		}
		if (0 == local_stricmp(option, "-nosync")) {
			noSync = 1;
			continue;
		}
		if (0 == local_stricmp(option, "-nofps")) {
			bShowFPS = 0;
			continue;
		}
		if (++i == argc)
			goto usage;
		if (0 == local_stricmp(option, "-width"))
			width = atoi(argv[i]);
		else if (0 == local_stricmp(option, "-height"))
			height = atoi(argv[i]);
		else if (0 == local_stricmp(option, "-depth"))
			depth = atoi(argv[i]);
		else if (0 == local_stricmp(option, "-buffers"))
			buffers = atoi(argv[i]);
		else if (0 == local_stricmp(option, "-envmap"))
			name = argv[i];
		else if (0 == local_stricmp(option, "-frames")) {
			frameLimit = atoi(argv[i]);
			if (frameLimit <= 0 || frameLimit > 1000000)
				goto usage;
		} else
			goto usage;
	}
	if (width <= 0 || height <= 0 || (depth != 16 && depth != 32) ||
	    buffers < 1 || buffers > 3)
		goto usage;

	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0);
	if (!GfxBase) {
		printf("Can't open graphics.library\n");
		goto cleanup;
	}

	if (MiniGLOpen() == GL_FALSE)
	{
		D(("milestone_gears_stats_library: could not open minigl_ppc.dll\n"));
		goto cleanup;
	}
	opened = 1;

	if (!frameLimit) {
		IntuitionBase = (struct IntuitionBase *)
			OpenLibrary((CONST_STRPTR)"intuition.library", 39UL);
		if (!IntuitionBase) {
			D(("milestone_gears_stats_library: no intuition.library\n"));
			goto cleanup;
		}
	}

	if (windowed)
		mglChooseWindowMode(GL_TRUE);
	mglChooseVertexBufferSize(1000);
	mglChoosePixelDepth(depth);
	mglChooseNumberOfBuffers(buffers);

	if (!mglCreateContext(0, 0, width, height))
	{
		D(("milestone_gears_stats_library: mglCreateContext failed\n"));
		goto cleanup;
	}

	/* R200 defaults to blit when sync is off; MINIGL_PRESENT can override
	 * the native host's present mode. Unsynced FPS is throughput, not refresh. */
	mglEnableSync(noSync ? GL_FALSE : GL_TRUE);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	{
		GLint tw, th;
		GLubyte *tmap = LoadPPM(name ? name : "data/chrome.ppm", &tw, &th);

		if (!tmap)
		{
			/* Fails the same way the original does: no fallback texture,
			 * no MGLMainLoop entry, just tear down cleanly. */
			D(("milestone_gears_stats_library: can't find texture %s\n", name ? name : "data/chrome.ppm"));
			goto cleanup;
		}
		else
		{
			glGenTextures(1, &texname);
			glBindTexture(GL_TEXTURE_2D, texname);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tw, th,
			             0, GL_RGB, GL_UNSIGNED_BYTE, tmap);
			free(tmap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			glEnable(GL_TEXTURE_2D);
			glEnable(GL_TEXTURE_GEN_S);
			glEnable(GL_TEXTURE_GEN_T);
			glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glColor3f(1.0f, 0.0f, 0.0f);

			glDisable(GL_CULL_FACE);
			glEnable(GL_DEPTH_TEST);

			glViewport(0, 0, width, height);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glFrustum(-1.0, 1.0, -1.0, 1.0, d_near, d_far);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();

			mglLockMode(MGL_LOCK_MANUAL);

#ifdef MGL_GEARS_PRECALC
			recordingGear = &precalculatedGears[0];
			gear(40, 0.4f, 2.0f, 1.1f, 0.8f, 0.4f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
			recordingGear = &precalculatedGears[1];
			gear(36, 0.4f, 2.0f, 1.1f, 0.7f, 0.2f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
			recordingGear = NULL;
			if (gearRecordingFailed) {
				puts("gears: precalculation allocation failed");
				goto cleanup;
			}
#endif
			glFinish();
			error = glGetError();
			if (error != GL_NO_ERROR) {
				printf("gears: setup error=%lu\n", (unsigned long)error);
				goto cleanup;
			}

			if (frameLimit) {
				struct DateStamp run_ds;
				LONG run_ticks, fps_x1000;
				/* No event, title, timer or logging calls in the fixed loop.
				 * Finish includes the final asynchronous PRESENT in wall time. */
				DateStamp(&g_run_ds);
				for (framesDone = 0; framesDone < frameLimit; ++framesDone)
					oneFrame();
				glFinish();
				DateStamp(&run_ds);
				error = glGetError();
				run_ticks = (run_ds.ds_Days - g_run_ds.ds_Days)
					    * 1440L * 3000L
					  + (run_ds.ds_Minute - g_run_ds.ds_Minute)
					    * 3000L
					  + (run_ds.ds_Tick - g_run_ds.ds_Tick);
				fps_x1000 = run_ticks > 0 ?
					(LONG)((unsigned long long)framesDone * 50000ULL /
					       (unsigned long)run_ticks) : 0;
				printf("GEARS_RUN frames=%d ticks=%ld fps=%ld.%03ld "
				       "variant=ppc-dll precalc=%d mode=%s depth=%d "
				       "buffers=%d sync=%d error=%lu\n",
				       framesDone, (long)run_ticks,
				       (long)(fps_x1000 / 1000L), (long)(fps_x1000 % 1000L),
#ifdef MGL_GEARS_PRECALC
				       1,
#else
				       0,
#endif
				       windowed ? "window" : "fullscreen", depth,
				       windowed ? 0 : buffers, !noSync, (unsigned long)error);
				fflush(stdout);
				result = error == GL_NO_ERROR && run_ticks > 0 ? 0 : 20;
			} else {
				DateStamp(&g_fps_ds);
				g_fps_frame_count = 0;
				mglIdleFunc(interactiveFrame);
				mglKeyFunc(keys);
				D(("gears: ESC quits, 1/2/3 selects gears, f toggles FPS, e toggles envmap\n"));
				MarkLog("mainloop enter\n");
				mglMainLoop();
				MarkLog("mainloop returned\n");
				result = 0;
			}
		}
	}

cleanup:
	if (opened) {
		MarkLog("before mglDeleteContext\n");
		mglDeleteContext();
		MarkLog("after mglDeleteContext\n");
		MiniGLClose();
		MarkLog("after MiniGLClose\n");
	}
#ifdef MGL_GEARS_PRECALC
	free(precalculatedGears[0].commands);
	free(precalculatedGears[1].commands);
#endif
	if (GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	return result;

usage:
	puts("usage: gears [-frames N] [-window] [-width N] [-height N] "
	     "[-depth 16|32] [-buffers 1|2|3] [-nosync] [-nofps] [-envmap file]");
	return 20;
}
