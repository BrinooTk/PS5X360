"""Assemble the current emulator release and the console-only AutoLog release."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
version = (root / 'VERSION').read_text(encoding='utf-8').strip()
autolog_version = '1.0.7-preview'
name = f'PS5X360-v{version}-AutoLog-v{autolog_version}'
destination = Path.home() / 'Desktop' / name
if destination.exists():
    raise SystemExit(f'Existing folder preserved: {destination}')
emulator_archive = root / 'dist' / f'PS5X360-v{version}.zip'
collector_archive = root / 'dist' / f'PS5X360-AutoLog-ELF-v{autolog_version}.zip'
with zipfile.ZipFile(emulator_archive) as archive:
    assert archive.testzip() is None
    assert not any('/roms/' in entry and not entry.endswith('README.txt') for entry in archive.namelist())
    for entry in archive.namelist():
        assert not entry.startswith('/') and '..' not in Path(entry).parts
    archive.extractall(destination / 'Emulator')
    expected = json.loads(archive.read('RELEASE.json'))['executable_sha256']
assert hashlib.sha256((destination / 'Emulator/PPSA50011/eboot.bin').read_bytes()).hexdigest() == expected
collector = destination / 'AutoLog'
collector.mkdir()
with zipfile.ZipFile(collector_archive) as archive:
    assert archive.testzip() is None
    data = archive.read('PS5X360-AutoLog-ELF/PS5X360-AutoLog.elf')
    manifest = json.loads(archive.read('PS5X360-AutoLog-ELF/manifest.json'))
    assert hashlib.sha256(data).hexdigest() == manifest['sha256']['PS5X360-AutoLog.elf']
    (collector / f'PS5X360-AutoLog-v{autolog_version}.elf').write_bytes(data)
    (collector / 'README.md').write_bytes(archive.read('PS5X360-AutoLog-ELF/native/autolog/README.md'))
shutil.copy2(collector_archive, collector / collector_archive.name)
(destination / 'START-HERE.md').write_text(f'''# PS5X360 testing bundle

Emulator: {version}. Console AutoLog: {autolog_version}.
This bundle includes the emulator build identified above and the separately
versioned AutoLog collector. See Emulator/RELEASE_NOTES.md for core changes.

## Install the emulator

Copy the entire `Emulator/PPSA50011` folder to `/data/homebrew/` on your PS5.
Keep your existing games, saves and settings when updating. Do not delete the
existing title folder. Open PS5X360 through your already configured homebrew
environment. Full instructions and licenses are in `Emulator`.

Place each legally obtained game in its own folder under
`/data/homebrew/PPSA50011/assets/roms/`, or add other locations in Settings.
Games, saves, BIOS and keys are not included.

## Enable optional AutoLog

Loading this ELF enables sharing game diagnostic excerpts with Brino's private
Discord channel through the preconfigured HTTPS relay. Read `AutoLog/README.md`
before enabling it. Redaction is best effort; no games or saves are uploaded.

Copy `AutoLog/PS5X360-AutoLog-v{autolog_version}.elf` to
`/data/etaHEN/payloads/` and load it through your existing compatible loader
once after each console boot. Internet access is required. Keep the console's
existing FTP service enabled (2121 or 1337) for the directory fallback.
No PC collector, account or webhook configuration is required.

Only sessions begun after the first activation cutoff are collected. The cutoff
is retained across boots. Reports require a recorded exit/restart/crash or a
subsequent emulator session; log silence and low FPS do not trigger uploads.
After the event, final logs must remain unchanged for 30 seconds. A forced
closure with no event is detected on the next emulator opening. A console-wide
freeze delays delivery until the collector and emulator are started again.
Reports identify the reason and include final diagnostic excerpts. Send a short
description or video of the scene/action if the exact gameplay point matters.

Status: `/data/homebrew/PPSA50011/autolog/status.txt`.
Opt out by creating `/data/homebrew/PPSA50011/no-log-upload`.
Original logs remain in `/data/homebrew/PPSA50011/logs`.

The AutoLog source, SDK adapter, dependencies and license notices are included
in the AutoLog ZIP. Direct PS5 delivery was tested on firmware 13.60. Lifecycle
filtering passed host regression checks, and session-event delivery was observed
on the owner's console. Other firmware and crash scenarios remain unverified.

Support the projects: https://ko-fi.com/brinoblitz
''', encoding='utf-8')
files = {p.relative_to(destination).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
         for p in destination.rglob('*') if p.is_file()}
(destination / 'SHA256.json').write_text(json.dumps(files, indent=2), encoding='utf-8')
archive_path = destination.parent / (destination.name + '.zip')
assert not archive_path.exists(), 'Existing bundle ZIP preserved'
with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED) as archive:
    for p in destination.rglob('*'):
        if p.is_file(): archive.write(p, name + '/' + p.relative_to(destination).as_posix())
with zipfile.ZipFile(archive_path) as archive: assert archive.testzip() is None
print(destination)
print(archive_path)
print('Verified emulator/collector hashes, ZIP CRCs and absence of games.')
