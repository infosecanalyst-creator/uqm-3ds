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

void
port3ds_game_frame (void)
{
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
