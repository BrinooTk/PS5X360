# PS5X360 Content Installer

A Windows app that installs Xbox 360 DLC and title updates into PS5X360 on the PS5 over FTP.

## Use

1. Close PS5X360 on the console completely. The app refuses to install while it runs
   (ports 8360-8367 answer, or `/mnt/sandbox/PPSA50011*` has `app0`).
2. Open `PS5X360-Content-Installer.exe`, type your PS5's IP address (PS5 Settings > Network >
   Connection status; the field starts empty), keep FTP port 2121 (anonymous login, as the etaHEN
   FTP server) and press **Check console**.
3. Drop package files or whole folders on the list (or use **Add files** / **Add folder**).
   Each file's Xbox 360 content header is read:
   - DLC: `LIVE`/`PIRS`/`CON ` package of content type `0x00000002`;
   - title update: content type `0x000B0000`, shown with its version and media ID;
   - anything else is listed as refused, with the reason.
4. Press **Install**. The list shows each file's result, and a report is saved beside the .exe.

## Where things go

Under `/data/homebrew/PPSA50011/saves/0000000000000000/<TITLE ID>/`, where the Canary core reads them:

| Content | Folder |
| --- | --- |
| DLC | `00000002/<original file name>` |
| Active title update | `000B0000/<file>` (the core mounts only the first package there) |
| Other title updates | `000B0000-off/` (kept, not applied) |

- Each file is uploaded to `PPSA50011/content-installer-staging/`, read back, compared by SHA-256
  and renamed into place.
- A file already there with the same contents is reported as "already installed"; a different
  file with the same name is left alone and the new one is refused.
- Nothing is deleted. When a title update is installed, any other package in `000B0000` is moved to
  `000B0000-off`. With several updates for one game, the newest becomes active.
- A title update must be for the same edition as the game: its media ID (header `0x354`) is
  compared with the game's, read from `default.xex` (execution info `0x00040006`), from an ISO
  over FTP (XDVDFS at `0`, `0x18300000`, `0xFD90000` or `0x2080000`) or from a game package
  in `assets/roms`. Another edition is refused. When no game or no media ID is found, the update
  is installed and marked "edition not checked".

## Build and test

```
pip install pyinstaller tkinterdnd2 pyftpdlib
python tools/content-installer/build.py
```

`build.py` runs `test_content_installer.py` (fake packages, games and ISO on a local FTP server)
and then writes `build/content-installer/dist/PS5X360-Content-Installer.exe`, with the icon
from `icon.ico` (drawn by `make_icon.py`). Game names come from
`assets/gameinfo.txt`. `content_installer.py` holds the logic and can be used from Python.
