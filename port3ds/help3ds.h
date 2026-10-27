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
  "The top screen shows what the buttons do right now.",
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
  "The game is on the touch screen. Tap space and the",
  "ship flies there; hold the stylus down to steer.",
  "MAP: tap a star for its distance and fuel; tap it",
  "again (or SET COURSE) and the autopilot flies you.",
  "The top screen shows the galaxy map, fuel range,",
  "your ship's numbers and the course you set.",
  "MENU (or B) opens the ship menu as big buttons.",
  "Fly into a star in HyperSpace to enter its system;",
  "leave a system by flying off the edge of the map.",
  "ZL puts the game on the top screen in 3D, with",
  "the touch controls below.",
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
{ "TOUCH, 3D AND NETPLAY", {
  "Every game menu (main menu, ship, orbit, scan,",
  "starbase, game, settings) appears as big buttons",
  "over the game: tap one to choose it.",
  "Super Melee: tap a ship slot or button in the game;",
  "in battle, tap the ship to send in next.",
  "3D slider: stars and planets get real depth in",
  "HyperSpace, solar systems, orbit and combat.",
  "Netplay: tap NET for the side your friend plays;",
  "one 3DS taps WAIT, the other types its IP and",
  "taps CONNECT. Both set their own side to HUMAN.",
  "Saves: sdmc:/3ds/uqm/save   Log: .../uqm/uqm.log",
} },
};

#define HELP_PAGE_COUNT ((int)(sizeof help_pages / sizeof help_pages[0]))
