/* The port's window into the game: what the game is doing (for the control
 * deck), ship status, the star catalogue for the touch galaxy map, and the
 * two places where the deck acts on the game (setting a course, picking a
 * conversation reply). Built inside the UQM tree for its headers.
 *
 * Threading: the deck runs on the main thread and only reads plain words
 * the game thread writes (a stale read shows old numbers for one redraw).
 * Anything that changes game state is queued here and applied on the game
 * thread by port3ds_game_frame() (called from DoInput) or by the comm hook.
 */
#include "port3ds.h"
#include "uqm/globdata.h"
#include "uqm/starmap.h"
#include "uqm/sis.h"
#include "uqm/units.h"
#include "uqm/gamestr.h"
#include "uqm/planets/planets.h"
#include "libs/mathlib.h"
#include "libs/timelib.h"
#include "uqm/menustat.h"
#include "uqm/controls.h"
#include "uqm/setup.h"
#include "uqm/intel.h"
#include "uqm/supermelee/melee.h"
#include "uqm/supermelee/meleesetup.h"
#include "uqm/supermelee/meleeship.h"
#include "uqm/races.h"
#include "uqm/units.h"
#include "libs/graphics/tfb_draw.h"
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern STAR_DESC starmap_array[];

int
port3ds_game_context (void)
{
	ACTIVITY act = GLOBAL (CurrentActivity);
	BYTE lo = LOBYTE (act);
#ifdef UQM3DS_HARNESS
	if (getenv ("UQM3DS_FAKE_COMM"))
		return CTX3DS_ENCOUNTER;
#endif

	if (lo == IN_HYPERSPACE || lo == IN_QUASISPACE)
		return CTX3DS_HYPERSPACE;
	if ((act & IN_BATTLE) || lo == IN_LAST_BATTLE)
		return CTX3DS_COMBAT;
	switch (lo)
	{
		case IN_INTERPLANETARY:
		case IN_PLANET_ORBIT:
			return CTX3DS_SOLARSYS;
		case IN_ENCOUNTER:
			return CTX3DS_ENCOUNTER;
		case IN_STARBASE:
			return CTX3DS_STARBASE;
		default:
			return CTX3DS_MENU;
	}
}

void
port3ds_game_read (Port3DSShip *s)
{
	const int ctx = port3ds_game_context ();
	memset (s, 0, sizeof *s);
	s->fuel = (int) GLOBAL_SIS (FuelOnBoard);
	s->ru = (unsigned) GLOBAL_SIS (ResUnits);
	s->crew = (int) GLOBAL_SIS (CrewEnlisted);
	s->landers = (int) GLOBAL_SIS (NumLanders);
	s->quasispace = GET_GAME_STATE (ARILOU_SPACE_SIDE) > 1;
	s->star = -1;
	if (ctx == CTX3DS_HYPERSPACE)
	{
		s->ux = LOGX_TO_UNIVERSE (GLOBAL_SIS (log_x));
		s->uy = LOGY_TO_UNIVERSE (GLOBAL_SIS (log_y));
		s->have_pos = 1;
	}
	else if (ctx == CTX3DS_SOLARSYS && CurStarDescPtr)
	{
		s->ux = CurStarDescPtr->star_pt.x;
		s->uy = CurStarDescPtr->star_pt.y;
		s->star = (int) (CurStarDescPtr - starmap_array);
		if (s->star < 0 || s->star >= NUM_SOLAR_SYSTEMS)
			s->star = -1;
		s->have_pos = 1;
		s->in_orbit = pSolarSysState && pSolarSysState->InOrbit;
	}
	s->dest_x = GLOBAL (autopilot.x);
	s->dest_y = GLOBAL (autopilot.y);
	s->have_dest = !(s->dest_x == (COORD) ~0 || s->dest_y == (COORD) ~0);
}

int
port3ds_star_count (void)
{
	return NUM_SOLAR_SYSTEMS;
}

void
port3ds_star_get (int i, int *x, int *y, int *color, int *size)
{
	const STAR_DESC *sd = &starmap_array[i];
	*x = sd->star_pt.x;
	*y = sd->star_pt.y;
	*color = STAR_COLOR (sd->Type);
	*size = STAR_TYPE (sd->Type);
}

void
port3ds_star_name (int i, char *buf, int size)
{
	/* same composition as GetClusterName(), bounded */
	const STAR_DESC *sd = &starmap_array[i];
	const char *pre = sd->Prefix ? GAME_STRING (STAR_NUMBER_BASE + sd->Prefix - 1) : NULL;
	const char *post = GAME_STRING (sd->Postfix);
	snprintf (buf, (size_t) size, "%s%s%s", pre ? pre : "", pre ? " " : "",
			post ? post : "?");
}

/* fuel needed to fly from (x0,y0) to (x1,y1), in FUEL_TANK_SCALE units,
 * exactly as the starmap computes it */
int
port3ds_fuel_needed (int x0, int y0, int x1, int y1)
{
	const long dx = x0 - x1, dy = y0 - y1;
	const DWORD f = (DWORD) (dx * dx + dy * dy);
	if (f == 0 || GET_GAME_STATE (ARILOU_SPACE_SIDE) > 1)
		return 0;
	return (int) square_root (f) + (FUEL_TANK_SCALE / 20);
}

/* ---- requests applied on the game thread ---------------------------- */

static volatile int course_pending = 0;
static volatile int course_x, course_y;

void
port3ds_request_course (int ux, int uy)
{
	course_x = ux;
	course_y = uy;
	course_pending = 1;
}

static void apply_menu_pick (void *input_state);

void
port3ds_game_frame (void *input_state)
{
	apply_menu_pick (input_state);
	if (course_pending)
	{
		course_pending = 0;
		/* only where the starmap itself would allow it */
		if (inHQSpace () || LOBYTE (GLOBAL (CurrentActivity)) == IN_INTERPLANETARY)
		{
			GLOBAL (autopilot.x) = (COORD) course_x;
			GLOBAL (autopilot.y) = (COORD) course_y;
		}
	}
}

/* ---- ship-menu shortcuts ------------------------------------------------- */

static volatile int sc_item = -1, sc_gameopt = -1;
static volatile TimeCount sc_time;

void
port3ds_request_shortcut (int sc)
{
	sc_item = -1;
	sc_gameopt = (sc >= P3SC_SAVE) ? sc - P3SC_SAVE : -1;
	sc_time = GetTimeCounter ();
	sc_item = (sc >= P3SC_SAVE) ? P3SC_GAME : sc;
}

static int
sc_valid (int in_orbit)
{
	const int it = sc_item;
	if (it < 0)
		return 0;
	if (GetTimeCounter () - sc_time > 2 * ONE_SECOND)
	{	/* nobody took it: never pop a menu up later by surprise */
		sc_item = sc_gameopt = -1;
		return 0;
	}
	return it != P3SC_SCAN || in_orbit;
}

int
port3ds_shortcut_pending (int in_orbit)
{
	return sc_valid (in_orbit);
}

int
port3ds_take_shortcut (int in_orbit)
{
	int it;
	if (!sc_valid (in_orbit))
		return -1;
	it = sc_item;
	sc_item = -1;
	if (it != P3SC_GAME)
		sc_gameopt = -1;
	return it;
}

int
port3ds_take_gameopt (void)
{
	const int g = sc_gameopt;
	sc_gameopt = -1;
	return g;
}

/* ---- the game's own menus on the deck ----------------------------------
 * Every list menu in UQM is driven by an InputFunc that DoInput() calls once
 * per frame. The hooks report "menu <base> is open, highlight on <cur>" each
 * frame (port3ds_menu_seen); the deck shows that menu as buttons; a tap is
 * applied at the top of the next frame of that same InputFunc (before it
 * reads the buttons) as "highlight the item, press A". The MENU_STATE is only
 * touched when DoInput is running exactly that menu, so a stale pointer is
 * never written. */

typedef char pm3_check[(PM3_SCAN == PM_SCAN && PM3_STARMAP == PM_STARMAP
		&& PM3_MIN_SCAN == PM_MIN_SCAN && PM3_SAVE_GAME == PM_SAVE_GAME
		&& PM3_CONVERSE == PM_CONVERSE && PM3_FUEL == PM_FUEL
		&& PM3_CREW == PM_CREW && PM3_SOUND_ON == PM_SOUND_ON) ? 1 : -1];

typedef struct
{
	int base, cur, n;
	TimeCount seen;
	Port3DSMenuItem items[P3_MENU_MAX];
} MenuSnap;

static MenuSnap msnap[2];
static volatile int msnap_cur = 0;
static void *volatile menu_pms = NULL;
static volatile int menu_base = -1;
static volatile int pick_base = -1, pick_state = 0;
static volatile TimeCount pick_time;

static BYTE
end_state (BYTE base)
{
	switch (base)
	{
		case PM_SCAN: case PM_STARMAP: return PM_NAVIGATE;
		case PM_MIN_SCAN: return PM_LAUNCH_LANDER;
		case PM_SAVE_GAME: return PM_EXIT_GAME_MENU;
		case PM_CONVERSE: return PM_ENCOUNTER_GAME_MENU;
		case PM_FUEL: return PM_EXIT_OUTFIT;
		case PM_CREW: return PM_EXIT_SHIPYARD;
		case PM_SOUND_ON: return PM_EXIT_SETTINGS;
	}
	return base;
}

static void
add_item (MenuSnap *m, int state, const char *label)
{
	Port3DSMenuItem *it;
	int i;
	if (m->n >= P3_MENU_MAX)
		return;
	it = &m->items[m->n++];
	it->state = state;
	snprintf (it->label, sizeof it->label, "%s", label ? label : "?");
	for (i = 0; it->label[i]; ++i)
		it->label[i] = (char) toupper ((unsigned char) it->label[i]);
}

static void
add_pm (MenuSnap *m, BYTE base, BYTE pm)
{
	char buf[40];
	const char *s = GAME_STRING (MAINMENU_STRING_BASE + pm);
	if (pm == PM_FUEL)
		snprintf (buf, sizeof buf, "%s (%d RU)", s, GLOBAL (FuelCost)), s = buf;
	else if (pm == PM_CREW)
		snprintf (buf, sizeof buf, "%s (%d RU)", s, GLOBAL (CrewCost)), s = buf;
	add_item (m, pm - base + (base == PM_STARMAP ? 1 : 0), s);
}

void
port3ds_menu_seen (int base, void *pms, int cur)
{
	MenuSnap *m = &msnap[msnap_cur ^ 1];
	m->base = base;
	m->cur = cur;
	m->n = 0;
	if (base == P3M_MAIN)
	{
		add_item (m, 0, "New Game");
		add_item (m, 1, "Load Game");
		add_item (m, 2, "Play Super Melee");
		add_item (m, 3, "Setup");
		add_item (m, 4, "Quit");
	}
	else if (base == P3M_STARBASE)
	{
		add_item (m, 0, "Talk to the Commander");
		add_item (m, 1, "Outfit Starship");
		add_item (m, 2, "Shipyard");
		add_item (m, 3, "Depart Starbase");
	}
	else if (base == PM_SOUND_ON)
	{	/* the toggles show their current value, like the game's own list */
		const BYTE f = (BYTE) GLOBAL (glob_flags);
		add_pm (m, base, (f & SOUND_DISABLED) ? PM_SOUND_OFF : PM_SOUND_ON);
		add_pm (m, base, (f & MUSIC_DISABLED) ? PM_MUSIC_OFF : PM_MUSIC_ON);
		add_pm (m, base, PM_CYBORG_OFF + ((f & COMBAT_SPEED_MASK) >> COMBAT_SPEED_SHIFT));
		add_pm (m, base, PM_CHANGE_CAPTAIN);
		add_pm (m, base, PM_CHANGE_SHIP);
		add_pm (m, base, PM_EXIT_SETTINGS);
	}
	else
	{
		BYTE pm;
		for (pm = (BYTE) base; pm <= end_state ((BYTE) base); ++pm)
			add_pm (m, (BYTE) base, pm);
	}
	m->seen = GetTimeCounter ();
	menu_pms = pms;
	menu_base = base;
	msnap_cur ^= 1;
}

int
port3ds_menu_items (int *base, int *cur, Port3DSMenuItem *items, int max)
{
	const MenuSnap *m = &msnap[msnap_cur];
	int n = m->n < max ? m->n : max;
	if (menu_base < 0 || GetTimeCounter () - m->seen > ONE_SECOND * 3 / 10)
		return 0;
	*base = m->base;
	*cur = m->cur;
	memcpy (items, m->items, sizeof *items * (size_t) n);
	return n;
}

void
port3ds_menu_pick (int base, int state)
{
	pick_state = state;
	pick_time = GetTimeCounter ();
	pick_base = base;
}

static void
apply_menu_pick (void *input_state)
{
	MENU_STATE *pMS;
	if (pick_base < 0)
		return;
	if (GetTimeCounter () - pick_time > ONE_SECOND)
	{
		pick_base = -1;
		return;
	}
	if (!input_state || input_state != menu_pms || pick_base != menu_base)
		return;
	pMS = (MENU_STATE *) input_state;
	pMS->CurState = (BYTE) pick_state;
	if (pick_base < P3M_MAIN)
		DrawMenuStateStrings ((BYTE) pick_base, pMS->CurState);
	PulsedInputState.menu[KEY_MENU_SELECT] = 1;
	pick_base = -1;
}

/* ---- Super Melee -------------------------------------------------------- */

static Port3DSMelee mel[2];
static volatile int mel_cur = 0;
static volatile TimeCount mel_seen;
static volatile int mel_act = 0, mel_a, mel_b, mel_c;

static const char *const ship_short[NUM_MELEE_SHIPS] = {
	"ANDROSY", "ARILOU", "CHENJES", "CHMMR", "DRUUGE", "EARTHLG", "ILWRATH",
	"KOHR-AH", "MELNORM", "MMRNMHR", "MYCON", "ORZ", "PKUNK", "SHOFIXT",
	"SLYLAND", "SPATHI", "SUPOX", "SYREEN", "THRADD", "UMGAH", "UR-QUAN",
	"UTWIG", "VUX", "YEHAT", "ZOQFOT" };
static int ship_cost[NUM_MELEE_SHIPS];

const char *
port3ds_ship_short (int ship)
{
	return (ship >= 0 && ship < NUM_MELEE_SHIPS) ? ship_short[ship] : "";
}

int
port3ds_ship_cost (int ship)
{
	return (ship >= 0 && ship < NUM_MELEE_SHIPS) ? ship_cost[ship] : 0;
}

int
port3ds_melee_take (void *pms, int *a, int *b, int *c)
{
	MELEE_STATE *pMS = (MELEE_STATE *) pms;
	Port3DSMelee *m = &mel[mel_cur ^ 1];
	int side, i, act;

	if (!ship_cost[0])
		for (i = 0; i < NUM_MELEE_SHIPS; ++i)
			ship_cost[i] = GetShipValue ((MeleeShip) i);

	m->active = 1;
	for (side = 0; side < 2; ++side)
	{
		for (i = 0; i < MELEE_FLEET_SIZE && i < 14; ++i)
			m->ships[side][i] = (unsigned char) MeleeSetup_getShip (pMS->meleeSetup, side, i);
		m->value[side] = MeleeSetup_getFleetValue (pMS->meleeSetup, side);
		snprintf (m->name[side], sizeof m->name[side], "%s",
				MeleeSetup_getTeamName (pMS->meleeSetup, side));
	}
	/* controls: the TOP button drives PlayerControl[1], BOTTOM [0] */
	for (side = 0; side < 2; ++side)
	{
		const BYTE pc = PlayerControl[side];
		m->control[side] = (pc & NETWORK_CONTROL) ? 4 : (pc & HUMAN_CONTROL) ? 0
				: (pc & AWESOME_RATING) ? 3 : (pc & GOOD_RATING) ? 2 : 1;
	}
#ifdef NETPLAY
	m->net = 1;
#else
	m->net = 0;
#endif
	mel_seen = GetTimeCounter ();
	mel_cur ^= 1;

	act = mel_act;
	if (!act)
		return 0;
	*a = mel_a; *b = mel_b; *c = mel_c;
	mel_act = 0;
	return act;
}

int
port3ds_melee_read (Port3DSMelee *m)
{
	if (GetTimeCounter () - mel_seen > ONE_SECOND * 3 / 10)
		return 0;
	*m = mel[mel_cur];
	return 1;
}

void
port3ds_melee_option (int opt)
{
	mel_a = opt; mel_b = mel_c = 0;
	mel_act = P3MEL_OPTION;
}

void
port3ds_melee_ship (int side, int slot, int ship)
{
	mel_a = side; mel_b = slot; mel_c = ship;
	mel_act = P3MEL_SHIP;
}

/* ---- stereo depth tags ---------------------------------------------------
 * Depth travels in the draw queue (a callback command), so it applies to
 * exactly the drawing that follows it, on the thread that renders. */

static int depth_queued = 0;
static int space_batch = 0;

static void
depth_cb (void *arg)
{
	port3ds_depth_now ((int) (intptr_t) arg);
}

void
port3ds_depth_set (int depth)
{
	if (depth == depth_queued)
		return;
	depth_queued = depth;
	TFB_DrawScreen_Callback (depth_cb, (void *) (intptr_t) depth);
}

void
port3ds_space_batch (int on)
{
	if (on)
	{	/* the galaxy starfield is the first prims of the display list in
		 * HyperSpace and in ship combat; elsewhere the list holds ships */
		const int ctx = port3ds_game_context ();
		space_batch = (ctx == CTX3DS_HYPERSPACE || ctx == CTX3DS_COMBAT);
	}
	else
	{
		space_batch = 0;
		port3ds_depth_set (0);
	}
}

void
port3ds_space_prim (int index)
{
	if (!space_batch)
		return;
	/* 30 big, 60 medium, 90 small stars: three layers behind the screen */
	port3ds_depth_set (index < 30 ? 3 : index < 90 ? 5 : index < 180 ? 7 : 0);
}

/* ---- choosing the next ship in a melee round (pickmele.c) ---------------- */

static unsigned char pk_ship[2][2][14];
static volatile int pk_cur[2] = { 0, 0 };
static volatile TimeCount pk_seen[2];
static volatile int pk_req[2] = { -1, -1 };

static int
species_to_melee (int id)
{
	switch (id)
	{
		case ARILOU_ID: return MELEE_ARILOU;
		case CHMMR_ID: return MELEE_CHMMR;
		case EARTHLING_ID: return MELEE_EARTHLING;
		case ORZ_ID: return MELEE_ORZ;
		case PKUNK_ID: return MELEE_PKUNK;
		case SHOFIXTI_ID: return MELEE_SHOFIXTI;
		case SPATHI_ID: return MELEE_SPATHI;
		case SUPOX_ID: return MELEE_SUPOX;
		case THRADDASH_ID: return MELEE_THRADDASH;
		case UTWIG_ID: return MELEE_UTWIG;
		case VUX_ID: return MELEE_VUX;
		case YEHAT_ID: return MELEE_YEHAT;
		case MELNORME_ID: return MELEE_MELNORME;
		case DRUUGE_ID: return MELEE_DRUUGE;
		case ILWRATH_ID: return MELEE_ILWRATH;
		case MYCON_ID: return MELEE_MYCON;
		case SLYLANDRO_ID: return MELEE_SLYLANDRO;
		case UMGAH_ID: return MELEE_UMGAH;
		case UR_QUAN_ID: return MELEE_URQUAN;
		case ZOQFOTPIK_ID: return MELEE_ZOQFOTPIK;
		case SYREEN_ID: return MELEE_SYREEN;
		case KOHR_AH_ID: return MELEE_KOHR_AH;
		case ANDROSYNTH_ID: return MELEE_ANDROSYNTH;
		case CHENJESU_ID: return MELEE_CHENJESU;
		case MMRNMHRM_ID: return MELEE_MMRNMHRM;
	}
	return 255;
}

int
port3ds_pick_frame (int player, const int *species, int n)
{
	int i, r;
	unsigned char *dst;
	if (player < 0 || player > 1)
		return -1;
	dst = pk_ship[player][pk_cur[player] ^ 1];
	for (i = 0; i < 14; ++i)
		dst[i] = (unsigned char) (i < n && species[i] ? species_to_melee (species[i]) : 255);
	pk_cur[player] ^= 1;
	pk_seen[player] = GetTimeCounter ();
	r = pk_req[player];
	pk_req[player] = -1;
	return r;
}

int
port3ds_pick_read (int player, unsigned char ships[14])
{
	if (GetTimeCounter () - pk_seen[player] > ONE_SECOND * 3 / 10)
		return 0;
	memcpy (ships, pk_ship[player][pk_cur[player]], 14);
	return 1;
}

void
port3ds_pick (int player, int slot)
{
	if (player >= 0 && player < 2)
		pk_req[player] = slot;
}

/* ---- netplay connection dialog (cnctdlg.c) -------------------------------- */

static Port3DSNetDlg nd[2];
static volatile int nd_cur = 0;
static volatile TimeCount nd_seen;
static volatile int nd_act = 0;
static char nd_text[64];

int
port3ds_netdlg_frame (const char *host, const char *port, int delay, int side,
		char *text_out, int text_size)
{
	Port3DSNetDlg *d = &nd[nd_cur ^ 1];
	int act;
	snprintf (d->host, sizeof d->host, "%s", host);
	snprintf (d->port, sizeof d->port, "%s", port);
	d->delay = delay;
	d->side = side;
	nd_cur ^= 1;
	nd_seen = GetTimeCounter ();
	act = nd_act;
	if (!act)
		return 0;
	snprintf (text_out, (size_t) text_size, "%s", nd_text);
	nd_act = 0;
	return act;
}

int
port3ds_netdlg_read (Port3DSNetDlg *d)
{
	if (GetTimeCounter () - nd_seen > ONE_SECOND * 3 / 10)
		return 0;
	*d = nd[nd_cur];
	return 1;
}

void
port3ds_netdlg_act (int act, const char *text)
{
	snprintf (nd_text, sizeof nd_text, "%s", text ? text : "");
	nd_act = act;
}

/* ---- the flagship on screen, for steering by touch ------------------------ */

volatile int port3ds_hs_facing = -1;     /* set by sis_ship.c each HyperSpace frame */

int
port3ds_flight_ship (int *sx, int *sy, int *facing)
{
	const int ctx = port3ds_game_context ();
	if (ctx == CTX3DS_HYPERSPACE)
	{	/* the view scrolls with the ship: it stays in the middle */
		if (port3ds_hs_facing < 0)
			return 0;
		*sx = SIS_ORG_X + SIS_SCREEN_WIDTH / 2;
		*sy = SIS_ORG_Y + SIS_SCREEN_HEIGHT / 2;
		*facing = port3ds_hs_facing & 15;
		return 1;
	}
	if (ctx == CTX3DS_SOLARSYS && pSolarSysState && !pSolarSysState->InOrbit
			&& pSolarSysState->InIpFlight)
	{
		*sx = SIS_ORG_X + GLOBAL (ShipStamp.origin.x);
		*sy = SIS_ORG_Y + GLOBAL (ShipStamp.origin.y);
		*facing = GetFrameIndex (GLOBAL (ShipStamp.frame)) & 15;
		return 1;
	}
	return 0;
}
