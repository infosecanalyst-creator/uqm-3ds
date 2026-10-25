/* Desktop implementation of harness/3ds.h -- see that file.
 *
 * UQM3DS_SCRIPT  file of "<ms> <command> [args]" lines, times from start:
 *     press A | release A | tap A      (A B X Y L R ZL ZR START SELECT UP DOWN LEFT RIGHT)
 *     touch <x> <y> | untouch | snap <name> | quit
 * UQM3DS_SNAPDIR directory for snapshots: <name>_top.ppm / <name>_bottom.ppm
 *               show what is currently on each screen (front buffer).
 * UQM3DS_KBD    text the software keyboard "types" when opened.
 */
#include "3ds.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

/* ---- time ---- */
static u64 t_start;
static u64 now_ms (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (u64) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
u64 osGetTime (void) { return now_ms (); }
u64 svcGetSystemTick (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ((u64) ts.tv_sec * 1000000000ULL + ts.tv_nsec) * 268 / 1000;
}
void svcSleepThread (s64 ns) { struct timespec ts = { ns / 1000000000, ns % 1000000000 }; nanosleep (&ts, NULL); }
void osSetSpeedupEnable (bool on) { (void) on; }

/* ---- framebuffers ---- */
static u32 fb[2][2][400 * 240];   /* [screen][buffer] */
static int back[2] = { 1, 1 };
static int swaps[2];

void gfxInit (GSPGPU_FramebufferFormat a, GSPGPU_FramebufferFormat b, bool v) { (void) a; (void) b; (void) v; t_start = now_ms (); }
void gfxExit (void) {}
void gfxSet3D (bool on) { (void) on; }
u8 *gfxGetFramebuffer (gfxScreen_t s, gfx3dSide_t side, u16 *w, u16 *h)
{
	(void) side;
	if (w) *w = 240;
	if (h) *h = s == GFX_TOP ? 400 : 320;
	return (u8 *) fb[s][back[s]];
}
void gfxScreenSwapBuffers (gfxScreen_t s, bool st) { (void) st; back[s] ^= 1; ++swaps[s]; }
void gfxSwapBuffers (void) { gfxScreenSwapBuffers (GFX_TOP, false); gfxScreenSwapBuffers (GFX_BOTTOM, false); }
Result GSPGPU_FlushDataCache (const void *p, u32 n) { (void) p; (void) n; return 0; }
void gspWaitForVBlank (void) { svcSleepThread (16000000); }
Result gspLcdInit (void) { return 0; }
void gspLcdExit (void) {}
static int backlight_off;
Result GSPLCD_PowerOffBacklight (u32 s) { (void) s; backlight_off = 1; return 0; }
Result GSPLCD_PowerOnBacklight (u32 s) { (void) s; backlight_off = 0; return 0; }

static void snap (const char *name)
{
	const char *dir = getenv ("UQM3DS_SNAPDIR");
	int s;
	if (!dir) dir = ".";
	for (s = 0; s < 2; ++s)
	{
		const int W = s == 0 ? 400 : 320;
		const u32 *p = fb[s][back[s] ^ 1];   /* front buffer */
		char path[512];
		FILE *f;
		int x, y;
		snprintf (path, sizeof path, "%s/%s_%s.ppm", dir, name, s == 0 ? "top" : "bottom");
		f = fopen (path, "wb");
		if (!f) continue;
		fprintf (f, "P6\n%d 240\n255\n", W);
		for (y = 0; y < 240; ++y)
			for (x = 0; x < W; ++x)
			{
				u32 c = p[x * 240 + 239 - y];
				unsigned char rgb[3] = { c >> 24, c >> 16, c >> 8 };
				if (s == 1 && backlight_off) rgb[0] = rgb[1] = rgb[2] = 0;
				fwrite (rgb, 1, 3, f);
			}
		fclose (f);
	}
	fprintf (stderr, "[harness] snap %s (swaps top %d bottom %d)\n", name, swaps[0], swaps[1]);
}

/* ---- scripted input ---- */
typedef struct { u64 t; char cmd[16]; char arg[32]; int x, y; } Ev;
static Ev evs[1024];
static int nev, evpos, loaded, quit;
static u32 held, prev_held, down_, up_;
static touchPosition touch;
static struct { int key; u64 until; } taps[16];

static u32 keybit (const char *n)
{
	static const struct { const char *n; u32 b; } t[] = {
		{"A",KEY_A},{"B",KEY_B},{"X",KEY_X},{"Y",KEY_Y},{"L",KEY_L},{"R",KEY_R},
		{"ZL",KEY_ZL},{"ZR",KEY_ZR},{"START",KEY_START},{"SELECT",KEY_SELECT},
		{"UP",KEY_DUP},{"DOWN",KEY_DDOWN},{"LEFT",KEY_DLEFT},{"RIGHT",KEY_DRIGHT} };
	size_t i;
	for (i = 0; i < sizeof t / sizeof t[0]; ++i)
		if (!strcmp (t[i].n, n)) return t[i].b;
	fprintf (stderr, "[harness] unknown key %s\n", n);
	return 0;
}

static void load_script (void)
{
	const char *path = getenv ("UQM3DS_SCRIPT");
	FILE *f;
	char line[128];
	loaded = 1;
	if (!path || !(f = fopen (path, "r"))) return;
	while (fgets (line, sizeof line, f) && nev < 1024)
	{
		Ev *e = &evs[nev];
		unsigned long long t;
		if (line[0] == '#' || sscanf (line, "%llu %15s", &t, e->cmd) < 2) continue;
		e->t = t;
		e->arg[0] = 0;
		sscanf (line, "%*u %*s %31s", e->arg);
		sscanf (line, "%*u %*s %d %d", &e->x, &e->y);
		++nev;
	}
	fclose (f);
}

void hidScanInput (void)
{
	const u64 t = now_ms () - t_start;
	int i;
	if (!loaded) load_script ();
	prev_held = held;
	for (i = 0; i < 16; ++i)
		if (taps[i].key && t >= taps[i].until) { held &= ~taps[i].key; taps[i].key = 0; }
	while (evpos < nev && evs[evpos].t <= t)
	{
		Ev *e = &evs[evpos++];
		if (!strcmp (e->cmd, "press")) held |= keybit (e->arg);
		else if (!strcmp (e->cmd, "release")) held &= ~keybit (e->arg);
		else if (!strcmp (e->cmd, "tap"))
		{
			for (i = 0; i < 16 && taps[i].key; ++i) ;
			if (i < 16) { taps[i].key = keybit (e->arg); taps[i].until = t + 150; held |= taps[i].key; }
		}
		else if (!strcmp (e->cmd, "touch")) { held |= KEY_TOUCH; touch.px = e->x; touch.py = e->y; }
		else if (!strcmp (e->cmd, "untouch")) held &= ~KEY_TOUCH;
		else if (!strcmp (e->cmd, "snap")) snap (e->arg);
		else if (!strcmp (e->cmd, "quit")) quit = 1;
	}
	down_ = held & ~prev_held;
	up_ = prev_held & ~held;
}
u32 hidKeysDown (void) { return down_; }
u32 hidKeysHeld (void) { return held; }
u32 hidKeysUp (void) { return up_; }
void hidCircleRead (circlePosition *p) { p->dx = p->dy = 0; }
void hidTouchRead (touchPosition *p) { *p = touch; }
bool aptMainLoop (void) { return !quit; }
Result APT_CheckNew3DS (bool *out) { *out = true; return 0; }
Result APT_SetAppCpuTimeLimit (u32 p) { (void) p; return 0; }

/* ---- threads ---- */
struct Thread_tag { pthread_t pt; ThreadFunc fn; void *arg; volatile pid_t tid; int core; };
static __thread struct Thread_tag *self;

static void *tramp (void *a)
{
	struct Thread_tag *t = a;
	self = t;
	t->tid = (pid_t) syscall (SYS_gettid);
	t->fn (t->arg);
	return NULL;
}
Thread threadCreate (ThreadFunc fn, void *arg, size_t stack, int prio, int core, bool det)
{
	struct Thread_tag *t = calloc (1, sizeof *t);
	pthread_attr_t at;
	(void) prio; (void) det;
	t->fn = fn; t->arg = arg; t->core = core;
	pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, stack);
	if (pthread_create (&t->pt, &at, tramp, t)) { free (t); return NULL; }
	while (!t->tid) sched_yield ();
	fprintf (stderr, "[harness] thread %d on core %d, stack %zu KB\n", (int) t->tid, core, stack / 1024);
	return t;
}
Result threadJoin (Thread t, u64 to) { (void) to; pthread_join (t->pt, NULL); return 0; }
void threadFree (Thread t) { free (t); }
Handle threadGetHandle (Thread t) { return (Handle) t->tid; }
Result svcGetThreadId (u32 *id, Handle h)
{
	*id = h == CUR_THREAD_HANDLE ? (u32) syscall (SYS_gettid) : h;
	return 0;
}
Result svcGetThreadPriority (s32 *p, Handle h) { (void) h; *p = 0x30; return 0; }

/* ---- software keyboard ---- */
void swkbdInit (SwkbdState *s, SwkbdType t, int b, int m) { (void) t; (void) b; s->max = m; }
void swkbdSetInitialText (SwkbdState *s, const char *t) { strncpy (s->initial, t, 127); }
void swkbdSetHintText (SwkbdState *s, const char *t) { (void) s; (void) t; }
void swkbdSetValidation (SwkbdState *s, int v, u32 f, int d) { (void) s; (void) v; (void) f; (void) d; }
SwkbdButton swkbdInputText (SwkbdState *s, char *out, size_t n)
{
	const char *t = getenv ("UQM3DS_KBD");
	fprintf (stderr, "[harness] keyboard opened, initial text '%s'\n", s->initial);
	if (!t) return SWKBD_BUTTON_LEFT;
	strncpy (out, t, n - 1);
	return SWKBD_BUTTON_CONFIRM;
}
