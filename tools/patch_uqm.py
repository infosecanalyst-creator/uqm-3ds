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

print(f"patch_uqm: {applied} edits applied, {skipped} already present")
