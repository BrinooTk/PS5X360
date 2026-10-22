"""Host test of content_installer.py: fake packages and games, a local FTP server laid out like
the console (pyftpdlib), then installs, re-installs, refusals and the running-app check.

    python tools/content-installer/test_content_installer.py
"""
import ftplib
import hashlib
import shutil
import socket
import struct
import sys
import tempfile
import threading
from pathlib import Path

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

sys.path.insert(0, str(Path(__file__).resolve().parent))
import content_installer as ci  # noqa: E402

GAME_XEX, GAME_ISO, NO_GAME = '4D530001', '4D530002', '4D530003'


def package(magic=b'LIVE', kind=ci.DLC_TYPE, title=GAME_XEX, media=0, version=0, name='Pack', size=0x2000, fill=0):
    raw = bytearray([fill]) * size
    raw[:4] = magic
    struct.pack_into('>I', raw, 0x344, kind)
    struct.pack_into('>III', raw, 0x354, media, version, 0)
    struct.pack_into('>I', raw, 0x360, int(title, 16))
    text = name.encode('utf-16-be') + b'\0\0'
    raw[0x411:0x411 + len(text)] = text
    return bytes(raw)


def xex(title, media):
    """An XEX2 header whose only optional header is the execution info (media ID, title ID)."""
    raw = bytearray(0x3000)
    raw[:4] = b'XEX2'
    struct.pack_into('>IIIII', raw, 4, 0, 0x3000, 0, 0x100, 1)
    struct.pack_into('>II', raw, 0x18, 0x00040006, 0x2000)
    struct.pack_into('>IIII', raw, 0x2000, media, 0, 0, int(title, 16))
    return bytes(raw)


def iso(path, base, title, media):
    """A disc image with XDVDFS at base: root table with two entries, default.xex on the right."""
    with open(path, 'wb') as f:
        f.seek(base + 32 * 2048)
        f.write(b'MICROSOFT*XBOX*MEDIA' + struct.pack('<II', 40, 2048))
        # Entries sit at 4-byte offsets; the first (21 bytes) points right to the second, at 24.
        table = bytearray(b'\xff' * 2048)
        table[:24] = (struct.pack('<HHIIBB', 0, 24 // 4, 50, 16, 0x20, 7) + b'AAA.BIN').ljust(24, b'\0')
        second = struct.pack('<HHIIBB', 0, 0, 41, 0x3000, 0x20, 11) + b'default.xex'
        table[24:24 + len(second)] = second
        f.seek(base + 40 * 2048)
        f.write(table)
        f.seek(base + 41 * 2048)
        f.write(xex(title, media))
        f.seek(base + 60 * 2048)
        f.write(b'\0')


def serve(root):
    authorizer = DummyAuthorizer()
    authorizer.add_anonymous(str(root), perm='elradfmwMT')
    handler = type('Handler', (FTPHandler,), {'authorizer': authorizer, 'banner': 'test'})
    server = FTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=server.serve_forever, kwargs={'timeout': .2}, daemon=True).start()
    return server, server.socket.getsockname()[1]


def files(root):
    return {str(p.relative_to(root)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    work = Path(tempfile.mkdtemp(prefix='content-installer-test-'))
    try:
        run(work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print('content installer host test: PASS')


def run(work):
    remote, local = work / 'console', work / 'local'
    app = remote / 'data/homebrew/PPSA50011'
    (remote / 'mnt/sandbox/PPSA50011_000/data').mkdir(parents=True)  # left behind by a closed app
    (app / 'assets/roms/Game A').mkdir(parents=True)
    (app / 'assets/roms/Game A/default.xex').write_bytes(xex(GAME_XEX, 0x11111111))
    iso(app / 'assets/roms/Game B.iso', 0x2080000, GAME_ISO, 0x22222222)
    saves = app / 'saves/0000000000000000'
    (saves / GAME_XEX / '000B0000').mkdir(parents=True)
    (saves / GAME_XEX / '000B0000/OLD_TU').write_bytes(package(kind=ci.UPDATE_TYPE, media=0x11111111, version=1))
    (saves / GAME_XEX / '00000002/PS5X360-boot-fix').mkdir(parents=True)
    (saves / GAME_XEX / '00000002/PS5X360-boot-fix/keep.txt').write_bytes(b'keep')
    (saves / GAME_XEX / '00000002/CLASH').write_bytes(b'someone else\'s file')
    before = files(remote)

    folder = local / 'dlc folder'
    (folder / 'sub').mkdir(parents=True)
    (folder / 'DLC1').write_bytes(package(name='Boxer One'))
    (folder / 'sub/DLC2').write_bytes(package(magic=b'PIRS', name='Boxer Two', size=0x30000, fill=7))
    (folder / 'CLASH').write_bytes(package(name='Clash'))
    (folder / 'SAVE').write_bytes(package(kind=0x1))
    (folder / 'notes.txt').write_bytes(b'hello')
    (local / 'TU_A').write_bytes(package(kind=ci.UPDATE_TYPE, media=0x11111111, version=3, name='Update A'))
    (local / 'TU_B_wrong').write_bytes(package(kind=ci.UPDATE_TYPE, title=GAME_ISO, media=0x99999999, version=2))
    (local / 'TU_B').write_bytes(package(kind=ci.UPDATE_TYPE, title=GAME_ISO, media=0x22222222, version=1))
    (local / 'TU_C1').write_bytes(package(kind=ci.UPDATE_TYPE, title=NO_GAME, media=0x33333333, version=1))
    (local / 'TU_C2').write_bytes(package(kind=ci.UPDATE_TYPE, title=NO_GAME, media=0x33333333, version=5))

    packages = ci.collect([folder, local / 'TU_A', local / 'TU_B_wrong', local / 'TU_B', local / 'TU_C1',
                           local / 'TU_C2', folder / 'DLC1'])
    by_file = {p.file: p for p in packages}
    assert len(packages) == 10, [p.file for p in packages]  # DLC1 given twice is listed once
    assert by_file['DLC1'].kind == 'dlc' and by_file['DLC1'].display == 'Boxer One'
    assert by_file['DLC2'].kind == 'dlc' and by_file['DLC2'].size == 0x30000
    assert 'saved game' in by_file['SAVE'].reason and 'not an Xbox 360 package' in by_file['notes.txt'].reason
    assert by_file['TU_A'].summary == 'Title update 0.0.0.3 (media 11111111)'

    server, port = serve(remote)
    try:
        closed_port = socket.socket()
        closed_port.bind(('127.0.0.1', 0))
        app_port = closed_port.getsockname()[1]
        closed_port.close()
        console = ci.Console('127.0.0.1', port, app_ports=[app_port], timeout=10)
        assert console.check() is None, console.check()

        # The ISO is read over FTP with partial reads, at the 0x2080000 partition offset.
        assert console.iso_info('/data/homebrew/PPSA50011/assets/roms/Game B.iso') == (GAME_ISO, 0x22222222)

        log = []
        results = {r.package.file: r for r in ci.install(console, packages, log=log.append)}
        states = {name: r.state for name, r in results.items()}
        assert states == {'DLC1': 'installed', 'DLC2': 'installed', 'CLASH': 'refused', 'SAVE': 'refused',
                          'notes.txt': 'refused', 'TU_A': 'installed', 'TU_B_wrong': 'refused',
                          'TU_B': 'installed', 'TU_C1': 'installed', 'TU_C2': 'installed'}, states
        assert 'another edition' in results['TU_B_wrong'].detail and '22222222' in results['TU_B_wrong'].detail
        assert 'edition checked' in results['TU_A'].detail and 'OLD_TU' in results['TU_A'].detail
        assert 'edition checked' in results['TU_B'].detail
        assert 'edition not checked' in results['TU_C2'].detail and 'kept off' in results['TU_C1'].detail
        assert 'different file' in results['CLASH'].detail

        after = files(remote)
        a, b, c = (f'data/homebrew/PPSA50011/saves/0000000000000000/{t}' for t in (GAME_XEX, GAME_ISO, NO_GAME))
        digest = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
        assert after[f'{a}/00000002/DLC1'] == digest(folder / 'DLC1')
        assert after[f'{a}/00000002/DLC2'] == digest(folder / 'sub/DLC2')
        assert after[f'{a}/00000002/CLASH'] == before[f'{a}/00000002/CLASH']
        assert after[f'{a}/000B0000-off/OLD_TU'] == before[f'{a}/000B0000/OLD_TU']
        active = lambda base: sorted(n.rsplit('/', 1)[-1] for n in after if n.startswith(f'{base}/000B0000/'))
        assert active(a) == ['TU_A'] and active(b) == ['TU_B'] and active(c) == ['TU_C2'], (active(a), active(b))
        assert f'{c}/000B0000-off/TU_C1' in after and f'{b}/000B0000/TU_B_wrong' not in after
        assert not any('staging' in n for n in after), [n for n in after if 'staging' in n]
        # Nothing was deleted: every earlier file is still there, at its place or moved off.
        moved = {f'{a}/000B0000/OLD_TU': f'{a}/000B0000-off/OLD_TU'}
        for name, sha in before.items():
            assert after.get(moved.get(name, name)) == sha, name

        # Again: the same files are recognised, nothing is uploaded twice.
        again = {r.package.file: r.state for r in ci.install(console, packages, log=log.append)}
        assert again['DLC1'] == again['DLC2'] == again['TU_A'] == 'already installed', again
        assert files(remote) == after
        print(ci.report(list(results.values()), '127.0.0.1'))

        # PS5X360 running: its sandbox folder has app0, or one of its ports answers.
        (remote / 'mnt/sandbox/PPSA50011_000/app0').mkdir()
        assert 'still running' in console.check()
        (remote / 'mnt/sandbox/PPSA50011_000/app0').rmdir()
        listener = socket.socket()
        listener.bind(('127.0.0.1', app_port))
        listener.listen()
        assert 'is open on the console' in console.check()
        listener.close()
        assert ci.Console('127.0.0.1', port, folder='/data/homebrew/PPSA99999', app_ports=[]).check() \
            .startswith('PS5X360 was not found')
        console.close()
    finally:
        server.close_all()


if __name__ == '__main__':
    main()
