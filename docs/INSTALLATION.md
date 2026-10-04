# Installation and usage

## Visible log folder (development v0.4.2-preview)

Game-session files prefer `/data/homebrew/PPSA50011/logs`. Each filename includes
the game name, source key and session date. The file header includes title ID,
source path and emulator version. Every launch starts a separate file.

If the application mount refuses writes, logs remain in persistent sandbox
storage. Run `tools/Download PS5 logs.bat` on your computer while the title's
storage is mounted: it exports them to that same `logs` folder through FTP and
creates a shareable ZIP. Exported files remain browsable after closing the app.
Early startup logs are collected separately as sandbox-prefixed boot files.

## Requirements

- A PS5 with an already working homebrew environment. The development setup is firmware **13.60**, etaHEN, ShadowMountPlus and kstuff; other configurations are unverified.
- An FTP client on your computer and network access to your PS5. The development FTP service uses port **2121**; use your console's own address.
- Your own Xbox 360 game files. No games, BIOS or keys are provided.

## Install the release

1. Download the latest **PS5X360-v<VERSION>.zip** from the [GitHub Releases page](https://github.com/BrinooTk/PS5X360/releases). GitHub's automatic “Source code” archives are not the installable build.
2. Extract the ZIP. You should see `PPSA50011`, `INSTALLATION.md`, `RELEASE_NOTES.md`, `MANIFEST.json` and license notices.
3. Connect your FTP client to your PS5's IP address and configured FTP port.
4. Copy the **entire** `PPSA50011` folder to `/data/homebrew/`. The final executable path must be `/data/homebrew/PPSA50011/eboot.bin`.
5. Allow ShadowMountPlus to register the title. Open **PS5X360** from the PS5 home screen.

Keep `sce_module`, `sce_sys` and `assets` alongside `eboot.bin`. Copying only the executable is insufficient.

## Add games

Use a separate folder for every game inside:

```text
/data/homebrew/PPSA50011/assets/roms/
```

Examples:

```text
roms/Game One/default.xex
roms/Game One/...all extracted data files...
roms/Game Two/game.iso
roms/Game Three/...GOD or STFS header and matching data...
```

Supported library formats:

| Format | What to copy |
| --- | --- |
| Extracted game | `default.xex` and all companion folders/files |
| Disc image | The `.iso` file |
| GOD / STFS | The package header and its matching `.data` folder, when required |

Reopen the emulator, or press **Square → Refresh game list**.

### Multiple locations and external devices (development v0.3.0-preview)

Open **Settings → Game folders**. Press **Square** to browse for a location,
**Cross** to enter a folder, **Circle** to go up a level, and **Triangle** to
add the folder currently displayed. **Square** cancels the folder picker.
Select a configured location and press **Triangle** to remove it from the list.

The library merges all configured locations and avoids duplicate entries from
overlapping folders. Removing a location does not remove its game files.
Disconnected locations remain saved and show as unavailable; connect the device
and refresh the library. The application can only scan mounts it can access.
This feature does not mount or unlock a device by itself.

Default locations include `/app0/assets/roms`, `/data/xbox360`, and
`/mnt/usb0/xbox360`, `/mnt/usb1/xbox360`, `/mnt/ext0/xbox360`, `/mnt/ext1/xbox360`.
Only visible paths contribute games. There is no guarantee all of these mounts
are exposed by every console setup.

For manual configuration, `game_paths.txt` holds one absolute PS5 directory
per line under `/download0/xbox360ps5`. An optional `assets/game_paths.txt` can
seed a new installation. Saved configuration takes priority. Reopen the title
after editing the file outside the application.

## Controls

| DualSense | Library | Xbox 360 gameplay input |
| --- | --- | --- |
| D-pad / stick | Select game or option | D-pad / stick |
| Cross | Launch / confirm | A |
| Circle | Close panel / back | B |
| Square | Settings | X |
| Triangle | Details and patches | Y |
| L1 / R1 | Library filters | LB / RB |
| OPTIONS | — | Start |
| Touchpad click | — | Emulator guide by default |

### In-game guide

Click the **touchpad** to open the guide. Use **L1 / R1** to switch pages.

- **Game:** resume, return to the library, send Xbox BACK, exit.
- **Settings:** FPS display, sound, image filter and touchpad behavior.

The guide reveals from the center of the screen and retracts when closed. The game keeps running behind it, but controller buttons are intercepted while the guide is open.

If you select **Touchpad click → Back button**, the touchpad sends Xbox BACK directly. Open the guide with **OPTIONS + touchpad** instead.

## VSync (development v0.4.0-preview)

Open **Settings → VSync** and use Cross, Left or Right to switch it on/off.
The default is **On**; the choice is saved. Closing the settings panel restarts
the emulator to apply a changed value before graphics initialization. Reverting
the value before closing cancels that restart.

This is Xenia's **emulated** vertical synchronization (`vsync`), not a promise
to disable PS5 display synchronization. Canary calculates part of its frame
pacing at startup, so the setting is not changed while a game is running.
Disabling it may affect gameplay timing and audio; it does not guarantee 60 FPS.
The applied value is recorded in each game log.

## Language

**Settings → Language** controls the interface and the language reported to the next game launched.

- **Automatic (console):** reads the PS5 system language.
- Interface translations: **English, Portuguese and Spanish**.
- Unsupported interface languages, or a failed system-language query, fall back to **English**.
- Manual choices are saved. Existing game-language preferences remain until a new selection is made.
- Games only show languages that are actually included in that game's version. Choosing a language does not add a translation to the game.

## Profiles and saves

The emulator creates a **Player** profile on first use. Choose or create a gamertag in **Settings → Player profile**. Names allow up to 15 letters and numbers and must start with a letter.

Each profile has its own saved games and achievements. The selected profile applies to the next game launched. Treat achievements as local emulator data; no Xbox network synchronization is provided.

## Covers

Select **Settings → Download covers** to fetch missing artwork from XboxUnity. The PS5 needs internet access.

Local artwork takes priority:

- Extracted game: place `cover.png` or `cover.jpg` in the game's folder.
- ISO: place an image with the same base filename beside it, such as `game.jpg` beside `game.iso`.

## Community patches

The release includes Xenia Canary community patches in `assets/patches`. They are **disabled by default**.

Select a game, press **Triangle**, select a patch and press **Cross** to toggle it. A patch is only applied to a matching executable version. Not every game has patches.

The v0.4.5 development build connects these choices to the native Canary core.
Per-game logs report executable hashes and `Patches: applied` names.
A checkbox alone is not proof of application; mismatching hashes are rejected.

GTA IV **Unlock FPS** requires **Settings > VSync > Off**, then restart.
Unlocking a limit does not guarantee 60 FPS in demanding scenes.
Keep detailed logging off for ordinary gameplay.

60 FPS patches may alter timing or cause scene-specific problems. If a patched game misbehaves, disable the patches and retest.

## Updates

Close the emulator before replacing its files. Back up your game library and saves first. Copy the new release's application files over the existing installation. Keep your own `assets/roms` folders; the release contains no game data.

After updating, reopen the title so it loads the new executable. Existing settings are stored separately under `/download0/xbox360ps5`.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No games listed | Correct `assets/roms` path, one folder per game, complete game files, then refresh the list |
| Title missing from home screen | Complete folder copied to `/data/homebrew/PPSA50011`; check your working ShadowMountPlus setup |
| Old interface after an update | Fully close and reopen the emulator |
| Black screen, crash or graphical corruption | Disable patches; note the exact scene; check [compatibility notes](COMPATIBILITY.md) |
| Cover download fails | Internet/DNS access; use local artwork as an alternative |

### Separate game logs (development v0.3.0-preview)

Logs are created automatically in `/download0/xbox360ps5/LOGS/`.
Each launch creates a new file containing the selected game's name, a source
identifier, UTC date/time and a collision counter. Its header includes the
release version, title ID and executable path. Launch failures are recorded too.
Boot/library messages have their own `Game-Launcher-...log` file.

The first 8 MiB segment is preserved; verbose sessions additionally use three
rolling `.part1.log`, `.part2.log`, `.part3.log` files. Send all files with the
same session prefix when reporting a problem. Native crash reports append to
the original session file; errors before game selection remain in `boot.log`.
Old sessions are kept until you remove them. Detailed logs slow games down;
enable them only while diagnosing a problem.

To download through FTP **while PS5X360 is open**, browse to the title sandbox,
usually `/mnt/sandbox/PPSA50011_000/download0/xbox360ps5/LOGS/`.
The sandbox suffix may vary. The persistent `/user/download/PPSA50011/download0.dat`
is a mounted image, not a plain directory to browse. Log files remain stored
between runs, but the FTP sandbox mount may disappear when the title closes.

With the `tools` folder included in the development ZIP (or the source),
download the files and create a shareable ZIP:

```sh
python tools/console.py game-logs --host YOUR_PS5_IP
```

The ZIP is written under `build/PS5X360-logs-<date>.zip`. The downloader also
supports old builds that only have `engine.log` and `boot.log`.
On Windows with Python installed, double-click `tools/Download PS5 logs.bat`
and enter the PS5 IP address; the same ZIP is generated. Keep these tools on
your computer; they are not application files to copy to the console.

When opening an issue, include release version, firmware, game name and title ID, region/executable version, enabled patches, reproduction steps and a screenshot/video or log excerpt. Do not upload game files.
