/* What the game is doing right now, for the control deck's button legend.
 * Built inside the UQM tree (needs uqm/globdata.h). Reads one word written
 * by the game thread; a stale read only shows the wrong legend for a frame. */
#include "port3ds.h"
#include "uqm/globdata.h"

int
port3ds_game_context (void)
{
	ACTIVITY act = GLOBAL (CurrentActivity);
	BYTE lo = LOBYTE (act);

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
