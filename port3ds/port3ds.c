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

#include "libs/input/sdl/input.h"   /* ProcessInputEvent() */
#include "font_big.h"
#include "font_small.h"
#include "help3ds.h"

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

static u32 deck_px[DECK_MAX_W * DECK_H];
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

static int layout = LAYOUT_GAME_TOP;
static SDL_Surface *video_surface = NULL;

static int deck_dirty = 1;
static int deck_ctx = -1;
static volatile int char_mode = 0;
static int help_page = -1;          /* -1 = closed */
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

static void
save_port_settings (void)
{
	char buf[64];
	snprintf (buf, sizeof buf, "layout=%d\nstats=%d\n", layout, show_stats);
	write_file (PORT3DS_ROOT "/port3ds.cfg", buf);
}

static void
load_port_settings (void)
{
	char buf[128];
	const char *p;
	if (!read_small_file (PORT3DS_ROOT "/port3ds.cfg", buf, sizeof buf))
		return;
	if ((p = strstr (buf, "layout=")))
		layout = atoi (p + 7) ? LAYOUT_GAME_BOTTOM : LAYOUT_GAME_TOP;
	if ((p = strstr (buf, "stats=")))
		show_stats = atoi (p + 6) ? 1 : 0;
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

static void tap_key (int vk);
static void toggle_layout (void);
static void open_keyboard (void);
static void set_backlight (int off);

/* ---- touch regions, rebuilt on every draw ---- */
enum {
	ACT_NONE, ACT_PAUSE, ACT_HELP, ACT_SWAP, ACT_DIM, ACT_STATS,
	ACT_KEYBOARD, ACT_HELP_PREV, ACT_HELP_CLOSE, ACT_HELP_NEXT,
	ACT_REPLY, ACT_REPLY_MORE, ACT_SKIP, ACT_REPLAY,
	ACT_MAP, ACT_GO, ACT_ZOOM_IN, ACT_ZOOM_OUT, ACT_SHIP
};
typedef struct { short x, y, w, h; short act, arg; } Region;
static Region regions[48];
static int nregions = 0;
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

#define MAP_X 2
#define MAP_Y 40
#define MAP_W 212
#define MAP_H 198

static float
map_scale (void)
{
	return (float) MAP_H / 10000.0f * (float) (1 << map_zoom);
}

static void
map_to_screen (int ox, int ux, int uy, int *sx, int *sy)
{
	const float s = map_scale ();
	*sx = ox + MAP_X + MAP_W / 2 + (int) ((ux - map_cx) * s);
	*sy = MAP_Y + MAP_H / 2 - (int) ((uy - map_cy) * s);
}

static void
put (int x, int y, u32 c, int ox)
{
	if (x >= ox + MAP_X && x < ox + MAP_X + MAP_W && y >= MAP_Y && y < MAP_Y + MAP_H)
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

	if (ship.quasispace || !ship.have_pos)
	{
		text_center (0, 120, deck_w, "No galaxy map here", COL_DIM, 1);
		return;
	}

	if (map_follow)
	{
		map_cx = ship.ux;
		map_cy = ship.uy;
	}

	/* map frame */
	fill (ox + MAP_X, MAP_Y, MAP_W, MAP_H, RGB (0x03, 0x05, 0x0C));
	fill (ox + MAP_X - 1, MAP_Y - 1, MAP_W + 2, 1, COL_BTN_EDGE);
	fill (ox + MAP_X - 1, MAP_Y + MAP_H, MAP_W + 2, 1, COL_BTN_EDGE);
	add_region (ox + MAP_X, MAP_Y, MAP_W, MAP_H, ACT_MAP, 0);

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
				button (px - 2, 118, pw, 30,
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
		button (px - 2, 154, 47, 26, "+", ACT_ZOOM_IN, 0, 0, map_zoom >= 3, 1);
		button (px + 49, 154, 47, 26, "-", ACT_ZOOM_OUT, 0, 0, map_zoom <= 0, 1);
		button (px - 2, 184, pw, 24, map_follow ? "SHIP (FOLLOW)" : "SHIP",
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
			text (px, 214, "COURSE:", COL_DIM, 0);
			name[16] = '\0';
			text (px, 226, name, COL_HOT, 0);
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
	{"PAD", "Turn"}, {"SELECT", "Retreat"}, {"START", "Pause"}, {NULL, NULL} };
static const Legend legend_flight[] = {
	{"PAD", "Steer"}, {"Y / UP", "Thrust"}, {"B", "Ship menu"},
	{"A", "Select"}, {"START", "Pause"}, {NULL, NULL} };
static const Legend legend_talk[] = {
	{"B", "Skip speech"}, {"RIGHT", "Fast-forward"}, {"LEFT", "Replay line"},
	{"START", "Pause"}, {NULL, NULL} };
static const Legend legend_starbase[] = {
	{"PAD", "Move"}, {"A", "Select"}, {"B", "Back"}, {"X", "Special"},
	{"START", "Pause"}, {NULL, NULL} };
static const Legend legend_text[] = {
	{"UP/DOWN", "Pick letter"}, {"LEFT/RIGHT", "Move"},
	{"A", "Done"}, {"SELECT", "Cancel"}, {NULL, NULL} };

static void
draw_legend_list (int ox, const Legend *lg, int y0)
{
	int i;
	for (i = 0; lg[i].chip; ++i)
	{
		const int col = i % 2, row = i / 2;
		const int x = ox + 8 + col * 156;
		const int y = y0 + row * 26;
		const int cw = text_w (lg[i].chip, 0) + 10;
		box (x, y, cw, 18, COL_CHIP, COL_BG);
		text (x + 5, y + 3, lg[i].chip, COL_TEXT, 0);
		text (x + cw + 6, y + 2, lg[i].what, COL_TEXT, 1);
	}
}

/* ---- the deck ---- */
enum { MODE_LEGEND, MODE_NAV, MODE_REPLIES, MODE_TALK, MODE_TEXT, MODE_HELP };

static int
deck_mode (void)
{
	if (help_page >= 0)
		return MODE_HELP;
	if (char_mode)
		return MODE_TEXT;
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
		case MODE_HELP: return "HELP";
		case MODE_TEXT: return "ENTER A NAME";
		case MODE_REPLIES: return "YOUR REPLY";
		case MODE_TALK: return "ENCOUNTER";
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
		{ "DIM", ACT_DIM, 32 }, { "FPS", ACT_STATS, 32 } };
	int i, x = ox + 320 - 3;
	char buf[40];

	fill (0, 0, deck_w, 22, COL_HEADER);
	fill (0, 22, deck_w, 1, COL_BTN_EDGE);
	text (ox + 5, 5, mode_title (mode), COL_ACCENT, 0);
	for (i = (int) (sizeof tb / sizeof tb[0]) - 1; i >= 0; --i)
	{
		x -= tb[i].w + 2;
		button (x, 2, tb[i].w, 18, tb[i].label, tb[i].act, 0,
				tb[i].act == ACT_STATS && show_stats, 0, 0);
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
draw_help (int ox)
{
	const HelpPage3DS *pg = &help_pages[help_page];
	char buf[32];
	int i;
	snprintf (buf, sizeof buf, "%d / %d", help_page + 1, HELP_PAGE_COUNT);
	text (ox + 8, 27, pg->title, COL_ACCENT, 0);
	text (ox + 312 - text_w (buf, 0), 27, buf, COL_DIM, 0);
	for (i = 0; i < 12 && pg->lines[i]; ++i)
		text (ox + 4, 42 + i * 13, pg->lines[i], COL_TEXT, 0);
	button (ox + 6, 206, 100, 30, "< PREV", ACT_HELP_PREV, 0, 0, help_page == 0, 1);
	button (ox + 110, 206, 100, 30, "CLOSE", ACT_HELP_CLOSE, 0, 0, 0, 1);
	button (ox + 214, 206, 100, 30, "NEXT >", ACT_HELP_NEXT, 0, 0,
			help_page + 1 >= HELP_PAGE_COUNT, 1);
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

static void
show_deck (void)
{
	const gfxScreen_t scr = deck_screen ();
	u16 fw, fh;
	u32 *fb;

	deck_w = (scr == GFX_TOP) ? 400 : 320;
	draw_deck ();
	fb = (u32 *) gfxGetFramebuffer (scr, GFX_LEFT, &fw, &fh);
	blit_to_fb (fb, 0, deck_px, deck_w, deck_w);
	GSPGPU_FlushDataCache (fb, (u32) deck_w * 240 * 4);
	gfxScreenSwapBuffers (scr, false);
	deck_dirty = 0;
}

/* ------------------------------------------------------------------ */
/* video                                                                */
/* ------------------------------------------------------------------ */

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

	if (!frame)
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
	fb = (u32 *) gfxGetFramebuffer (scr, GFX_LEFT, &fw, &fh);
	if (scr == GFX_TOP)
	{	/* pillar-box: the 40-pixel bands either side stay black */
		clear_fb_columns (fb, 0, 40);
		clear_fb_columns (fb, 360, 40);
	}
	blit_to_fb (fb, x0, (const u32 *) frame->pixels, 320, frame->pitch / 4);
	GSPGPU_FlushDataCache (fb, (u32) (scr == GFX_TOP ? 400 : 320) * 240 * 4);
	gfxScreenSwapBuffers (scr, false);

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
enum { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_A, VK_B, VK_X, VK_Y,
	VK_START, VK_SELECT, VK_L, VK_R, VK_COUNT };

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
	help_page = -1;
	touch_act = ACT_NONE;
	touch_live = 0;
	save_port_settings ();
	relayout ();
}

static void
help_next (void)
{
	help_page = (help_page + 1 >= HELP_PAGE_COUNT) ? -1 : help_page + 1;
	deck_dirty = 1;
}

static void
do_action (int act, int arg, int px, int py)
{
	const int ox = 0;   /* actions only exist when the deck is on the bottom */
	switch (act)
	{
		case ACT_PAUSE: tap_key (VK_START); break;
		case ACT_HELP: help_page = (help_page < 0) ? 0 : -1; break;
		case ACT_SWAP: toggle_layout (); return;
		case ACT_DIM: set_backlight (1); break;
		case ACT_STATS: show_stats = !show_stats; save_port_settings (); break;
		case ACT_KEYBOARD: if (char_mode) open_keyboard (); return;
		case ACT_HELP_PREV: if (help_page > 0) --help_page; break;
		case ACT_HELP_CLOSE: help_page = -1; break;
		case ACT_HELP_NEXT: if (help_page + 1 < HELP_PAGE_COUNT) ++help_page; break;
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
			}
			break;
		case ACT_ZOOM_IN: if (map_zoom < 3) ++map_zoom; break;
		case ACT_ZOOM_OUT: if (map_zoom > 0) --map_zoom; break;
		case ACT_SHIP: map_follow = 1; break;
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
	if (deck_screen () != GFX_BOTTOM)
		return;               /* the game itself has no pointer input */

	if (held & KEY_TOUCH)
	{
		const Region *r;
		hidTouchRead (&tp);
		last = tp;
		r = region_at (tp.px, tp.py);
		if (down & KEY_TOUCH)
		{
			touch_act = r ? r->act : ACT_NONE;
			touch_arg = r ? r->arg : 0;
			touch_live = 1;
			drag_x0 = drag_last_x = tp.px;
			drag_y0 = drag_last_y = tp.py;
			dragging = 0;
			deck_dirty = 1;
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
		const int fire = touch_live && act != ACT_NONE && !(act == ACT_MAP && dragging);
		touch_act = ACT_NONE;
		touch_live = 0;
		deck_dirty = 1;
		if (fire)
			do_action (act, arg, last.px, last.py);
	}
}

int
port3ds_pump_input (void)
{
	u32 kdown, kheld, kup, v, changed;
	int i;

	if (!aptMainLoop ())
		return 0;

	hidScanInput ();
	kdown = hidKeysDown ();
	kheld = hidKeysHeld ();
	kup = hidKeysUp ();

	/* port hotkeys */
	if (kdown & KEY_ZL)
		toggle_layout ();
	if (kdown & KEY_ZR)
	{
		if (help_page < 0) { help_page = 0; deck_dirty = 1; }
		else help_next ();
	}

	handle_touch (kdown, kheld, kup);

	/* buttons -> keys */
	v = read_virtual_keys (kheld);
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
	stat_second_start = osGetTime ();
	relayout ();
}

void
port3ds_exit (void)
{
	set_backlight (0);
	if (lcd_ok)
		gspLcdExit ();
	gfxExit ();
}
