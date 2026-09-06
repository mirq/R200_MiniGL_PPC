/*
 * Common gears benchmark for the 68k and WarpOS/PPC R200 frontends.
 * MGL_GEARS_PRECALC records each gear's immediate-mode geometry once and
 * replays the same GL command stream every frame. Matrix work, state changes,
 * submission, rendering, and presentation remain identical to the live-math
 * variant. MGL_GEARS_ALTIVEC identifies the AltiVec frontend build.
 */

#define __USE_NEW_TIMEVAL__

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <devices/timer.h>
#include <proto/timer.h>
#include <libraries/minigl_perf.h>
#ifdef MINIGLPPC_PROFILE
#include <libraries/minigl_ppc_profile.h>
#endif
#ifdef MINIGLPPC_SEMANTIC_PROFILE
#include <libraries/minigl_ppc_semantic_profile.h>

static ULONG SemanticProfileMicros(ULONG ticks, ULONG rate)
{
    if (!rate) return 0;
    return (ULONG)(((unsigned long long)ticks * 1000000ULL) / rate);
}
#endif
#ifdef MGL_GEARS_ALTIVEC
#include <libraries/minigl_ppc_altivec.h>
#endif

#ifndef D
#define D(x) do { printf x; } while (0)
#endif


#ifndef M_PI
#define M_PI 3.14159265
#endif

#define fcos  cos
#define fsin  sin
#define fsqrt sqrt

#if !defined(__GNUC__) && !defined(MINIGLPPC_SEMANTIC)
struct GfxBase *GfxBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
#endif

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

#define MILESTONE_WIDTH 640
#define MILESTONE_HEIGHT 480

static int show = 1;
static int bShowFPS = 0;
static GLint g_render_width = MILESTONE_WIDTH;
static GLint g_render_height = MILESTONE_HEIGHT;
static GLboolean bEnvMap = GL_TRUE;
static GLboolean running = GL_TRUE;
static GLboolean benchmarkActive = GL_FALSE;
static GLboolean g_windowed = GL_FALSE;

#ifndef MINIGLPPC_SEMANTIC
struct Device *TimerBase = NULL;
#endif

typedef struct {
	struct MsgPort port;
	TimeRequest_Type request;
} BenchmarkTimer;

typedef struct {
	GLint frames;
	GLint primitives;
	GLint inputVertices;
	GLint submittedVertices;
	GLint drawRecords;
	GLint clearRecords;
	GLint submits;
	GLint recordDwords;
	GLint generatedDwords;
	GLint frontendTicks;
	GLint executeTicks;
	GLint waitTicks;
	GLint presentTicks;
	GLint matrixTicks;
	GLint serializeTicks;
	GLint serializeStallTicks;
	GLint serializeKernelFlushes;
	GLint serializeKernelVertices;
	GLint sphereMapCalls;
	GLint sphereMapSamples;
	GLint sphereMapTicks;
	GLint sphereMapCacheHits;
	GLint sphereMapCacheMisses;
	GLint normalCacheHits;
	GLint normalCacheMisses;
	GLint textureResidencyTicks;
	GLint radeonCaps;
	GLint nativeAttempts;
	GLint nativeUsed;
	GLint nativeFallbackCaps;
	GLint nativeFallbackCull;
	GLint nativeFallbackFlat;
	GLint nativeFallbackSize;
	GLint nativeFallbackClip;
	GLint quadAttempts;
	GLint quadUsed;
	GLint quadFallbackCaps;
	GLint quadFallbackState;
	GLint quadFallbackClip;
	GLint asyncSubmits;
	GLint fenceWaits;
	GLint fencePollHits;
	GLint bufferWaits;
	GLint textureWaits;
	GLint drainWaits;
} BenchmarkPerf;

/* Client-side timing complements the R200 counters.  The backend measures
 * emission/submission/present work, but the legacy gears workload also spends
 * substantial time generating geometry and making immediate-mode calls. */
typedef struct {
	ULONG frameTicks;
	ULONG eventTicks;
	ULONG clearTicks;
	ULONG sceneTicks;
	ULONG overlayTicks;
	ULONG switchTicks;
	ULONG immediateCalls;
	ULONG immediateSamples;
	ULONG immediateTicks;
	ULONG stateCalls;
	ULONG stateTicks;
	ULONG timerOverheadTicks;
} BenchmarkProfile;

static GLboolean benchmarkProfiling = GL_FALSE;
static GLboolean stageProfiling = GL_FALSE;
static volatile GLboolean benchmarkApplicationProbe = GL_FALSE;
static BenchmarkProfile benchmarkProfile;

#if defined(__PPC__) && defined(MGL_GEARS_ALTIVEC)
static const char *benchmarkVariant = "ppc-altivec";
#elif defined(__PPC__) && defined(MGL_GEARS_PRECALC)
static const char *benchmarkVariant = "ppc-precalc";
#elif defined(__PPC__)
static const char *benchmarkVariant = "ppc";
#elif defined(MGL_GEARS_PRECALC)
static const char *benchmarkVariant = "68k-precalc";
#else
static const char *benchmarkVariant = "68k";
#endif

#ifdef MGL_GEARS_PRECALC
enum GearCommandType {
	GEAR_COMMAND_BEGIN,
	GEAR_COMMAND_NORMAL,
	GEAR_COMMAND_VERTEX,
	GEAR_COMMAND_END
};

typedef struct {
	ULONG type;
	GLenum mode;
	GLfloat x, y, z;
} GearCommand;

typedef struct {
	GearCommand *commands;
	ULONG count;
	ULONG capacity;
} GearCommandBuffer;

static GearCommandBuffer *recordingGear;
static GearCommandBuffer precalculatedGears[2];
static GLboolean gearRecordingFailed;

static void RecordGearCommand(ULONG type, GLenum mode,
	                          GLfloat x, GLfloat y, GLfloat z)
{
	GearCommand *commands;
	ULONG capacity;

	if (!recordingGear || gearRecordingFailed) return;
	if (recordingGear->count == recordingGear->capacity) {
		capacity = recordingGear->capacity ? recordingGear->capacity * 2UL : 1024UL;
		commands = (GearCommand *)realloc(recordingGear->commands,
		                                 capacity * sizeof(*commands));
		if (!commands) {
			gearRecordingFailed = GL_TRUE;
			return;
		}
		recordingGear->commands = commands;
		recordingGear->capacity = capacity;
	}
	commands = &recordingGear->commands[recordingGear->count++];
	commands->type = type;
	commands->mode = mode;
	commands->x = x;
	commands->y = y;
	commands->z = z;
}
#endif

static void InitExecList(struct List *list)
{
	list->lh_Head = (struct Node *)&list->lh_Tail;
	list->lh_Tail = NULL;
	list->lh_TailPred = (struct Node *)&list->lh_Head;
}

static GLboolean OpenBenchmarkTimer(BenchmarkTimer *timer)
{
	memset(timer, 0, sizeof(*timer));
	timer->port.mp_Node.ln_Type = NT_MSGPORT;
	timer->port.mp_Flags = PA_IGNORE;
	InitExecList(&timer->port.mp_MsgList);
	timer->request.tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
	timer->request.tr_node.io_Message.mn_ReplyPort = &timer->port;
	timer->request.tr_node.io_Message.mn_Length = sizeof(timer->request);
	if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK,
	               (struct IORequest *)&timer->request, 0))
		return GL_FALSE;
	TimerBase = timer->request.tr_node.io_Device;
	return GL_TRUE;
}

static ULONG ReadClockLow(void)
{
	struct EClockVal value;
	ReadEClock(&value);
	return value.ev_lo;
}

static ULONG ReadClockRate(void)
{
	struct EClockVal value;
	return ReadEClock(&value);
}

#if defined(__PPC__) && defined(__VBCC__)
/* WarpOS ReadEClock crosses to the 68k. Keep it for wall time and timeline
 * synchronization, but sample hot profile sections on the PPC itself. */
static ULONG ReadProfileClockLow(void) = "\tmftb\tr3";
#else
static ULONG ReadProfileClockLow(void)
{
	return ReadClockLow();
}
#endif

static void AddProfileTicks(ULONG *total, ULONG start)
{
	if (benchmarkProfiling)
		*total += ReadProfileClockLow() - start;
}

static ULONG ProfileStart(void)
{
	return benchmarkProfiling ? ReadProfileClockLow() : 0;
}

static ULONG EstimateProfileTicks(ULONG ticks, ULONG samples, ULONG calls,
	                              ULONG timerOverhead)
{
	ULONG overhead;
	if (!samples || !calls) return 0;
	overhead = samples * timerOverhead;
	if (ticks <= overhead) return 0;
	return (ULONG)((double)(ticks - overhead) * (double)calls /
	               (double)samples + 0.5);
}

static ULONG ImmediateProfileStart(void)
{
	if (!benchmarkProfiling || stageProfiling) return 0;
	benchmarkProfile.immediateCalls++;
	if ((benchmarkProfile.immediateCalls & 63UL) != 0) return 0;
	benchmarkProfile.immediateSamples++;
	return ReadProfileClockLow();
}

static void ImmediateProfileEnd(ULONG start)
{
	if (start) benchmarkProfile.immediateTicks += ReadProfileClockLow() - start;
}

static ULONG StateProfileStart(void)
{
	if (!benchmarkProfiling || stageProfiling) return 0;
	benchmarkProfile.stateCalls++;
	return ReadProfileClockLow();
}

static void StateProfileEnd(ULONG start)
{
	if (start) benchmarkProfile.stateTicks += ReadProfileClockLow() - start;
}

static __inline__ void ProfiledBegin(GLenum mode)
{
	ULONG start = ImmediateProfileStart();
	if (benchmarkApplicationProbe) return;
#ifdef MGL_GEARS_PRECALC
	if (recordingGear) {
		RecordGearCommand(GEAR_COMMAND_BEGIN, mode, 0.0f, 0.0f, 0.0f);
		return;
	}
#endif
	glBegin(mode);
	ImmediateProfileEnd(start);
}

static __inline__ void ProfiledNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
	ULONG start = ImmediateProfileStart();
	if (benchmarkApplicationProbe) return;
#ifdef MGL_GEARS_PRECALC
	if (recordingGear) {
		RecordGearCommand(GEAR_COMMAND_NORMAL, 0, x, y, z);
		return;
	}
#endif
	glNormal3f(x, y, z);
	ImmediateProfileEnd(start);
}

static __inline__ void ProfiledVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	ULONG start = ImmediateProfileStart();
	if (benchmarkApplicationProbe) return;
#ifdef MGL_GEARS_PRECALC
	if (recordingGear) {
		RecordGearCommand(GEAR_COMMAND_VERTEX, 0, x, y, z);
		return;
	}
#endif
	glVertex3f(x, y, z);
	ImmediateProfileEnd(start);
}

static __inline__ void ProfiledEnd(void)
{
	if (benchmarkApplicationProbe) return;
#ifdef MGL_GEARS_PRECALC
	if (recordingGear) {
		RecordGearCommand(GEAR_COMMAND_END, 0, 0.0f, 0.0f, 0.0f);
		return;
	}
#endif
	glEnd();
}

static __inline__ void ProfiledPushMatrix(void)
{
	ULONG start = StateProfileStart();
	glPushMatrix();
	StateProfileEnd(start);
}

static __inline__ void ProfiledPopMatrix(void)
{
	ULONG start = StateProfileStart();
	glPopMatrix();
	StateProfileEnd(start);
}

static __inline__ void ProfiledTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	ULONG start = StateProfileStart();
	glTranslatef(x, y, z);
	StateProfileEnd(start);
}

static __inline__ void ProfiledRotatef(GLfloat angle, GLfloat x, GLfloat y,
	                                  GLfloat z)
{
	ULONG start = StateProfileStart();
	glRotatef(angle, x, y, z);
	StateProfileEnd(start);
}

static __inline__ void ProfiledTexEnvi(GLenum target, GLenum pname, GLint value)
{
	ULONG start = StateProfileStart();
	glTexEnvi(target, pname, value);
	StateProfileEnd(start);
}

static __inline__ void ProfiledColor3f(GLfloat r, GLfloat g, GLfloat b)
{
	ULONG start = StateProfileStart();
	glColor3f(r, g, b);
	StateProfileEnd(start);
}

static void ReadPerf(BenchmarkPerf *perf)
{
	glGetIntegerv(MGL_PERF_FRAMES, &perf->frames);
	glGetIntegerv(MGL_PERF_PRIMITIVES, &perf->primitives);
	glGetIntegerv(MGL_PERF_INPUT_VERTICES, &perf->inputVertices);
	glGetIntegerv(MGL_PERF_SUBMITTED_VERTICES, &perf->submittedVertices);
	glGetIntegerv(MGL_PERF_DRAW_RECORDS, &perf->drawRecords);
	glGetIntegerv(MGL_PERF_CLEAR_RECORDS, &perf->clearRecords);
	glGetIntegerv(MGL_PERF_SUBMITS, &perf->submits);
	glGetIntegerv(MGL_PERF_RECORD_DWORDS, &perf->recordDwords);
	glGetIntegerv(MGL_PERF_GENERATED_DWORDS, &perf->generatedDwords);
	glGetIntegerv(MGL_PERF_FRONTEND_TICKS, &perf->frontendTicks);
	glGetIntegerv(MGL_PERF_EXECUTE_TICKS, &perf->executeTicks);
	glGetIntegerv(MGL_PERF_WAIT_TICKS, &perf->waitTicks);
	glGetIntegerv(MGL_PERF_PRESENT_TICKS, &perf->presentTicks);
	glGetIntegerv(MGL_PERF_MATRIX_TICKS, &perf->matrixTicks);
	glGetIntegerv(MGL_PERF_SERIALIZE_TICKS, &perf->serializeTicks);
	glGetIntegerv(MGL_PERF_SERIALIZE_STALL_TICKS, &perf->serializeStallTicks);
	glGetIntegerv(MGL_PERF_SERIALIZE_KERNEL_FLUSHES,
	              &perf->serializeKernelFlushes);
	glGetIntegerv(MGL_PERF_SERIALIZE_KERNEL_VERTICES,
	              &perf->serializeKernelVertices);
	glGetIntegerv(MGL_PERF_SPHEREMAP_CALLS, &perf->sphereMapCalls);
	glGetIntegerv(MGL_PERF_SPHEREMAP_SAMPLES, &perf->sphereMapSamples);
	glGetIntegerv(MGL_PERF_SPHEREMAP_TICKS, &perf->sphereMapTicks);
	glGetIntegerv(MGL_PERF_SPHEREMAP_CACHE_HITS, &perf->sphereMapCacheHits);
	glGetIntegerv(MGL_PERF_SPHEREMAP_CACHE_MISSES, &perf->sphereMapCacheMisses);
	glGetIntegerv(MGL_PERF_NORMAL_CACHE_HITS, &perf->normalCacheHits);
	glGetIntegerv(MGL_PERF_NORMAL_CACHE_MISSES, &perf->normalCacheMisses);
	glGetIntegerv(MGL_PERF_TEXTURE_RESIDENCY_TICKS, &perf->textureResidencyTicks);
	glGetIntegerv(MGL_PERF_RADEON_CAPS, &perf->radeonCaps);
	glGetIntegerv(MGL_PERF_NATIVE_ATTEMPTS, &perf->nativeAttempts);
	glGetIntegerv(MGL_PERF_NATIVE_USED, &perf->nativeUsed);
	glGetIntegerv(MGL_PERF_NATIVE_FALLBACK_CAPS, &perf->nativeFallbackCaps);
	glGetIntegerv(MGL_PERF_NATIVE_FALLBACK_CULL, &perf->nativeFallbackCull);
	glGetIntegerv(MGL_PERF_NATIVE_FALLBACK_FLAT, &perf->nativeFallbackFlat);
	glGetIntegerv(MGL_PERF_NATIVE_FALLBACK_SIZE, &perf->nativeFallbackSize);
	glGetIntegerv(MGL_PERF_NATIVE_FALLBACK_CLIP, &perf->nativeFallbackClip);
	glGetIntegerv(MGL_PERF_QUAD_ATTEMPTS, &perf->quadAttempts);
	glGetIntegerv(MGL_PERF_QUAD_USED, &perf->quadUsed);
	glGetIntegerv(MGL_PERF_QUAD_FALLBACK_CAPS, &perf->quadFallbackCaps);
	glGetIntegerv(MGL_PERF_QUAD_FALLBACK_STATE, &perf->quadFallbackState);
	glGetIntegerv(MGL_PERF_QUAD_FALLBACK_CLIP, &perf->quadFallbackClip);
	glGetIntegerv(MGL_PERF_ASYNC_SUBMITS, &perf->asyncSubmits);
	glGetIntegerv(MGL_PERF_FENCE_WAITS, &perf->fenceWaits);
	glGetIntegerv(MGL_PERF_FENCE_POLL_HITS, &perf->fencePollHits);
	glGetIntegerv(MGL_PERF_BUFFER_WAITS, &perf->bufferWaits);
	glGetIntegerv(MGL_PERF_TEXTURE_WAITS, &perf->textureWaits);
	glGetIntegerv(MGL_PERF_DRAIN_WAITS, &perf->drainWaits);
}

/* ================= Real gear geometry, verbatim from gears.c ================= */

/* Sample the dispatch-visible immediate API cost once per 64 calls. glEnd is
 * intentionally not wrapped: its backend
 * work is already split into the exclusive frontend/serialize/execute timers. */
#undef glBegin
#undef glEnd
#undef glNormal3f
#undef glVertex3f
#define glBegin ProfiledBegin
#define glEnd ProfiledEnd
#define glNormal3f ProfiledNormal3f
#define glVertex3f ProfiledVertex3f

typedef struct {
  float rad, wid;
} Profile;

int circle_subdiv;
#define MIN_SUBDIV 30

static float GearCircleCos(int index)
{
  return fcos(2.0 * M_PI * index / (float)circle_subdiv);
}

static float GearCircleSin(int index)
{
  return fsin(2.0 * M_PI * index / (float)circle_subdiv);
}

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
		glVertex3f(GearCircleCos(i) * or,
		  GearCircleSin(i) * or,
		  w);
	  }
	  glVertex3f(or, 0.0, w);
	  glEnd();
	} else {
	  glBegin(GL_TRIANGLE_STRIP);
	  glVertex3f(or, 0.0, w);
	  glVertex3f(ir, 0.0, w);
	  for (i = 1; i < circle_subdiv; i++) {
		glVertex3f(GearCircleCos(i) * or,
		  GearCircleSin(i) * or,
		  w);
		glVertex3f(GearCircleCos(i) * ir,
		  GearCircleSin(i) * ir,
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
	  c = GearCircleCos(i);
	  s = GearCircleSin(i);
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
	  c = GearCircleCos(i);
	  s = GearCircleSin(i);
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
	x[2] = or * c[1];
	y[2] = or * s[1];
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

#undef glBegin
#undef glEnd
#undef glNormal3f
#undef glVertex3f
#define glBegin MGLD_glBegin
#define glEnd MGLD_glEnd
#define glNormal3f MGLD_glNormal3f
#define glVertex3f MGLD_glVertex3f

Profile gear_profile[] =
{0.000, 0.0,
  0.300, 7.0,
  0.340, 0.4,
  0.550, 0.64,
  0.600, 0.4,
  0.950, 1.0
};

#ifdef MGL_GEARS_PRECALC
static void DrawPrecalculatedGear(const GearCommandBuffer *gearBuffer);
#endif

static ULONG RunApplicationProbe(ULONG frames)
{
	ULONG frame;
	ULONG start = ReadProfileClockLow();
	benchmarkApplicationProbe = GL_TRUE;
	for (frame = 0; frame < frames; ++frame)
	{
		if (show & 1)
#ifdef MGL_GEARS_PRECALC
			DrawPrecalculatedGear(&precalculatedGears[0]);
#else
			gear(40, 0.4f, 2.0f, 1.1f, 0.8f, 0.4f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
#endif
		if (show & 2)
#ifdef MGL_GEARS_PRECALC
			DrawPrecalculatedGear(&precalculatedGears[1]);
#else
			gear(36, 0.4f, 2.0f, 1.1f, 0.7f, 0.2f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
#endif
	}
	benchmarkApplicationProbe = GL_FALSE;
	return ReadProfileClockLow() - start;
}

static ULONG RunApplicationProbeOverhead(ULONG beginCalls, ULONG normalCalls,
	                                     ULONG vertexCalls)
{
	ULONG start = ReadProfileClockLow();
	benchmarkApplicationProbe = GL_TRUE;
	while (beginCalls--) ProfiledBegin(GL_TRIANGLES);
	while (normalCalls--) ProfiledNormal3f(0.0f, 0.0f, 1.0f);
	while (vertexCalls--) ProfiledVertex3f(0.0f, 0.0f, 0.0f);
	benchmarkApplicationProbe = GL_FALSE;
	return ReadProfileClockLow() - start;
}

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
		D(("gears_benchmark: LoadPPM: error scanning PPM header in %s (i=%ld maxval=%lu)\n", name, (LONG)i, maxval));
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
		D(("gears_benchmark: LoadPPM: out of memory loading %s\n", name));
		fclose(f);
		*w = 0; *h = 0;
		return NULL;
	}

	i = fread(where, 1, x * y * 3, f);
	fclose(f);

	if ((unsigned long)i != x * y * 3)
	{
		D(("gears_benchmark: LoadPPM: short read on %s\n", name));
		free(where);
		*w = 0; *h = 0;
		return NULL;
	}

	return where;
}

static float a1 = 27.0f, a2 = 67.0f, a3 = 47.0f, a4 = 87.0f;
static const float i1 = 1.2f, i2 = 3.1f, i3 = 2.3f, i4 = 1.1f;

#ifdef MGL_GEARS_PRECALC
static GLboolean BuildPrecalculatedGears(void)
{
	recordingGear = &precalculatedGears[0];
	gear(40, 0.4f, 2.0f, 1.1f, 0.8f, 0.4f,
	     sizeof(gear_profile) / sizeof(Profile), gear_profile);
	recordingGear = &precalculatedGears[1];
	gear(36, 0.4f, 2.0f, 1.1f, 0.7f, 0.2f,
	     sizeof(gear_profile) / sizeof(Profile), gear_profile);
	recordingGear = NULL;
	if (gearRecordingFailed) {
		free(precalculatedGears[0].commands);
		free(precalculatedGears[1].commands);
		memset(precalculatedGears, 0, sizeof(precalculatedGears));
		return GL_FALSE;
	}
	return GL_TRUE;
}

static void DrawPrecalculatedGear(const GearCommandBuffer *gearBuffer)
{
	ULONG index;
	for (index = 0; index < gearBuffer->count; ++index) {
		const GearCommand *command = &gearBuffer->commands[index];
		switch (command->type) {
		case GEAR_COMMAND_BEGIN:
			ProfiledBegin(command->mode);
			break;
		case GEAR_COMMAND_NORMAL:
			ProfiledNormal3f(command->x, command->y, command->z);
			break;
		case GEAR_COMMAND_VERTEX:
			ProfiledVertex3f(command->x, command->y, command->z);
			break;
		case GEAR_COMMAND_END:
			ProfiledEnd();
			break;
		}
	}
}
#endif

/* Live, on-screen FPS overlay -- the original gears.c's own bShowFPS
 * mechanism, ported to 68k instead of the PPC-only GetSysTimePPC/
 * SubTimePPC it used. DateStamp()'s own resolution is 1/50th sec (one
 * "tick") -- too coarse to measure a single frame meaningfully (a fast
 * frame would read back 0 elapsed ticks). Instead accumulate a small
 * window of frames (FPS_SAMPLE_FRAMES) and recompute/redraw only every
 * Nth frame, giving a stable reading updated periodically throughout
 * the run rather than a single end-of-run number. */
#define FPS_SAMPLE_FRAMES 20
static struct DateStamp g_fps_ds;
static int g_fps_frame_count = 0;
static char g_fps_text[40] = "fps: --";

static void UpdateFPSOverlay(void)
{
	struct DateStamp now_ds;
	LONG elapsed_ticks;

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
}

static void DrawFPSCharacter(char character, GLfloat x, GLfloat y)
{
	static const UBYTE digit_segments[10] = {
		0x3f, 0x06, 0x5b, 0x4f, 0x66,
		0x6d, 0x7d, 0x07, 0x7f, 0x6f
	};
	UBYTE segments = 0;

	if (character >= '0' && character <= '9')
		segments = digit_segments[character - '0'];
	else if (character == 'f')
		segments = 0x71;
	else if (character == 'p')
		segments = 0x73;
	else if (character == 's')
		segments = 0x6d;
	else if (character == ':')
	{
		glVertex2f(x + 3.0f, y + 3.0f);
		glVertex2f(x + 3.0f, y + 3.8f);
		glVertex2f(x + 3.0f, y + 7.0f);
		glVertex2f(x + 3.0f, y + 7.8f);
		return;
	}
	else if (character == '.')
	{
		glVertex2f(x + 3.0f, y + 9.2f);
		glVertex2f(x + 3.0f, y + 10.0f);
		return;
	}

	if (segments & 0x01) {
		glVertex2f(x + 1.0f, y); glVertex2f(x + 5.0f, y);
	}
	if (segments & 0x02) {
		glVertex2f(x + 6.0f, y + 1.0f); glVertex2f(x + 6.0f, y + 4.0f);
	}
	if (segments & 0x04) {
		glVertex2f(x + 6.0f, y + 6.0f); glVertex2f(x + 6.0f, y + 9.0f);
	}
	if (segments & 0x08) {
		glVertex2f(x + 1.0f, y + 10.0f); glVertex2f(x + 5.0f, y + 10.0f);
	}
	if (segments & 0x10) {
		glVertex2f(x, y + 6.0f); glVertex2f(x, y + 9.0f);
	}
	if (segments & 0x20) {
		glVertex2f(x, y + 1.0f); glVertex2f(x, y + 4.0f);
	}
	if (segments & 0x40) {
		glVertex2f(x + 1.0f, y + 5.0f); glVertex2f(x + 5.0f, y + 5.0f);
	}
}

static void DrawFPSOverlay(void)
{
	GLfloat scale = g_render_width >= 640 ? 1.5f : 1.0f;
	GLfloat overlay_width = (GLfloat)strlen(g_fps_text) * 8.0f * scale + 8.0f;
	ULONG index;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0, g_render_width, g_render_height, 0.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glColor3f(0.0f, 0.0f, 0.0f);
	glBegin(GL_QUADS);
	glVertex2f(4.0f, 4.0f);
	glVertex2f(overlay_width, 4.0f);
	glVertex2f(overlay_width, 22.0f * scale);
	glVertex2f(4.0f, 22.0f * scale);
	glEnd();
	glScalef(scale, scale, 1.0f);
	glColor3f(1.0f, 1.0f, 0.2f);
	glBegin(GL_LINES);
	for (index = 0; g_fps_text[index]; ++index)
		DrawFPSCharacter(g_fps_text[index], 6.0f + index * 8.0f, 6.0f);
	glEnd();
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glEnable(GL_TEXTURE_2D);
	if (bEnvMap) {
		glEnable(GL_TEXTURE_GEN_S);
		glEnable(GL_TEXTURE_GEN_T);
	}
	glEnable(GL_DEPTH_TEST);
}

static void oneFrame(void)
{
	ULONG start;

	/* Restored (Phase K) -- see milestone_gears.c's own oneFrame()
	 * comment for the full account of why this is confirmed harmless/
	 * no-op re: hardware. */
	mglLockDisplay();

	start = ProfileStart();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	AddProfileTicks(&benchmarkProfile.clearTicks, start);

	start = ProfileStart();
	if (show & 1)
	{
		ProfiledPushMatrix();
			ProfiledTranslatef(0.0f, 0.0f, -4.0f);
			ProfiledRotatef(a3, 1.0f, 1.0f, 1.0f);
			ProfiledRotatef(a4, 0.0f, 0.0f, -1.0f);
			ProfiledTranslatef(0.14f, 0.2f, 0.0f);
			ProfiledTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#ifdef MGL_GEARS_PRECALC
			DrawPrecalculatedGear(&precalculatedGears[0]);
#else
			gear(40, 0.4f, 2.0f, 1.1f, 0.8f, 0.4f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
#endif
		ProfiledPopMatrix();
	}

	if (show & 2)
	{
		ProfiledPushMatrix();
			ProfiledTranslatef(0.1f, 0.2f, -3.8f);
			ProfiledRotatef(a2, -4.0f, 2.0f, -1.0f);
			ProfiledRotatef(a1, 1.0f, -3.0f, 1.0f);
			ProfiledTranslatef(0.0f, -0.2f, 0.0f);
			ProfiledTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			ProfiledColor3f(1.0f, 0.8f, 0.0f);
#ifdef MGL_GEARS_PRECALC
			DrawPrecalculatedGear(&precalculatedGears[1]);
#else
			gear(36, 0.4f, 2.0f, 1.1f, 0.7f, 0.2f,
			     sizeof(gear_profile) / sizeof(Profile), gear_profile);
#endif
		ProfiledPopMatrix();
	}

	a1 += i1; if (a1 > 360.0f) a1 -= 360.0f;
	a2 += i2; if (a2 > 360.0f) a2 -= 360.0f;
	a3 += i3; if (a3 > 360.0f) a3 -= 360.0f;
	a4 += i4; if (a4 > 360.0f) a4 -= 360.0f;
	AddProfileTicks(&benchmarkProfile.sceneTicks, start);

	if (bShowFPS && !benchmarkActive)
	{
		start = ProfileStart();
		UpdateFPSOverlay();
		DrawFPSOverlay();
		AddProfileTicks(&benchmarkProfile.overlayTicks, start);
	}

	/* The overlay is regular MiniGL geometry, so it is presented with the
	 * scene and works for both windowed blits and fullscreen buffer swaps. */
	start = ProfileStart();
	mglSwitchDisplay();
	AddProfileTicks(&benchmarkProfile.switchTicks, start);
}

/* Real keyboard handler -- see milestone_gears.c's own comment on the
 * identical function. 'f' has a real effect HERE (gates whether oneFrame
 * calls Update/DrawFPSOverlay above), unlike milestone_gears.c where
 * there's no overlay code to gate at all. */
static void keys(char c)
{
	if (c == 27)
	{
		D(("gears_benchmark: ESC pressed, exiting\n"));
		running = GL_FALSE;
	}
	if (benchmarkActive)
		return;

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

static char RawKeyAscii(UWORD code)
{
	switch (code & 0x7f) {
	case 0x01: return '1';
	case 0x02: return '2';
	case 0x03: return '3';
	case 0x12: return 'e';
	case 0x23: return 'f';
	case 0x45: return 27;
	default: return 0;
	}
}

static void ProcessEvents(void)
{
#ifdef MINIGLPPC_SEMANTIC
	static ULONG pollCounter;

	/* A PPC GetMsg() crosses to 68k Exec and costs roughly 32 ms even when
	 * the port is empty. The host event ring does not receive fullscreen
	 * raw-key events reliably, so keep direct polling but amortize it. */
	if ((pollCounter++ & 31UL) != 0)
		return;
#endif

	struct Window *window = (struct Window *)mglGetWindowHandle();
	struct IntuiMessage *message;
	if (!window) return;
	while ((message = (struct IntuiMessage *)GetMsg(window->UserPort)))
	{
		ULONG class = message->Class;
		UWORD code = message->Code;
		ReplyMsg((struct Message *)message);
		if (class == IDCMP_CLOSEWINDOW) running = GL_FALSE;
		else if (class == IDCMP_VANILLAKEY) keys((char)code);
		else if (class == IDCMP_RAWKEY && !(code & 0x80)) {
			char key = RawKeyAscii(code);
			if (key) keys(key);
		}
	}
}

int main(int argc, char *argv[])
{
	GLint width = MILESTONE_WIDTH, height = MILESTONE_HEIGHT;
	int i;
	char *name = 0;
	GLuint texname;
	double d_near = 1.0, d_far = 2000.0;
	int depth = 16;
	int mglBuffersOverride = 0;
	printf("CLIENT boot\n");
	fflush(stdout);
	int frames = 300;
	int warmup = 10;
	int frame;
	int result = 0;
	GLboolean windowed = GL_FALSE;
	GLboolean benchmarkMode = GL_FALSE;
	GLboolean profileMode = GL_FALSE;
	BenchmarkTimer timer;
	GLboolean pollEvents = GL_TRUE;
	BenchmarkPerf perf;
#ifdef MGL_GEARS_ALTIVEC
	MGLPPCAltivecStats altivecStats;
#endif
#ifdef MINIGLPPC_PROFILE
	MGLPPCClientProfile ppcProfile;
#endif
	ULONG clockRate = 0, startTicks = 0, elapsedTicks = 0;
	ULONG profileClockRate = 0, profileStartTicks = 0, profileElapsedTicks = 0;
	ULONG measuredFrames = 0, fpsX1000 = 0;
	ULONG immediateApiTicks = 0, stateApiTicks = 0;
	ULONG applicationTicks = 0, totalApiTicks = 0;
	ULONG applicationProbeTicks = 0, applicationProbeOverheadTicks = 0;
	ULONG nonBackendTicks = 0, normalCalls = 0;
	memset(&perf, 0, sizeof(perf));
#ifdef MGL_GEARS_ALTIVEC
	memset(&altivecStats, 0, sizeof(altivecStats));
#endif
	
GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0);
IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0);
if (!GfxBase || !IntuitionBase)
{
	    printf("Can't open graphics.library or intuition.library\n");
	    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
	    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
	    return 20;
	}	
	if (!OpenBenchmarkTimer(&timer))
	{
		printf("Can't open timer.device\n");
		CloseLibrary((struct Library *)GfxBase);
		CloseLibrary((struct Library *)IntuitionBase);
		GfxBase = NULL;
		IntuitionBase = NULL;
		return 21;
	}
	clockRate = ReadClockRate();
	if (!clockRate)
	{
		printf("timer.device returned a zero EClock rate\n");
		CloseDevice((struct IORequest *)&timer.request);
		TimerBase = NULL;
		CloseLibrary((struct Library *)GfxBase);
		CloseLibrary((struct Library *)IntuitionBase);
		GfxBase = NULL;
		IntuitionBase = NULL;
		return 22;
	}

	/* Same arg set as milestone_gears.c -- see its own comment on -envmap
	 * (real PPM path now) and -window's real limitations here. */
	for (i = 1; i < argc; i++)
	{
		if (0 == local_stricmp(argv[i], "-width") && i + 1 < argc)
		{
			i++;
			width = atoi(argv[i]);
		}
		if (0 == local_stricmp(argv[i], "-height") && i + 1 < argc)
		{
			i++;
			height = atoi(argv[i]);
		}
		if (0 == local_stricmp(argv[i], "-depth") && i + 1 < argc)
		{
			i++;
			depth = atoi(argv[i]);
		}
		if (0 == local_stricmp(argv[i], "-frames") && i + 1 < argc)
		{
			i++;
			frames = atoi(argv[i]);
			if (frames < 1) frames = 1;
		}
		if (0 == local_stricmp(argv[i], "-warmup") && i + 1 < argc)
		{
			i++;
			warmup = atoi(argv[i]);
			if (warmup < 0) warmup = 0;
		}
		if (0 == local_stricmp(argv[i], "-show") && i + 1 < argc)
		{
			i++;
			show = atoi(argv[i]);
			if (show < 1 || show > 3) show = 1;
		}
		if (0 == local_stricmp(argv[i], "-overlay"))
			bShowFPS = 1;
		if (0 == local_stricmp(argv[i], "-benchmark"))
			benchmarkMode = GL_TRUE;
		if (0 == local_stricmp(argv[i], "-profile"))
			profileMode = GL_TRUE;
		if (0 == local_stricmp(argv[i], "-stageprof"))
			stageProfiling = GL_TRUE;
		if (0 == local_stricmp(argv[i], "-noevents"))
			pollEvents = GL_FALSE;
		if (0 == local_stricmp(argv[i], "-envmap") && i + 1 < argc)
		{
			i++;
			name = argv[i];
		}
		if (0 == local_stricmp(argv[i], "-noenvmap"))
			bEnvMap = GL_FALSE;
		if (0 == local_stricmp(argv[i], "-window"))
		{
			windowed = GL_TRUE;
		}
		if (0 == local_stricmp(argv[i], "-buffers") && i + 1 < argc)
		{
			i++;
			mglBuffersOverride = atoi(argv[i]);
		}
	}

	if (MiniGLOpen() == GL_FALSE)
	{
		D(("gears_benchmark: could not open minigl.library\n"));
		CloseDevice((struct IORequest *)&timer.request);
		TimerBase = NULL;
		CloseLibrary((struct Library *)GfxBase);
		CloseLibrary((struct Library *)IntuitionBase);
		GfxBase = NULL;
		IntuitionBase = NULL;
		return -1;
	}

	g_render_width = width;
	g_render_height = height;
	g_windowed = windowed;
	mglChooseWindowMode(windowed ? GL_TRUE : GL_FALSE);
	mglChoosePixelDepth(depth);
	if (mglBuffersOverride >= 1 && mglBuffersOverride <= 3)
		mglChooseNumberOfBuffers(mglBuffersOverride);
	mglChooseVertexBufferSize(1000);

	if (!mglCreateContext(0, 0, width, height))
	{
		D(("gears_benchmark: mglCreateContext failed\n"));
		MiniGLClose();
		CloseDevice((struct IORequest *)&timer.request);
		TimerBase = NULL;
		CloseLibrary((struct Library *)GfxBase);
		CloseLibrary((struct Library *)IntuitionBase);
		GfxBase = NULL;
		IntuitionBase = NULL;
		return -2;
	}
	{
		struct Window *window = (struct Window *)mglGetWindowHandle();
		if (window)
			ModifyIDCMP(window, g_windowed
			    ? IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY
			    : IDCMP_RAWKEY);
	}



	mglEnableSync(GL_FALSE);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	{
		GLint tw, th;
		GLubyte *tmap = LoadPPM(name ? name : "data/chrome.ppm", &tw, &th);

		if (!tmap)
		{
			/* Fails the same way the original does: no fallback texture,
			 * no MGLMainLoop entry, just tear down cleanly. */
			D(("gears_benchmark: can't find texture %s\n", name ? name : "data/chrome.ppm"));
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

			if (bEnvMap)
			{
				glEnable(GL_TEXTURE_2D);
				glEnable(GL_TEXTURE_GEN_S);
				glEnable(GL_TEXTURE_GEN_T);
			}
			glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
			glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glColor3f(1.0f, 0.0f, 0.0f);

			glDisable(GL_CULL_FACE);
			glHint(MGL_W_ONE_HINT, GL_FASTEST);
			glEnable(GL_DEPTH_TEST);

			glViewport(0, 0, width, height);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glFrustum(-1.0, 1.0, -1.0, 1.0, d_near, d_far);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();

			DateStamp(&g_fps_ds);
			g_fps_frame_count = 0;

			D(("gears_benchmark: setup done, starting benchmark (ESC to quit)\n"));

			/* Restored (Phase K) -- matches the original's own main(). */
			mglLockMode(MGL_LOCK_MANUAL);

#ifdef MGL_GEARS_PRECALC
			if (!BuildPrecalculatedGears()) {
				printf("Can't allocate precalculated gear geometry\n");
				running = GL_FALSE;
				result = 23;
			} else {
				printf("MGL_GEARS_PRECALC_READY gear1_commands=%lu gear2_commands=%lu\n",
				       precalculatedGears[0].count, precalculatedGears[1].count);
			}
#endif

			benchmarkActive = benchmarkMode;
			for (frame = 0; frame < warmup && running; ++frame)
			{
				if (pollEvents) ProcessEvents();
				if (!running) break;
				oneFrame();
			}

			/* Setup intentionally uses a few classic calls this implementation
			 * treats as no-ops; don't mistake their stale error for profiler failure. */
			(void)glGetError();
			if (profileMode)
				glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);
			if (profileMode && glGetError() != GL_NO_ERROR)
			{
				D(("gears_benchmark: performance counters unavailable\n"));
				running = GL_FALSE;
			}
			memset(&benchmarkProfile, 0, sizeof(benchmarkProfile));
			if (benchmarkMode && profileMode)
			{
				ULONG timerTicks = 0;
				for (i = 0; i < 256; ++i)
				{
					ULONG timerStart = ReadProfileClockLow();
					timerTicks += ReadProfileClockLow() - timerStart;
				}
				benchmarkProfile.timerOverheadTicks =
					(timerTicks + 128UL) / 256UL;
			}
			benchmarkProfiling = benchmarkMode &&
				(profileMode || stageProfiling);
#ifdef MGL_GEARS_ALTIVEC
			MGLPPCAltivecResetStats();
#endif
#ifdef MINIGLPPC_PROFILE
			memset(&ppcProfile, 0, sizeof(ppcProfile));
			MGLPPCProfileReset();
#endif
			startTicks = ReadClockLow();
			profileStartTicks = ReadProfileClockLow();
#ifdef MINIGLPPC_SEMANTIC_PROFILE
			R200PPCTimelineMark(MGLPPC_TL_SYNC, ReadClockLow(), 0);
#endif
			for (frame = 0; frame < frames && running; ++frame)
			{
				ULONG frameStart = ProfileStart();
				ULONG eventStart = frameStart;
				if (pollEvents) ProcessEvents();
				AddProfileTicks(&benchmarkProfile.eventTicks, eventStart);
				if (!running) break;
#ifdef MINIGLPPC_SEMANTIC_PROFILE
				R200PPCTimelineMark(MGLPPC_TL_FRAME, (ULONG)frame, 0);
#endif
				oneFrame();
#ifdef MINIGLPPC_SEMANTIC_PROFILE
				R200PPCTimelineMark(MGLPPC_TL_FRAME, (ULONG)frame, 1);
#endif
				AddProfileTicks(&benchmarkProfile.frameTicks, frameStart);
				measuredFrames++;
			}
#ifdef MINIGLPPC_SEMANTIC_PROFILE
			R200PPCTimelineMark(MGLPPC_TL_SYNC, ReadClockLow(), 1);
#endif
			benchmarkProfiling = GL_FALSE;
			profileElapsedTicks = ReadProfileClockLow() - profileStartTicks;
			elapsedTicks = ReadClockLow() - startTicks;
			if (elapsedTicks && profileElapsedTicks)
				profileClockRate = (ULONG)(((unsigned long long)profileElapsedTicks *
				                    clockRate + elapsedTicks / 2UL) / elapsedTicks);
			else
				profileClockRate = clockRate;
			ReadPerf(&perf);
#ifdef MGL_GEARS_ALTIVEC
			MGLPPCAltivecReadStats(&altivecStats);
#endif
#ifdef MINIGLPPC_PROFILE
			MGLPPCProfileRead(&ppcProfile);
#endif
			if (elapsedTicks)
				fpsX1000 = (ULONG)(((double)measuredFrames *
				             (double)clockRate * 1000.0) /
				             (double)elapsedTicks + 0.5);
			immediateApiTicks = EstimateProfileTicks(
				benchmarkProfile.immediateTicks,
				benchmarkProfile.immediateSamples,
				benchmarkProfile.immediateCalls,
				benchmarkProfile.timerOverheadTicks);
			stateApiTicks = EstimateProfileTicks(
				benchmarkProfile.stateTicks,
				benchmarkProfile.stateCalls,
				benchmarkProfile.stateCalls,
				benchmarkProfile.timerOverheadTicks);
			/* frontendTicks includes serializeTicks, but excludes Execute/waits. */
			nonBackendTicks = (ULONG)perf.frontendTicks +
				(ULONG)perf.executeTicks + (ULONG)perf.waitTicks;
			nonBackendTicks = benchmarkProfile.sceneTicks > nonBackendTicks
				? benchmarkProfile.sceneTicks - nonBackendTicks : 0;
			if (benchmarkMode && profileMode)
			{
				applicationProbeTicks = RunApplicationProbe(measuredFrames);
				normalCalls = benchmarkProfile.immediateCalls >
					(ULONG)perf.inputVertices + (ULONG)perf.primitives
					? benchmarkProfile.immediateCalls -
					  (ULONG)perf.inputVertices - (ULONG)perf.primitives : 0;
				applicationProbeOverheadTicks = RunApplicationProbeOverhead(
					(ULONG)perf.primitives, normalCalls,
					(ULONG)perf.inputVertices);
				applicationTicks = applicationProbeTicks > applicationProbeOverheadTicks
					? applicationProbeTicks - applicationProbeOverheadTicks : 0;
			}
			totalApiTicks = nonBackendTicks > applicationTicks
				? nonBackendTicks - applicationTicks : 0;
			printf("MINIGL_GEARS_SUMMARY version=5 variant=%s mode=%s size=%ldx%ld depth=%ld "
			       "benchmark=%ld show=%ld envmap=%ld overlay=%ld warmup=%ld frames=%lu "
			       "elapsed_ticks=%lu eclock_hz=%lu client_clock_hz=%lu fps_x1000=%lu "
			       "perf_frames=%ld primitives=%ld input_vertices=%ld "
			       "submitted_vertices=%ld draw_records=%ld clear_records=%ld "
			       "submits=%ld record_dwords=%ld generated_dwords=%ld "
			       "frontend_ticks=%ld execute_ticks=%ld wait_ticks=%ld "
			       "present_ticks=%ld matrix_ticks=%ld serialize_ticks=%ld "
			       "serialize_stall_ticks=%ld "
			       "serialize_kernel_flushes=%ld "
			       "serialize_kernel_vertices=%ld "
			       "spheremap_calls=%ld spheremap_samples=%ld spheremap_ticks=%ld "
			       "spheremap_cache_hits=%ld spheremap_cache_misses=%ld "
			       "normal_cache_hits=%ld normal_cache_misses=%ld "
			       "texture_residency_ticks=%ld "
			       "radeon_caps=%08lx native_attempts=%ld native_used=%ld "
			       "native_fallback_caps=%ld native_fallback_cull=%ld "
			       "native_fallback_flat=%ld native_fallback_size=%ld "
			       "native_fallback_clip=%ld "
			       "quad_attempts=%ld quad_used=%ld quad_fallback_caps=%ld "
			       "quad_fallback_state=%ld quad_fallback_clip=%ld "
			       "async_submits=%ld fence_waits=%ld fence_poll_hits=%ld "
			       "buffer_waits=%ld texture_waits=%ld drain_waits=%ld "
			       "client_frame_ticks=%lu client_event_ticks=%lu "
			       "client_clear_ticks=%lu client_scene_ticks=%lu "
			       "client_overlay_ticks=%lu client_switch_ticks=%lu "
			       "client_api_calls=%lu client_api_samples=%lu "
			       "client_api_sample_ticks=%lu client_timer_overhead_ticks=%lu "
			       "client_immediate_api_ticks=%lu client_state_api_calls=%lu "
			       "client_state_api_ticks=%lu client_application_ticks=%lu "
			       "client_application_probe_ticks=%lu "
			       "client_application_probe_overhead_ticks=%lu "
			       "client_total_api_ticks=%lu client_nonbackend_ticks=%lu\n",
			       benchmarkVariant, windowed ? "window" : "fullscreen", (LONG)width,
			       (LONG)height, (LONG)depth, (LONG)benchmarkMode,
			       (LONG)show, (LONG)bEnvMap,
			       (LONG)bShowFPS, (LONG)warmup, measuredFrames,
			       elapsedTicks, clockRate, profileClockRate, fpsX1000, (LONG)perf.frames,
			       (LONG)perf.primitives, (LONG)perf.inputVertices,
			       (LONG)perf.submittedVertices, (LONG)perf.drawRecords,
			       (LONG)perf.clearRecords, (LONG)perf.submits,
			       (LONG)perf.recordDwords, (LONG)perf.generatedDwords,
			       (LONG)perf.frontendTicks, (LONG)perf.executeTicks,
			       (LONG)perf.waitTicks, (LONG)perf.presentTicks,
			       (LONG)perf.matrixTicks, (LONG)perf.serializeTicks,
			       (LONG)perf.serializeStallTicks,
			       (LONG)perf.serializeKernelFlushes,
			       (LONG)perf.serializeKernelVertices,
			       (LONG)perf.sphereMapCalls, (LONG)perf.sphereMapSamples,
			       (LONG)perf.sphereMapTicks,
			       (LONG)perf.sphereMapCacheHits,
			       (LONG)perf.sphereMapCacheMisses,
			       (LONG)perf.normalCacheHits, (LONG)perf.normalCacheMisses,
			       (LONG)perf.textureResidencyTicks,
			       (ULONG)perf.radeonCaps, (LONG)perf.nativeAttempts,
			       (LONG)perf.nativeUsed, (LONG)perf.nativeFallbackCaps,
			       (LONG)perf.nativeFallbackCull,
			       (LONG)perf.nativeFallbackFlat,
			       (LONG)perf.nativeFallbackSize,
			       (LONG)perf.nativeFallbackClip,
			       (LONG)perf.quadAttempts, (LONG)perf.quadUsed,
			       (LONG)perf.quadFallbackCaps,
			       (LONG)perf.quadFallbackState,
			       (LONG)perf.quadFallbackClip,
			       (LONG)perf.asyncSubmits, (LONG)perf.fenceWaits,
			       (LONG)perf.fencePollHits, (LONG)perf.bufferWaits,
			       (LONG)perf.textureWaits, (LONG)perf.drainWaits,
			       benchmarkProfile.frameTicks, benchmarkProfile.eventTicks,
			       benchmarkProfile.clearTicks, benchmarkProfile.sceneTicks,
			       benchmarkProfile.overlayTicks, benchmarkProfile.switchTicks,
			       benchmarkProfile.immediateCalls,
			       benchmarkProfile.immediateSamples,
			       benchmarkProfile.immediateTicks,
			       benchmarkProfile.timerOverheadTicks,
			       immediateApiTicks, benchmarkProfile.stateCalls,
			       stateApiTicks, applicationTicks, applicationProbeTicks,
			       applicationProbeOverheadTicks, totalApiTicks, nonBackendTicks);
#ifdef MGL_GEARS_ALTIVEC
			printf("MGL_GEARS_ALTIVEC vector_copies=%lu scalar_copies=%lu\n",
			       altivecStats.vectorCopies, altivecStats.scalarCopies);
#endif
#ifdef MINIGLPPC_PROFILE
			printf("MINIGLPPC_PROFILE vertex_calls=%lu vertex_tb=%lu "
			       "sphere_tb=%lu transform_tb=%lu end_calls=%lu end_tb=%lu "
			       "submit_calls=%lu submit_tb=%lu\n",
			       ppcProfile.vertexCalls, ppcProfile.vertexTicks,
			       ppcProfile.sphereTicks, ppcProfile.transformTicks,
			       ppcProfile.endCalls, ppcProfile.endTicks,
			       ppcProfile.submitCalls, ppcProfile.submitTicks);
#endif
#ifdef MINIGLPPC_SEMANTIC_PROFILE
			{
				const MGLPPCSemanticPlatformProfile *semProfile =
					R200PPCPlatformProfile();
				ULONG semRate = profileClockRate ? profileClockRate :
					semProfile->clockRate;
				printf("MGLPPC_SEMANTIC_PROFILE publishes=%lu send_us=%lu "
				       "crc_us=%lu flush_us=%lu waits=%lu wait_us=%lu "
				       "wait_max_us=%lu barriers=%lu barrier_us=%lu "
				       "controls=%lu control_us=%lu chunks=%lu chunk_us=%lu "
				       "ring_stall_us=%lu publish_stall_us=%lu "
				       "barrier_stall_us=%lu drain_us=%lu "
				       "session_us=%lu tb_hz=%lu\n",
				       (unsigned long)semProfile->publishCount,
				       (unsigned long)SemanticProfileMicros(
				           semProfile->publishSendTicks, semRate),
				       (unsigned long)SemanticProfileMicros(
				           semProfile->publishCrcTicks, semRate),
				       (unsigned long)SemanticProfileMicros(
				           semProfile->publishFlushTicks, semRate),
				       (unsigned long)semProfile->replyWaitCount,
				       (unsigned long)SemanticProfileMicros(
				           semProfile->replyWaitTicks, semRate),
				       (unsigned long)SemanticProfileMicros(
				           semProfile->replyWaitMaxTicks, semRate),
				       (unsigned long)semProfile->barrierCount,
				       (unsigned long)SemanticProfileMicros(
				           semProfile->barrierTicks, semRate),
				       (unsigned long)semProfile->controlCount,
				       (unsigned long)SemanticProfileMicros(
				           semProfile->controlTicks, semRate),
				       (unsigned long)semProfile->chunkCount,
				       (unsigned long)SemanticProfileMicros(
				           semProfile->chunkTicks, semRate),
				       (unsigned long)SemanticProfileMicros(
				           semProfile->ringStallTicks, semRate),
				       (unsigned long)SemanticProfileMicros(
				           semProfile->publishStallTicks, semRate),
				       (unsigned long)SemanticProfileMicros(
				           semProfile->barrierStallTicks, semRate),
				       (unsigned long)SemanticProfileMicros(
				           semProfile->drainTicks, semRate),
				       (unsigned long)SemanticProfileMicros(
				           semProfile->sessionTicks, semRate),
				       (unsigned long)semRate);
				fflush(stdout);
				R200PPCTimelineDump();
			}
#endif

			D(("gears_benchmark: run complete, tearing down\n"));
		}
	}

	mglDeleteContext();
	MiniGLClose();
#ifdef MGL_GEARS_PRECALC
	free(precalculatedGears[0].commands);
	free(precalculatedGears[1].commands);
#endif
	if (TimerBase)
	{
		CloseDevice((struct IORequest *)&timer.request);
		TimerBase = NULL;
	}
	
if (GfxBase)
{
    CloseLibrary((struct Library *)GfxBase);
    GfxBase = NULL;
} 	
if (IntuitionBase)
{
    CloseLibrary((struct Library *)IntuitionBase);
    IntuitionBase = NULL;
}

	return result;
}
