# Changelog

## 2026-11-27

- New game: the story can be skipped in one go. SKIP INTRO is on the touch screen, B does the same, and the top screen says so ("A next slide, B skip the intro").
- Main menu: CONTINUE (tap it or press X) loads the exit save, slot 50, when there is one. The buttons take taps as soon as the menu appears. The top screen shows a title card.
- START menu in the house shape: RESUME, MANUAL, SAVE GAME, LOAD GAME, LEFT HAND, EXIT GAME. SAVE GAME and LOAD GAME open the game's own save list while flying and are grey elsewhere.
- Exit question in the house wording: "Exit Ur-Quan Masters?", then "It is saved first (slot 50, Exit save)." or "Not saved here: unsaved progress is lost."
- Hold SELECT (quit to the main menu) asks with YES / NO like every other question and says that it does not save.
- Flight buttons (MENU, MAP, II, ?, SWAP) are 30 px tall and see-through, so the top of the game picture shows behind them; the one under the stylus turns solid.
- Flight dashboard: the touch hints are white and three lines; in the Sol system it says "Earth: the 3rd planet" until a course is set.
- Manual: "Left hand: off" and "Close" buttons in the house position; Stereo 3D is now part of The two screens.
- A settings file that cannot be written says "Settings not saved" on the top screen. The log says "closed from EXIT GAME" when that is how the game was left.

## 2026-11-21

- New game: the flight buttons (MENU, MAP, II, ?, SWAP) no longer sit on top of the story slides' first line of text.
- Top screen: the last help line in flight ("ZL: game on top, 3D") and the Super Melee hint no longer overlap the footer line.
- The game's settings (uqm.cfg, port3ds.cfg) are written through a temporary file and only when something changed: fewer SD card writes, and a failed write cannot damage them.
- Quieter touch click.

## 2026-11-20

- Slot 50 (the exit save) keeps its previous copy if the new save fails, and a save whose file cannot be closed counts as failed.
- HOME > Close makes the exit save too; the game pauses while the HOME menu or sleep is up.
- The game waits for memory to settle at start and keeps 4 MB free.
- Install path: the program is /3ds/uqm.3dsx. The START menu shows the build version.
- Fixed a crash on exit (the click's sound reference is released first).
- The manual source and the built-in manual are kept in sync (CI checks it).

## 2026-11-19

- EXIT GAME never closes while the slot 50 save is being written, and asks "Exit anyway?" when it failed.
- Fixed a crash when choosing EXIT GAME at the title screen.
- The course panel says SHORT, not only red, when the fuel will not reach.
- Manual: drag the stylus to scroll the text, with a little momentum.
