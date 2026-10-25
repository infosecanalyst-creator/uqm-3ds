/* UQM threads on libctru.
 *
 * 3ds-sdl's SDL_CreateThread always uses the app core and a 128 KB stack, and
 * its SDL_ThreadID() returns 0 on the main thread. UQM's sdlthreads.c is
 * patched to call these instead (its SDL mutexes/semaphores/condvars are kept:
 * 3ds-sdl implements those on libctru primitives and they work across cores).
 *
 *  - Every thread gets a 256 KB stack (UQM asks for 1 KB and relies on the
 *    platform default; the game thread recurses through the whole game).
 *  - New threads run one priority step above their creator, like 3ds-sdl, so
 *    the audio and game threads preempt the main loop when they wake.
 *  - The music/speech decoder is placed on a spare core: core 2 on a New 3DS,
 *    else the system core (with a 30% time slice), else the app core. Vorbis
 *    decoding is the biggest steady CPU cost in UQM; off the app core it no
 *    longer competes with the game or the frame presenter.
 */
#include "port3ds.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define THREAD_STACK   (256 * 1024)

typedef struct {
	Thread handle;
	int (*fn)(void *);
	void *data;
	volatile int result;
} Thread3DS;

static int syscore_ok = -1;    /* -1 unknown, 0 no, 1 yes */

static void
entry (void *arg)
{
	Thread3DS *t = (Thread3DS *) arg;
	t->result = t->fn (t->data);
}

static s32
creator_priority (void)
{
	s32 prio = 0x30;
	svcGetThreadPriority (&prio, CUR_THREAD_HANDLE);
	return prio;
}

SDL_Thread *
port3ds_thread_create (int (*fn)(void *), void *data, const char *name)
{
	Thread3DS *t = (Thread3DS *) calloc (1, sizeof (*t));
	s32 prio;
	if (!t)
		return NULL;
	t->fn = fn;
	t->data = data;

	prio = creator_priority ();
	prio = prio > 0x19 ? prio - 1 : 0x19;
	if (prio > 0x2F)
		prio = 0x2F;

	if (name && strcmp (name, "audio stream decoder") == 0)
	{
		bool n3ds = false;
		APT_CheckNew3DS (&n3ds);
		if (n3ds)
			t->handle = threadCreate (entry, t, THREAD_STACK, 0x18, 2, false);
		if (!t->handle)
		{
			if (syscore_ok < 0)
				syscore_ok = R_SUCCEEDED (APT_SetAppCpuTimeLimit (30)) ? 1 : 0;
			if (syscore_ok)
				t->handle = threadCreate (entry, t, THREAD_STACK, 0x18, 1, false);
		}
	}
	if (!t->handle)
		t->handle = threadCreate (entry, t, THREAD_STACK, prio, -2, false);
	if (!t->handle)
	{
		free (t);
		return NULL;
	}
	return (SDL_Thread *) t;
}

void
port3ds_thread_wait (SDL_Thread *st, int *status)
{
	Thread3DS *t = (Thread3DS *) st;
	if (!t)
		return;
	threadJoin (t->handle, U64_MAX);
	if (status)
		*status = t->result;
	threadFree (t->handle);
	free (t);
}

Uint32
port3ds_thread_id (void)
{
	u32 id = 0;
	svcGetThreadId (&id, CUR_THREAD_HANDLE);
	return id;
}

Uint32
port3ds_thread_get_id (SDL_Thread *st)
{
	Thread3DS *t = (Thread3DS *) st;
	u32 id = 0;
	if (t && t->handle)
		svcGetThreadId (&id, threadGetHandle (t->handle));
	return id;
}
