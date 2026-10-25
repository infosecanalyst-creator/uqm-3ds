# The Ur-Quan Masters (Star Control II) for the New 3DS

UQM 0.8.0 with a purpose-built 3DS platform layer. No emulation, no scaling:
the game's native 320x240 frame is shown pixel-for-pixel.

## Build and install (PC, devkitPro MSYS2)

```
bash /c/devkitPro/3ds-projects/uqm-3ds/build-uqm.sh
bash /c/devkitPro/3ds-projects/install-3ds.sh <3DS IP> --uqm-data   # first time: ~150 MB of game data
bash /c/devkitPro/3ds-projects/install-3ds.sh <3DS IP> uqm          # the game (1.6 MB), every update
```

Open ftpd on the 3DS first (it shows the IP). Or skip installing a build and
run it straight from the PC: hold Y in the Homebrew Launcher, then
`3dslink -a <3DS IP> SD-CARD/3ds/uqm/uqm.3dsx`.

On the SD card: `sdmc:/3ds/uqm/uqm.3dsx`, data in `sdmc:/3ds/uqm/content/packages/`,
saves in `sdmc:/3ds/uqm/save/`, errors in `sdmc:/3ds/uqm/uqm.log`.
Sound needs `sdmc:/3ds/dspfirm.cdc` (same as every other homebrew with audio).

## Screens and controls

Game on the top screen (1:1, pillar-boxed); the bottom screen is a control deck
that always shows what each button does right now (menus, combat, HyperSpace,
solar system, conversations, starbase, name entry), plus touch buttons:
PAUSE, KEYBOARD (system keyboard whenever the game asks for a name), HELP
(5 pages: controls, getting started, starmap, talking/fighting, this port),
SWAP, SCREEN OFF (bottom backlight; touch to wake), STATS (fps + frame cost).

| Button | Does |
|---|---|
| A | select / fire weapon |
| B | back, ship menu / special |
| Y or Up | thrust |
| X | special action in menus |
| D-pad, Circle Pad, C-stick | move / steer |
| L / R | page up/down, starmap zoom |
| START / SELECT | pause / retreat (when allowed), cancel name |
| ZL / ZR | swap screens / help |

The layout choice and STATS persist in `sdmc:/3ds/uqm/port3ds.cfg`. UQM's own
Setup menu (music, speech, subtitles, melee zoom) works as on PC.

## What was done for speed

- New 3DS 804 MHz + L2 cache on from the first instruction.
- No SDL video driver: UQM composes into a surface whose pixel layout already
  matches the RGBA8 framebuffer, so presenting is one rotate-copy, and
  presents are capped at the LCD's 60 Hz (UQM otherwise spins hundreds of
  redraws a second during fades).
- Music/speech (Vorbis via integer Tremor) decode on the New 3DS's spare CPU
  core 2 (system core on an old 3DS), off the game's core.
- Mixer at 22 kHz for the "medium" audio setting (the DSP resamples anyway).
- The base content pack is repacked uncompressed: no inflating ~10,000 zip
  entries on load screens.
- Logging to the SD card limited to warnings/errors (upstream writes a line
  for every resource load).
- 256 KB stacks for all game threads; memory peak measured at ~21 MB on a
  desktop build of the same code, far under the 3DS app limit.

## Layout of this folder

| Path | What |
|---|---|
| `port3ds/` | the platform layer: screens, deck, input, keyboard, threads, config defaults |
| `tools/patch_uqm.py` | 24 anchored, idempotent `#ifdef __3DS__` edits to UQM |
| `tools/repack_stored.py` | content pack repacker |
| `Makefile.3ds` | cross build |
| `harness/` | desktop test build: same code, fake libctru, scripted buttons, screenshots |

## Not in this version

Stereo 3D (the slider does nothing), netplay, and touch control inside the
game itself (UQM has no pointer input). Ideas for 3D: ships and shots in
front of a deeper starfield in combat; planets popping out in orbit view.

## License

The port code (`port3ds/`, `tools/`, `harness/`, build files) is GPL-2.0-or-later,
the same licence as The Ur-Quan Masters code it patches (see `COPYING`). The UQM
source and game content are not stored here: the build downloads them from the
UQM project on SourceForge. UQM content is CC BY-NC-SA 2.5 (UQM team / Toys for
Bob). The fonts in `port3ds/font_*.h` are generated from the public-domain X11
misc-fixed fonts; the icon comes from UQM's own resources.
