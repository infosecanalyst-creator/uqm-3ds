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
static int pressed_btn = -1;
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

typedef struct { const char *chip; const char *what; } Legend;

static const Legend legend_menu[] = {
	{"A", "Select"}, {"B", "Back"}, {"PAD", "Move"}, {"L / R", "Page up/down"},
	{"X", "Special"}, {"START", "Pause"}, {NULL, NULL} };
static const Legend legend_combat[] = {
	{"A", "Fire weapon"}, {"B", "Special"}, {"Y / UP", "Thrust"},
	{"PAD", "Turn"}, {"SELECT", "Retreat"}, {"START", "Pause"}, {NULL, NULL} };
static const Legend legend_hyper[] = {
	{"PAD", "Steer"}, {"Y / UP", "Thrust"}, {"B", "Ship menu"},
	{"L / R", "Map zoom"}, {"START", "Pause"}, {NULL, NULL} };
static const Legend legend_solar[] = {
	{"PAD", "Fly"}, {"Y / UP", "Thrust"}, {"B", "Ship menu"},
	{"A", "Select"}, {"START", "Pause"}, {NULL, NULL} };
static const Legend legend_encounter[] = {
	{"UP/DOWN", "Pick a reply"}, {"A", "Say it"}, {"B", "Skip/summary"},
	{"LEFT", "Replay line"}, {"RIGHT", "Fast-forward"}, {"START", "Pause"},
	{NULL, NULL} };
static const Legend legend_starbase[] = {
	{"PAD", "Move"}, {"A", "Select"}, {"B", "Back"}, {"X", "Special"},
	{"START", "Pause"}, {NULL, NULL} };
static const Legend legend_text[] = {
	{"TOUCH", "KEYBOARD"}, {"UP/DOWN", "Pick letter"}, {"LEFT/RIGHT", "Move"},
	{"A", "Done"}, {"SELECT", "Cancel"}, {NULL, NULL} };

static const char *ctx_title[CTX3DS_COUNT] = {
	"MENUS", "SHIP COMBAT", "HYPERSPACE", "SOLAR SYSTEM", "ENCOUNTER",
	"STARBASE" };

enum { BTN_PAUSE, BTN_KEYBOARD, BTN_HELP, BTN_SWAP, BTN_LIGHT, BTN_STATS,
	BTN_COUNT };
enum { HBTN_PREV, HBTN_CLOSE, HBTN_NEXT, HBTN_COUNT };

#define BTN_W 100
#define BTN_H 30

static void
button_rect (int i, int *x, int *y)
{
	const int ox = (deck_w - 320) / 2;
	*x = ox + 6 + (i % 3) * (BTN_W + 4);
	*y = (i < 3) ? 168 : 204;
}

static void
draw_button (int i, const char *label, int hot, int disabled)
{
	int x, y;
	u32 bg, fg;
	button_rect (i, &x, &y);
	bg = hot ? COL_HOT : (pressed_btn == i ? COL_BTN_DOWN : COL_BTN);
	fg = hot ? COL_HOT_TEXT : (disabled ? COL_OFF : COL_TEXT);
	box (x, y, BTN_W, BTN_H, hot ? COL_HOT : COL_BTN_EDGE, COL_BG);
	box (x + 1, y + 1, BTN_W - 2, BTN_H - 2, bg, hot ? COL_HOT : COL_BTN_EDGE);
	text_center (x, y + (BTN_H - FONT_BIG_H) / 2, BTN_W, label, fg, 1);
}

static void
draw_header (void)
{
	char buf[48];
	fill (0, 0, deck_w, 22, COL_HEADER);
	fill (0, 22, deck_w, 1, COL_BTN_EDGE);
	text (6, 4, "THE UR-QUAN MASTERS", COL_TEXT, 1);
	if (show_stats)
	{
		snprintf (buf, sizeof buf, "%2lu fps %2lu.%lums",
				(unsigned long) stat_fps,
				(unsigned long) (stat_us / 1000),
				(unsigned long) ((stat_us % 1000) / 100));
		text (deck_w - 6 - text_w (buf, 0), 5, buf, COL_ACCENT, 0);
	}
}

static void
draw_legend (void)
{
	const Legend *lg;
	const int ox = (deck_w - 320) / 2;
	int i, ctx = deck_ctx;

	if (char_mode)
	{
		lg = legend_text;
		text (ox + 8, 28, "ENTER A NAME", COL_ACCENT, 0);
	}
	else
	{
		switch (ctx)
		{
			case CTX3DS_COMBAT: lg = legend_combat; break;
			case CTX3DS_HYPERSPACE: lg = legend_hyper; break;
			case CTX3DS_SOLARSYS: lg = legend_solar; break;
			case CTX3DS_ENCOUNTER: lg = legend_encounter; break;
			case CTX3DS_STARBASE: lg = legend_starbase; break;
			default: lg = legend_menu; break;
		}
		text (ox + 8, 28, ctx_title[ctx < 0 ? 0 : ctx], COL_ACCENT, 0);
	}

	for (i = 0; lg[i].chip; ++i)
	{
		const int col = i % 2, row = i / 2;
		const int x = ox + 8 + col * 156;
		const int y = 46 + row * 26;
		const int cw = text_w (lg[i].chip, 0) + 10;
		box (x, y, cw, 18, COL_CHIP, COL_BG);
		text (x + 5, y + 3, lg[i].chip, COL_TEXT, 0);
		text (x + cw + 6, y + 2, lg[i].what, COL_TEXT, 1);
	}
}

static void
draw_help (void)
{
	const HelpPage3DS *pg = &help_pages[help_page];
	const int ox = (deck_w - 320) / 2;
	char buf[32];
	int i;

	snprintf (buf, sizeof buf, "%d / %d", help_page + 1, HELP_PAGE_COUNT);
	text (ox + 8, 28, pg->title, COL_ACCENT, 0);
	text (ox + 312 - text_w (buf, 0), 28, buf, COL_DIM, 0);
	for (i = 0; i < 12 && pg->lines[i]; ++i)
		text (ox + 4, 44 + i * 13, pg->lines[i], COL_TEXT, 0);
}

static void
draw_deck (void)
{
	const int bottom = deck_screen () == GFX_BOTTOM;
	fill (0, 0, deck_w, DECK_H, COL_BG);
	draw_header ();

	if (help_page >= 0)
	{
		draw_help ();
		/* help buttons on the bottom row only */
		{
			static const char *labels[HBTN_COUNT] = { "< PREV", "CLOSE", "NEXT >" };
			int i;
			for (i = 0; i < HBTN_COUNT; ++i)
				draw_button (3 + i, labels[i], 0, !bottom);
		}
		if (!bottom)
			text_center (0, 228, deck_w, "ZR: next page", COL_DIM, 0);
		return;
	}

	draw_legend ();
	if (!bottom)
	{	/* the top screen has no touch: shortcuts instead of buttons */
		fill (0, 200, deck_w, 1, COL_BTN_EDGE);
		text_center (0, 210, deck_w, "ZL  game back to the top screen", COL_DIM, 1);
		if (char_mode)
			text_center (0, 182, deck_w, "Swap screens (ZL) to use the keyboard",
					COL_HOT, 0);
		return;
	}
	draw_button (BTN_PAUSE, "PAUSE", 0, 0);
	draw_button (BTN_KEYBOARD, "KEYBOARD", char_mode, !char_mode);
	draw_button (BTN_HELP, "HELP", 0, 0);
	draw_button (BTN_SWAP, "SWAP", 0, 0);
	draw_button (BTN_LIGHT, "SCREEN OFF", 0, 0);
	draw_button (BTN_STATS, show_stats ? "STATS ON" : "STATS", 0, 0);
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
	pressed_btn = -1;
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
deck_button (int i)
{
	if (help_page >= 0)
	{
		switch (i)
		{
			case 3 + HBTN_PREV: if (help_page > 0) --help_page; break;
			case 3 + HBTN_CLOSE: help_page = -1; break;
			case 3 + HBTN_NEXT: if (help_page + 1 < HELP_PAGE_COUNT) ++help_page; break;
		}
		deck_dirty = 1;
		return;
	}
	switch (i)
	{
		case BTN_PAUSE: tap_key (VK_START); break;
		case BTN_KEYBOARD: if (char_mode) open_keyboard (); break;
		case BTN_HELP: help_page = 0; break;
		case BTN_SWAP: toggle_layout (); break;
		case BTN_LIGHT: set_backlight (1); break;
		case BTN_STATS:
			show_stats = !show_stats;
			save_port_settings ();
			break;
	}
	deck_dirty = 1;
}

static int
button_at (int px, int py)
{
	int i;
	deck_w = 320;
	for (i = 0; i < BTN_COUNT; ++i)
	{
		int x, y;
		button_rect (i, &x, &y);
		if (px >= x && px < x + BTN_W && py >= y && py < y + BTN_H)
		{
			if (help_page >= 0 && i < 3)
				return -1;     /* only the bottom row exists in help */
			return i;
		}
	}
	return -1;
}

static void
handle_touch (u32 down, u32 held, u32 up)
{
	touchPosition tp;

	if (backlight_off)
	{	/* any touch just wakes the screen */
		if (down & KEY_TOUCH)
		{
			set_backlight (0);
			deck_dirty = 1;
		}
		return;
	}
	if (deck_screen () != GFX_BOTTOM)
		return;               /* the game has no pointer input */

	if (held & KEY_TOUCH)
	{
		int b;
		hidTouchRead (&tp);
		b = button_at (tp.px, tp.py);
		if (down & KEY_TOUCH)
			pressed_btn = b;
		else if (b != pressed_btn && pressed_btn >= 0)
			pressed_btn = -2 - pressed_btn;     /* slid off: disarm */
		deck_dirty = 1;
	}
	else if (up & KEY_TOUCH)
	{
		const int b = pressed_btn;
		pressed_btn = -1;
		deck_dirty = 1;
		if (b >= 0)
			deck_button (b);
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
