/*
 * The Ur-Quan Masters for the New Nintendo 3DS -- screens, input, deck.
 *
 * Lessons carried over from the earlier 3DS ports (Micropolis, OpenXcom,
 * DCSS, ScummVM/Ultima):
 *  - New 3DS speed-up (804 MHz + L2) on from the first line.
 *  - Pixel-exact output: UQM is 320x240, the 3DS screens are 400x240 and
 *    320x240, so the frame is copied 1:1 -- no scaling blur, no stretched rows.
 *  - Crisp bitmap fonts (X11 misc-fixed) for everything the port draws.
 *  - No stereo depth by default (the layered 3D in Micropolis was tiring and
 *    left black seams); the 3D slider has no effect in this version.
 *  - First-run defaults are written once and versioned, never silently
 *    overwriting settings the player changed in UQM's Setup menu.
 *  - Clear on-screen errors instead of a black screen when data is missing.
 *  - A log file on the SD card (sdmc:/3ds/uqm/uqm.log) for crash reports.
 *
 * Drawing model: we own both framebuffers (RGBA8). The game frame is an SDL
 * software surface whose pixel layout already matches RGBA8, so presenting
 * it is a rotate-copy (the 3DS framebuffer is column-major, bottom-up).
 * The control deck is drawn into its own buffer only when something on it
 * changes.
 */
#include "port3ds.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <malloc.h>
#include <unistd.h>
#ifndef UQM3DS_HARNESS
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include "libs/input/sdl/input.h"   /* ProcessInputEvent() */
#include "font_big.h"
#include "font_small.h"
#include "manual_text.h"
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* configuration written on first run                                  */
/* ------------------------------------------------------------------ */

/* Bump when the shipped defaults (controls template) change; the port
 * then rewrites flight.cfg once. uqm.cfg is only written when missing. */
#define PORT_CFG_VERSION "1"

static const char default_uqm_cfg[] =
	"config.reswidth = INT32:320\n"
	"config.resheight = INT32:240\n"
	"config.fullscreen = BOOLEAN:false\n"
	"config.scaler = STRING:no\n"
	"config.scanlines = BOOLEAN:false\n"
	"config.showfps = BOOLEAN:false\n"
	"config.usegl = BOOLEAN:false\n"
	"config.audiodriver = STRING:mixsdl\n"
	"config.audioquality = STRING:medium\n"
	"config.3domusic = BOOLEAN:true\n"
	"config.speech = BOOLEAN:true\n"
	"config.subtitles = BOOLEAN:true\n"
	"config.positionalsfx = BOOLEAN:true\n";

/* Template 1 is the 3DS layout; the rest are UQM's own defaults so Super
 * Melee player 2 and the Setup screen still offer the usual choices. */
static const char default_flight_cfg[] =
	"1.name = STRING:3DS buttons\n"
	"1.up.1 = STRING:key Up\n"
	"1.up.2 = STRING:key y\n"
	"1.down.1 = STRING:key Down\n"
	"1.left.1 = STRING:key Left\n"
	"1.right.1 = STRING:key Right\n"
	"1.weapon.1 = STRING:key RightControl\n"
	"1.special.1 = STRING:key RightShift\n"
	"1.escape.1 = STRING:key Escape\n"
	"\n"
	"2.name = STRING:WASD\n"
	"2.up.1 = STRING:key w\n"
	"2.down.1 = STRING:key s\n"
	"2.left.1 = STRING:key a\n"
	"2.right.1 = STRING:key d\n"
	"2.weapon.1 = STRING:key v\n"
	"2.special.1 = STRING:key b\n"
	"2.escape.1 = STRING:key Escape\n"
	"\n"
	"3.name = STRING:Arrows (2)\n"
	"3.up.1 = STRING:key Up\n"
	"3.down.1 = STRING:key Down\n"
	"3.left.1 = STRING:key Left\n"
	"3.right.1 = STRING:key Right\n"
	"3.weapon.1 = STRING:key ]\n"
	"3.special.1 = STRING:key [\n"
	"3.escape.1 = STRING:key Escape\n"
	"\n"
	"4.name = STRING:ESDF\n"
	"4.up.1 = STRING:key e\n"
	"4.down.1 = STRING:key d\n"
	"4.left.1 = STRING:key s\n"
	"4.right.1 = STRING:key f\n"
	"4.weapon.1 = STRING:key q\n"
	"4.special.1 = STRING:key a\n"
	"4.escape.1 = STRING:key Escape\n"
	"\n"
	"5.name = STRING:Joystick 1\n"
	"5.up.1 = STRING:joystick 0 axis 1 negative\n"
	"5.down.1 = STRING:joystick 0 axis 1 positive\n"
	"5.left.1 = STRING:joystick 0 axis 0 negative\n"
	"5.right.1 = STRING:joystick 0 axis 0 positive\n"
	"5.weapon.1 = STRING:joystick 0 button 0\n"
	"5.special.1 = STRING:joystick 0 button 1\n"
	"5.escape.1 = STRING:key Escape\n"
	"\n"
	"6.name = STRING:Joystick 2\n"
	"6.up.1 = STRING:joystick 1 axis 1 negative\n"
	"6.down.1 = STRING:joystick 1 axis 1 positive\n"
	"6.left.1 = STRING:joystick 1 axis 0 negative\n"
	"6.right.1 = STRING:joystick 1 axis 0 positive\n"
	"6.weapon.1 = STRING:joystick 1 button 0\n"
	"6.special.1 = STRING:joystick 1 button 1\n"
	"6.escape.1 = STRING:key Escape\n";

static int
file_exists (const char *path)
{
	struct stat st;
	return stat (path, &st) == 0;
}

static void
write_file (const char *path, const char *text)
{
	FILE *f = fopen (path, "w");
	if (!f)
		return;
	fputs (text, f);
	fclose (f);
}

static int
read_small_file (const char *path, char *buf, int size)
{
	FILE *f = fopen (path, "r");
	int n;
	if (!f)
		return 0;
	n = (int) fread (buf, 1, size - 1, f);
	fclose (f);
	buf[n > 0 ? n : 0] = '\0';
	return n > 0;
}

/* ------------------------------------------------------------------ */
/* drawing                                                              */
/* ------------------------------------------------------------------ */

#define RGB(r, g, b) ((u32) (((r) << 24) | ((g) << 16) | ((b) << 8) | 0xFF))

#define COL_BG        RGB (0x07, 0x0A, 0x14)
#define COL_HEADER    RGB (0x10, 0x24, 0x3A)
#define COL_ACCENT    RGB (0x5F, 0xD3, 0xE6)
#define COL_TEXT      RGB (0xE6, 0xEE, 0xF5)
#define COL_DIM       RGB (0x7F, 0x8F, 0xA6)
#define COL_CHIP      RGB (0x22, 0x32, 0x4A)
#define COL_BTN       RGB (0x13, 0x22, 0x37)
#define COL_BTN_EDGE  RGB (0x3A, 0x6C, 0x8C)
#define COL_BTN_DOWN  RGB (0x2E, 0x5E, 0x80)
#define COL_HOT       RGB (0xF2, 0xB1, 0x34)
#define COL_HOT_TEXT  RGB (0x1A, 0x12, 0x02)
#define COL_OFF       RGB (0x3C, 0x46, 0x55)

#define DECK_MAX_W 400
#define DECK_H     240

static u32 deck_buf[DECK_MAX_W * DECK_H];
/* play mode (game on the touch screen): buttons drawn over the game.
 * Low byte is alpha: 0 shows the game, 0xFF covers it, between blends. */
static u32 ovl_buf[320 * 240];
static u32 *deck_px = deck_buf;
static int deck_w = 320;

static void
fill (int x, int y, int w, int h, u32 c)
{
	int i, j;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > deck_w) w = deck_w - x;
	if (y + h > DECK_H) h = DECK_H - y;
	for (j = 0; j < h; ++j)
	{
		u32 *p = deck_px + (y + j) * deck_w + x;
		for (i = 0; i < w; ++i)
			p[i] = c;
	}
}

/* rounded-corner box: square box with the corner pixels left as background */
static void
box (int x, int y, int w, int h, u32 c, u32 bg)
{
	fill (x, y, w, h, c);
	deck_px[y * deck_w + x] = bg;
	deck_px[y * deck_w + x + w - 1] = bg;
	deck_px[(y + h - 1) * deck_w + x] = bg;
	deck_px[(y + h - 1) * deck_w + x + w - 1] = bg;
}

static int
text_w (const char *s, int big)
{
	return (int) strlen (s) * (big ? FONT_BIG_W : FONT_SMALL_W);
}

static void
text (int x, int y, const char *s, u32 c, int big)
{
	const int cw = big ? FONT_BIG_W : FONT_SMALL_W;
	const int ch = big ? FONT_BIG_H : FONT_SMALL_H;
	for (; *s; ++s, x += cw)
	{
		const uint16_t *rows;
		int r, b;
		unsigned char cp = (unsigned char) *s;
		if (cp < 0x20 || cp > 0x7E)
			cp = '?';
		rows = big ? font_big_bits[cp - 0x20] : font_small_bits[cp - 0x20];
		for (r = 0; r < ch; ++r)
		{
			const int py = y + r;
			if (py < 0 || py >= DECK_H || !rows[r])
				continue;
			for (b = 0; b < cw; ++b)
			{
				const int px = x + b;
				if ((rows[r] & (0x8000 >> b)) && px >= 0 && px < deck_w)
					deck_px[py * deck_w + px] = c;
			}
		}
	}
}

static void
text_center (int x, int y, int w, const char *s, u32 c, int big)
{
	text (x + (w - text_w (s, big)) / 2, y, s, c, big);
}

/* Copy a row-major w x 240 image into a column-major 3DS framebuffer
 * (pixel (x,y) lives at fb[x * 240 + 239 - y]). */
static void
blit_to_fb (u32 *fb, int fb_x, const u32 *src, int w, int src_pitch_px)
{
	int x, y;
	for (x = 0; x < w; ++x)
	{
		u32 *col = fb + (fb_x + x) * 240 + 239;
		const u32 *s = src + x;
		for (y = 0; y < 240; ++y, s += src_pitch_px)
			col[-y] = *s;
	}
}

static void
clear_fb_columns (u32 *fb, int x0, int w)
{
	memset (fb + x0 * 240, 0, (size_t) w * 240 * sizeof (u32));
}

/* ------------------------------------------------------------------ */
/* state                                                                */
/* ------------------------------------------------------------------ */

enum { LAYOUT_GAME_TOP = 0, LAYOUT_GAME_BOTTOM = 1 };

static int layout = LAYOUT_GAME_BOTTOM;   /* the game under the stylus */
static SDL_Surface *video_surface = NULL;

static int deck_dirty = 1;
static int deck_ctx = -1;
static volatile int char_mode = 0;
static int help_page = -1;          /* the manual: -1 = closed */
volatile int port3ds_manual_active = 0;   /* the game waits while it is open */
static int stereo_on;
static int backlight_off = 0;
static int show_stats = 0;
static int lcd_ok = 0;

/* text entry hand-off between the main thread (keyboard applet) and the
 * game thread (DoTextEntry) */
static char text_now[128];
static volatile int text_max = 32;
static char text_result[128];
static volatile int text_ready = 0;

/* stats */
static u32 stat_frames = 0, stat_fps = 0;
static u64 stat_ticks = 0, stat_us = 0;     /* present cost, last second */
static u64 stat_second_start = 0;

/* left-handed play: the D-pad and the face buttons swap places
 * (Y<->Up, A<->Right, B<->Down, X<->Left), applied where HID is read */
static int lefty = 0;

static u32
lefty_remap (u32 m)
{
	static const u32 pr[4][2] = { { KEY_Y, KEY_DUP }, { KEY_A, KEY_DRIGHT },
			{ KEY_B, KEY_DDOWN }, { KEY_X, KEY_DLEFT } };
	u32 o = m;
	int i;
	if (!lefty)
		return m;
	for (i = 0; i < 4; ++i)
	{
		o &= ~(pr[i][0] | pr[i][1]);
		if (m & pr[i][0]) o |= pr[i][1];
		if (m & pr[i][1]) o |= pr[i][0];
	}
	return o;
}

static void
save_port_settings (void)
{
	char buf[64];
	snprintf (buf, sizeof buf, "screens=%d\nstats=%d\nlefty=%d\n", layout, show_stats, lefty);
	write_file (PORT3DS_ROOT "/port3ds.cfg", buf);
}

static void
load_port_settings (void)
{
	char buf[128];
	const char *p;
	if (!read_small_file (PORT3DS_ROOT "/port3ds.cfg", buf, sizeof buf))
		return;
	/* "screens" replaced "layout" when the game moved to the touch screen;
	 * an old layout=0 is not carried over */
	if ((p = strstr (buf, "screens=")))
		layout = atoi (p + 8) ? LAYOUT_GAME_BOTTOM : LAYOUT_GAME_TOP;
	if ((p = strstr (buf, "stats=")))
		show_stats = atoi (p + 6) ? 1 : 0;
	if ((p = strstr (buf, "lefty=")))
		lefty = atoi (p + 6) ? 1 : 0;
}

static gfxScreen_t
game_screen (void)
{
	return layout == LAYOUT_GAME_TOP ? GFX_TOP : GFX_BOTTOM;
}

static gfxScreen_t
deck_screen (void)
{
	return layout == LAYOUT_GAME_TOP ? GFX_BOTTOM : GFX_TOP;
}

static void
set_backlight (int off)
{
	if (!lcd_ok || backlight_off == off)
		return;
	backlight_off = off;
	if (off)
		GSPLCD_PowerOffBacklight (GSPLCD_SCREEN_BOTTOM);
	else
		GSPLCD_PowerOnBacklight (GSPLCD_SCREEN_BOTTOM);
}

/* ------------------------------------------------------------------ */
/* the control deck                                                     */
/* ------------------------------------------------------------------ */
/*
 * Design (audited against DS/3DS stylus strategy games -- Advance Wars,
 * Civilization Revolution, Anno, Final Fantasy Tactics A2 -- and mouse-era
 * strategy UI conventions):
 *  - The bottom screen changes with the task, so what you need is always
 *    one tap away, never behind a menu:
 *      flying   -> ship status strip + a touch galaxy map: tap a star to see
 *                  name, distance and fuel needed (red if you can't make it),
 *                  tap it again or SET COURSE to fly there. Replaces
 *                  B > Starmap > cursor x many > A > B (10-40 presses).
 *      talking  -> every reply as a large readable button: one tap says it.
 *                  While an alien speaks: SKIP and REPLAY buttons.
 *      typing   -> the system keyboard is one tap.
 *      fighting -> nothing to touch; the deck shows the controls only.
 *  - A slim toolbar is always present (pause, help, swap, dim, fps) instead
 *    of a row of large rarely-used buttons taking a third of the screen.
 *  - Feedback on press (button highlight), disarm by sliding off, no
 *    accidental double actions (a reply can only be sent once).
 *  - Physical controls keep working everywhere; touch only ever adds.
 */

enum { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_A, VK_B, VK_X, VK_Y,
	VK_START, VK_SELECT, VK_L, VK_R, VK_COUNT };
static void tap_key (int vk);
static const char *net_local_ip (void);
static void relayout (void);
static void toggle_layout (void);
static void open_keyboard (void);
static void set_backlight (int off);
static void apt_events (void);
static void close_now (void);
static u64 home_note_until = 0;
static volatile int apt_returned = 0;
static void draw_home_note (void);
static void draw_confirm (int ox);

/* ---- touch regions, rebuilt on every draw ---- */
enum {
	ACT_NONE, ACT_PAUSE, ACT_HELP, ACT_SWAP, ACT_DIM, ACT_STATS, ACT_LEFTY,
	ACT_KEYBOARD, ACT_HELP_PREV, ACT_HELP_CLOSE, ACT_HELP_NEXT, ACT_MAN_SEC,
	ACT_REPLY, ACT_REPLY_MORE, ACT_SKIP, ACT_REPLAY,
	ACT_MAP, ACT_GO, ACT_ZOOM_IN, ACT_ZOOM_OUT, ACT_SHIP, ACT_SHORTCUT,
	ACT_MENU, ACT_PICK, ACT_NET, ACT_GAME, ACT_TAB_MENU, ACT_TAB_MAP, ACT_MAP_CLOSE,
	ACT_MELEE_OPT, ACT_MELEE_SLOT, ACT_MELEE_SHIP, ACT_MELEE_CANCEL, ACT_CONFIRM
};
typedef struct { short x, y, w, h; short act, arg; } Region;
static Region regions[64];
static int nregions = 0;
/* The standard touch click (Ryan's rule for every 3DS port): a very quiet 28 ms tick on its own
   DSP channel (23), next to the engine's audio. ndspInit is reference counted. */
#ifndef UQM3DS_HARNESS
static int click_ok = -1;
static s16 *click_pcm;
static ndspWaveBuf click_wb;
static void
ui_click (void)
{
	if (click_ok < 0)
	{
		const int n = 22050 * 28 / 1000;
		unsigned seed = 12345;
		int i;
		click_ok = 0;
		if (R_FAILED (ndspInit ()))
			return;
		click_pcm = (s16 *) linearAlloc (n * sizeof (s16));
		if (!click_pcm)
			return;
		for (i = 0; i < n; i++)
		{
			const float t = (float) i / 22050.f;
			float noise;
			seed = seed * 1664525u + 1013904223u;
			noise = ((int) (seed >> 16) - 32768) / 32768.f;
			click_pcm[i] = (s16) ((0.75f * sinf (6.2831853f * 1900.f * t)
					+ 0.25f * noise) * expf (-t * 260.f) * 20000.f);
		}
		DSP_FlushDataCache (click_pcm, n * sizeof (s16));
		click_ok = 1;
	}
	if (click_ok != 1)
		return;
	{
		float mix[12] = { 0.8f, 0.8f };
		ndspChnWaveBufClear (23);
		ndspChnReset (23);
		ndspChnSetInterp (23, NDSP_INTERP_LINEAR);
		ndspChnSetRate (23, 22050.f);
		ndspChnSetFormat (23, NDSP_FORMAT_MONO_PCM16);
		ndspChnSetMix (23, mix);
		memset (&click_wb, 0, sizeof click_wb);
		click_wb.data_vaddr = click_pcm;
		click_wb.nsamples = 22050 * 28 / 1000;
		ndspChnWaveBufAdd (23, &click_wb);
	}
}
#else
static void ui_click (void) {}
#endif

static int touch_act = ACT_NONE, touch_arg = 0;   /* armed on press */
static int touch_live = 0;                          /* finger still on it */

static void
add_region (int x, int y, int w, int h, int act, int arg)
{
	if (nregions < (int) (sizeof regions / sizeof regions[0]))
	{
		Region *r = &regions[nregions++];
		r->x = x; r->y = y; r->w = w; r->h = h; r->act = act; r->arg = arg;
	}
}

static const Region *
region_at (int px, int py)
{
	int i;
	for (i = nregions - 1; i >= 0; --i)
	{
		const Region *r = &regions[i];
		if (px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h)
			return r;
	}
	return NULL;
}

/* a button: drawn and registered in one call */
static void
button (int x, int y, int w, int h, const char *label, int act, int arg,
		int hot, int disabled, int big)
{
	const int pressed = touch_live && touch_act == act && touch_arg == arg;
	const u32 edge = hot ? COL_HOT : COL_BTN_EDGE;
	const u32 bg = hot ? COL_HOT : (pressed ? COL_BTN_DOWN : COL_BTN);
	const u32 fg = hot ? COL_HOT_TEXT : (disabled ? COL_OFF : COL_TEXT);
	box (x, y, w, h, edge, COL_BG);
	box (x + 1, y + 1, w - 2, h - 2, bg, edge);
	text_center (x, y + (h - (big ? FONT_BIG_H : FONT_SMALL_H)) / 2, w,
			label, fg, big);
	if (!disabled)
		add_region (x, y, w, h, act, arg);
}

/* ---- word wrap for replies and star names ---- */
static int
wrap_text (const char *s, int cols, char out[][64], int maxlines)
{
	int n = 0;
	while (*s && n < maxlines)
	{
		int len = (int) strlen (s), cut;
		while (*s == ' ')
			++s, --len;
		if (len <= cols)
			cut = len;
		else
		{
			cut = cols;
			while (cut > 0 && s[cut] != ' ')
				--cut;
			if (cut == 0)
				cut = cols;
		}
		if (cut > 63)
			cut = 63;
		memcpy (out[n], s, (size_t) cut);
		out[n][cut] = '\0';
		if (n == maxlines - 1 && s[cut])
		{	/* last allowed line: mark the overflow */
			if (cut > 2) { out[n][cut - 2] = '.'; out[n][cut - 1] = '.'; }
		}
		++n;
		s += cut;
	}
	return n;
}

/* ---- conversation replies, published by the game thread ---- */
#define MAX_REPLIES 8
static SDL_mutex *comm_lock = NULL;
static char comm_text[MAX_REPLIES][200];
static int comm_n = 0, comm_cur = 0;
static volatile u64 comm_stamp = 0;
static volatile int comm_changed = 0;
static volatile int comm_pick = -1;
static int comm_page = 0;

int
port3ds_comm_frame (const char *const *texts, int n, int cur)
{
	int i, pick, changed = 0;
	if (!comm_lock)
		return -1;
	SDL_mutexP (comm_lock);
	if (n > MAX_REPLIES)
		n = MAX_REPLIES;
	if (n != comm_n || cur != comm_cur)
		changed = 1;
	for (i = 0; i < n; ++i)
	{
		const char *t = texts[i] ? texts[i] : "";
		if (!changed && strncmp (comm_text[i], t, sizeof comm_text[i] - 1))
			changed = 1;
		strncpy (comm_text[i], t, sizeof comm_text[i] - 1);
		comm_text[i][sizeof comm_text[i] - 1] = '\0';
	}
	comm_n = n;
	comm_cur = cur;
	comm_stamp = osGetTime ();
	pick = comm_pick;
	comm_pick = -1;
	if (pick >= 0)
	{	/* the reply is being said: stop offering the list */
		comm_n = 0;
		changed = 1;
	}
	SDL_mutexV (comm_lock);
	if (changed)
		comm_changed = 1;
	return pick;
}

static int
comm_fresh (void)
{
	return comm_n > 0 && osGetTime () - comm_stamp < 600;
}

/* ---- galaxy map state ---- */
static int map_zoom = 2;                 /* 0..3: 1x 2x 4x 8x */
static int map_cx = 5000, map_cy = 5000; /* universe centre of the view */
static int map_follow = 1;               /* keep the ship centred */
static int sel_star = -1;
static Port3DSShip ship;
static u64 last_nav_draw = 0;

static int map_x = 2, map_y = 40, map_w = 212, map_h = 170;
static int nav_dash = 0;        /* drawing the map on the (untouchable) dashboard */

static float
map_scale (void)
{
	return (float) map_h / 10000.0f * (float) (1 << map_zoom);
}

static void
map_to_screen (int ox, int ux, int uy, int *sx, int *sy)
{
	const float s = map_scale ();
	*sx = ox + map_x + map_w / 2 + (int) ((ux - map_cx) * s);
	*sy = map_y + map_h / 2 - (int) ((uy - map_cy) * s);
}

static void
put (int x, int y, u32 c, int ox)
{
	if (x >= ox + map_x && x < ox + map_x + map_w && y >= map_y && y < map_y + map_h)
		deck_px[y * deck_w + x] = c;
}

static u32
star_rgb (int color)
{
	switch (color)
	{
		case 0: return RGB (0x6A, 0x9A, 0xFF);   /* blue */
		case 1: return RGB (0x6A, 0xEE, 0x6A);   /* green */
		case 2: return RGB (0xFF, 0xA8, 0x48);   /* orange */
		case 3: return RGB (0xFF, 0x58, 0x58);   /* red */
		case 4: return RGB (0xEE, 0xEE, 0xEE);   /* white */
		default: return RGB (0xFF, 0xEC, 0x5A);  /* yellow */
	}
}

static void
fmt_fuel (char *buf, int n, int fuel)
{
	if (fuel < 0)
		fuel = 0;
	snprintf (buf, (size_t) n, "%d.%d", fuel / 100, (fuel % 100) / 10);
}

static void
draw_nav (int ox)
{
	char buf[96], name[48], lines[3][64];
	int i, n, sx, sy;
	const int dz = (ship.fuel > 5) ? ship.fuel - 5 : 0;   /* reach in units */

	/* status strip */
	fmt_fuel (name, sizeof name, ship.fuel);
	snprintf (buf, sizeof buf, "FUEL %s  RU %u  CREW %d  LANDERS %d",
			name, ship.ru, ship.crew, ship.landers);
	text (ox + 4, 26, buf, COL_TEXT, 0);

	/* ship menu, one tap each (was B, then the cursor along the menu, then A) */
	if (!nav_dash)
	{
		static const struct { const char *label; int sc; } sc[] = {
			{ "SCAN", P3SC_SCAN }, { "DEVICES", P3SC_DEVICES }, { "CARGO", P3SC_CARGO },
			{ "ROSTER", P3SC_ROSTER }, { "SAVE", P3SC_SAVE }, { "LOAD", P3SC_LOAD } };
		const int first = ship.in_orbit ? 0 : 1, nb = 6 - first;
		const int bw = (316 - (nb - 1) * 3) / nb;
		for (i = first; i < 6; ++i)
			button (ox + 2 + (i - first) * (bw + 3), 214, bw, 24, sc[i].label,
					ACT_SHORTCUT, sc[i].sc, 0, 0, 1);
	}

	if (ship.quasispace || !ship.have_pos)
	{
		text_center (0, 110, deck_w, "No galaxy map here", COL_DIM, 1);
		return;
	}

	if (map_follow)
	{
		map_cx = ship.ux;
		map_cy = ship.uy;
	}

	/* map frame */
	fill (ox + map_x, map_y, map_w, map_h, RGB (0x03, 0x05, 0x0C));
	fill (ox + map_x - 1, map_y - 1, map_w + 2, 1, COL_BTN_EDGE);
	fill (ox + map_x - 1, map_y + map_h, map_w + 2, 1, COL_BTN_EDGE);
	add_region (ox + map_x, map_y, map_w, map_h, ACT_MAP, 0);

	/* fuel range: dotted circle around the ship */
	{
		const float r = dz * map_scale ();
		int a;
		map_to_screen (ox, ship.ux, ship.uy, &sx, &sy);
		if (r > 2 && r < 2000)
			for (a = 0; a < 360; a += 3)
			{
				const float rad = a * 3.14159265f / 180.0f;
				put (sx + (int) (r * cosf (rad)), sy + (int) (r * sinf (rad)),
						RGB (0x2A, 0x6A, 0x50), ox);
			}
	}

	/* stars */
	n = port3ds_star_count ();
	for (i = 0; i < n; ++i)
	{
		int x, y, c, sz;
		u32 col;
		port3ds_star_get (i, &x, &y, &c, &sz);
		map_to_screen (ox, x, y, &sx, &sy);
		col = star_rgb (c);
		put (sx, sy, col, ox);
		if (sz >= 1 || map_zoom >= 2)
		{
			put (sx + 1, sy, col, ox); put (sx, sy + 1, col, ox);
			put (sx + 1, sy + 1, col, ox);
		}
		if (sz >= 2)
		{
			put (sx - 1, sy, col, ox); put (sx, sy - 1, col, ox);
			put (sx + 2, sy, col, ox); put (sx, sy + 2, col, ox);
		}
	}

	/* course line and destination */
	if (ship.have_dest)
	{
		int dx, dy, k;
		map_to_screen (ox, ship.dest_x, ship.dest_y, &dx, &dy);
		map_to_screen (ox, ship.ux, ship.uy, &sx, &sy);
		for (k = 0; k <= 64; ++k)
			if ((k & 3) < 2)
				put (sx + (dx - sx) * k / 64, sy + (dy - sy) * k / 64, COL_HOT, ox);
		for (k = -3; k <= 3; ++k)
		{
			put (dx + k, dy - 3, COL_HOT, ox); put (dx + k, dy + 3, COL_HOT, ox);
			put (dx - 3, dy + k, COL_HOT, ox); put (dx + 3, dy + k, COL_HOT, ox);
		}
	}

	/* selected star: ring + label */
	if (sel_star >= 0)
	{
		int x, y, c, sz, k;
		port3ds_star_get (sel_star, &x, &y, &c, &sz);
		map_to_screen (ox, x, y, &sx, &sy);
		for (k = 0; k < 360; k += 20)
		{
			const float rad = k * 3.14159265f / 180.0f;
			put (sx + (int) (6 * cosf (rad)), sy + (int) (6 * sinf (rad)), COL_ACCENT, ox);
		}
	}

	/* the ship */
	map_to_screen (ox, ship.ux, ship.uy, &sx, &sy);
	for (i = -2; i <= 2; ++i)
	{
		put (sx + i, sy, COL_TEXT, ox);
		put (sx, sy + i, COL_TEXT, ox);
	}

	/* side panel */
	if (!nav_dash)
	{
		const int px = ox + 218, pw = 98;
		int y = 42;
		if (sel_star >= 0)
		{
			int x0, y0, c, sz, need, nl;
			port3ds_star_get (sel_star, &x0, &y0, &c, &sz);
			port3ds_star_name (sel_star, name, sizeof name);
			nl = wrap_text (name, 16, lines, 2);
			for (i = 0; i < nl; ++i, y += 13)
				text (px, y, lines[i], COL_ACCENT, 0);
			y += 3;
			need = port3ds_fuel_needed (ship.ux, ship.uy, x0, y0);
			snprintf (buf, sizeof buf, "DIST  %d.%d",
					(need - 5 > 0 ? need - 5 : 0) / 10, (need - 5 > 0 ? need - 5 : 0) % 10);
			text (px, y, buf, COL_TEXT, 0); y += 13;
			fmt_fuel (name, sizeof name, need);
			snprintf (buf, sizeof buf, "FUEL  %s", name);
			text (px, y, buf, need > ship.fuel ? RGB (0xFF, 0x58, 0x58) : COL_TEXT, 0);
			y += 13;
			if (need > ship.fuel)
			{
				fmt_fuel (name, sizeof name, need - ship.fuel);
				snprintf (buf, sizeof buf, "SHORT %s", name);
				text (px, y, buf, RGB (0xFF, 0x58, 0x58), 0);
			}
			else
			{
				fmt_fuel (name, sizeof name, ship.fuel - need);
				snprintf (buf, sizeof buf, "LEFT  %s", name);
				text (px, y, buf, COL_DIM, 0);
			}
			{
				const int is_dest = ship.have_dest && ship.dest_x == x0
						&& ship.dest_y == y0;
				const int is_here = (sel_star == ship.star)
						|| (x0 == ship.ux && y0 == ship.uy);
				button (px - 2, 112, pw, 28,
						is_here ? "YOU ARE HERE" : is_dest ? "ON COURSE" : "SET COURSE",
						ACT_GO, 0, !is_dest && !is_here, is_dest || is_here, 0);
			}
		}
		else
		{
			text (px, y, "Tap a star", COL_DIM, 0); y += 13;
			text (px, y, "for distance", COL_DIM, 0); y += 13;
			text (px, y, "and fuel.", COL_DIM, 0); y += 13;
			text (px, y, "Drag to pan.", COL_DIM, 0);
		}
		button (px - 2, 144, 47, 24, "+", ACT_ZOOM_IN, 0, 0, map_zoom >= 3, 1);
		button (px + 49, 144, 47, 24, "-", ACT_ZOOM_OUT, 0, 0, map_zoom <= 0, 1);
		button (px - 2, 171, pw, 20, map_follow ? "SHIP (FOLLOW)" : "SHIP",
				ACT_SHIP, 0, 0, 0, 0);
		if (ship.have_dest)
		{
			int k, best = -1;
			for (k = 0; k < port3ds_star_count (); ++k)
			{
				int x0, y0, c, sz;
				port3ds_star_get (k, &x0, &y0, &c, &sz);
				if (x0 == ship.dest_x && y0 == ship.dest_y) { best = k; break; }
			}
			if (best >= 0)
				port3ds_star_name (best, name, sizeof name);
			else
				strcpy (name, "map point");
			snprintf (buf, sizeof buf, "TO %s", name);
			buf[16] = '\0';
			text (px, 197, buf, COL_HOT, 0);
		}
	}
}

static int
nearest_star (int ox, int px, int py, int maxd)
{
	int i, best = -1, bestd = maxd * maxd + 1;
	for (i = 0; i < port3ds_star_count (); ++i)
	{
		int x, y, c, sz, sx, sy, d;
		port3ds_star_get (i, &x, &y, &c, &sz);
		map_to_screen (ox, x, y, &sx, &sy);
		d = (sx - px) * (sx - px) + (sy - py) * (sy - py);
		if (d < bestd) { bestd = d; best = i; }
	}
	return best;
}

/* ---- legends for the screens with nothing to touch ---- */
typedef struct { const char *chip; const char *what; } Legend;

static const Legend legend_menu[] = {
	{"A", "Select"}, {"B", "Back"}, {"PAD", "Move"}, {"L / R", "Page up/down"},
	{"X", "Special"}, {"START", "Pause"}, {NULL, NULL} };
static const Legend legend_combat[] = {
	{"A", "Fire weapon"}, {"B", "Special"}, {"Y / UP", "Thrust"},
	{"PAD", "Turn"}, {"SELECT", "Retreat"}, {"START", "Pause"},
	{"HOLD SELECT", "Quit"}, {NULL, NULL} };
static const Legend legend_flight[] = {
	{"PAD", "Steer"}, {"Y / UP", "Thrust"}, {"B", "Ship menu"},
	{"A", "Select"}, {"START", "Pause"}, {"HOLD SELECT", "Quit"}, {NULL, NULL} };
static const Legend legend_talk[] = {
	{"B", "Skip speech"}, {"RIGHT", "Fast-forward"}, {"LEFT", "Replay line"},
	{"START", "Pause"}, {"HOLD SELECT", "Quit"}, {NULL, NULL} };
static const Legend legend_starbase[] = {
	{"PAD", "Move"}, {"A", "Select"}, {"B", "Back"}, {"UP/DOWN", "Buy / sell"},
	{"START", "Pause"}, {"HOLD SELECT", "Quit"}, {NULL, NULL} };
static const Legend legend_text[] = {
	{"UP/DOWN", "Pick letter"}, {"LEFT/RIGHT", "Move"},
	{"A", "Done"}, {"SELECT", "Cancel"}, {NULL, NULL} };

/* Left-handed mode swaps the D-pad and A/B/X/Y: show the button the player actually presses. */
static const char *
chip_name (const char *c)
{
	static const char *const sw[][2] = {
		{"A", "RIGHT"}, {"B", "DOWN"}, {"X", "LEFT"}, {"PAD", "ABXY"}, {"Y / UP", "UP / Y"},
		{"RIGHT", "A"}, {"LEFT", "X"}, {"UP/DOWN", "Y / B"}, {"LEFT/RIGHT", "X / A"} };
	size_t i;
	if (!lefty) return c;
	for (i = 0; i < sizeof sw / sizeof sw[0]; ++i)
		if (!strcmp (c, sw[i][0])) return sw[i][1];
	return c;
}

static void
draw_legend_list (int ox, const Legend *lg, int y0)
{
	int i;
	for (i = 0; lg[i].chip; ++i)
	{
		const int col = i % 2, row = i / 2;
		const int x = ox + 8 + col * 156;
		const int y = y0 + row * 26;
		const char *chip = chip_name (lg[i].chip);
		const int cw = text_w (chip, 0) + 10;
		box (x, y, cw, 18, COL_CHIP, COL_BG);
		text (x + 5, y + 3, chip, COL_TEXT, 0);
		text (x + cw + 6, y + 2, lg[i].what, COL_TEXT, 1);
	}
}

/* ---- the deck ---- */
enum { MODE_LEGEND, MODE_NAV, MODE_REPLIES, MODE_TALK, MODE_TEXT, MODE_HELP,
	MODE_MENU, MODE_MELEE, MODE_PICK, MODE_NET, MODE_CONFIRM };
static int confirm_on = 0, confirm_resp = 0;
static Port3DSNetDlg netdlg;
static int netdlg_on = 0;
static unsigned char pick_ships[2][14];
static int pick_on[2] = { 0, 0 };

/* the game's own menus, refreshed by pump_input */
static Port3DSMenuItem menu_items[P3_MENU_MAX];
static int menu_n = 0, menu_base = -1, menu_cur = 0;
/* Super Melee */
static Port3DSMelee melee;
static int melee_on = 0;
static int melee_pick_side = -1, melee_pick_slot = -1;

static int
deck_mode (void)
{
	if (help_page >= 0)
		return MODE_HELP;
	if (confirm_on)
		return MODE_CONFIRM;
	if (char_mode)
		return MODE_TEXT;
	if (netdlg_on)
		return MODE_NET;
	if (pick_on[0] || pick_on[1])
		return MODE_PICK;
	if (melee_on)
		return MODE_MELEE;
	if (deck_ctx == CTX3DS_ENCOUNTER && comm_fresh ())
		return MODE_REPLIES;
	if (menu_n > 0)
		return MODE_MENU;
	if (deck_ctx == CTX3DS_ENCOUNTER)
		return comm_fresh () ? MODE_REPLIES : MODE_TALK;
	if (deck_ctx == CTX3DS_HYPERSPACE || deck_ctx == CTX3DS_SOLARSYS)
		return MODE_NAV;
	return MODE_LEGEND;
}

static const char *
mode_title (int mode)
{
	switch (mode)
	{
		case MODE_HELP: return "MANUAL";
		case MODE_CONFIRM: return "QUIT";
		case MODE_TEXT: return "ENTER A NAME";
		case MODE_REPLIES: return "YOUR REPLY";
		case MODE_TALK: return "ENCOUNTER";
		case MODE_MELEE: return "SUPER MELEE";
		case MODE_PICK: return "PICK SHIP";
		case MODE_NET: return "NETWORK MELEE";
		case MODE_MENU:
			switch (menu_base)
			{
				case P3M_MAIN: return "MAIN MENU";
				case P3M_STARBASE: return "STARBASE";
				case PM3_SCAN: return "IN ORBIT";
				case PM3_STARMAP: return "SHIP MENU";
				case PM3_MIN_SCAN: return "PLANET SCAN";
				case PM3_SAVE_GAME: return "GAME";
				case PM3_CONVERSE: return "ENCOUNTER";
				case PM3_FUEL: return "OUTFIT STARSHIP";
				case PM3_CREW: return "SHIPYARD";
				case PM3_SOUND_ON: return "SETTINGS";
				default: return "MENU";
			}
		case MODE_NAV:
			return deck_ctx == CTX3DS_HYPERSPACE ? "HYPERSPACE" : "SOLAR SYSTEM";
		default:
			switch (deck_ctx)
			{
				case CTX3DS_COMBAT: return "SHIP COMBAT";
				case CTX3DS_STARBASE: return "STARBASE";
				default: return "UR-QUAN MASTERS";
			}
	}
}

static void
draw_toolbar (int ox, int mode)
{
	static const struct { const char *label; int act; int w; } tb[] = {
		{ "II", ACT_PAUSE, 26 }, { "?", ACT_HELP, 26 }, { "SWAP", ACT_SWAP, 40 },
		{ "DIM", ACT_DIM, 32 }, { "FPS", ACT_STATS, 32 }, { "LH", ACT_LEFTY, 24 } };
	int i, x = ox + 320 - 3;
	char buf[40];

	fill (0, 0, deck_w, 22, COL_HEADER);
	fill (0, 22, deck_w, 1, COL_BTN_EDGE);
	text (ox + 5, 5, mode_title (mode), COL_ACCENT, 0);
	for (i = (int) (sizeof tb / sizeof tb[0]) - 1; i >= 0; --i)
	{
		x -= tb[i].w + 2;
		button (x, 2, tb[i].w, 18, tb[i].label, tb[i].act, 0,
				(tb[i].act == ACT_STATS && show_stats) || (tb[i].act == ACT_LEFTY && lefty), 0, 0);
	}
	if (show_stats)
	{
		snprintf (buf, sizeof buf, "%lufps %lu.%lums", (unsigned long) stat_fps,
				(unsigned long) (stat_us / 1000),
				(unsigned long) ((stat_us % 1000) / 100));
		text (x - 4 - text_w (buf, 0), 5, buf, COL_DIM, 0);
	}
}

static void
draw_replies (int ox)
{
	char lines[3][64];
	int i, y = 28, shown = 0, more = 0;
	SDL_mutexP (comm_lock);
	if (comm_page >= comm_n)
		comm_page = 0;
	for (i = comm_page; i < comm_n; ++i)
	{
		const int nl = wrap_text (comm_text[i], 50, lines, 3);
		const int h = 8 + nl * 13;
		const int hot = (i == comm_cur);
		int k;
		if (y + h > (i + 1 < comm_n ? 206 : 238) && shown > 0)
		{
			more = 1;
			break;
		}
		box (ox + 3, y, 314, h, hot ? COL_ACCENT : COL_BTN_EDGE, COL_BG);
		box (ox + 4, y + 1, 312, h - 2,
				(touch_live && touch_act == ACT_REPLY && touch_arg == i)
				? COL_BTN_DOWN : COL_BTN, hot ? COL_ACCENT : COL_BTN_EDGE);
		for (k = 0; k < nl; ++k)
			text (ox + 9, y + 4 + k * 13, lines[k], COL_TEXT, 0);
		add_region (ox + 3, y, 314, h, ACT_REPLY, i);
		y += h + 4;
		++shown;
	}
	SDL_mutexV (comm_lock);
	if (more || comm_page > 0)
		button (ox + 3, 212, 314, 26, more ? "MORE REPLIES  v" : "BACK TO FIRST  ^",
				ACT_REPLY_MORE, 0, 0, 0, 0);
}

static void
draw_menu (int ox)
{
	int i, y = 27, h, gap = 4;
	if (deck_ctx == CTX3DS_HYPERSPACE || deck_ctx == CTX3DS_SOLARSYS)
	{	/* keep the ship's numbers in view while its menus are open */
		char buf[96], f[16];
		port3ds_game_read (&ship);
		fmt_fuel (f, sizeof f, ship.fuel);
		snprintf (buf, sizeof buf, "FUEL %s  RU %u  CREW %d  LANDERS %d",
				f, ship.ru, ship.crew, ship.landers);
		text (ox + 4, 26, buf, COL_TEXT, 0);
		y = 42;
	}
	{
		const int n = menu_n + (menu_base == P3M_MAIN);   /* + MANUAL on the main menu */
		h = (238 - y) / (n > 0 ? n : 1) - gap;
		if (h > 40)
			h = 40;
		if (h < 18)
			gap = 2, h = (238 - y) / n - gap;
		for (i = 0; i < n; ++i, y += h + gap)
			if (i == menu_n)
				button (ox + 6, y, 308, h, "MANUAL", ACT_HELP, 0, 0, 0, h >= 18);
			else
				button (ox + 6, y, 308, h, menu_items[i].label, ACT_MENU, i,
						menu_items[i].state == menu_cur, 0, h >= 18);
	}
}

static const char *const control_name[5] = {
	"HUMAN", "WEAK CPU", "GOOD CPU", "AWESOME CPU", "NETWORK" };

static void
draw_melee_team (int ox, int team, int y)
{
	char buf[64];
	int r, c;
	snprintf (buf, sizeof buf, "%s  %s", team == 0 ? "TOP" : "BOTTOM",
			melee.name[team]);
	text (ox + 4, y, buf, COL_ACCENT, 0);
	snprintf (buf, sizeof buf, "%d PTS", melee.value[team]);
	text (ox + 316 - text_w (buf, 0), y, buf, COL_DIM, 0);
	for (r = 0; r < 2; ++r)
		for (c = 0; c < 7; ++c)
		{
			const int slot = r * 7 + c;
			const int sh = melee.ships[team][slot];
			const int sel = melee_pick_side == team && melee_pick_slot == slot;
			button (ox + 3 + c * 45, y + 12 + r * 23, 43, 21,
					sh == 255 ? "-" : port3ds_ship_short (sh),
					ACT_MELEE_SLOT, team * 16 + slot, sel, 0, 0);
		}
}

static void
draw_melee_buttons (int ox, int top, int y)
{
	/* the TOP controls button drives PlayerControl[1] (as on screen) */
	const int ctl = melee.control[top ? 1 : 0];
	char buf[40];
	int x = ox + 3, w = melee.net ? 110 : 150;
	snprintf (buf, sizeof buf, "%s", control_name[ctl < 5 ? ctl : 0]);
	button (x, y, w, 22, buf, ACT_MELEE_OPT, top ? P3MO_CTRL_TOP : P3MO_CTRL_BOT, 0, 0, 0);
	x += w + 3;
	if (melee.net)
	{
		button (x, y, 48, 22, "NET", ACT_MELEE_OPT, top ? P3MO_NET_TOP : P3MO_NET_BOT, 0, 0, 0);
		x += 51;
	}
	w = (ox + 317 - x - 3) / 2;
	button (x, y, w, 22, "LOAD", ACT_MELEE_OPT, top ? P3MO_LOAD_TOP : P3MO_LOAD_BOT, 0, 0, 0);
	button (x + w + 3, y, w, 22, "SAVE", ACT_MELEE_OPT, top ? P3MO_SAVE_TOP : P3MO_SAVE_BOT, 0, 0, 0);
}

static void
draw_net (int ox)
{
	char buf[96];
	snprintf (buf, sizeof buf, "%s player.  This 3DS: %s",
			netdlg.side ? "Top" : "Bottom", net_local_ip ());
	text (ox + 4, 26, buf, COL_ACCENT, 0);
	snprintf (buf, sizeof buf, "HOST  %s", netdlg.host[0] ? netdlg.host : "(tap to type)");
	button (ox + 3, 42, 314, 28, buf, ACT_NET, P3ND_HOST, 0, 0, 1);
	snprintf (buf, sizeof buf, "PORT  %s", netdlg.port);
	button (ox + 3, 74, 200, 28, buf, ACT_NET, P3ND_PORT, 0, 0, 1);
	snprintf (buf, sizeof buf, "DELAY %d", netdlg.delay);
	text (ox + 210, 82, buf, COL_TEXT, 0);
	button (ox + 262, 74, 26, 28, "-", ACT_NET, P3ND_DELAY_DOWN, 0, netdlg.delay <= 0, 1);
	button (ox + 291, 74, 26, 28, "+", ACT_NET, P3ND_DELAY_UP, 0, netdlg.delay >= 9, 1);
	button (ox + 3, 110, 314, 34, "CONNECT TO HOST", ACT_NET, P3ND_CONNECT, 1, !netdlg.host[0], 1);
	button (ox + 3, 150, 314, 34, "WAIT FOR CONNECTION", ACT_NET, P3ND_LISTEN, 0, 0, 1);
	button (ox + 3, 190, 314, 26, "CANCEL", ACT_NET, P3ND_CANCEL, 0, 0, 1);
	text_center (0, 224, deck_w, "One 3DS waits; the other connects to its IP", COL_DIM, 0);
}

static void
net_keyboard (int which)
{
	SwkbdState kb;
	char out[64];
	SwkbdButton b;
	const int port = which == P3ND_PORT;
	swkbdInit (&kb, port ? SWKBD_TYPE_NUMPAD : SWKBD_TYPE_NORMAL, 2, port ? 5 : 48);
	swkbdSetInitialText (&kb, port ? netdlg.port : netdlg.host);
	swkbdSetHintText (&kb, port ? "Port (21837)" : "Host IP, e.g. 192.168.1.50");
	swkbdSetValidation (&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	memset (out, 0, sizeof out);
	b = swkbdInputText (&kb, out, sizeof out);
	if (b == SWKBD_BUTTON_CONFIRM && out[0])
		port3ds_netdlg_act (which, out);
	relayout ();
}

static void
draw_pick (int ox)
{
	int pl, y = 26, both = pick_on[0] && pick_on[1];
	for (pl = 1; pl >= 0; --pl)
	{
		int r, c;
		if (!pick_on[pl])
			continue;
		text (ox + 4, y, pl == 1 ? "TOP PLAYER" : "BOTTOM PLAYER", COL_ACCENT, 0);
		for (r = 0; r < 2; ++r)
			for (c = 0; c < 7; ++c)
			{
				const int slot = r * 7 + c, sh = pick_ships[pl][slot];
				button (ox + 3 + c * 45, y + 14 + r * (both ? 25 : 40), 43, both ? 23 : 37,
						sh == 255 ? "" : port3ds_ship_short (sh), ACT_PICK, pl * 16 + slot,
						0, sh == 255, 0);
			}
		button (ox + 3, y + (both ? 66 : 100), 314, both ? 26 : 40, "RANDOM SHIP",
				ACT_PICK, pl * 16 + 15, 1, 0, 1);
		y += both ? 106 : 160;
	}
}

static void
draw_melee (int ox)
{
	if (melee_pick_side >= 0)
	{	/* ship picker for one slot */
		char buf[48];
		int i;
		snprintf (buf, sizeof buf, "%s FLEET, SLOT %d: PICK A SHIP",
				melee_pick_side == 0 ? "TOP" : "BOTTOM", melee_pick_slot + 1);
		text (ox + 4, 26, buf, COL_ACCENT, 0);
		for (i = 0; i < 25; ++i)
		{
			snprintf (buf, sizeof buf, "%s %d", port3ds_ship_short (i),
					port3ds_ship_cost (i));
			button (ox + 3 + (i % 5) * 63, 41 + (i / 5) * 32, 60, 29, buf,
					ACT_MELEE_SHIP, i, melee.ships[melee_pick_side][melee_pick_slot] == i,
					0, 0);
		}
		button (ox + 3, 203, 153, 32, "EMPTY SLOT", ACT_MELEE_SHIP, 255, 0, 0, 1);
		button (ox + 162, 203, 153, 32, "CANCEL", ACT_MELEE_CANCEL, 0, 0, 0, 1);
		return;
	}
	draw_melee_team (ox, 0, 25);
	draw_melee_buttons (ox, 1, 84);
	button (ox + 3, 110, 210, 30, "BATTLE!", ACT_MELEE_OPT, P3MO_BATTLE, 1, 0, 1);
	button (ox + 217, 110, 100, 30, "QUIT", ACT_MELEE_OPT, P3MO_QUIT, 0, 0, 1);
	draw_melee_team (ox, 1, 145);
	draw_melee_buttons (ox, 0, 204);
	if (melee.net)
	{
		char buf[64];
		if (port3ds_net_on ())
			snprintf (buf, sizeof buf, "Tap a slot to change it.  This 3DS: %s", net_local_ip ());
		else
			snprintf (buf, sizeof buf, "Tap a slot to change it.");
		text_center (0, 228, deck_w, buf, COL_DIM, 0);
	}
	else
		text_center (0, 228, deck_w, "Tap a slot to change its ship", COL_DIM, 0);
}

/* ---- the instruction manual --------------------------------------------
 * Opened from the main menu, the ? button and ZR. The top screen is for
 * reading (a section's text, scrolled with the pad), the touch screen for
 * choosing (a button per section). L / R step through sections, B / Start /
 * ZR close, and it reopens where it was left. The game waits underneath.
 * The text is manual.txt built in (tools/embed_manual.py); a manual.txt in
 * sdmc:/3ds/uqm/ replaces it. Format: "# " section, "## " heading, "- " list
 * item, "1. " step, "| a | b" two-column row, blank line ends a paragraph. */

#define MAN_W 63                /* characters per line on the top screen */
#define MAN_COL 18              /* width of a table's first column */
#define MAN_ROWS 15
#define MAN_MAX_SEC 24
#define MAN_MAX_LINES 1400
enum { ML_TEXT, ML_HEAD, ML_TABLE, ML_BLANK };
typedef struct { unsigned char kind; char a[MAN_W + 1]; char b[MAN_W + 1]; } ManLine;
typedef struct { char title[32]; int first, count; } ManSec;
static ManLine man_lines[MAN_MAX_LINES];
static ManSec man_secs[MAN_MAX_SEC];
static int man_nsec = 0, man_nlines = 0;
static int man_cur = 0, man_scroll[MAN_MAX_SEC];
static u32 man_buf[400 * 240];  /* the top screen while the game is up there */

static ManLine *
man_add (int kind)
{
	ManLine *l;
	if (man_nlines >= MAN_MAX_LINES || !man_nsec)
		return NULL;
	l = &man_lines[man_nlines++];
	memset (l, 0, sizeof *l);
	l->kind = (unsigned char) kind;
	man_secs[man_nsec - 1].count++;
	return l;
}

static int
man_last_blank (void)
{
	const ManSec *sec = &man_secs[man_nsec - 1];
	return !sec->count || man_lines[man_nlines - 1].kind == ML_BLANK;
}

/* how much of s fits in room characters, broken at a space */
static int
man_cut (const char *s, int room)
{
	int len = (int) strlen (s), cut;
	if (len <= room)
		return len;
	cut = room;
	while (cut > 0 && s[cut] != ' ')
		--cut;
	return cut ? cut : room;
}

static void
man_wrap (const char *s, const char *pre)
{
	const int pl = (int) strlen (pre);
	int first = 1;
	for (;;)
	{
		ManLine *l;
		int cut;
		while (*s == ' ')
			++s;
		if (!*s && !first)
			break;
		cut = man_cut (s, MAN_W - pl);
		if (!(l = man_add (ML_TEXT)))
			return;
		snprintf (l->a, sizeof l->a, "%-*s%.*s", pl, first ? pre : "", cut, s);
		s += cut;
		first = 0;
		if (!*s)
			break;
	}
}

static void
man_table (const char *c1, const char *c2)
{
	int first = 1;
	for (;;)
	{
		ManLine *l;
		int cut;
		while (*c2 == ' ')
			++c2;
		cut = man_cut (c2, MAN_W - MAN_COL);
		if (!(l = man_add (ML_TABLE)))
			return;
		snprintf (l->a, MAN_COL, "%s", first ? c1 : "");
		snprintf (l->b, sizeof l->b, "%.*s", cut, c2);
		c2 += cut;
		first = 0;
		while (*c2 == ' ')
			++c2;
		if (!*c2)
			break;
	}
}

static void
man_trim (char *t)
{
	char *e;
	while (*t == ' ')
		memmove (t, t + 1, strlen (t));
	e = t + strlen (t);
	while (e > t && (e[-1] == ' ' || e[-1] == '|'))
		*--e = '\0';
}

static void
man_parse (const char *p)
{
	static char para[4096];
	char line[512];
	int plen = 0;
	man_nsec = man_nlines = 0;
#define MAN_FLUSH() do { if (plen) { para[plen] = '\0'; man_wrap (para, ""); plen = 0; } } while (0)
	while (*p)
	{
		const char *e = strchr (p, '\n');
		int n = e ? (int) (e - p) : (int) strlen (p);
		if (n > (int) sizeof line - 1)
			n = sizeof line - 1;
		memcpy (line, p, (size_t) n);
		line[n] = '\0';
		if (n && line[n - 1] == '\r')
			line[--n] = '\0';
		p = e ? e + 1 : p + strlen (p);

		if (!strncmp (line, "# ", 2))
		{
			MAN_FLUSH ();
			if (man_nsec < MAN_MAX_SEC)
			{
				ManSec *sec = &man_secs[man_nsec++];
				snprintf (sec->title, sizeof sec->title, "%s", line + 2);
				sec->first = man_nlines;
				sec->count = 0;
			}
		}
		else if (!man_nsec)
			continue;
		else if (!strncmp (line, "## ", 3))
		{
			ManLine *l;
			MAN_FLUSH ();
			if (!man_last_blank ())
				man_add (ML_BLANK);
			if ((l = man_add (ML_HEAD)))
				snprintf (l->a, sizeof l->a, "%s", line + 3);
		}
		else if (!strncmp (line, "- ", 2))
		{
			MAN_FLUSH ();
			man_wrap (line + 2, "- ");
		}
		else if (isdigit ((unsigned char) line[0]) && line[1] == '.' && line[2] == ' ')
		{
			char pre[4] = { line[0], '.', ' ', '\0' };
			MAN_FLUSH ();
			man_wrap (line + 3, pre);
		}
		else if (line[0] == '|')
		{
			char *c1 = line + 1, *bar = strchr (c1, '|');
			MAN_FLUSH ();
			if (bar)
			{
				*bar = '\0';
				man_trim (c1);
				man_trim (bar + 1);
				man_table (c1, bar + 1);
			}
		}
		else if (!line[0])
		{
			MAN_FLUSH ();
			if (!man_last_blank ())
				man_add (ML_BLANK);
		}
		else if (plen + n + 2 < (int) sizeof para)
		{
			if (plen)
				para[plen++] = ' ';
			memcpy (para + plen, line, (size_t) n);
			plen += n;
		}
	}
	MAN_FLUSH ();
#undef MAN_FLUSH
	/* no trailing blank lines */
	{
		int i;
		for (i = 0; i < man_nsec; ++i)
			while (man_secs[i].count
					&& man_lines[man_secs[i].first + man_secs[i].count - 1].kind == ML_BLANK)
				man_secs[i].count--;
	}
}

static void
man_load (void)
{
	FILE *f = fopen (PORT3DS_ROOT "/manual.txt", "rb");
	if (f)
	{
		char *buf = (char *) malloc (65536);
		if (buf)
		{
			const size_t n = fread (buf, 1, 65535, f);
			buf[n] = '\0';
			man_parse (buf);
			free (buf);
		}
		fclose (f);
	}
	if (!man_nsec)
		man_parse (manual_builtin);
	if (man_cur >= man_nsec)
		man_cur = 0;
}

static void
man_open (void)
{
	man_load ();
	help_page = 0;
	port3ds_manual_active = 1;
	if (stereo_on)
	{	/* the manual is flat; the presenter turns 3D back on after */
		gfxSet3D (false);
		stereo_on = 0;
	}
	deck_dirty = 1;
}

static void
man_close (void)
{
	help_page = -1;
	port3ds_manual_active = 0;
	deck_dirty = 1;
	if (layout == LAYOUT_GAME_TOP && video_surface)
		port3ds_present (video_surface);    /* the game back on top */
}

static void
man_step (int d)
{
	if (!man_nsec)
		return;
	man_cur = (man_cur + d + man_nsec) % man_nsec;
	deck_dirty = 1;
}

static void
man_go (int i)
{	/* D-pad on the 2-column section grid; stops at the edges */
	if (i < 0 || i >= man_nsec || i == man_cur)
		return;
	man_cur = i;
	deck_dirty = 1;
}

static void
man_scroll_by (int d)
{
	const int max = man_nsec ? man_secs[man_cur].count - MAN_ROWS : 0;
	int v = man_scroll[man_cur] + d;
	if (v > max) v = max;
	if (v < 0) v = 0;
	if (v != man_scroll[man_cur])
	{
		man_scroll[man_cur] = v;
		deck_dirty = 1;
	}
}

/* the reading side: into a 400-wide target (deck_px / deck_w) */
static void
draw_man_top (void)
{
	char buf[48];
	const ManSec *sec;
	int i, top, max;
	fill (0, 0, 400, 240, COL_BG);
	fill (0, 0, 400, 22, COL_HEADER);
	fill (0, 22, 400, 1, COL_BTN_EDGE);
	text (6, 5, "MANUAL", COL_ACCENT, 0);
	if (!man_nsec)
	{
		text (8, 40, "No manual text found.", COL_DIM, 0);
		return;
	}
	sec = &man_secs[man_cur];
	text (54, 5, sec->title, COL_TEXT, 0);
	snprintf (buf, sizeof buf, "%d / %d", man_cur + 1, man_nsec);
	text (394 - text_w (buf, 0), 5, buf, COL_DIM, 0);
	max = sec->count - MAN_ROWS;
	if (max < 0)
		max = 0;
	top = man_scroll[man_cur] > max ? max : man_scroll[man_cur];
	for (i = 0; i < MAN_ROWS && top + i < sec->count; ++i)
	{
		const ManLine *l = &man_lines[sec->first + top + i];
		const int y = 28 + i * 13;
		switch (l->kind)
		{
			case ML_HEAD: text (8, y, l->a, COL_ACCENT, 0); break;
			case ML_TABLE:
				text (8, y, l->a, COL_HOT, 0);
				text (8 + MAN_COL * 6, y, l->b, COL_TEXT, 0);
				break;
			case ML_TEXT: text (8, y, l->a, COL_TEXT, 0); break;
			default: break;
		}
	}
	if (max > 0)
	{	/* scroll bar */
		const int h = 195 * MAN_ROWS / sec->count;
		fill (394, 28, 3, 195, COL_CHIP);
		fill (394, 28 + (195 - h) * top / max, 3, h, COL_ACCENT);
	}
	fill (0, 224, 400, 16, COL_HEADER);
	text (6, 226, lefty ? "Stick: scroll    A/B/X/Y, L / R: section    D-pad down: close"
	                    : "Stick: scroll    D-pad, L / R: section    B: close", COL_DIM, 0);
}

/* the choosing side: a button per section */
static void
draw_man_buttons (int ox)
{
	int i;
	for (i = 0; i <= man_nsec && i < 16; ++i)
	{
		const int c = i & 1, r = i / 2;
		if (i == man_nsec)
			button (ox + 4 + c * 158, 26 + r * 26, 154, 24, "CLOSE", ACT_HELP_CLOSE, 0, 0, 0, 0);
		else
			button (ox + 4 + c * 158, 26 + r * 26, 154, 24, man_secs[i].title,
					ACT_MAN_SEC, i, i == man_cur, 0, 0);
	}
}

static void
draw_help (int ox)
{
	draw_man_buttons (ox);
}

static void
draw_deck (void)
{
	const int ox = (deck_w - 320) / 2;
	const int mode = deck_mode ();

	nregions = 0;
	fill (0, 0, deck_w, DECK_H, COL_BG);
	draw_toolbar (ox, mode);

	switch (mode)
	{
		case MODE_HELP:
			draw_help (ox);
			break;
		case MODE_CONFIRM:
			draw_confirm (ox);
			break;
		case MODE_TEXT:
			draw_legend_list (ox, legend_text, 32);
			button (ox + 30, 120, 260, 56, "OPEN KEYBOARD", ACT_KEYBOARD, 0, 1, 0, 1);
			text_center (0, 186, deck_w, "or pick letters with the pad", COL_DIM, 0);
			break;
		case MODE_NAV:
			port3ds_game_read (&ship);
			draw_nav (ox);
			last_nav_draw = osGetTime ();
			break;
		case MODE_REPLIES:
			draw_replies (ox);
			break;
		case MODE_MENU:
			draw_menu (ox);
			break;
		case MODE_MELEE:
			draw_melee (ox);
			break;
		case MODE_PICK:
			draw_pick (ox);
			break;
		case MODE_NET:
			draw_net (ox);
			break;
		case MODE_TALK:
			draw_legend_list (ox, legend_talk, 32);
			button (ox + 6, 150, 150, 50, "SKIP  >>", ACT_SKIP, 0, 0, 0, 1);
			button (ox + 164, 150, 150, 50, "<< REPLAY", ACT_REPLAY, 0, 0, 0, 1);
			break;
		default:
			draw_legend_list (ox, deck_ctx == CTX3DS_COMBAT ? legend_combat
					: deck_ctx == CTX3DS_STARBASE ? legend_starbase
					: (deck_ctx == CTX3DS_HYPERSPACE || deck_ctx == CTX3DS_SOLARSYS)
					? legend_flight : legend_menu, 32);
			break;
	}
	if (deck_screen () != GFX_BOTTOM)
	{	/* no touch on the top screen: regions are useless, say how to swap */
		nregions = 0;
		fill (0, 228, deck_w, 12, COL_BG);
		text_center (0, 228, deck_w, "ZL: game to the top screen, touch here",
				COL_DIM, 0);
	}
}

/* ------------------------------------------------------------------ */
/* play mode: the game on the touch screen, a dashboard on top          */
/* ------------------------------------------------------------------ */
/* The game's 320x240 frame fills the bottom screen 1:1 and the stylus acts
 * on it: tap space to fly there, hold to steer, tap a Super Melee slot or
 * button. Big buttons appear over the game only while they are needed
 * (menus, replies, the ship picker). The top screen becomes a dashboard you
 * glance at: the galaxy map and the ship's numbers. ZL swaps back to the
 * game on top in 3D with the touch deck below. */

static int map_open = 0;               /* galaxy map over the game (MAP tab) */
static int steer_mode = 0;             /* 0 off, 1 follow the stylus, 2 fly to a point, 3 cruise */
static int steer_tx, steer_ty, steer_best;
static u64 steer_t0;
static u64 touch_down_ms;

static int
play_mode (void)
{
	return layout == LAYOUT_GAME_BOTTOM;
}

static int
flight_ctx (void)
{
	return deck_ctx == CTX3DS_HYPERSPACE || deck_ctx == CTX3DS_SOLARSYS;
}

static void
target_overlay (void)
{
	deck_px = ovl_buf;
	deck_w = 320;
}

static void
target_dash (void)
{
	deck_px = deck_buf;
	deck_w = 400;
}

#define SHADE(c, a) (((c) & 0xFFFFFF00u) | (u32) (a))

static void
steer_stop (void)
{
	if (steer_mode)
	{
		steer_mode = 0;
		deck_dirty = 1;
	}
}

/* ---- dashboard (top) ---- */

static void
draw_dash_flight (void)
{
	char buf[96], name[48];
	const int sv_cx = map_cx, sv_cy = map_cy, sv_follow = map_follow;
	const int px = 262;
	int y = 42;

	port3ds_game_read (&ship);
	/* the map follows the ship here; the MAP tab keeps its own view */
	map_x = 4; map_y = 40; map_w = 250; map_h = 196;
	nav_dash = 1;
	map_follow = 1;
	draw_nav (0);
	nav_dash = 0;
	map_cx = sv_cx; map_cy = sv_cy; map_follow = sv_follow;
	map_x = 2; map_y = 40; map_w = 212; map_h = 170;

	if (ship.quasispace)
	{
		text (px, y, "QUASISPACE", COL_ACCENT, 0);
		y += 18;
	}
	else if (deck_ctx == CTX3DS_HYPERSPACE && ship.have_pos)
	{
		text (px, y, "HYPERSPACE", COL_ACCENT, 0);
		y += 13;
		snprintf (buf, sizeof buf, "%d.%d : %d.%d", ship.ux / 10, ship.ux % 10,
				ship.uy / 10, ship.uy % 10);
		text (px, y, buf, COL_DIM, 0);
		y += 18;
	}
	else if (ship.star >= 0)
	{
		char lines[2][64];
		int nl, i;
		port3ds_star_name (ship.star, name, sizeof name);
		nl = wrap_text (name, 22, lines, 2);
		for (i = 0; i < nl; ++i, y += 13)
			text (px, y, lines[i], COL_ACCENT, 0);
		text (px, y, ship.in_orbit ? "In orbit" : "Solar system", COL_DIM, 0);
		y += 18;
	}
	if (ship.have_dest && !ship.quasispace)
	{
		int k, best = -1, need;
		for (k = 0; k < port3ds_star_count (); ++k)
		{
			int x0, y0, c, sz;
			port3ds_star_get (k, &x0, &y0, &c, &sz);
			if (x0 == ship.dest_x && y0 == ship.dest_y)
			{
				best = k;
				break;
			}
		}
		if (best >= 0)
			port3ds_star_name (best, name, sizeof name);
		else
			strcpy (name, "map point");
		name[22] = '\0';
		text (px, y, "COURSE", COL_DIM, 0); y += 13;
		text (px, y, name, COL_HOT, 0); y += 13;
		need = port3ds_fuel_needed (ship.ux, ship.uy, ship.dest_x, ship.dest_y);
		k = need - 5 > 0 ? need - 5 : 0;
		snprintf (buf, sizeof buf, "DIST  %d.%d", k / 10, k % 10);
		text (px, y, buf, COL_TEXT, 0); y += 13;
		fmt_fuel (name, sizeof name, need);
		snprintf (buf, sizeof buf, "FUEL  %s", name);
		text (px, y, buf, need > ship.fuel ? RGB (0xFF, 0x58, 0x58) : COL_TEXT, 0);
		y += 18;
	}
	else
	{
		text (px, y, "No course set", COL_DIM, 0);
		y += 18;
	}
	if (steer_mode)
		text (px, y, steer_mode == 1 ? "Steering" : "Flying by touch", COL_HOT, 0);
	y = 172;
	text (px, y, "Tap space: fly there", COL_DIM, 0); y += 13;
	text (px, y, "Hold: steer", COL_DIM, 0); y += 13;
	text (px, y, "MAP: set a course", COL_DIM, 0); y += 13;
	text (px, y, "MENU: ship menu", COL_DIM, 0); y += 13;
	text (px, y, "ZL: game on top, 3D", COL_DIM, 0);
}

static void
draw_dash_melee (void)
{
	int t;
	for (t = 0; t < 2; ++t)
	{
		const int y0 = 30 + t * 92, ctl = melee.control[t == 0 ? 1 : 0];
		char buf[64];
		int i;
		snprintf (buf, sizeof buf, "%s  %s", t == 0 ? "TOP" : "BOTTOM", melee.name[t]);
		text (8, y0, buf, COL_ACCENT, 0);
		snprintf (buf, sizeof buf, "%d PTS  %s", melee.value[t], control_name[ctl < 5 ? ctl : 0]);
		text (392 - text_w (buf, 0), y0, buf, COL_DIM, 0);
		for (i = 0; i < 14; ++i)
		{
			const int sh = melee.ships[t][i];
			text (8 + (i % 7) * 55, y0 + 18 + (i / 7) * 16,
					sh == 255 ? "-" : port3ds_ship_short (sh),
					sh == 255 ? COL_OFF : COL_TEXT, 0);
		}
	}
	text_center (0, 214, 400, "Touch screen: tap a ship slot to change it,", COL_DIM, 0);
	text_center (0, 226, 400, "or the buttons on the right of the game", COL_DIM, 0);
}

static void
draw_dashboard (void)
{
	const int mode = deck_mode ();
	char buf[48];

	target_dash ();
	nregions = 0;
	fill (0, 0, 400, 240, COL_BG);
	fill (0, 0, 400, 22, COL_HEADER);
	fill (0, 22, 400, 1, COL_BTN_EDGE);
	text (6, 5, mode_title (mode), COL_ACCENT, 0);
	if (show_stats)
		snprintf (buf, sizeof buf, "%lu fps  %lu.%lu ms", (unsigned long) stat_fps,
				(unsigned long) (stat_us / 1000), (unsigned long) ((stat_us % 1000) / 100));
	else
		snprintf (buf, sizeof buf, "ZL swap screens  ZR help");
	text (394 - text_w (buf, 0), 5, buf, COL_DIM, 0);

	if (mode == MODE_HELP)
	{
		draw_man_top ();
		return;
	}
	if (flight_ctx () && mode != MODE_REPLIES && mode != MODE_TALK)
	{
		draw_dash_flight ();
		return;
	}
	switch (mode)
	{
		case MODE_MELEE:
		case MODE_PICK:
		case MODE_NET:
			draw_dash_melee ();
			break;
		case MODE_REPLIES:
		case MODE_TALK:
			draw_legend_list (40, legend_talk, 34);
			text_center (0, 200, 400, mode == MODE_REPLIES
					? "Tap your reply on the touch screen"
					: "SKIP and REPLAY are on the touch screen", COL_DIM, 1);
			break;
		case MODE_TEXT:
			draw_legend_list (40, legend_text, 34);
			text_center (0, 200, 400, "OPEN KEYBOARD is on the touch screen", COL_DIM, 1);
			break;
		case MODE_MENU:
			draw_legend_list (40, legend_menu, 34);
			text_center (0, 200, 400, "Tap a choice on the touch screen", COL_DIM, 1);
			break;
		default:
			draw_legend_list (40, deck_ctx == CTX3DS_COMBAT ? legend_combat
					: deck_ctx == CTX3DS_STARBASE ? legend_starbase : legend_menu, 34);
			break;
	}
}

/* ---- overlay (bottom, over the game) ---- */

/* the game's "really quit?" window, as two big buttons (ox: left edge) */
static void
draw_confirm (int ox)
{
	const int mel = port3ds_in_melee ();
	const char *q = mel ? "End this battle?" : "Quit to the main menu?";
	const char *n1 = mel ? "Back to the Super Melee screen." : "Anything not saved is lost.";
	text (ox + (320 - text_w (q, 1)) / 2, 50, q, COL_TEXT, 1);
	text (ox + (320 - text_w (n1, 0)) / 2, 72, n1, COL_DIM, 0);
	button (ox + 20, 100, 280, 50, mel ? "END BATTLE" : "QUIT TO MAIN MENU", ACT_CONFIRM, 1,
			confirm_resp, 0, 1);
	button (ox + 20, 162, 280, 50, "KEEP PLAYING", ACT_CONFIRM, 2, !confirm_resp, 0, 1);
}

static void
ovl_page (const char *title, int close_act)
{	/* a full-screen page that covers the game */
	fill (0, 0, 320, 240, COL_BG);
	fill (0, 0, 320, 22, COL_HEADER);
	fill (0, 22, 320, 1, COL_BTN_EDGE);
	text (5, 5, title, COL_ACCENT, 0);
	if (close_act)
		button (250, 2, 66, 18, "CLOSE", close_act, 0, 0, 0, 0);
}

static void
draw_ovl_tabs (void)
{
	static const struct { const char *l; int act; int w; } t[] = {
		{ "MENU", ACT_TAB_MENU, 56 }, { "MAP", ACT_TAB_MAP, 56 }, { "II", ACT_PAUSE, 34 },
		{ "?", ACT_HELP, 34 }, { "SWAP", ACT_SWAP, 56 } };
	int i, x = 2;
	for (i = 0; i < 5; ++i)
	{
		button (x, 1, t[i].w, 16, t[i].l, t[i].act, 0, 0, 0, 0);
		x += t[i].w + 2;
	}
	if (steer_mode == 2)
	{	/* where the ship is flying to */
		int k;
		for (k = -4; k <= 4; ++k)
		{
			if (steer_tx + k >= 0 && steer_tx + k < 320 && steer_ty >= 0 && steer_ty < 240)
				ovl_buf[steer_ty * 320 + steer_tx + k] = COL_HOT;
			if (steer_ty + k >= 0 && steer_ty + k < 240 && steer_tx >= 0 && steer_tx < 320)
				ovl_buf[(steer_ty + k) * 320 + steer_tx] = COL_HOT;
		}
	}
}

static void
draw_ovl_menu (void)
{
	const int x = 6, w = 240, top = 22, bot = 236;
	const int n = menu_n + (menu_base == P3M_MAIN);   /* + MANUAL on the main menu */
	int i, h, gap = 3, total, y;
	if (menu_n <= 0)
		return;
	h = (bot - top) / n - gap;
	if (h > 34)
		h = 34;
	total = n * (h + gap) - gap;
	y = top + (bot - top - total) / 2;
	fill (x - 4, y - 4, w + 8, total + 8, SHADE (COL_BG, 0xC8));
	for (i = 0; i < n; ++i, y += h + gap)
		if (i == menu_n)
			button (x, y, w, h, "MANUAL", ACT_HELP, 0, 0, 0, h >= 18);
		else
			button (x, y, w, h, menu_items[i].label, ACT_MENU, i,
					menu_items[i].state == menu_cur, 0, h >= 18);
}

static void
draw_ovl_replies (void)
{
	static char wrapped[MAX_REPLIES][3][64];
	int i, n = 0, hs[MAX_REPLIES], nls[MAX_REPLIES], total = 0, more = 0, y;

	SDL_mutexP (comm_lock);
	if (comm_page >= comm_n)
		comm_page = 0;
	for (i = comm_page; i < comm_n && n < MAX_REPLIES; ++i)
	{
		const int nl = wrap_text (comm_text[i], 50, wrapped[n], 3);
		const int h = 8 + nl * 13;
		if (total + h + 3 > 164 && n > 0)
		{
			more = 1;
			break;
		}
		hs[n] = h;
		nls[n] = nl;
		total += h + 3;
		++n;
	}
	if (more || comm_page > 0)
		total += 24;
	y = 239 - total;
	fill (0, y - 3, 320, 243 - y, SHADE (COL_BG, 0xE0));
	for (i = 0; i < n; ++i)
	{
		const int idx = comm_page + i, hot = (idx == comm_cur);
		int k;
		box (3, y, 314, hs[i], hot ? COL_ACCENT : COL_BTN_EDGE, COL_BG);
		box (4, y + 1, 312, hs[i] - 2,
				(touch_live && touch_act == ACT_REPLY && touch_arg == idx)
				? COL_BTN_DOWN : COL_BTN, hot ? COL_ACCENT : COL_BTN_EDGE);
		for (k = 0; k < nls[i]; ++k)
			text (9, y + 4 + k * 13, wrapped[i][k], COL_TEXT, 0);
		add_region (3, y, 314, hs[i], ACT_REPLY, idx);
		y += hs[i] + 3;
	}
	SDL_mutexV (comm_lock);
	if (more || comm_page > 0)
		button (3, y, 314, 21, more ? "MORE REPLIES  v" : "BACK TO FIRST  ^",
				ACT_REPLY_MORE, 0, 0, 0, 0);
}

static void
draw_overlay (void)
{
	const int mode = deck_mode ();
	memset (ovl_buf, 0, sizeof ovl_buf);
	target_overlay ();
	nregions = 0;
	switch (mode)
	{
		case MODE_HELP:
			ovl_page ("MANUAL", 0);
			draw_man_buttons (0);
			break;
		case MODE_CONFIRM:
			ovl_page ("QUIT", 0);
			draw_confirm (0);
			break;
		case MODE_TEXT:
			button (40, 96, 240, 48, "OPEN KEYBOARD", ACT_KEYBOARD, 0, 1, 0, 1);
			break;
		case MODE_NET:
			ovl_page ("NETWORK MELEE", 0);
			draw_net (0);
			break;
		case MODE_PICK:
			ovl_page ("PICK YOUR SHIP", 0);
			draw_pick (0);
			break;
		case MODE_MELEE:
			if (melee_pick_side >= 0)
			{
				ovl_page ("SUPER MELEE", ACT_MELEE_CANCEL);
				draw_melee (0);
			}
			break;
		case MODE_MENU:
			draw_ovl_menu ();
			break;
		case MODE_REPLIES:
			draw_ovl_replies ();
			break;
		case MODE_TALK:
			button (4, 216, 96, 22, "<< REPLAY", ACT_REPLAY, 0, 0, 0, 0);
			button (220, 216, 96, 22, "SKIP >>", ACT_SKIP, 0, 0, 0, 0);
			break;
		case MODE_NAV:
			if (map_open)
			{
				ovl_page ("GALAXY MAP", ACT_MAP_CLOSE);
				port3ds_game_read (&ship);
				draw_nav (0);
			}
			else
				draw_ovl_tabs ();
			break;
		default:
			break;
	}
	target_dash ();
}

/* the game frame with the overlay on top, into a column-major framebuffer */
static void
blit_game_ovl (u32 *fb, const u32 *src, int pitch)
{
	int x, y;
	for (x = 0; x < 320; ++x)
	{
		u32 *col = fb + x * 240 + 239;
		const u32 *sp = src + x;
		const u32 *o = ovl_buf + x;
		for (y = 0; y < 240; ++y, sp += pitch, o += 320)
		{
			const u32 ov = *o, a = ov & 0xFF;
			if (!a)
				col[-y] = *sp;
			else if (a == 0xFF)
				col[-y] = ov;
			else
			{
				const u32 g = *sp;
				u32 r = 0xFF;
				int sh;
				for (sh = 8; sh < 32; sh += 8)
				{
					const u32 cg = (g >> sh) & 0xFF, co = (ov >> sh) & 0xFF;
					r |= ((co * a + cg * (255 - a)) / 255) << sh;
				}
				col[-y] = r;
			}
		}
	}
}

/* ---- the stylus on the game ---- */

static void
melee_tap (int x, int y)
{
	if (x < 246)
	{	/* a fleet slot: 2 x 7 boxes of 34 px per team */
		const int col = (x - 2) / 35;
		int side, row;
		if (x < 2 || col > 6)
			return;
		for (side = 0; side < 2; ++side)
			for (row = 0; row < 2; ++row)
			{
				const int y0 = 21 + side * 92 + row * 35;
				if (y >= y0 && y < y0 + 34)
				{
					melee_pick_side = side;
					melee_pick_slot = row * 7 + col;
					return;
				}
			}
	}
	else
	{	/* the button column on the right */
		int opt;
		if (y < 22) opt = P3MO_NET_TOP;
		else if (y < 57) opt = P3MO_CTRL_TOP;
		else if (y < 67) opt = P3MO_SAVE_TOP;
		else if (y < 78) opt = P3MO_LOAD_TOP;
		else if (y < 143) opt = P3MO_BATTLE;
		else if (y < 155) opt = P3MO_LOAD_BOT;
		else if (y < 165) opt = P3MO_SAVE_BOT;
		else if (y < 200) opt = P3MO_CTRL_BOT;
		else if (y < 212) opt = P3MO_NET_BOT;
		else opt = P3MO_QUIT;
		if ((opt == P3MO_NET_TOP || opt == P3MO_NET_BOT) && !melee.net)
			return;
		port3ds_melee_option (opt);
	}
}

static int
game_flight_touchable (void)
{
	return play_mode () && flight_ctx () && deck_mode () == MODE_NAV && !map_open;
}

static void
game_touch_down (int x, int y)
{
	steer_stop ();
	touch_down_ms = osGetTime ();
	if (game_flight_touchable ())
	{
		steer_mode = 1;
		steer_tx = x;
		steer_ty = y;
		steer_t0 = osGetTime ();
	}
}

static void
game_touch_move (int x, int y)
{
	if (steer_mode == 1)
	{
		steer_tx = x;
		steer_ty = y;
	}
}

static void
game_touch_up (int x, int y, int moved)
{
	const int tap = osGetTime () - touch_down_ms < 300 && !moved;
	if (deck_mode () == MODE_MELEE && melee_pick_side < 0)
	{
		if (tap)
			melee_tap (x, y);
		return;
	}
	if (steer_mode != 1)
		return;
	steer_mode = 0;
	if (tap && game_flight_touchable ())
	{
		int sx, sy, f;
		if (!port3ds_flight_ship (&sx, &sy, &f))
			return;
		steer_t0 = osGetTime ();
		if (deck_ctx == CTX3DS_SOLARSYS)
		{	/* fly to the spot */
			steer_mode = 2;
			steer_tx = x;
			steer_ty = y;
			steer_best = 1 << 30;
		}
		else
		{	/* HyperSpace scrolls with the ship: cruise that way */
			steer_mode = 3;
			steer_tx = x - sx;
			steer_ty = y - sy;
		}
	}
	deck_dirty = 1;
}

/* virtual buttons that turn the ship toward the target and thrust */
static u32
steer_keys (void)
{
	int sx, sy, f, dx, dy, t, diff, dist;
	u32 k = 0;
	if (!steer_mode)
		return 0;
	if (!game_flight_touchable () || !port3ds_flight_ship (&sx, &sy, &f)
			|| osGetTime () - steer_t0 > 90000)
	{
		steer_stop ();
		return 0;
	}
	if (steer_mode == 3)
	{
		dx = steer_tx;
		dy = steer_ty;
	}
	else
	{
		dx = steer_tx - sx;
		dy = steer_ty - sy;
	}
	dist = (int) sqrtf ((float) (dx * dx + dy * dy));
	if (steer_mode == 2)
	{
		if (dist < 5)
		{
			steer_stop ();
			return 0;
		}
		if (dist < steer_best)
			steer_best = dist;
		else if (dist > steer_best + 24)
		{	/* overshot, or the view zoomed: stop rather than wander */
			steer_stop ();
			return 0;
		}
	}
	if (dist < 4)
		return 0;
	{	/* facing 0 is up, 16 steps clockwise */
		const float a = atan2f ((float) dx, (float) -dy);
		t = ((int) floorf (a * 16.0f / 6.2831853f + 0.5f)) & 15;
	}
	diff = (t - f) & 15;
	if (diff >= 1 && diff <= 8)
		k |= 1u << VK_RIGHT;
	else if (diff >= 9)
		k |= 1u << VK_LEFT;
	if (diff <= 1 || diff >= 15)
		k |= 1u << VK_UP;
	return k;
}

static void
show_deck (void)
{
	const gfxScreen_t scr = deck_screen ();
	u16 fw, fh;
	u32 *fb;

	if (play_mode ())
	{
		draw_dashboard ();
		draw_home_note ();
		fb = (u32 *) gfxGetFramebuffer (GFX_TOP, GFX_LEFT, &fw, &fh);
		blit_to_fb (fb, 0, deck_buf, 400, 400);
		GSPGPU_FlushDataCache (fb, 400 * 240 * 4);
		gfxScreenSwapBuffers (GFX_TOP, false);
		draw_overlay ();
		deck_dirty = 0;
		if (video_surface)
			port3ds_present (video_surface);      /* re-composite the bottom */
		return;
	}
	if (help_page >= 0)
	{	/* the manual takes the top screen from the game */
		u32 *t = (u32 *) gfxGetFramebuffer (GFX_TOP, GFX_LEFT, &fw, &fh);
		deck_px = man_buf;
		deck_w = 400;
		draw_man_top ();
		blit_to_fb (t, 0, man_buf, 400, 400);
		GSPGPU_FlushDataCache (t, 400 * 240 * 4);
		gfxScreenSwapBuffers (GFX_TOP, false);
	}
	deck_px = deck_buf;
	deck_w = (scr == GFX_TOP) ? 400 : 320;
	draw_deck ();
	draw_home_note ();
	fb = (u32 *) gfxGetFramebuffer (scr, GFX_LEFT, &fw, &fh);
	blit_to_fb (fb, 0, deck_px, deck_w, deck_w);
	GSPGPU_FlushDataCache (fb, (u32) deck_w * 240 * 4);
	gfxScreenSwapBuffers (scr, false);
	deck_dirty = 0;
}

/* ------------------------------------------------------------------ */
/* video                                                                */
/* ------------------------------------------------------------------ */

/* ---- stereo 3D -----------------------------------------------------------
 * The game draws one flat 320x240 picture. Alongside it the port keeps a
 * depth per pixel: drawing commands are tagged in the queue (starfield
 * layers in HyperSpace and combat, the solar system behind the ships, the
 * orbiting planet in front of the screen), and every primitive that lands on
 * the main screen stamps the current tag into the pixels it actually covers
 * (transparent sprite pixels are skipped). Presenting with the 3D slider up
 * shifts each pixel by its depth for each eye, nearer pixels winning. */

static signed char depth_buf[240][320];
static volatile int cur_depth = 0;
static SDL_Surface *main_screen = NULL;
static volatile u64 depth_last_used = 0;
static int stereo_on = 0;
static float stereo_slider = 0.0f;
int port3ds_stereo_ok = 1;

void
port3ds_set_main_screen (SDL_Surface *m)
{
	main_screen = m;
	memset (depth_buf, 0, sizeof depth_buf);
}

void
port3ds_depth_now (int depth)
{
	cur_depth = depth < -16 ? -16 : depth > 15 ? 15 : depth;
	if (depth)
		depth_last_used = osGetTime ();
}

static int
clip_box (SDL_Surface *dst, int *x, int *y, int *w, int *h, int *ox, int *oy)
{
	int x0 = dst->clip_rect.x, y0 = dst->clip_rect.y;
	int x1 = x0 + dst->clip_rect.w, y1 = y0 + dst->clip_rect.h;
	if (x1 > 320) x1 = 320;
	if (y1 > 240) y1 = 240;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	*ox = *oy = 0;
	if (*x < x0) { *ox = x0 - *x; *w -= *ox; *x = x0; }
	if (*y < y0) { *oy = y0 - *y; *h -= *oy; *y = y0; }
	if (*x + *w > x1) *w = x1 - *x;
	if (*y + *h > y1) *h = y1 - *y;
	return *w > 0 && *h > 0;
}

void
port3ds_depth_mark_rect (SDL_Surface *dst, int x, int y, int w, int h)
{
	int ox, oy, r;
	if (dst != main_screen || !clip_box (dst, &x, &y, &w, &h, &ox, &oy))
		return;
	for (r = 0; r < h; ++r)
		memset (&depth_buf[y + r][x], cur_depth, (size_t) w);
}

void
port3ds_depth_mark_line (SDL_Surface *dst, int x1, int y1, int x2, int y2)
{
	int dx, dy, n, i;
	const SDL_Rect *c;
	if (dst != main_screen)
		return;
	c = &dst->clip_rect;
	dx = x2 - x1;
	dy = y2 - y1;
	n = abs (dx) > abs (dy) ? abs (dx) : abs (dy);
	for (i = 0; i <= n; ++i)
	{
		const int x = n ? x1 + (dx * i + (dx > 0 ? n / 2 : -n / 2)) / n : x1;
		const int y = n ? y1 + (dy * i + (dy > 0 ? n / 2 : -n / 2)) / n : y1;
		if (x >= c->x && y >= c->y && x < c->x + c->w && y < c->y + c->h
				&& x >= 0 && y >= 0 && x < 320 && y < 240)
			depth_buf[y][x] = (signed char) cur_depth;
	}
}

void
port3ds_depth_mark_blit (SDL_Surface *src, const SDL_Rect *sr,
		SDL_Surface *dst, const SDL_Rect *dr)
{
	int sx = sr ? sr->x : 0, sy = sr ? sr->y : 0;
	int w = sr ? sr->w : src->w, h = sr ? sr->h : src->h;
	int x = dr ? dr->x : 0, y = dr ? dr->y : 0;
	int ox, oy, r, c;
	const int ck = (src->flags & SDL_SRCCOLORKEY) != 0;
	const Uint32 amask = (src->flags & SDL_SRCALPHA) ? src->format->Amask : 0;
	const Uint32 key = src->format->colorkey;
	const int bpp = src->format->BytesPerPixel;

	if (dst != main_screen)
		return;
	if (sx < 0) { x -= sx; w += sx; sx = 0; }
	if (sy < 0) { y -= sy; h += sy; sy = 0; }
	if (sx + w > src->w) w = src->w - sx;
	if (sy + h > src->h) h = src->h - sy;
	if (!clip_box (dst, &x, &y, &w, &h, &ox, &oy))
		return;
	sx += ox;
	sy += oy;
	if ((!ck && !amask) || !src->pixels)
	{	/* opaque (or pixels not reachable): the whole box */
		for (r = 0; r < h; ++r)
			memset (&depth_buf[y + r][x], cur_depth, (size_t) w);
		return;
	}
	for (r = 0; r < h; ++r)
	{
		const Uint8 *row = (const Uint8 *) src->pixels + (sy + r) * src->pitch + sx * bpp;
		signed char *d = &depth_buf[y + r][x];
		for (c = 0; c < w; ++c)
		{
			Uint32 p;
			switch (bpp)
			{
				case 1: p = row[c]; break;
				case 2: p = ((const Uint16 *) row)[c]; break;
				case 4: p = ((const Uint32 *) row)[c]; break;
				default: p = row[c * 3] | (row[c * 3 + 1] << 8) | (row[c * 3 + 2] << 16); break;
			}
			if ((ck && p == key) || (amask && !(p & amask)))
				continue;
			d[c] = (signed char) cur_depth;
		}
	}
}

static int
stereo_wanted (void)
{
	return game_screen () == GFX_TOP && stereo_slider > 0.02f && port3ds_stereo_ok
			&& (deck_ctx == CTX3DS_COMBAT || deck_ctx == CTX3DS_HYPERSPACE
				|| deck_ctx == CTX3DS_SOLARSYS)
			&& osGetTime () - depth_last_used < 1500;
}

static void
present_stereo (const SDL_Surface *frame)
{
	static signed char zb[400 * 240];
	const u32 *pix = (const u32 *) frame->pixels;
	const int pitch = frame->pitch / 4;
	int eye;
	for (eye = 0; eye < 2; ++eye)
	{
		u16 fw, fh;
		u32 *fb = (u32 *) gfxGetFramebuffer (GFX_TOP, eye ? GFX_RIGHT : GFX_LEFT, &fw, &fh);
		int off[32], d, x, y;
		for (d = -16; d < 16; ++d)
		{	/* depth 1 = 1 px of disparity at full slider, split over the eyes */
			const float disp = d * stereo_slider * 1.25f;
			const int half = (int) (disp >= 0 ? disp / 2 + 0.5f : disp / 2 - 0.5f);
			off[d + 16] = eye ? half : -half;
		}
		memset (fb, 0, 400 * 240 * sizeof (u32));
		memset (zb, 127, sizeof zb);
		for (y = 0; y < 240; ++y)
		{
			const u32 *s = pix + y * pitch;
			const signed char *dr = depth_buf[y];
			const int fy = 239 - y;
			for (x = 0; x < 320; ++x)
			{
				const int dd = dr[x];
				const int X = 40 + x + off[dd + 16];
				int idx;
				/* black is empty space: never let it hide what lies behind */
				if (!(s[x] & 0xFFFFFF00u) || (unsigned) X >= 400u)
					continue;
				idx = X * 240 + fy;
				if (dd < zb[idx])
				{
					zb[idx] = (signed char) dd;
					fb[idx] = s[x];
				}
			}
		}
		GSPGPU_FlushDataCache (fb, 400 * 240 * 4);
	}
}

/* the slider moved: re-present the last frame even if the game is idle */
static void
stereo_follow_slider (void)
{
	const float sl = osGet3DSliderState ();
	if (sl - stereo_slider > 0.03f || stereo_slider - sl > 0.03f
			|| (sl == 0.0f) != (stereo_slider == 0.0f))
	{
		stereo_slider = sl;
		if (video_surface && (stereo_on || stereo_wanted ()))
			port3ds_present (video_surface);
	}
}

SDL_Surface *
port3ds_create_video_surface (void)
{
	/* RGBA8 framebuffer pixels are 0xRRGGBBAA words; UQM picks the unused
	 * low byte for its own alpha, which the LCD ignores. */
	video_surface = SDL_CreateRGBSurface (SDL_SWSURFACE, 320, 240, 32,
			0xFF000000, 0x00FF0000, 0x0000FF00, 0);
	return video_surface;
}

void
port3ds_present (SDL_Surface *frame)
{
	const gfxScreen_t scr = game_screen ();
	const int x0 = (scr == GFX_TOP) ? 40 : 0;
	u64 t0;
	u16 fw, fh;
	u32 *fb;

	if (!frame || (help_page >= 0 && scr == GFX_TOP))
		return;
	/* UQM redraws as fast as its main loop spins during fades (hundreds of
	 * presents a second). Cap at the LCD's 60 Hz: the sleep hands the core
	 * to the game and audio threads instead, and a swap is never queued
	 * twice in one refresh (which would draw into the visible buffer). */
	{
		static u64 last_present = 0;
		const u64 now = svcGetSystemTick ();
		const u64 frame_ticks = SYSCLOCK_ARM11 / 60;
		if (last_present && now - last_present < frame_ticks)
			svcSleepThread ((s64) ((frame_ticks - (now - last_present))
					* 1000000000ULL / SYSCLOCK_ARM11));
		last_present = svcGetSystemTick ();
	}
	t0 = svcGetSystemTick ();
	{
		const int want = stereo_wanted ();
		if (want != stereo_on)
		{
			stereo_on = want;
			gfxSet3D (want ? true : false);
		}
	}
	if (stereo_on)
	{
		present_stereo (frame);
		gfxScreenSwapBuffers (GFX_TOP, true);
		goto stats;
	}
	fb = (u32 *) gfxGetFramebuffer (scr, GFX_LEFT, &fw, &fh);
	if (scr == GFX_TOP)
	{	/* pillar-box: the 40-pixel bands either side stay black */
		clear_fb_columns (fb, 0, 40);
		clear_fb_columns (fb, 360, 40);
	}
	if (scr == GFX_BOTTOM && play_mode ())
		blit_game_ovl (fb, (const u32 *) frame->pixels, frame->pitch / 4);
	else
		blit_to_fb (fb, x0, (const u32 *) frame->pixels, 320, frame->pitch / 4);
	GSPGPU_FlushDataCache (fb, (u32) (scr == GFX_TOP ? 400 : 320) * 240 * 4);
	gfxScreenSwapBuffers (scr, false);

stats:
	/* stats: presents per second and the cost of this function */
	++stat_frames;
	stat_ticks += svcGetSystemTick () - t0;
	{
		const u64 now = osGetTime ();
		if (now - stat_second_start >= 1000)
		{
			stat_fps = stat_frames;
			stat_us = stat_frames ? (stat_ticks * 1000000ULL
					/ SYSCLOCK_ARM11) / stat_frames : 0;
			stat_frames = 0;
			stat_ticks = 0;
			stat_second_start = now;
			if (show_stats)
				deck_dirty = 1;
		}
	}
}

static void
relayout (void)
{
	u16 fw, fh;
	int i;
	/* clear both buffers of both screens, then redraw everything */
	for (i = 0; i < 2; ++i)
	{
		u32 *t = (u32 *) gfxGetFramebuffer (GFX_TOP, GFX_LEFT, &fw, &fh);
		u32 *b = (u32 *) gfxGetFramebuffer (GFX_BOTTOM, GFX_LEFT, &fw, &fh);
		memset (t, 0, 400 * 240 * 4);
		memset (b, 0, 320 * 240 * 4);
		GSPGPU_FlushDataCache (t, 400 * 240 * 4);
		GSPGPU_FlushDataCache (b, 320 * 240 * 4);
		gfxScreenSwapBuffers (GFX_TOP, false);
		gfxScreenSwapBuffers (GFX_BOTTOM, false);
	}
	set_backlight (0);
	port3ds_present (video_surface);
	show_deck ();
}

/* ------------------------------------------------------------------ */
/* input                                                                */
/* ------------------------------------------------------------------ */

/* button -> key in UQM's key maps (content menu.key + our flight.cfg):
 *   A  RightControl  menu select, flight weapon
 *   B  RightShift    menu cancel, flight special
 *   X  LeftAlt       menu special
 *   Y  y             flight thrust (second binding of "up")
 *   START Pause, SELECT Escape (retreat / cancel text), L/R PageUp/PageDown
 *   D-pad, Circle Pad, C-stick -> arrow keys */

static const SDLKey vk_sym[VK_COUNT] = {
	SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_RCTRL, SDLK_RSHIFT,
	SDLK_LALT, SDLK_y, SDLK_PAUSE, SDLK_ESCAPE, SDLK_PAGEUP, SDLK_PAGEDOWN };

static u32 vk_held = 0;
static int tap_vk = -1;
static u64 tap_release_at = 0;

static void
send_key (SDLKey sym, int down)
{
	SDL_Event e;
	memset (&e, 0, sizeof e);
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.sym = sym;
	ProcessInputEvent (&e);
}

/* A deck button "presses" a game key for ~100 ms so the game thread, which
 * samples input once per game frame, cannot miss it. */
static void
tap_key (int vk)
{
	if (tap_vk >= 0)
		send_key (vk_sym[tap_vk], 0);
	tap_vk = vk;
	tap_release_at = osGetTime () + 100;
	send_key (vk_sym[vk], 1);
}

static u32
read_virtual_keys (u32 held)
{
	u32 v = 0;
	circlePosition cp;
	const int dz = 48;   /* of ~155: firm push, avoids drift */

	hidCircleRead (&cp);
	if ((held & (KEY_DUP | KEY_CSTICK_UP)) || cp.dy > dz) v |= 1u << VK_UP;
	if ((held & (KEY_DDOWN | KEY_CSTICK_DOWN)) || cp.dy < -dz) v |= 1u << VK_DOWN;
	if ((held & (KEY_DLEFT | KEY_CSTICK_LEFT)) || cp.dx < -dz) v |= 1u << VK_LEFT;
	if ((held & (KEY_DRIGHT | KEY_CSTICK_RIGHT)) || cp.dx > dz) v |= 1u << VK_RIGHT;
	if (held & KEY_A) v |= 1u << VK_A;
	if (held & KEY_B) v |= 1u << VK_B;
	if (held & KEY_X) v |= 1u << VK_X;
	if (held & KEY_Y) v |= 1u << VK_Y;
	if (held & KEY_START) v |= 1u << VK_START;
	if (held & KEY_SELECT) v |= 1u << VK_SELECT;
	if (held & KEY_L) v |= 1u << VK_L;
	if (held & KEY_R) v |= 1u << VK_R;
	return v;
}

static void
open_keyboard (void)
{
	SwkbdState kb;
	char out[128];
	int max = text_max;
	SwkbdButton b;

	if (max < 2)
		max = 2;
	if (max > (int) sizeof out)
		max = sizeof out;
	swkbdInit (&kb, SWKBD_TYPE_NORMAL, 2, max - 1);
	swkbdSetInitialText (&kb, text_now);
	swkbdSetHintText (&kb, "Name");
	swkbdSetValidation (&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	memset (out, 0, sizeof out);
	b = swkbdInputText (&kb, out, sizeof out);
	if (b == SWKBD_BUTTON_CONFIRM && out[0])
	{
		strncpy (text_result, out, sizeof text_result - 1);
		text_result[sizeof text_result - 1] = '\0';
		text_ready = 1;
	}
	/* the applet drew over both screens */
	relayout ();
}

static void
toggle_layout (void)
{
	layout = (layout == LAYOUT_GAME_TOP) ? LAYOUT_GAME_BOTTOM : LAYOUT_GAME_TOP;
	map_open = 0;
	steer_stop ();
	help_page = -1;
	touch_act = ACT_NONE;
	touch_live = 0;
	save_port_settings ();
	relayout ();
}


static void
do_action (int act, int arg, int px, int py)
{
	const int ox = 0;   /* actions only exist when the deck is on the bottom */
	switch (act)
	{
		case ACT_PAUSE: tap_key (VK_START); break;
		case ACT_HELP: if (help_page < 0) man_open (); else man_close (); break;
		case ACT_SWAP: toggle_layout (); return;
		case ACT_DIM: set_backlight (1); break;
		case ACT_STATS: show_stats = !show_stats; save_port_settings (); break;
		case ACT_LEFTY: lefty = !lefty; save_port_settings (); break;
		case ACT_KEYBOARD: if (char_mode) open_keyboard (); return;
		case ACT_HELP_PREV: man_step (-1); break;
		case ACT_HELP_CLOSE: man_close (); break;
		case ACT_HELP_NEXT: man_step (1); break;
		case ACT_MAN_SEC: if (arg >= 0 && arg < man_nsec) man_cur = arg; break;
		case ACT_REPLY:
			SDL_mutexP (comm_lock);
			if (arg < comm_n)
				comm_pick = arg;
			comm_n = 0;              /* never send twice */
			SDL_mutexV (comm_lock);
			comm_page = 0;
			break;
		case ACT_REPLY_MORE:
		{
			/* advance to the first reply not shown, or back to the top */
			int i, shown = 0;
			for (i = 0; i < nregions; ++i)
				if (regions[i].act == ACT_REPLY && regions[i].arg >= shown)
					shown = regions[i].arg + 1;
			comm_page = (shown < comm_n && shown > comm_page) ? shown : 0;
			break;
		}
		case ACT_SKIP: tap_key (VK_B); break;
		case ACT_REPLAY: tap_key (VK_LEFT); break;
		case ACT_MAP:
		{
			const int s = nearest_star (ox, px, py, 18);
			if (s >= 0 && s == sel_star)
			{	/* second tap on the same star: go there */
				int x, y, c, sz;
				port3ds_star_get (s, &x, &y, &c, &sz);
				port3ds_request_course (x, y);
				map_open = 0;
			}
			else
				sel_star = s;
			break;
		}
		case ACT_GO:
			if (sel_star >= 0)
			{
				int x, y, c, sz;
				port3ds_star_get (sel_star, &x, &y, &c, &sz);
				port3ds_request_course (x, y);
				map_open = 0;      /* course set: back to the game */
			}
			break;
		case ACT_ZOOM_IN: if (map_zoom < 3) ++map_zoom; break;
		case ACT_ZOOM_OUT: if (map_zoom > 0) --map_zoom; break;
		case ACT_SHIP: map_follow = 1; break;
		case ACT_SHORTCUT: port3ds_request_shortcut (arg); break;
		case ACT_TAB_MENU: tap_key (VK_B); break;      /* B opens the ship menu in flight */
		case ACT_TAB_MAP: map_open = 1; sel_star = -1; map_follow = 1; break;
		case ACT_MAP_CLOSE: map_open = 0; break;
		case ACT_MENU:
			if (arg >= 0 && arg < menu_n)
				port3ds_menu_pick (menu_base, menu_items[arg].state);
			break;
		case ACT_NET:
			if (arg == P3ND_HOST || arg == P3ND_PORT)
			{
				net_keyboard (arg);
				return;
			}
			port3ds_netdlg_act (arg, NULL);
			break;
		case ACT_PICK:
			port3ds_pick (arg / 16, arg % 16 == 15 ? P3_PICK_RANDOM : arg % 16);
			break;
		case ACT_MELEE_OPT: port3ds_melee_option (arg); break;
		case ACT_MELEE_SLOT:
			melee_pick_side = arg / 16;
			melee_pick_slot = arg % 16;
			break;
		case ACT_MELEE_SHIP:
			if (melee_pick_side >= 0)
				port3ds_melee_ship (melee_pick_side, melee_pick_slot, arg);
			melee_pick_side = melee_pick_slot = -1;
			break;
		case ACT_MELEE_CANCEL: melee_pick_side = melee_pick_slot = -1; break;
		case ACT_CONFIRM: port3ds_confirm_act (arg); break;
	}
	deck_dirty = 1;
}

static int drag_x0, drag_y0, drag_last_x, drag_last_y, dragging;

static void
handle_touch (u32 down, u32 held, u32 up)
{
	static touchPosition last;
	touchPosition tp;

	if (backlight_off)
	{	/* any touch just wakes the screen */
		if (down & KEY_TOUCH)
		{
			set_backlight (0);
			deck_dirty = 1;
			touch_act = ACT_NONE;
		}
		return;
	}

	if (held & KEY_TOUCH)
	{
		const Region *r;
		hidTouchRead (&tp);
		last = tp;
		r = region_at (tp.px, tp.py);
		if (down & KEY_TOUCH)
		{
			touch_act = r ? r->act : (play_mode () ? ACT_GAME : ACT_NONE);
			touch_arg = r ? r->arg : 0;
			touch_live = 1;
			if (touch_act != ACT_NONE && touch_act != ACT_GAME)
				ui_click ();   /* every button press clicks */
			drag_x0 = drag_last_x = tp.px;
			drag_y0 = drag_last_y = tp.py;
			dragging = 0;
			deck_dirty = 1;
			if (touch_act == ACT_GAME)
				game_touch_down (tp.px, tp.py);
		}
		else if (touch_act == ACT_GAME)
		{
			if (abs (tp.px - drag_x0) > 6 || abs (tp.py - drag_y0) > 6)
				dragging = 1;
			game_touch_move (tp.px, tp.py);
		}
		else if (touch_act == ACT_MAP)
		{	/* drag the map */
			const int dx = tp.px - drag_last_x, dy = tp.py - drag_last_y;
			if (!dragging && (abs (tp.px - drag_x0) > 5 || abs (tp.py - drag_y0) > 5))
				dragging = 1;
			if (dragging && (dx || dy))
			{
				const float s = map_scale ();
				map_follow = 0;
				map_cx -= (int) (dx / s);
				map_cy += (int) (dy / s);
				if (map_cx < 0) map_cx = 0;
				if (map_cx > 9999) map_cx = 9999;
				if (map_cy < 0) map_cy = 0;
				if (map_cy > 9999) map_cy = 9999;
				drag_last_x = tp.px;
				drag_last_y = tp.py;
				deck_dirty = 1;
			}
		}
		else
		{	/* slid off the button: disarm */
			const int on = r && r->act == touch_act && r->arg == touch_arg;
			if (on != touch_live)
			{
				touch_live = on;
				deck_dirty = 1;
			}
		}
	}
	else if (up & KEY_TOUCH)
	{
		const int act = touch_act, arg = touch_arg;
		const int fire = touch_live && act != ACT_NONE && act != ACT_GAME
				&& !(act == ACT_MAP && dragging);
		touch_act = ACT_NONE;
		touch_live = 0;
		deck_dirty = 1;
		if (act == ACT_GAME)
			game_touch_up (last.px, last.py, dragging);
		else if (fire)
			do_action (act, arg, last.px, last.py);
	}
}

int
port3ds_pump_input (void)
{
	u32 kdown, kheld, kup, v, changed;
	int i;

	apt_events ();
	if (!aptMainLoop ())
	{
		close_now ();
		return 0;
	}
	if (apt_returned)
	{	/* back from the HOME menu or sleep: redraw both screens */
		apt_returned = 0;
		relayout ();
	}

	hidScanInput ();
	kdown = lefty_remap (hidKeysDown ());
	kheld = lefty_remap (hidKeysHeld ());
	kup = lefty_remap (hidKeysUp ());

	/* port hotkeys */
	if (kdown & KEY_ZL)
		toggle_layout ();
	if (kdown & KEY_ZR)
	{
		if (help_page < 0)
			man_open ();
		else
			man_close ();
	}

	handle_touch (kdown, kheld, kup);

	/* hold SELECT: the game's "quit to the main menu?" window (key F10) */
	{
		static u64 sel_since = 0, f10_up_at = 0;
		static int sel_fired = 0;
		const u64 now = osGetTime ();
		if ((kheld & KEY_SELECT) && help_page < 0 && !confirm_on && deck_ctx != CTX3DS_MENU)
		{
			if (!sel_since)
				sel_since = now;
			else if (!sel_fired && now - sel_since >= 800)
			{
				sel_fired = 1;
				send_key (SDLK_F10, 1);
				f10_up_at = now + 100;
			}
		}
		else
		{
			sel_since = 0;
			sel_fired = 0;
		}
		if (f10_up_at && now >= f10_up_at)
		{
			send_key (SDLK_F10, 0);
			f10_up_at = 0;
		}
	}

	/* buttons -> keys */
	v = read_virtual_keys (kheld);
	if (help_page >= 0)
	{	/* reading the manual: the buttons are its own, the game gets none */
		/* the standard for all the 3DS ports: Circle Pad and C-stick scroll
		 * (hold to keep going), the D-pad moves between the section buttons,
		 * L/R previous/next section, B or START close */
		static int last_sd = 0;
		static u64 next_scroll = 0;
		const u64 now = osGetTime ();
		circlePosition cp;
		int sd = 0;
		hidCircleRead (&cp);
		if (cp.dy > 48 || (kheld & KEY_CSTICK_UP))
			sd = -1;
		else if (cp.dy < -48 || (kheld & KEY_CSTICK_DOWN))
			sd = 1;
		if (kdown & (KEY_B | KEY_START))
			man_close ();
		else if (kdown & KEY_L)
			man_step (-1);
		else if (kdown & KEY_R)
			man_step (1);
		else if (kdown & KEY_DLEFT)
			man_go (man_cur - 1);
		else if (kdown & KEY_DRIGHT)
			man_go (man_cur + 1);
		else if (kdown & KEY_DUP)
			man_go (man_cur - 2);
		else if (kdown & KEY_DDOWN)
			man_go (man_cur + 2);
		if (sd && sd != last_sd)
		{
			man_scroll_by (sd);
			next_scroll = now + 300;
		}
		else if (sd && now >= next_scroll)
		{
			man_scroll_by (sd);
			next_scroll = now + 60;
		}
		last_sd = sd;
		v = 0;
		steer_stop ();
	}
	else if (v & ((1u << VK_UP) | (1u << VK_DOWN) | (1u << VK_LEFT) | (1u << VK_RIGHT)
			| (1u << VK_A) | (1u << VK_B)))
		steer_stop ();            /* the buttons take over */
	else
		v |= steer_keys ();
	changed = v ^ vk_held;
	for (i = 0; i < VK_COUNT; ++i)
	{
		if (!(changed & (1u << i)) || i == tap_vk)
			continue;
		send_key (vk_sym[i], (v >> i) & 1);
	}
	vk_held = v;
	if (tap_vk >= 0 && osGetTime () >= tap_release_at)
	{
		if (!(vk_held & (1u << tap_vk)))
			send_key (vk_sym[tap_vk], 0);
		tap_vk = -1;
	}

	/* the deck follows what the game is doing */
	{
		const int ctx = port3ds_game_context ();
		if (ctx != deck_ctx)
		{
			deck_ctx = ctx;
			deck_dirty = 1;
			map_open = 0;
			steer_stop ();
		}
	}
	if (comm_changed)
	{
		comm_changed = 0;
		deck_dirty = 1;
	}
	{	/* replies vanish when the game stops offering them */
		static int was_fresh = 0;
		const int fresh = comm_fresh ();
		if (fresh != was_fresh)
		{
			was_fresh = fresh;
			deck_dirty = 1;
		}
	}
	{	/* the game's menus: redraw when one opens, closes or moves */
		static u32 last_sig = 0;
		int b = -1, c = 0, n;
		u32 sig;
		n = port3ds_menu_items (&b, &c, menu_items, P3_MENU_MAX);
		menu_n = n;
		menu_base = b;
		menu_cur = c;
		sig = n ? ((u32) n << 24) ^ ((u32) b << 8) ^ (u32) c ^ 0x80000000u : 0;
		if (n)
		{
			int i;
			for (i = 0; i < n; ++i)
				sig = sig * 31 + (u32) menu_items[i].label[0] + (u32) menu_items[i].state;
		}
		if (sig != last_sig)
		{
			last_sig = sig;
			deck_dirty = 1;
		}
	}
	{	/* Super Melee setup */
		static u32 last_sig = 0;
		u32 sig = 0;
		melee_on = port3ds_melee_read (&melee);
		if (melee_on)
		{
			const unsigned char *p = (const unsigned char *) &melee;
			size_t i;
			sig = 1;
			for (i = 0; i < sizeof melee; ++i)
				sig = sig * 131 + p[i];
		}
		else
			melee_pick_side = melee_pick_slot = -1;
		if (sig != last_sig)
		{
			last_sig = sig;
			deck_dirty = 1;
		}
	}
	{	/* network dialog */
		static u32 last_sig = 0;
		u32 sig = 0;
		netdlg_on = port3ds_netdlg_read (&netdlg);
		{
			int r = 0;
			const int on = port3ds_confirm_read (&r);
			if (on != confirm_on || (on && r != confirm_resp))
				deck_dirty = 1;
			confirm_on = on;
			confirm_resp = r;
		}
		if (netdlg_on)
		{
			const char *p;
			sig = 5 + (u32) netdlg.delay * 7 + (u32) netdlg.side;
			for (p = netdlg.host; *p; ++p) sig = sig * 31 + (u32) *p;
			for (p = netdlg.port; *p; ++p) sig = sig * 31 + (u32) *p;
		}
		if (sig != last_sig)
		{
			last_sig = sig;
			deck_dirty = 1;
		}
	}
	{	/* choosing ships in a melee round */
		static u32 last_sig = 0;
		u32 sig = 0;
		int pl, i;
		for (pl = 0; pl < 2; ++pl)
		{
			pick_on[pl] = port3ds_pick_read (pl, pick_ships[pl]);
			if (pick_on[pl])
				for (sig += 7 + pl, i = 0; i < 14; ++i)
					sig = sig * 33 + pick_ships[pl][i];
		}
		if (sig != last_sig)
		{
			last_sig = sig;
			deck_dirty = 1;
		}
	}
	stereo_follow_slider ();
	if (play_mode () && flight_ctx () && osGetTime () - last_nav_draw >= 250)
	{	/* dashboard: the ship moves, refresh map and fuel 4x/s */
		last_nav_draw = osGetTime ();
		deck_dirty = 1;
	}
	if (deck_mode () == MODE_NAV && osGetTime () - last_nav_draw >= 250)
		deck_dirty = 1;       /* the ship moves: refresh map and fuel 4x/s */
	if (deck_dirty)
		show_deck ();
	return 1;
}

void
port3ds_set_char_mode (int on)
{
	char_mode = on;
	deck_dirty = 1;      /* picked up by the main thread's next pump */
}

int
port3ds_text_entry_frame (char *base, int maxsize)
{
	strncpy (text_now, base, sizeof text_now - 1);
	text_now[sizeof text_now - 1] = '\0';
	text_max = maxsize;
	if (!text_ready)
		return 0;
	text_ready = 0;
	{
		int n = (int) strlen (text_result);
		if (n > maxsize - 1)
		{	/* cut on a UTF-8 character boundary */
			n = maxsize - 1;
			while (n > 0 && ((unsigned char) text_result[n] & 0xC0) == 0x80)
				--n;
		}
		memcpy (base, text_result, (size_t) n);
		base[n] = '\0';
	}
	return 1;
}

/* ------------------------------------------------------------------ */
/* newlib gaps                                                          */
/* ------------------------------------------------------------------ */

#ifndef UQM3DS_HARNESS
#include <pwd.h>
/* UQM's expandPath() resolves "~" through these; our paths never use "~" */
uid_t getuid (void) { return 0; }
struct passwd *getpwuid (uid_t uid) { (void) uid; return NULL; }
#endif

/* ------------------------------------------------------------------ */
/* start-up and shut-down                                               */
/* ------------------------------------------------------------------ */

static void
fatal_screen (const char *const *lines)
{
	int i;
	deck_w = 400;
	fill (0, 0, deck_w, DECK_H, COL_BG);
	fill (0, 0, deck_w, 22, COL_HEADER);
	text (6, 4, "THE UR-QUAN MASTERS", COL_TEXT, 1);
	for (i = 0; lines[i]; ++i)
		text (8, 34 + i * 15, lines[i], i == 0 ? COL_HOT : COL_TEXT, 0);
	text (8, 222, "Press START to exit.", COL_ACCENT, 0);
	{
		u16 fw, fh;
		u32 *fb = (u32 *) gfxGetFramebuffer (GFX_TOP, GFX_LEFT, &fw, &fh);
		blit_to_fb (fb, 0, deck_px, 400, 400);
		GSPGPU_FlushDataCache (fb, 400 * 240 * 4);
		gfxScreenSwapBuffers (GFX_TOP, false);
	}
	while (aptMainLoop ())
	{
		hidScanInput ();
		if (hidKeysDown () & KEY_START)
			break;
		gspWaitForVBlank ();
	}
	gfxExit ();
	exit (0);
}

static void
check_content (void)
{
	static const char *const missing[] = {
		"Game data not found.",
		"",
		"Copy uqm-0.8.0-content.uqm into",
		"   sdmc:/3ds/uqm/content/packages/",
		"",
		"Optional, same folder: uqm-0.8.0-3domusic.uqm and",
		"uqm-0.8.0-voice.uqm (3DO music and speech).",
		"",
		"From the PC:  install-3ds.sh <3DS IP> --uqm-data",
		"sends all of it over Wi-Fi with ftpd.",
		NULL };
	DIR *d;
	int found = 0;

	d = opendir (PORT3DS_CONTENT "/packages");
	if (d)
	{
		struct dirent *de;
		while ((de = readdir (d)))
		{
			const char *n = de->d_name;
			const size_t l = strlen (n);
			if (strstr (n, "content") && l > 4 && !strcmp (n + l - 4, ".uqm"))
				found = 1;
		}
		closedir (d);
	}
	if (!found)
		fatal_screen (missing);

	/* UQM recognises its content folder by this file */
	if (!file_exists (PORT3DS_CONTENT "/version"))
		write_file (PORT3DS_CONTENT "/version", "0.8.0\n");
}

static void
write_defaults (void)
{
	char ver[16];
	if (!file_exists (PORT3DS_ROOT "/uqm.cfg"))
		write_file (PORT3DS_ROOT "/uqm.cfg", default_uqm_cfg);
	if (!read_small_file (PORT3DS_ROOT "/port3ds.ver", ver, sizeof ver)
			|| strncmp (ver, PORT_CFG_VERSION, strlen (PORT_CFG_VERSION)) != 0
			|| !file_exists (PORT3DS_ROOT "/flight.cfg"))
	{
		write_file (PORT3DS_ROOT "/flight.cfg", default_flight_cfg);
		write_file (PORT3DS_ROOT "/port3ds.ver", PORT_CFG_VERSION "\n");
	}
}

static void
apt_log (const char *what)
{
	fprintf (stderr, "port3ds: %s\n", what);
	fflush (stderr);
}

/* sockets for Super Melee netplay (1 MB, page-aligned, owned by soc:u).
 * The socket service has to be shut down before the HOME menu, sleep or
 * closing the game, or the system hangs on black screens; it is started
 * again on return.  The buffer is kept for the whole run. */
static u32 *soc_buf = NULL;
static int soc_on = 0;
static char local_ip[32] = "";

static void
net_start (void)
{
#ifndef UQM3DS_HARNESS
	if (soc_on)
		return;
	if (!soc_buf)
		soc_buf = (u32 *) memalign (0x1000, 0x100000);
	if (soc_buf && R_SUCCEEDED (socInit (soc_buf, 0x100000)))
		soc_on = 1;
#endif
}

static void
net_stop (void)
{
#ifndef UQM3DS_HARNESS
	if (soc_on)
	{
		socExit ();
		soc_on = 0;
	}
#endif
}

/* Sockets are opened only when the player opens the netplay dialog and
 * closed again at the main menu, both on the game thread (the thread that
 * uses them).  Left open, the socket service hangs the HOME menu. */
void
port3ds_net_start (void)
{
	static u64 last_try = 0;
	if (soc_on || (last_try && osGetTime () - last_try < 2000))
		return;            /* no Wi-Fi: retry every 2 s, not every frame */
	last_try = osGetTime ();
	net_start ();
	apt_log (soc_on ? "sockets on" : "sockets: no Wi-Fi");
}

void
port3ds_net_stop (void)
{
	if (soc_on)
		apt_log ("sockets off");
	net_stop ();
}

int
port3ds_net_on (void)
{
	return soc_on;
}

/* ---- HOME menu, sleep and closing from the HOME menu ---- */
static aptHookCookie apt_cookie;
static int home_blocked = 0;
static int soc_resume = 0;

static void
apt_hook (APT_HookType hook, void *param)
{
	(void) param;
	switch (hook)
	{
		case APTHOOK_ONSUSPEND:
		case APTHOOK_ONSLEEP:
			apt_log (hook == APTHOOK_ONSUSPEND ? "to HOME menu" : "to sleep");
			soc_resume = soc_on;
			net_stop ();
			if (backlight_off && lcd_ok)
				GSPLCD_PowerOnBacklight (GSPLCD_SCREEN_BOTTOM);
			backlight_off = 0;
			break;
		case APTHOOK_ONRESTORE:
		case APTHOOK_ONWAKEUP:
			apt_log (hook == APTHOOK_ONRESTORE ? "back from HOME menu" : "awake");
			if (soc_resume)
				net_start ();
			soc_resume = 0;
			apt_returned = 1;
			break;
		default:
			break;
	}
}

/* called on the main thread before each aptMainLoop () */
static void
apt_events (void)
{
	static int home_seen = 0;
	const int live = port3ds_net_live ();
	const int jump = aptShouldJumpToHome ();
	if (jump && !home_seen)
		apt_log ("HOME pressed");
	home_seen = jump;
	if (live != home_blocked)
	{	/* a netplay connection would be cut: HOME and sleep wait for it */
		home_blocked = live;
		aptSetHomeAllowed (!live);
		aptSetSleepAllowed (!live);
	}
	if (live && aptCheckHomePressRejected ())
	{
		home_note_until = osGetTime () + 3000;
		deck_dirty = 1;
	}
	if (home_note_until && osGetTime () > home_note_until)
	{
		home_note_until = 0;
		deck_dirty = 1;
	}
}

/* Close was chosen in the HOME menu: the system waits for the process to
 * end, so shut the services down and leave now.  The game's own shutdown
 * waits on its threads and could hang the console. */
static void
close_now (void)
{
	static int once = 0;
	if (!once++)
		apt_log ("closed from the HOME menu");
#ifndef UQM3DS_HARNESS
	net_stop ();
	ndspExit ();
	if (lcd_ok)
		gspLcdExit ();
	gfxExit ();
	_exit (0);
#endif
}

static void
draw_home_note (void)
{
	static const char msg[] = "HOME and sleep are off while a side is set to Net.";
	int x;
	if (!home_note_until)
		return;
	x = (deck_w - text_w (msg, 0)) / 2;
	fill (0, DECK_H - 20, deck_w, 20, COL_HOT);
	text (x < 2 ? 2 : x, DECK_H - 17, msg, COL_HOT_TEXT, 0);
}

static const char *
net_local_ip (void)
{
#ifndef UQM3DS_HARNESS
	if (!soc_on)
		local_ip[0] = 0;
	else if (!local_ip[0])
	{	/* asked once per session, not on every redraw */
		struct in_addr a;
		a.s_addr = (in_addr_t) gethostid ();
		if (a.s_addr && a.s_addr != INADDR_NONE)
			inet_ntop (AF_INET, &a, local_ip, sizeof local_ip);
	}
#else
	strcpy (local_ip, "127.0.0.1");
#endif
	return local_ip[0] ? local_ip : "no Wi-Fi";
}

void
port3ds_init (void)
{
	osSetSpeedupEnable (true);
	gfxInit (GSP_RGBA8_OES, GSP_RGBA8_OES, false);
	gfxSet3D (false);
	lcd_ok = R_SUCCEEDED (gspLcdInit ());

	mkdir ("/3ds", 0777);
	mkdir (PORT3DS_ROOT, 0777);
	mkdir (PORT3DS_CONTENT, 0777);
	mkdir (PORT3DS_CONTENT "/packages", 0777);
	mkdir (PORT3DS_CONTENT "/addons", 0777);
	mkdir (PORT3DS_ROOT "/save", 0777);
	mkdir (PORT3DS_ROOT "/teams", 0777);

	/* UQM logs errors to stderr; keep them on the card for bug reports */
	if (freopen (PORT3DS_ROOT "/uqm.log", "w", stderr))
		setvbuf (stderr, NULL, _IOLBF, 0);

	check_content ();
	write_defaults ();
	load_port_settings ();

	comm_lock = SDL_CreateMutex ();
	aptHook (&apt_cookie, apt_hook, NULL);
	stat_second_start = osGetTime ();
	relayout ();
}

void
port3ds_exit (void)
{
	set_backlight (0);
	if (lcd_ok)
		gspLcdExit ();
#ifndef UQM3DS_HARNESS
	aptUnhook (&apt_cookie);
	net_stop ();
#endif
	gfxExit ();
}
