# Installation and usage

## Requirements

- A PS5 with an already working homebrew environment. The development setup is firmware **13.60**, etaHEN, ShadowMountPlus and kstuff; other configurations are unverified.
- An FTP client on your computer and network access to your PS5. The development FTP service uses port **2121**; use your console's own address.
- Your own Xbox 360 game files. No games, BIOS or keys are provided.

## Install the release

1. Download **PS5X360-release.zip** from the [GitHub Releases page](https://github.com/BrinooTk/PS5X360/releases). GitHub's automatic “Source code” archives are not the installable build.
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

Reopen the emulator, or press **Square → Refresh game list**. The title scans its own `assets/roms` directory; other PS5 locations and external USB paths are not scanned.

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

The main log is `/download0/xbox360ps5/engine.log`. Detailed logs slow games down; enable them only while diagnosing a problem.

When opening an issue, include release version, firmware, game name and title ID, region/executable version, enabled patches, reproduction steps and a screenshot/video or log excerpt. Do not upload game files.
