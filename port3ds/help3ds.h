/* In-game help pages for the control deck (ZR or the HELP button).
 * Lines are at most 52 characters: the small font is 6 px wide and the
 * deck is 320 px. Keep each page to 12 lines. */
#pragma once

typedef struct { const char *title; const char *lines[12]; } HelpPage3DS;

static const HelpPage3DS help_pages[] = {
{ "CONTROLS", {
  "A        Select / fire weapon",
  "B        Back, ship menu / special weapon",
  "Y        Thrust (Up on the pad works too)",
  "X        Special action in menus",
  "Pad      Circle Pad, D-pad and C-stick all steer",
  "L / R    Page up / down, zoom the starmap",
  "START    Pause",
  "SELECT   Retreat from a fight (when allowed)",
  "ZL       Swap the two screens",
  "ZR       This help (press again for next page)",
  "",
  "The bottom screen always shows what A/B/X/Y do now.",
} },
{ "GETTING STARTED", {
  "You command a starship rebuilt from ancient",
  "Precursor technology. Earth is enslaved; find",
  "allies, resources and a way to fight back.",
  "",
  "First stop: the starbase orbiting Earth. Then",
  "explore Sol. Fly into a planet to orbit it, scan",
  "it, and send the lander down for minerals. They",
  "become credits (RU) at the starbase, which buy",
  "fuel, crew, ships and modules.",
  "",
  "Watch your fuel. Running dry in HyperSpace is the",
  "most common way a new captain gets stranded.",
} },
{ "FLYING AND THE STARMAP", {
  "While flying, the bottom screen is a galaxy map.",
  "Tap a star: its name, distance and the fuel it",
  "takes (red if you can't make it). Tap it again or",
  "SET COURSE and the autopilot flies you there.",
  "Drag to pan, + / - to zoom, SHIP to re-centre.",
  "The green ring is how far your fuel reaches.",
  "",
  "Press B while flying for the ship menu: Starmap,",
  "Devices, Cargo, Roster, Save / Load and more.",
  "Fly into a star in HyperSpace to enter its system;",
  "leave a system by flying off the edge of the map.",
  "",
} },
{ "TALKING AND FIGHTING", {
  "Conversations: tap a reply on the bottom screen,",
  "or Up / Down and A.",
  "Left replays the last line, Right fast-forwards,",
  "B skips the speech or shows a summary of the talk.",
  "Ship combat: A fires the main weapon, B uses the",
  "ship's special ability, Y or Up thrusts, the pad",
  "turns. Energy recharges on its own; crew is your",
  "hit points. Each ship plays very differently - try",
  "them all in Super Melee from the main menu.",
  "SELECT tries to retreat (not every fight lets",
  "you leave).",
} },
{ "THIS 3DS PORT", {
  "Saves: sdmc:/3ds/uqm/save    Settings: uqm.cfg",
  "Game data: sdmc:/3ds/uqm/content/packages",
  "",
  "OPEN KEYBOARD appears when the game asks for a",
  "name; the pad and A also work there.",
  "Toolbar: II pause, ? help, SWAP screens, DIM the",
  "bottom backlight (touch to wake), FPS frame rate.",
  "",
  "Setup in the main menu changes music, speech,",
  "subtitles and melee zoom; the 3DO music and voice",
  "packs are used when they are on the SD card.",
  "",
} },
};

#define HELP_PAGE_COUNT ((int)(sizeof help_pages / sizeof help_pages[0]))
