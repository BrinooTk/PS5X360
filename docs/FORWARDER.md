# Starting a game from a home screen forwarder

A forwarder is a separate small app with its own home screen tile (its own title ID, icon and
name). When it is opened, it launches PS5X360 (`PPSA50011`) with launch arguments, and PS5X360
starts that game directly instead of opening its launcher.

## Arguments

| Argument | Meaning |
| --- | --- |
| `--rom <file>` or `--rom=<file>` | The game to start. An absolute path (`/mnt/usb0/xbox360/Halo 3.iso`), or a path inside the library's game folders (Settings; by default `/app0/assets/roms`, `/data/xbox360` and `xbox360` on the USB and extended storage drives), looked for in their order: `Halo 3.iso`, `RPG/Fable 2`. An extracted game can be given as its folder: it stands for its `default.xex` (or its only `.xex`), as the library lists it. An `.iso` or a GOD/STFS package is given as its file. A relative path may not contain `..`. |
| `--exit-after-game` | When that game goes back to the launcher (the guide's back-to-the-launcher entry), close PS5X360 so the console returns to the home screen. Without it, the launcher opens, as usual. |

Unknown arguments are ignored. Parsing is in `include/xbox360ps5/forward.hpp`, checked on a PC by
`tools/check-forward.cpp`; `src/canary_game_main.cpp` uses it once the settings are read, so a
relative path resolves against the game folders chosen in Settings. A first argument that is a path
(not an option) starts that file, as the title always has.

## Behaviour

- The game starts as one chosen in the library would: its own options apply (a game whose start
  options differ restarts the title once, straight into it, still as the forwarder's game), and it
  becomes the library's most recent game. `game.txt` and `assets/game.txt` are not read.
- If the game is not found, the launcher opens with *Could not launch the game:* and why.
- If it does not start, PS5X360 starts again into the launcher, which says why, even with
  `--exit-after-game` (a direct start that fails without a forwarder still closes the title).
- *Close the emulator* in the guide closes PS5X360 either way. A game that switches to another
  executable (a collection's menu, a second disc) restarts the title from Canary's launch data, and
  `--exit-after-game` no longer applies to that executable.
- Every restart PS5X360 makes of itself runs without arguments, so the forwarded game never starts
  twice.
- Arguments only reach a new PS5X360 process. If it is already running, the system brings it to the
  front and `main` does not run again; a forwarder should close it first.

## Example

```
--rom "Halo 3.iso"
--rom "/mnt/usb0/xbox360/Fable 2" --exit-after-game
```
