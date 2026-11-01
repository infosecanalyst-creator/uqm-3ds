/*
 * The Ur-Quan Masters for the New Nintendo 3DS -- platform layer.
 *
 * Everything 3DS-specific lives in port3ds/; the UQM tree is only touched
 * through small, anchored edits made by tools/patch_uqm.py, each of which
 * calls into the functions below.
 *
 *   Screens   game frame 1:1 (320x240) on the top screen, touch "control
 *             deck" on the bottom; ZL swaps them.
 *   Input     buttons are read from the HID service and turned into the key
 *             events UQM's own key maps expect (see port3ds.c, keymap[]).
 *   Threads   UQM threads are libctru threads; the music/speech decoder runs
 *             on its own CPU core when the console allows it.
 */
#ifndef PORT3DS_H
#define PORT3DS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL.h>

/* paths on the SD card (no "sdmc:" prefix: libctru makes sdmc the default) */
#define PORT3DS_ROOT       "/3ds/uqm"
#define PORT3DS_CONTENT    PORT3DS_ROOT "/content"
#define PORT3DS_USERDIR    PORT3DS_ROOT "/"

/* uqm.c main(): first and last thing that runs */
void port3ds_init (void);
void port3ds_exit (void);

/* pure.c: the 320x240 frame UQM composes into; masks match the
 * framebuffer's RGBA8 layout so presenting is a rotate-copy only */
SDL_Surface *port3ds_create_video_surface (void);
void port3ds_present (SDL_Surface *frame);

/* sdl_common.c TFB_ProcessEvents(): buttons/touch -> UQM input.
 * Returns 0 once the system asks the app to close. */
int port3ds_pump_input (void);

/* getchar.c DoTextEntry(): offers the software keyboard's result.
 * Returns 1 if 'base' was replaced (caller moves the cursor to the end). */
int port3ds_text_entry_frame (char *base, int maxsize);

/* input.c Enter/ExitCharacterMode() */
void port3ds_set_char_mode (int on);

/* threads (sdlthreads.c is redirected here) */
SDL_Thread *port3ds_thread_create (int (*fn)(void *), void *data,
		const char *name);
void port3ds_thread_wait (SDL_Thread *t, int *status);
Uint32 port3ds_thread_id (void);
Uint32 port3ds_thread_get_id (SDL_Thread *t);

/* context3ds.c (built against UQM headers) */
enum {
	CTX3DS_MENU = 0,     /* main menu, setup, Super Melee team screens */
	CTX3DS_COMBAT,
	CTX3DS_HYPERSPACE,
	CTX3DS_SOLARSYS,
	CTX3DS_ENCOUNTER,
	CTX3DS_STARBASE,
	CTX3DS_COUNT
};
int port3ds_game_context (void);

/* game3ds.c: ship status for the deck (main thread, read-only) */
typedef struct {
	int fuel;            /* FUEL_TANK_SCALE units: 100 = 1.0 on screen */
	unsigned ru;
	int crew, landers;
	int have_pos, ux, uy;        /* universe coords, 0..9999, y up */
	int have_dest, dest_x, dest_y;
	int star;                    /* system we are in, or -1 */
	int quasispace;
	int in_orbit;        /* orbiting a planet (ship menu has SCAN) */
} Port3DSShip;
void port3ds_game_read (Port3DSShip *s);
int port3ds_star_count (void);
void port3ds_star_get (int i, int *x, int *y, int *color, int *size);
void port3ds_star_name (int i, char *buf, int size);
int port3ds_fuel_needed (int x0, int y0, int x1, int y1);
/* main thread queues, game thread (DoInput) applies */
void port3ds_request_course (int ux, int uy);

/* Ship-menu shortcuts: values are the ship menu's own item numbers (menustat.h),
 * SAVE/LOAD go one level deeper into the Game menu. A request opens the menu,
 * runs the item and returns to flight; it expires after 2 s if nothing takes it. */
enum { P3SC_SCAN = 0, P3SC_DEVICES = 2, P3SC_CARGO = 3, P3SC_ROSTER = 4,
	P3SC_GAME = 5, P3SC_SAVE = 100, P3SC_LOAD = 101 };
void port3ds_request_shortcut (int sc);
int port3ds_shortcut_pending (int in_orbit);   /* game thread: open the menu? */
int port3ds_take_shortcut (int in_orbit);      /* game thread: item, or -1 */
int port3ds_take_gameopt (void);               /* game thread: 0 save, 1 load, -1 */

/* The game's own list menus as deck buttons (game3ds.c). Bases are UQM's
 * PM_* values (menustat.h) or these: */
#define P3M_MAIN 200
/* copies of the menustat.h bases the deck names (checked in game3ds.c) */
enum { PM3_SCAN = 0, PM3_STARMAP = 1, PM3_MIN_SCAN = 7, PM3_SAVE_GAME = 13,
	PM3_CONVERSE = 18, PM3_FUEL = 21, PM3_CREW = 25, PM3_SOUND_ON = 28 };
#define P3M_STARBASE 201
#define P3_MENU_MAX 10
typedef struct { int state; char label[32]; } Port3DSMenuItem;
void port3ds_menu_seen (int base, void *pms, int cur);    /* game thread, each frame */
int port3ds_menu_items (int *base, int *cur, Port3DSMenuItem *items, int max);
void port3ds_menu_pick (int base, int state);

/* Super Melee setup screen */
enum { P3MO_CTRL_TOP, P3MO_LOAD_TOP, P3MO_SAVE_TOP, P3MO_BATTLE, P3MO_LOAD_BOT,
	P3MO_SAVE_BOT, P3MO_CTRL_BOT, P3MO_QUIT, P3MO_NET_TOP, P3MO_NET_BOT };
enum { P3MEL_NONE, P3MEL_OPTION, P3MEL_SHIP };
typedef struct
{
	int active, net;
	unsigned char ships[2][14];   /* team 0 is drawn on top */
	int value[2];
	char name[2][32];
	int control[2];               /* PlayerControl[i]: 0 human, 1-3 cyborg, 4 net */
} Port3DSMelee;
int port3ds_melee_take (void *pms, int *a, int *b, int *c);  /* game thread */
int port3ds_melee_read (Port3DSMelee *m);
void port3ds_melee_option (int opt);
void port3ds_melee_ship (int side, int slot, int ship);      /* ship 255 = empty */
const char *port3ds_ship_short (int ship);
/* choosing the next ship in a round: player 1 is drawn on top */
#define P3_PICK_RANDOM (-2)
int port3ds_pick_frame (int player, const int *species, int n);  /* game thread */
int port3ds_pick_read (int player, unsigned char ships[14]);
void port3ds_pick (int player, int slot);
int port3ds_ship_cost (int ship);

/* Super Melee network dialog (host/port/delay, connect or wait) */
enum { P3ND_NONE, P3ND_CONNECT, P3ND_LISTEN, P3ND_CANCEL, P3ND_DELAY_DOWN,
	P3ND_DELAY_UP, P3ND_HOST, P3ND_PORT };
typedef struct { char host[64]; char port[16]; int delay, side; } Port3DSNetDlg;
int port3ds_netdlg_frame (const char *host, const char *port, int delay, int side,
		char *text_out, int text_size);           /* game thread */
int port3ds_netdlg_read (Port3DSNetDlg *d);
void port3ds_netdlg_act (int act, const char *text);

/* The flagship's screen position and facing (0 up, 16 steps clockwise) in
 * HyperSpace or interplanetary flight; 0 elsewhere. */
int port3ds_flight_ship (int *sx, int *sy, int *facing);
extern volatile int port3ds_hs_facing;

/* the instruction manual is open: the game waits in port3ds_game_frame */
extern volatile int port3ds_manual_active;
int port3ds_net_live (void);

/* Stereo 3D: a depth per pixel of the main screen, set by tags in the draw
 * queue. Depth > 0 is behind the screen, < 0 in front of it. */
void port3ds_depth_set (int depth);       /* game thread: tag what follows */
void port3ds_space_batch (int on);        /* process.c around the display list */
void port3ds_space_prim (int index);      /* frame.c, per display-list prim */
void port3ds_depth_now (int depth);       /* render thread (queue callback) */
void port3ds_depth_mark_rect (SDL_Surface *dst, int x, int y, int w, int h);
void port3ds_depth_mark_blit (SDL_Surface *src, const SDL_Rect *sr,
		SDL_Surface *dst, const SDL_Rect *dr);
void port3ds_depth_mark_line (SDL_Surface *dst, int x1, int y1, int x2, int y2);
void port3ds_set_main_screen (SDL_Surface *main);
extern int port3ds_stereo_ok;             /* no fade or transition this frame */
void port3ds_game_frame (void *input_state);   /* DoInput, before the InputFunc */

/* comm.c PlayerResponseInput(), game thread, once per frame while the
 * player is choosing: publishes the replies, returns a reply tapped on
 * the deck or -1 */
int port3ds_comm_frame (const char *const *texts, int n, int cur);

#ifdef __cplusplus
}
#endif

#endif /* PORT3DS_H */
