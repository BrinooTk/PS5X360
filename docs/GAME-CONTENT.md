# DLC, title updates, multi-disc games and more

How to add the extra content Xbox 360 games use, as of **v1.0.2**. Every path below is on the
PS5 (copy files over FTP, for example with FileZilla to port 2121). `PPSA50011` means
`/data/homebrew/PPSA50011`. Close PS5X360 (**Options → Fechar**, or the guide's "Close the
emulator") before copying files into it.

## DLC (downloadable content)

DLC packages are files whose first bytes read `CON `, `LIVE` or `PIRS`, usually named with a long
hexadecimal string and found in an Xbox folder such as
`Content/0000000000000000/<TITLE ID>/00000002/`.

**Easy way: the inbox.** Copy the DLC files (any folder layout) to one of these, then press
**Play** on the game. PS5X360 installs what is new before the game starts and shows the progress
on the loading screen:

- `PPSA50011/dlc/`
- a `dlc` folder beside the game, for example `PPSA50011/assets/roms/Minecraft/dlc/`
- a `dlc` folder at the root of a library location you added in **Settings → Game folders**

**Check or switch them:** on the game's sheet (Cross on the game) open the **DLCs** tab. Each
package shows as **On**, **Off** or **Not installed**; Cross switches it. A file that was ignored
says why (not a DLC package, or made for another game).

**From a Windows computer:** `PS5X360-Content-Installer.exe` from the
[release page](https://github.com/BrinooTk/PS5X360/releases) installs DLC and title updates over
FTP. Close PS5X360, open the app, type your PS5's IP address (PS5 Settings > Network > Connection
status), press **Check console**, drag the files or folders onto the window and press
**Install**. It refuses anything that is not DLC or a title update (with the reason), checks a
title update's edition against your game, verifies each upload and never deletes or overwrites
files. See [its guide](../tools/content-installer/README.md).

**Direct way:** copy the files to the folder the games read,
`PPSA50011/saves/0000000000000000/<TITLE ID>/00000002/`. The title ID is shown on the game's
sheet and in the library (for example `584111F7` for Minecraft).

## Title updates (TU)

A title update is one package with content type `000B0000`, often named like
`tu00000001_00000000`.

- Copy it to `PPSA50011/updates/` (or an `updates` folder beside the game, or at a library root;
  the `dlc` folders are searched too) and press **Play**: the newest update that fits the game is
  installed by itself.
- The **Updates** tab on the game's sheet installs, turns on and turns off updates. Only **one**
  update is active at a time (`saves/0000000000000000/<TITLE ID>/000B0000/`); the others wait in
  `000B0000-off`.
- An update made for another edition of the disc (a different media ID) is flagged and not
  installed by itself.
- Patches made for the game *without* an update stop applying while an update is active.

## Games with two or more discs

Keep each complete disc in its own folder, inside one folder for the game:

```text
PPSA50011/assets/roms/Max Payne 3/
├── Disc 1/default.xex
└── Disc 2/default.xex
```

Start **Disc 1**. When the game asks for the next disc, PS5X360 finds the matching one and
switches. Disc images work the same way (`Disc 1.iso`, `Disc 2.iso`). A `discs.m3u` file beside
them can list the discs explicitly, one relative path per line. Install-only discs need their
game's full disc. Full campaign disc changes still need more testing on the PS5.

## Profiles, saves and achievements

- **Home → the player slot** lists the profiles: create one, choose who plays, and give each
  controller its own player (up to four local players).
- When a game asks who is playing, where to save, or shows a message, the console's own window
  appears on screen and the controller answers it, as on an Xbox 360.
- Saves, profiles and achievements live in `PPSA50011/saves/`. Back that folder up before
  reinstalling.

## Online play and System Link (experimental)

Home → **Online** channel:

- **Online: DESLIGADO / LIGADO** — play over the internet through the public Xenia netplay
  server (Xenia Canary netplay by AdrianCassar). Cross switches it and the emulator restarts. A
  profile that signs in while online becomes an Xbox Live profile by itself, keeping its saves.
- **System Link** — consoles on the same local network.

The choice is kept in `PPSA50011/network.conf` (`mode=offline`, `mode=systemlink` or
`mode=online`; `server=` selects another Xenia server). To play with people far away, use the
game's **Xbox LIVE** menu: System Link only finds games on the same home network.

Since v1.0.2, Halo 3 plays online and Call of Duty: Modern Warfare 2 searches and finds Xbox LIVE
matches through the Xenia server; joining a whole match is not confirmed yet. The PS5 does not let
programs use ports below 1024, so a game's port 1000 is opened as 11000. If the game's log shows
`UPnP error code 501`, your router refused to open the ports by itself: forward them by hand,
UDP, to your PS5's address (MW2: 1000 to 11000, 1001 to 11001, 1013 to 11013; the log's
`XSocket::Bind` lines show each game's ports). If a game still says you must sign in to Xbox
LIVE, attach the game's log to your report.

## Kinect (experimental)

- **Phone as Kinect:** Settings → Kinect shows a QR code; the phone opens a page served by the PS5
  itself (the first time it installs a certificate made by your console).
- **Kinect on USB:** a Kinect sensor connected to the PS5 can be used; see
  [KINECT-INTEGRATION.md](KINECT-INTEGRATION.md).
- Only Dragon Ball Z for Kinect has been played this way so far.

## Your own pictures and dashboard

- **Home pictures:** any file of `PPSA50011/assets/skin/` (icons, background, slots) can be
  replaced by a file of the same name in `PPSA50011/skin/`.
- **Game art on the home:** the "Jogar *game*" slot uses the game's Marketplace background and
  icon, downloaded once by title ID into `PPSA50011/game-art/`
  (`<TITLE ID>-background.jpg`, `<TITLE ID>-tile.png`). You can put your own there.
- **Your own Xbox 360 dashboard:** copy your console's `dash.xex` (and its files) to
  `PPSA50011/dashboard/`; then **Configurações → Painel original** opens it. It is your file;
  PS5X360 ships nothing of it.
- `ui-classic.on` in `PPSA50011` brings back the previous library screen.

## Frame generation (experimental)

Settings → Performance → **Frame generation**: Off, Automatic, 2x, 3x, 4x
([ps5-framegen](https://github.com/Swordpdf/ps5-framegen), FSR3 optical flow). It adds pictures
between the game's frames; it does not make the game itself run faster, and fast motion can
smear when most pictures are generated.

## Per-game settings and patches

Cross on a game opens its sheet: **Play**, **Patches**, **Settings**, **Achievements**, **DLCs**,
**Updates**. Triangle in the library goes straight to the game's settings. What a game does not
change follows the general settings; many games pick the settings they need by themselves.
