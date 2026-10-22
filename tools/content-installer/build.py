"""Builds PS5X360-Content-Installer.exe (one file, no Python needed on the user's PC) with
PyInstaller, after running the host test. Needs: pip install pyinstaller tkinterdnd2 pyftpdlib

    python tools/content-installer/build.py

Output: build/content-installer/dist/PS5X360-Content-Installer.exe. The game names come from
assets/gameinfo.txt (title ID and name only, as titles.txt inside the .exe); the icon is
icon.ico (make_icon.py).
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / 'build/content-installer'
NAME = 'PS5X360-Content-Installer'

subprocess.run([sys.executable, str(HERE / 'test_content_installer.py')], check=True)
OUT.mkdir(parents=True, exist_ok=True)
titles, title = [], None
for line in (ROOT / 'assets/gameinfo.txt').read_text('utf-8').splitlines():
    if line.startswith('[') and line.endswith(']'):
        title = line[1:-1]
    elif title and title != '00000000' and line.startswith('title='):
        titles.append(f'{title}\t{line[6:]}')
(OUT / 'titles.txt').write_text('# Title ID and name, from assets/gameinfo.txt\n' + '\n'.join(titles) + '\n', 'utf-8')
subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--windowed',
                '--name', NAME, '--collect-all', 'tkinterdnd2',
                '--add-data', f'{OUT / "titles.txt"};.', '--add-data', f'{HERE / "icon.ico"};.',
                '--icon', str(HERE / 'icon.ico'),
                '--distpath', str(OUT / 'dist'), '--workpath', str(OUT / 'work'), '--specpath', str(OUT),
                '--paths', str(HERE), str(HERE / 'content_installer_gui.py')], check=True)
print('built', OUT / 'dist' / f'{NAME}.exe', f'({len(titles)} game names)')
