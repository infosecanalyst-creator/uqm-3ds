# Changelog

## 2026-11-21

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
