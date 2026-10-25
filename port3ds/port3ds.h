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

#ifdef __cplusplus
}
#endif

#endif /* PORT3DS_H */
