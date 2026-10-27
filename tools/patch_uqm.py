#!/usr/bin/env python3
"""Apply the 3DS port's edits to a pristine UQM 0.8.0 source tree.

Usage: patch_uqm.py <path to uqm-0.8.0>

Every edit is anchored on exact upstream text and guarded by #ifdef __3DS__,
so the patched tree still builds for the desktop. Re-running is safe: an edit
whose marker is already present is skipped; an anchor that is missing (an
upstream change) stops the script instead of guessing.
"""
import os, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "uqm-0.8.0"
SRC = os.path.join(ROOT, "src")
applied = skipped = 0


def rw(rel, old, new, marker, count=1):
    global applied, skipped
    path = os.path.join(SRC, rel)
    with open(path, encoding="latin-1") as f:
        s = f.read()
    if marker in s:
        skipped += 1
        return
    n = s.count(old)
    if n != count:
        sys.exit(f"patch_uqm: anchor found {n}x (want {count}) in {rel}:\n{old}")
    s = s.replace(old, new)
    with open(path, "w", encoding="latin-1") as f:
        f.write(s)
    applied += 1


# ---- uqm.c: bring the console up before anything else ----------------------
rw("uqm.c",
   "#include <stdarg.h>\n#include <errno.h>\n",
   "#include <stdarg.h>\n#include <errno.h>\n#ifdef __3DS__\n#\tinclude \"port3ds.h\"\n#\tinclude \"libs/alarm.h\"  /* Alarm_init(): only included for NETPLAY upstream */\n#endif\n",
   '#\tinclude "port3ds.h"')
rw("uqm.c",
   "main (int argc, char *argv[])\n{\n\tstruct options_struct options = {\n",
   "main (int argc, char *argv[])\n{\n#ifdef __3DS__\n\t/* registered before any of UQM's own atexit() handlers, so it is\n\t * also the last thing to run on exit */\n\tport3ds_init ();\n\tatexit (port3ds_exit);\n#endif\n\tstruct options_struct options = {\n",
   "port3ds_init ();")

# ---- sdl1_common.c: SDL without its video driver -----------------------------
# We drive the framebuffers ourselves; 3ds-sdl's video driver would start its
# own GPU thread and own the screens. Software surfaces work without it.
rw("libs/graphics/sdl/sdl1_common.c",
   "\tif ((SDL_Init (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) == -1))\n",
   "#ifdef __3DS__\n\tif ((SDL_Init (SDL_INIT_NOPARACHUTE) == -1))\n#else\n\tif ((SDL_Init (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) == -1))\n#endif\n",
   "SDL_Init (SDL_INIT_NOPARACHUTE)")
rw("libs/graphics/sdl/sdl1_common.c",
   "\tSDL_WM_SetCaption (caption, NULL);\n\n\tif (flags & TFB_GFXFLAGS_FULLSCREEN)\n\t\tSDL_ShowCursor (SDL_DISABLE);\n\telse\n\t\tSDL_ShowCursor (SDL_ENABLE);\n",
   "#ifndef __3DS__\n\tSDL_WM_SetCaption (caption, NULL);\n\n\tif (flags & TFB_GFXFLAGS_FULLSCREEN)\n\t\tSDL_ShowCursor (SDL_DISABLE);\n\telse\n\t\tSDL_ShowCursor (SDL_ENABLE);\n#endif\n",
   "#ifndef __3DS__\n\tSDL_WM_SetCaption")
rw("libs/graphics/sdl/sdl1_common.c",
   "\treturn (SDL_SetGamma (gamma, gamma, gamma) == 0);\n",
   "#ifdef __3DS__\n\t(void) gamma;\n\treturn false;\n#else\n\treturn (SDL_SetGamma (gamma, gamma, gamma) == 0);\n#endif\n",
   "(void) gamma;\n\treturn false;")

# ---- sdl_common.c ------------------------------------------------------------
rw("libs/graphics/sdl/sdl_common.c",
   "\tSDL_WM_SetCaption (caption, NULL);\n#endif\n\n\tif (flags & TFB_GFXFLAGS_FULLSCREEN)\n\t\tSDL_ShowCursor (SDL_DISABLE);\n",
   "#ifndef __3DS__\n\tSDL_WM_SetCaption (caption, NULL);\n#endif\n#endif\n\n#ifndef __3DS__\n\tif (flags & TFB_GFXFLAGS_FULLSCREEN)\n\t\tSDL_ShowCursor (SDL_DISABLE);\n#endif\n",
   "#ifndef __3DS__\n\tSDL_WM_SetCaption (caption, NULL);")
rw("libs/graphics/sdl/sdl_common.c",
   "#include \"libs/vidlib.h\"\n",
   "#include \"libs/vidlib.h\"\n#ifdef __3DS__\n#\tinclude \"port3ds.h\"\n#endif\n",
   '#\tinclude "port3ds.h"')
rw("libs/graphics/sdl/sdl_common.c",
   "TFB_ProcessEvents ()\n{\n\tSDL_Event Event;\n\n",
   "TFB_ProcessEvents ()\n{\n#ifdef __3DS__\n\t/* buttons and touch are read from the HID service, not SDL events */\n\tif (!port3ds_pump_input ())\n\t\tQuitPosted = 1;\n\treturn;\n#endif\n\tSDL_Event Event;\n\n",
   "if (!port3ds_pump_input ())")

# ---- pure.c: the frame surface and presenting it -------------------------------
rw("libs/graphics/sdl/pure.c",
   "#include \"libs/log.h\"\n",
   "#include \"libs/log.h\"\n#ifdef __3DS__\n#\tinclude <string.h>\n#\tinclude \"port3ds.h\"\n#endif\n",
   '#\tinclude "port3ds.h"')
rw("libs/graphics/sdl/pure.c",
   "\tif (width == 320 && height == 240)\n\t{\n",
   "#ifdef __3DS__\n\twidth = 320;\n\theight = 240;\n#endif\n\tif (width == 320 && height == 240)\n\t{\n",
   "#ifdef __3DS__\n\twidth = 320;")
rw("libs/graphics/sdl/pure.c",
   "\tSDL_Video = SDL_SetVideoMode (ScreenWidthActual, ScreenHeightActual, \n\t\t32, videomode_flags);\n",
   "#ifdef __3DS__\n\t(void) videomode_flags;\n\tif (!SDL_Video)\n\t\tSDL_Video = port3ds_create_video_surface ();\n#else\n\tSDL_Video = SDL_SetVideoMode (ScreenWidthActual, ScreenHeightActual, \n\t\t32, videomode_flags);\n#endif\n",
   "port3ds_create_video_surface ()")
rw("libs/graphics/sdl/pure.c",
   "\t\tconst SDL_Surface *video = SDL_GetVideoSurface ();\n",
   "#ifdef __3DS__\n\t\tconst SDL_Surface *video = SDL_Video;\n#else\n\t\tconst SDL_Surface *video = SDL_GetVideoSurface ();\n#endif\n",
   "const SDL_Surface *video = SDL_Video;")
rw("libs/graphics/sdl/pure.c",
   "\tSDL_VideoDriverName (VideoName, sizeof (VideoName));\n",
   "#ifdef __3DS__\n\tstrcpy (VideoName, \"3ds framebuffer\");\n#else\n\tSDL_VideoDriverName (VideoName, sizeof (VideoName));\n#endif\n",
   'strcpy (VideoName, "3ds framebuffer");')
rw("libs/graphics/sdl/pure.c",
   "\tSDL_UpdateRect (SDL_Video, updated.x, updated.y,\n\t\t\tupdated.w, updated.h);\n",
   "#ifdef __3DS__\n\tport3ds_present (SDL_Video);\n#else\n\tSDL_UpdateRect (SDL_Video, updated.x, updated.y,\n\t\t\tupdated.w, updated.h);\n#endif\n",
   "port3ds_present (SDL_Video);")

# ---- sdlthreads.c: libctru threads ---------------------------------------------
T = "libs/threads/sdl/sdlthreads.c"
rw(T,
   "#include \"libs/log.h\"\n",
   "#include \"libs/log.h\"\n#ifdef __3DS__\n#\tinclude \"port3ds.h\"\n#\tdefine SDL_ThreadID port3ds_thread_id\n#\tdefine SDL_GetThreadID port3ds_thread_get_id\n#\tdefine SDL_WaitThread port3ds_thread_wait\n#endif\n",
   "#\tdefine SDL_ThreadID port3ds_thread_id")
rw(T,
   "#if SDL_MAJOR_VERSION == 1\n\t// SDL1 case\n\tthread->native = SDL_CreateThread (ThreadHelper, (void *) startInfo);\n",
   "#if defined(__3DS__)\n\tthread->native = port3ds_thread_create (ThreadHelper, (void *) startInfo,\n\t\t\tthread->name);\n#elif SDL_MAJOR_VERSION == 1\n\t// SDL1 case\n\tthread->native = SDL_CreateThread (ThreadHelper, (void *) startInfo);\n",
   "port3ds_thread_create (ThreadHelper")

# ---- input.c: tell the deck when the game wants typed text ----------------------
I = "libs/input/sdl/input.c"
rw(I,
   "#include \"libs/log.h\"\n",
   "#include \"libs/log.h\"\n#ifdef __3DS__\n#\tinclude \"port3ds.h\"\n#endif\n",
   '#\tinclude "port3ds.h"')
rw(I,
   "\tin_character_mode = TRUE;\n\tVControl_ResetInput ();\n}\n",
   "\tin_character_mode = TRUE;\n\tVControl_ResetInput ();\n#ifdef __3DS__\n\tport3ds_set_char_mode (1);\n#endif\n}\n",
   "port3ds_set_char_mode (1);")
rw(I,
   "\tVControl_ResetInput ();\n\tin_character_mode = FALSE;\n",
   "\tVControl_ResetInput ();\n\tin_character_mode = FALSE;\n#ifdef __3DS__\n\tport3ds_set_char_mode (0);\n#endif\n",
   "port3ds_set_char_mode (0);")

# ---- getchar.c: take the software keyboard's text --------------------------------
G = "uqm/getchar.c"
rw(G,
   "#include \"libs/log.h\"\n",
   "#include \"libs/log.h\"\n#ifdef __3DS__\n#\tinclude \"port3ds.h\"\n#endif\n",
   '#\tinclude "port3ds.h"')
rw(G,
   "\t// process the pending character buffer\n\tch = GetNextCharacter ();\n",
   "#ifdef __3DS__\n\tif (port3ds_text_entry_frame (pTES->BaseStr, pTES->MaxSize))\n\t{\t// the whole name came from the software keyboard\n\t\tpTES->CursorPos = utf8StringCount (pTES->BaseStr);\n\t\tpStr = pTES->BaseStr + strlen (pTES->BaseStr);\n\t\tlen = 0;\n\t\tchanged = TRUE;\n\t}\n#endif\n\t// process the pending character buffer\n\tch = GetNextCharacter ();\n",
   "port3ds_text_entry_frame (pTES->BaseStr")

# ---- audiodrv_sdl.c: "medium" quality mixes at 22 kHz on the 3DS -------------------
# The DSP resamples to its native ~32.7 kHz anyway; mixing at 22050 halves
# the mixer's CPU time. "high" in Setup still gives 44.1 kHz.
rw("libs/sound/mixer/sdl/audiodrv_sdl.c",
   "\telse\n\t{\n\t\tquality = MIX_QUALITY_DEFAULT;\n\t\tdesired.freq = 44100;\n\t\tdesired.samples = 4096;\n",
   "\telse\n\t{\n\t\tquality = MIX_QUALITY_DEFAULT;\n#ifdef __3DS__\n\t\tdesired.freq = 22050;\n\t\tdesired.samples = 2048;\n#else\n\t\tdesired.freq = 44100;\n\t\tdesired.samples = 4096;\n#endif\n",
   "#ifdef __3DS__\n\t\tdesired.freq = 22050;")

# ---- uqmlog.c: only warnings and errors reach the log file -------------------------
# Upstream streams every message (each resource load, each sound bank) to
# stderr; on the 3DS that is a line-buffered write to the SD card per load.
rw("libs/log/uqmlog.c",
   "static int maxStreamLevel = log_Debug;\n",
   "#ifdef __3DS__\nstatic int maxStreamLevel = log_Warning;\n#else\nstatic int maxStreamLevel = log_Debug;\n#endif\n",
   "static int maxStreamLevel = log_Warning;")

# ---- oggaud.c: devkitPro's Tremor embeds the stream state ---------------------------
# Upstream's Tremor branch expects the old pointer layout (vf.os->pageno).
rw("libs/sound/decoders/oggaud.c",
   "#ifdef OVCODEC_TREMOR\n\treturn ova->vf.os->pageno;\n",
   "#if defined(OVCODEC_TREMOR) && !defined(__3DS__)\n\treturn ova->vf.os->pageno;\n",
   "#if defined(OVCODEC_TREMOR) && !defined(__3DS__)")

# ---- gameinp.c: apply the deck's queued actions on the game thread -----------------
rw("uqm/gameinp.c",
   "\t\tUpdateInputState ();\n",
   "\t\tUpdateInputState ();\n#ifdef __3DS__\n\t\tport3ds_game_frame ();\n#endif\n",
   "port3ds_game_frame (")
rw("uqm/gameinp.c",
   "#include \"libs/threadlib.h\"\n",
   "#include \"libs/threadlib.h\"\n#ifdef __3DS__\n#\tinclude \"port3ds.h\"\n#endif\n",
   '#\tinclude "port3ds.h"')

# ---- comm.c: conversation replies as touch buttons on the deck ----------------------
rw("uqm/comm.c",
   "\tif (pES->top_response == (BYTE)~0)\n\t{\n\t\tpES->top_response = 0;\n\t\tRefreshResponses (pES);\n\t}\n\n\tif (PulsedInputState.menu[KEY_MENU_SELECT])\n",
   "\tif (pES->top_response == (BYTE)~0)\n\t{\n\t\tpES->top_response = 0;\n\t\tRefreshResponses (pES);\n\t}\n\n"
   "#ifdef __3DS__\n\t{\t/* one tap on the deck says a reply */\n\t\tconst char *texts[MAX_RESPONSES];\n\t\tint i, pick;\n"
   "\t\tfor (i = 0; i < pES->num_responses; ++i)\n\t\t\ttexts[i] = pES->response_list[i].response_text.pStr;\n"
   "\t\tpick = port3ds_comm_frame (texts, pES->num_responses, pES->cur_response);\n"
   "\t\tif (pick >= 0 && pick < pES->num_responses)\n\t\t{\n\t\t\tpES->cur_response = (BYTE) pick;\n\t\t\tSelectResponse (pES);\n\t\t\treturn;\n\t\t}\n\t}\n#endif\n"
   "\tif (PulsedInputState.menu[KEY_MENU_SELECT])\n",
   "port3ds_comm_frame (texts")
rw("uqm/comm.c",
   "#include \"libs/log.h\"\n",
   "#include \"libs/log.h\"\n#ifdef __3DS__\n#\tinclude \"port3ds.h\"\n#endif\n",
   '#\tinclude "port3ds.h"')

# ---- ship-menu shortcuts: one deck tap opens Devices / Cargo / Roster / Save / Load
# (and Scan in orbit) directly, then drops back to flight -------------------------
INC = '#ifdef __3DS__\n#\tinclude "port3ds.h"\n#endif\n'
for rel, anchor in (("uqm/hyper.c", '#include "libs/mathlib.h"\n'),
                    ("uqm/planets/solarsys.c", '#include "libs/mathlib.h"\n'),
                    ("uqm/planets/planets.c", '#include "options.h"\n'),
                    ("uqm/gameopt.c", '#include "util.h"\n'),
                    ("uqm/ships/sis_ship/sis_ship.c", '#include "libs/mathlib.h"\n')):
    rw(rel, anchor, anchor + INC, '#\tinclude "port3ds.h"')

# flight: a pending shortcut opens the ship menu like B does
rw("uqm/ships/sis_ship/sis_ship.c",
   "\t\t\tPulsedInputState.menu[KEY_MENU_CANCEL])\n",
   "\t\t\tPulsedInputState.menu[KEY_MENU_CANCEL]\n#ifdef __3DS__\n\t\t\t|| port3ds_shortcut_pending (0)\n#endif\n\t\t\t)\n",
   "port3ds_shortcut_pending (0)")
rw("uqm/planets/solarsys.c",
   "\tBOOLEAN cancel = PulsedInputState.menu[KEY_MENU_CANCEL];\n",
   "\tBOOLEAN cancel = PulsedInputState.menu[KEY_MENU_CANCEL];\n#ifdef __3DS__\n\tif (!pSS->InOrbit && port3ds_shortcut_pending (0))\n\t\tcancel = TRUE;\n#endif\n",
   "port3ds_shortcut_pending (0)")

# the menus: jump to the item and select it
JUMP = ("#ifdef __3DS__\n\tconst int p3sc = port3ds_take_shortcut (%d);\n\tif (p3sc %s)\n\t{\n"
        "\t\tpMS->CurState = (BYTE) p3sc;\n\t\tDrawMenuStateStrings (%s, pMS->CurState);\n"
        "\t\tselect = TRUE;\n\t\thandled = FALSE;\n\t}\n\telse\n#endif\n")
for rel, base, orbit in (("uqm/hyper.c", "PM_STARMAP", 0),
                         ("uqm/planets/solarsys.c", "PM_STARMAP", 0),
                         ("uqm/planets/planets.c", "PM_SCAN", 1)):
    call = "\thandled = DoMenuChooser (pMS, %s);\n" % base
    rw(rel, call, (JUMP % (orbit, ">= 0" if orbit else "> 0", base)) + call,
       "port3ds_take_shortcut (")
    tail = "\n\tif (!(GLOBAL (CurrentActivity) & CHECK_ABORT))\n\t{\n\t\tif (select)\n"
    if orbit:   # stay in orbit, item still highlighted
        back = ("\n#ifdef __3DS__\n\tif (p3sc >= 0 && !(GLOBAL (CurrentActivity) & CHECK_ABORT))\n\t{\n"
                "\t\tDrawMenuStateStrings (PM_SCAN, pMS->CurState);\n\t\tSetFlashRect (SFR_MENU_3DO);\n"
                "\t\treturn TRUE;\n\t}\n#endif\n")
    else:       # back to flight
        back = "\n#ifdef __3DS__\n\tif (p3sc > 0)\n\t\treturn FALSE;\n#endif\n"
    rw(rel, tail, back + tail, "p3sc >= 0 && !(" if orbit else "if (p3sc > 0)\n\t\treturn FALSE;")

# Game menu: straight into the Save / Load picker
rw("uqm/gameopt.c",
   "DoGameOptions (MENU_STATE *pMS)\n{\n\tif (GLOBAL (CurrentActivity) & CHECK_ABORT)\n\t\treturn FALSE;\n",
   "DoGameOptions (MENU_STATE *pMS)\n{\n\tif (GLOBAL (CurrentActivity) & CHECK_ABORT)\n\t\treturn FALSE;\n"
   "#ifdef __3DS__\n\t{\n\t\tconst int p3go = port3ds_take_gameopt ();\n"
   "\t\tif (p3go == SAVE_GAME || p3go == LOAD_GAME)\n\t\t{\n"
   "\t\t\tpMS->CurState = (BYTE) p3go;\n\t\t\tDrawMenuStateStrings (PM_SAVE_GAME, p3go);\n"
   "\t\t\tSetFlashRect (NULL);\n\t\t\tPickGame (p3go == SAVE_GAME, FALSE);\n"
   "\t\t\treturn FALSE;\n\t\t}\n\t}\n#endif\n",
   "port3ds_take_gameopt ()")

# ---- DoInput hands the running menu to the deck hook --------------------------------
rw("uqm/gameinp.c",
   "\t\tport3ds_game_frame ();\n",
   "\t\tport3ds_game_frame (pInputState);\n",
   "port3ds_game_frame (pInputState);")

# ---- the game's own list menus as deck buttons ---------------------------------------
for rel, anchor in (("uqm/menu.c", '#include "libs/log.h"\n'),
                    ("uqm/restart.c", '#include "libs/inplib.h"\n'),
                    ("uqm/starbase.c", '#include "libs/tasklib.h"\n'),
                    ("uqm/supermelee/melee.c", '#include "melee.h"\n'),
                    ("uqm/process.c", '#include "libs/misc.h"\n'),
                    ("libs/graphics/frame.c", '#include "tfb_prim.h"\n'),
                    ("uqm/planets/pl_stuff.c", '#include "options.h"\n'),
                    ("libs/graphics/sdl/canvas.c", '#include "libs/graphics/cmap.h"\n')):
    rw(rel, anchor, anchor + INC, '#\tinclude "port3ds.h"')

# every DoMenuChooser menu (ship, orbit, scan, game, settings, encounter, outfit, shipyard)
rw("uqm/menu.c",
   "\tBOOLEAN useAltMenu = FALSE;\n\tif (optWhichMenu == OPT_PC)\n\t\tuseAltMenu = GetAlternateMenu (&BaseState, &NewState);\n",
   "\tBOOLEAN useAltMenu = FALSE;\n#ifdef __3DS__\n\tport3ds_menu_seen (BaseState, pMS, pMS->CurState);\n#endif\n"
   "\tif (optWhichMenu == OPT_PC)\n\t\tuseAltMenu = GetAlternateMenu (&BaseState, &NewState);\n",
   "port3ds_menu_seen (BaseState")
rw("uqm/restart.c",
   "\tif (pMS->Initialized)\n\t\tFlash_process(pMS->flashContext);\n",
   "\tif (pMS->Initialized)\n\t\tFlash_process(pMS->flashContext);\n#ifdef __3DS__\n\tif (pMS->Initialized)\n\t\tport3ds_menu_seen (P3M_MAIN, pMS, pMS->CurState);\n#endif\n",
   "port3ds_menu_seen (P3M_MAIN")
rw("uqm/starbase.c",
   "\tSetMenuSounds (MENU_SOUND_ARROWS, MENU_SOUND_SELECT);\n\n\tif (!pMS->Initialized)\n\t{\n\t\tLastActivity &= ~CHECK_LOAD;\n",
   "\tSetMenuSounds (MENU_SOUND_ARROWS, MENU_SOUND_SELECT);\n#ifdef __3DS__\n\tif (pMS->Initialized)\n\t\tport3ds_menu_seen (P3M_STARBASE, pMS, pMS->CurState);\n#endif\n\n"
   "\tif (!pMS->Initialized)\n\t{\n\t\tLastActivity &= ~CHECK_LOAD;\n",
   "port3ds_menu_seen (P3M_STARBASE")

# Super Melee setup: buttons and fleet slots from the deck
rw("uqm/supermelee/melee.c",
   "#ifdef NETPLAY\n\tnetInput ();\n#endif\n\t\n\tif (PulsedInputState.menu[KEY_MENU_CANCEL] ||\n",
   "#ifdef NETPLAY\n\tnetInput ();\n#endif\n"
   "#ifdef __3DS__\n\t{\t/* the deck: option buttons and fleet slots by touch */\n"
   "\t\tint p3a = 0, p3b = 0, p3c = 0;\n"
   "\t\tconst int p3k = port3ds_melee_take (pMS, &p3a, &p3b, &p3c);\n"
   "\t\tif (p3k == P3MEL_SHIP && p3a >= 0 && p3a < 2 && p3b >= 0 && p3b < MELEE_FLEET_SIZE\n"
   "\t\t\t\t&& (p3c < NUM_MELEE_SHIPS || p3c == MELEE_NONE))\n\t\t{\n"
   "\t\t\tpMS->LastInputTime = GetTimeCounter ();\n"
   "\t\t\tMelee_LocalChange_ship (pMS, (COUNT) p3a, (FleetShipIndex) p3b, (MeleeShip) p3c);\n\t\t}\n"
   "\t\telse if (p3k == P3MEL_OPTION)\n\t\t{\n\t\t\tint opt = -1;\n\t\t\tswitch (p3a)\n\t\t\t{\n"
   "\t\t\t\tcase P3MO_CTRL_TOP: opt = CONTROLS_TOP; break;\n"
   "\t\t\t\tcase P3MO_LOAD_TOP: opt = LOAD_TOP; break;\n"
   "\t\t\t\tcase P3MO_SAVE_TOP: opt = SAVE_TOP; break;\n"
   "\t\t\t\tcase P3MO_BATTLE: opt = START_MELEE; break;\n"
   "\t\t\t\tcase P3MO_LOAD_BOT: opt = LOAD_BOT; break;\n"
   "\t\t\t\tcase P3MO_SAVE_BOT: opt = SAVE_BOT; break;\n"
   "\t\t\t\tcase P3MO_CTRL_BOT: opt = CONTROLS_BOT; break;\n"
   "\t\t\t\tcase P3MO_QUIT: opt = QUIT_BOT; break;\n"
   "#ifdef NETPLAY\n\t\t\t\tcase P3MO_NET_TOP: opt = NET_TOP; break;\n\t\t\t\tcase P3MO_NET_BOT: opt = NET_BOT; break;\n#endif\n"
   "\t\t\t}\n\t\t\tif (opt >= 0)\n\t\t\t{\n"
   "\t\t\t\tpMS->LastInputTime = GetTimeCounter ();\n"
   "\t\t\t\tDeselect (pMS->MeleeOption);\n\t\t\t\tpMS->MeleeOption = (MELEE_OPTIONS) opt;\n"
   "\t\t\t\tSelect (pMS->MeleeOption);\n\t\t\t\tMeleeOptionSelect (pMS);\n"
   "\t\t\t\tif (GLOBAL (CurrentActivity) & CHECK_ABORT)\n\t\t\t\t\treturn FALSE;\n"
   "\t\t\t\tSleepThreadUntil (TimeIn + ONE_SECOND / 30);\n\t\t\t\treturn TRUE;\n\t\t\t}\n\t\t}\n\t}\n#endif\n"
   "\t\n\tif (PulsedInputState.menu[KEY_MENU_CANCEL] ||\n",
   "port3ds_melee_take (pMS")

# ---- stereo 3D: depth tags in the draw queue, depth marks in the canvas ---------------
rw("uqm/process.c",
   "\t\t\tDrawBatch (DisplayArray, DisplayLinks, 0);//BATCH_BUILD_PAGE);\n",
   "#ifdef __3DS__\n\t\t\tport3ds_space_batch (1);\n#endif\n"
   "\t\t\tDrawBatch (DisplayArray, DisplayLinks, 0);//BATCH_BUILD_PAGE);\n"
   "#ifdef __3DS__\n\t\t\tport3ds_space_batch (0);\n#endif\n",
   "port3ds_space_batch (1)")
rw("libs/graphics/frame.c",
   "\t\t\tlpPrim = &lpBasePrim[CurIndex];\n",
   "\t\t\tlpPrim = &lpBasePrim[CurIndex];\n#ifdef __3DS__\n\t\t\tport3ds_space_prim (CurIndex);\n#endif\n",
   "port3ds_space_prim (CurIndex)")
rw("uqm/planets/solarsys.c",
   "\ts.frame = SolarSysFrame;\n\tDrawStamp (&s);\n}\n",
   "\ts.frame = SolarSysFrame;\n#ifdef __3DS__\n\tport3ds_depth_set (2);\t/* the system behind the ships */\n#endif\n"
   "\tDrawStamp (&s);\n#ifdef __3DS__\n\tport3ds_depth_set (0);\n#endif\n}\n",
   "port3ds_depth_set (2)", count=2)
rw("uqm/planets/pl_stuff.c",
   "\tBatchGraphics ();\n\ts.frame = Orbit->SphereFrame;\n\tDrawStamp (&s);\n\tif (Orbit->ObjectFrame)\n\t{\n\t\ts.frame = Orbit->ObjectFrame;\n\t\tDrawStamp (&s);\n\t}\n\tUnbatchGraphics ();\n",
   "\tBatchGraphics ();\n#ifdef __3DS__\n\tport3ds_depth_set (-3);\t/* the planet floats in front */\n#endif\n"
   "\ts.frame = Orbit->SphereFrame;\n\tDrawStamp (&s);\n\tif (Orbit->ObjectFrame)\n\t{\n\t\ts.frame = Orbit->ObjectFrame;\n\t\tDrawStamp (&s);\n\t}\n"
   "#ifdef __3DS__\n\tport3ds_depth_set (0);\n#endif\n\tUnbatchGraphics ();\n",
   "port3ds_depth_set (-3)")
rw("libs/graphics/sdl/canvas.c",
   "\tSDL_LockSurface (dst);\n\tline_prim (x1, y1, x2, y2, sdlColor, plotFn, mode.factor, dst);\n",
   "#ifdef __3DS__\n\tport3ds_depth_mark_line (dst, x1, y1, x2, y2);\n#endif\n"
   "\tSDL_LockSurface (dst);\n\tline_prim (x1, y1, x2, y2, sdlColor, plotFn, mode.factor, dst);\n",
   "port3ds_depth_mark_line (dst")
rw("libs/graphics/sdl/canvas.c",
   "\tsr.h = rect->extent.height;\n\n\tcheckPrimitiveMode (dst, &color, &mode);\n",
   "\tsr.h = rect->extent.height;\n#ifdef __3DS__\n\tport3ds_depth_mark_rect (dst, sr.x, sr.y, sr.w, sr.h);\n#endif\n\n\tcheckPrimitiveMode (dst, &color, &mode);\n",
   "port3ds_depth_mark_rect (dst, sr.x")
rw("libs/graphics/sdl/canvas.c",
   "\t\tSDL_Surface *dst, SDL_Rect *dst_r, DrawMode mode)\n{\n\tSDL_PixelFormat *srcfmt = src->format;\n",
   "\t\tSDL_Surface *dst, SDL_Rect *dst_r, DrawMode mode)\n{\n\tSDL_PixelFormat *srcfmt = src->format;\n#ifdef __3DS__\n\tport3ds_depth_mark_blit (src, src_r, dst, dst_r);\n#endif\n",
   "port3ds_depth_mark_blit (src")
rw("libs/graphics/sdl/canvas.c",
   "\ttargetRect.h = srcRect->extent.height;\n\n\tSDL_BlitSurface (source, &sourceRect, target, &targetRect);\n",
   "\ttargetRect.h = srcRect->extent.height;\n#ifdef __3DS__\n\tport3ds_depth_mark_rect ((SDL_Surface *) target, targetRect.x, targetRect.y,\n\t\t\tsourceRect.w, sourceRect.h);\n#endif\n\n\tSDL_BlitSurface (source, &sourceRect, target, &targetRect);\n",
   "port3ds_depth_mark_rect ((SDL_Surface *) target")
# no RLE: the depth marker reads sprite pixels (RLE surfaces drop them)
rw("libs/graphics/sdl/sdl1_common.c",
   "\treturn SDL_SetColorKey (surface, SDL_SRCCOLORKEY | (rleaccel ? SDL_RLEACCEL : 0), key);\n",
   "#ifdef __3DS__\n\t(void) rleaccel;\n\treturn SDL_SetColorKey (surface, SDL_SRCCOLORKEY, key);\n#else\n"
   "\treturn SDL_SetColorKey (surface, SDL_SRCCOLORKEY | (rleaccel ? SDL_RLEACCEL : 0), key);\n#endif\n",
   "\t(void) rleaccel;\n\treturn SDL_SetColorKey (surface, SDL_SRCCOLORKEY, key);")
rw("libs/graphics/sdl/pure.c",
   "\tbackbuffer = SDL_Video;\n\t(void)transition_amount;\n\t(void)fade_amount;\n}\n",
   "\tbackbuffer = SDL_Video;\n#ifdef __3DS__\n\tport3ds_stereo_ok = (transition_amount == 255 && fade_amount == 255);\n#endif\n"
   "\t(void)transition_amount;\n\t(void)fade_amount;\n}\n",
   "port3ds_stereo_ok = (transition_amount")
rw("libs/graphics/sdl/pure.c",
   "\tSDL_Screen = SDL_Screens[0];\n",
   "\tSDL_Screen = SDL_Screens[0];\n#ifdef __3DS__\n\tport3ds_set_main_screen (SDL_Screens[0]);\n#endif\n",
   "port3ds_set_main_screen (SDL_Screens[0])")

# Super Melee: choosing the next ship by touch
rw("uqm/supermelee/pickmele.c", '#include "libs/mathlib.h"\n', '#include "libs/mathlib.h"\n' + INC,
   '#\tinclude "port3ds.h"')
rw("uqm/supermelee/pickmele.c",
   "\tBATTLE_INPUT_STATE inputState =\n\t\t\tPulsedInputToBattleInput (context->playerNr);\n\n"
   "\treturn SelectShip_processInput (gms, context->playerNr, inputState);\n",
   "\tBATTLE_INPUT_STATE inputState =\n\t\t\tPulsedInputToBattleInput (context->playerNr);\n\n"
   "#ifdef __3DS__\n\t{\t/* the deck lists this player's remaining ships */\n"
   "\t\tconst COUNT pl = context->playerNr;\n\t\tint species[MELEE_FLEET_SIZE], i, p3;\n"
   "\t\tfor (i = 0; i < MELEE_FLEET_SIZE; ++i)\n\t\t{\n"
   "\t\t\tHSTARSHIP h = MeleeShipByQueueIndex (&race_q[pl], (COUNT) i);\n"
   "\t\t\tspecies[i] = 0;\n\t\t\tif (h)\n\t\t\t{\n"
   "\t\t\t\tSTARSHIP *sp = LockStarShip (&race_q[pl], h);\n"
   "\t\t\t\tspecies[i] = sp->SpeciesID;\n\t\t\t\tUnlockStarShip (&race_q[pl], h);\n\t\t\t}\n\t\t}\n"
   "\t\tp3 = port3ds_pick_frame (pl, species, MELEE_FLEET_SIZE);\n"
   "\t\tif (p3 == P3_PICK_RANDOM)\n\t\t{\n"
   "\t\t\t(void) setShipSelected (gms, pl, (COUNT) ~0, TRUE);\n\t\t\treturn TRUE;\n\t\t}\n"
   "\t\tif (p3 >= 0 && p3 < MELEE_FLEET_SIZE && species[p3])\n\t\t{\n"
   "\t\t\t(void) setShipSelected (gms, pl, (COUNT) p3, TRUE);\n\t\t\treturn TRUE;\n\t\t}\n\t}\n#endif\n"
   "\treturn SelectShip_processInput (gms, context->playerNr, inputState);\n",
   "port3ds_pick_frame (pl")

# Super Melee network dialog from the deck
rw("uqm/cnctdlg.c", '#include "supermelee/netplay/netoptions.h"\n',
   '#include "supermelee/netplay/netoptions.h"\n' + INC, '#\tinclude "port3ds.h"')
rw("uqm/cnctdlg.c",
   "\tchanged = TRUE;\n\n\tif (PulsedInputState.menu[KEY_MENU_UP])\n",
   "#ifdef __3DS__\n\t{\t/* the deck: host, port, delay and the three buttons */\n"
   "\t\tchar p3text[64];\n"
   "\t\tconst int p3 = port3ds_netdlg_frame (texts[0].value, texts[1].value,\n"
   "\t\t\t\tslider.value, state->which_side, p3text, sizeof p3text);\n"
   "\t\tswitch (p3)\n\t\t{\n"
   "\t\t\tcase P3ND_CONNECT: do_connect (NULL, WIDGET_EVENT_SELECT); break;\n"
   "\t\t\tcase P3ND_LISTEN: do_listen (NULL, WIDGET_EVENT_SELECT); break;\n"
   "\t\t\tcase P3ND_CANCEL: do_cancel (NULL, WIDGET_EVENT_SELECT); break;\n"
   "\t\t\tcase P3ND_DELAY_DOWN:\n\t\t\t\tif (slider.value > slider.min)\n\t\t\t\t\tslider.value -= slider.step;\n"
   "\t\t\t\tDrawConnectDialog ();\n\t\t\t\tbreak;\n"
   "\t\t\tcase P3ND_DELAY_UP:\n\t\t\t\tif (slider.value < slider.max)\n\t\t\t\t\tslider.value += slider.step;\n"
   "\t\t\t\tDrawConnectDialog ();\n\t\t\t\tbreak;\n"
   "\t\t\tcase P3ND_HOST:\n\t\t\tcase P3ND_PORT:\n\t\t\t{\n"
   "\t\t\t\tWIDGET_TEXTENTRY *t = &texts[p3 == P3ND_HOST ? 0 : 1];\n"
   "\t\t\t\tstrncpy (t->value, p3text, (size_t) t->maxlen);\n\t\t\t\tt->value[t->maxlen] = 0;\n"
   "\t\t\t\tDrawConnectDialog ();\n\t\t\t\tbreak;\n\t\t\t}\n\t\t}\n"
   "\t\tif (done)\n\t\t\treturn FALSE;\n\t}\n#endif\n"
   "\tchanged = TRUE;\n\n\tif (PulsedInputState.menu[KEY_MENU_UP])\n",
   "port3ds_netdlg_frame (texts[0].value")

# ---- netplay sockets on libctru ------------------------------------------------------
rw("libs/network/socket/socket_bsd.c",
   "#include <netinet/ip.h>\n",
   "#ifdef __3DS__\n#\tinclude <netinet/in.h>\t/* libctru has no netinet/ip.h */\n#\tifndef IPTOS_LOWDELAY\n#\t\tdefine IPTOS_LOWDELAY 0x10\n#\tendif\n#\tifndef SO_KEEPALIVE\n#\t\tdefine SO_KEEPALIVE 0x0008\n#\tendif\n#else\n#include <netinet/ip.h>\n#endif\n",
   "libctru has no netinet/ip.h")
# TOS and keep-alive are optional: soc:u may refuse them; never fail a connection for it
rw("libs/network/socket/socket_bsd.c",
   "\tif (Socket_setTOS(sock, IPTOS_LOWDELAY) == -1) {\n\t\t// errno is set\n\t\treturn -1;\n\t}\n",
   "#ifdef __3DS__\n\t(void) Socket_setTOS(sock, IPTOS_LOWDELAY);\n#else\n"
   "\tif (Socket_setTOS(sock, IPTOS_LOWDELAY) == -1) {\n\t\t// errno is set\n\t\treturn -1;\n\t}\n#endif\n",
   "(void) Socket_setTOS(sock, IPTOS_LOWDELAY);")
rw("libs/network/socket/socket_bsd.c",
   "Socket_setKeepAlive(Socket *sock) {\n\tint flag = 1;\n\n",
   "Socket_setKeepAlive(Socket *sock) {\n\tint flag = 1;\n\n#ifdef __3DS__\t/* not offered by soc:u */\n\t(void) sock;\n\t(void) flag;\n\treturn 0;\n#endif\n",
   "#ifdef __3DS__\t/* not offered by soc:u */")

# HyperSpace: the flagship's facing for steering by touch (read on the main thread)
rw("uqm/ships/sis_ship/sis_ship.c",
   "\tGLOBAL (velocity) = ElementPtr->velocity;\n\n\tGetElementStarShip (ElementPtr, &StarShipPtr);\n",
   "\tGLOBAL (velocity) = ElementPtr->velocity;\n\n\tGetElementStarShip (ElementPtr, &StarShipPtr);\n"
   "#ifdef __3DS__\n\tport3ds_hs_facing = StarShipPtr->ShipFacing;\n#endif\n",
   "port3ds_hs_facing = StarShipPtr->ShipFacing;")

print(f"patch_uqm: {applied} edits applied, {skipped} already present")
