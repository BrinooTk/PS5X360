"""Installs Xbox 360 DLC and title updates into PS5X360 on the PS5 over FTP.

The packages are read here (STFS/SVOD content header), then copied to the folders the Canary
core reads, under <app folder>/saves/0000000000000000/<TITLE ID>/:

  00000002/<file>      DLC (MarketplaceContent, content type 0x00000002)
  000B0000/<file>      the one active title update (content type 0x000B0000); the core mounts
                       only the first package there, so any other one is moved to 000B0000-off/

Each file is uploaded to a staging folder, read back and compared by SHA-256, then renamed into
place. Nothing is overwritten and nothing is deleted. A title update must be for the same edition
(media ID) as the game on the console: the media ID is read from default.xex, from an ISO over
FTP (partial reads) or from a game package. When it cannot be read the update is installed and
marked "edition not checked".

Used by content_installer_gui.py (the Windows app) and test_content_installer.py.
"""
import datetime
import ftplib
import hashlib
import os
import socket
import struct
import sys
from pathlib import Path

FOLDER = '/data/homebrew/PPSA50011'
FTP_PORT = 2121
APP_PORTS = range(8360, 8368)  # open while PS5X360 runs
DLC_TYPE, UPDATE_TYPE = 0x00000002, 0x000B0000
MAGICS = (b'LIVE', b'PIRS', b'CON ')
ISO_BASES = (0, 0x18300000, 0xFD90000, 0x2080000)  # game partition offsets, see src/covers.cpp
CONTENT_TYPES = {0x1: 'saved game', 0x2: 'DLC', 0x3: 'publisher content', 0x10000: 'profile',
                 0x20000: 'gamer picture', 0x30000: 'theme', 0x40000: 'cache file', 0x50000: 'storage download',
                 0x60000: 'avatar asset', 0x70000: 'game demo', 0x80000: 'video', 0xA0000: 'game trailer',
                 0xB0000: 'title update', 0xD0000: 'arcade game', 0x4000: 'installed game',
                 0x7000: 'game on demand', 0x80000000: 'license'}
be32 = lambda b, at: struct.unpack_from('>I', b, at)[0]


def resource(name):
    """A file shipped beside this script, or inside the PyInstaller bundle."""
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent)) / name


def load_titles():
    """Title ID -> game name, from titles.txt (made from assets/gameinfo.txt by build.py)."""
    names = {}
    for path in (resource('titles.txt'), Path(__file__).resolve().parents[2] / 'assets/gameinfo.txt'):
        if not path.is_file():
            continue
        title = None
        for line in path.read_text('utf-8', 'replace').splitlines():
            if line.startswith('[') and line.endswith(']'):
                title = line[1:-1].upper()
            elif '\t' in line and not line.startswith('#'):
                key, value = line.split('\t', 1)
                names[key.upper()] = value
            elif title and line.startswith('title='):
                names[title] = line[6:]
        if names:
            break
    return names


def version_text(version):
    """major.minor.build.revision, as the console shows a title's version."""
    return f'{version >> 28}.{(version >> 24) & 15}.{(version >> 8) & 0xFFFF}.{version & 0xFF}'


def utf16(raw):
    text = raw.decode('utf-16-be', 'replace')
    return text.split('\0', 1)[0].strip()


class Package:
    """One local file and what its Xbox 360 content header says."""

    def __init__(self, path):
        self.path = Path(path)
        self.file = self.path.name
        self.size = self.path.stat().st_size
        self.kind = self.title = self.reason = None
        self.media = self.version = self.type = 0
        self.display = self.title_name = ''
        try:
            with open(self.path, 'rb') as f:
                head = f.read(0x1800)
        except OSError as error:
            self.reason = f'cannot be read ({error.strerror})'
            return
        if len(head) < 0x971 or head[:4] not in MAGICS:
            self.reason = 'not an Xbox 360 package (no LIVE/PIRS/CON header)'
            return
        self.type = be32(head, 0x344)
        self.media, self.version = be32(head, 0x354), be32(head, 0x358)
        title = be32(head, 0x360)
        self.title = f'{title:08X}'
        self.display = utf16(head[0x411:0x511])
        if len(head) >= 0x1691 + 0x80:
            self.title_name = utf16(head[0x1691:0x1711])
        if self.type == DLC_TYPE:
            self.kind = 'dlc'
        elif self.type == UPDATE_TYPE:
            self.kind = 'update'
        else:
            name = CONTENT_TYPES.get(self.type, f'content type 0x{self.type:08X}')
            self.reason = f'{name}, not DLC or a title update'
        if not title and not self.reason:
            self.reason = 'the package has no title ID'
        if self.reason:
            self.kind = None

    def sha256(self):
        digest = hashlib.sha256()
        with open(self.path, 'rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                digest.update(block)
        return digest.hexdigest()

    @property
    def summary(self):
        if self.kind == 'update':
            return f'Title update {version_text(self.version)} (media {self.media:08X})'
        return self.display or self.file


def collect(paths):
    """Packages from dropped files and folders (folders are walked). Returns (packages, refused)."""
    found, seen = [], set()
    for path in map(Path, paths):
        files = sorted(p for p in path.rglob('*') if p.is_file()) if path.is_dir() else [path]
        for file in files:
            key = os.path.normcase(str(file.resolve()))
            if key not in seen:
                seen.add(key)
                found.append(Package(file))
    return found


def xex_info(header):
    """(title ID, media ID) from an XEX2 header (execution info, optional header 0x00040006),
    or None. Raises IndexError when the header is longer than the bytes given."""
    if header[:4] != b'XEX2':
        return None
    count = be32(header, 0x14)
    for n in range(min(count, 96)):
        if be32(header, 0x18 + n * 8) == 0x00040006:
            at = be32(header, 0x1C + n * 8)
            if at + 16 > len(header):
                raise IndexError(at + 16)
            return f'{be32(header, at + 12):08X}', be32(header, at)
    return None


class Console:
    """The PS5 FTP server (anonymous) and the PS5X360 folder on it."""

    def __init__(self, host, port=FTP_PORT, folder=FOLDER, app_ports=APP_PORTS, timeout=25):
        self.host, self.port, self.folder, self.app_ports, self.timeout = host, port, folder, app_ports, timeout
        self.ftp = None
        self.games = {}  # path -> (title, media or 0, how) for the games already read

    def connect(self):
        self.close()
        self.ftp = ftplib.FTP()
        self.ftp.connect(self.host, self.port, timeout=self.timeout)
        self.ftp.login()
        self.ftp.voidcmd('TYPE I')

    def close(self):
        if self.ftp:
            try:
                self.ftp.quit()
            except (ftplib.all_errors, AttributeError):
                self.ftp.close()
            self.ftp = None

    def listing(self, folder):
        """name -> (is folder, size) of a console folder; None when it does not exist."""
        rows = []
        try:
            self.ftp.retrlines(f'LIST {folder}', rows.append)
        except ftplib.error_perm:
            return None
        entries = {}
        for row in rows:
            fields = row.split(None, 8)
            if len(fields) == 9 and fields[-1] not in ('.', '..'):
                entries[fields[-1]] = (row.startswith('d'), int(fields[4]) if fields[4].isdigit() else -1)
        return entries

    def running(self):
        """Why PS5X360 counts as running (same check as the build install scripts), or None."""
        for port in self.app_ports:
            with socket.socket() as s:
                s.settimeout(.3)
                if s.connect_ex((self.host, port)) == 0:
                    return f'PS5X360 is open on the console (port {port} answers): close it completely first'
        sandbox = self.listing('/mnt/sandbox')
        if sandbox is None:
            return 'could not list /mnt/sandbox to check whether PS5X360 is running'
        # A closed app can leave its sandbox folder behind with only "data"; a running one has app0.
        for name, (is_folder, _) in sandbox.items():
            if name.startswith('PPSA50011') and 'app0' in (self.listing(f'/mnt/sandbox/{name}') or {}):
                return 'PS5X360 is still running on the console: close it completely first'
        return None

    def check(self):
        """Connects and returns the reason PS5X360 cannot receive content now, or None."""
        self.connect()
        if self.listing(self.folder) is None:
            return f'PS5X360 was not found on the console ({self.folder})'
        return self.running()

    def makedirs(self, path):
        parts = path.strip('/').split('/')
        for n in range(1, len(parts) + 1):
            parent, name = '/' + '/'.join(parts[:n - 1]), parts[n - 1]
            entries = self.listing(parent)
            if entries is None or name not in entries:
                self.ftp.mkd(f'{parent.rstrip("/")}/{name}')

    def read_part(self, path, offset, size):
        """size bytes of a console file from offset (FTP REST). The transfer is cut once enough
        has arrived; some servers answer the abort late, so the connection is renewed."""
        chunks, total = [], 0

        class Enough(Exception):
            pass

        def take(block):
            nonlocal total
            chunks.append(block)
            total += len(block)
            if total >= size:
                raise Enough()

        try:
            self.ftp.retrbinary(f'RETR {path}', take, blocksize=65536, rest=offset or None)
        except ftplib.error_perm:
            return b''  # past the end of the file
        except Enough:
            self.ftp.close()  # an abort is answered late by some servers: start a clean session
            self.ftp = None
            self.connect()
        return b''.join(chunks)[:size]

    def sha256(self, path, progress=None):
        digest = hashlib.sha256()

        def take(block):
            digest.update(block)
            if progress:
                progress(len(block))

        self.ftp.retrbinary(f'RETR {path}', take, blocksize=1 << 20)
        return digest.hexdigest()

    def upload(self, local, remote, progress=None):
        with open(local, 'rb') as f:
            self.ftp.storbinary(f'STOR {remote}', f, blocksize=1 << 20,
                                callback=(lambda block: progress(len(block))) if progress else None)

    def move(self, source, target):
        self.ftp.rename(source, target)

    # The games on the console, to compare a title update's media ID with the game's.

    def xex_at(self, path, offset=0):
        header = self.read_part(path, offset, 0x10000)
        try:
            return xex_info(header)
        except IndexError:
            return xex_info(self.read_part(path, offset, min(be32(header, 8), 1 << 22)))

    def iso_info(self, path):
        """(title, media) of default.xex inside an Xbox 360 disc image (XDVDFS), or None."""
        for base in ISO_BASES:
            volume = self.read_part(path, base + 32 * 2048, 2048)
            if volume[:20] != b'MICROSOFT*XBOX*MEDIA':
                continue
            root, root_size = struct.unpack_from('<II', volume, 20)
            table = self.read_part(path, base + root * 2048, min(root_size, 1 << 20))
            pending, visited = [0], set()
            while pending:
                at = pending.pop()
                if at in visited or at + 14 > len(table):
                    continue
                visited.add(at)
                left, right, sector = struct.unpack_from('<HHI', table, at)
                if left == 0xFFFF:
                    continue
                name = table[at + 14:at + 14 + table[at + 13]].decode('latin-1').lower()
                if name == 'default.xex':
                    return self.xex_at(path, base + sector * 2048)
                pending += [x * 4 for x in (left, right) if x]
            return None
        return None

    def package_info(self, path):
        head = self.read_part(path, 0, 0x364)
        if len(head) == 0x364 and head[:4] in MAGICS and be32(head, 0x344) not in (DLC_TYPE, UPDATE_TYPE):
            return f'{be32(head, 0x360):08X}', be32(head, 0x354)
        return None

    def find_games(self, title, status=None):
        """[(path, media)] of the games in assets/roms with this title ID (media 0 = unknown)."""
        if not self.games:
            self.scan(f'{self.folder}/assets/roms', 0, status)
        return [(path, media) for path, (found, media) in self.games.items() if found == title]

    def remember(self, path, read):
        try:
            info = read(path)
        except (ftplib.all_errors, struct.error, IndexError, ValueError):
            info = None
        if info:
            self.games[path] = info

    def scan(self, folder, depth, status):
        entries = self.listing(folder) or {}
        lower = {name.lower(): name for name in entries}
        if depth and 'default.xex' in lower:
            if status:
                status(f'Reading {folder}/{lower["default.xex"]}')
            self.remember(f'{folder}/{lower["default.xex"]}', self.xex_at)
            return
        packages = 0
        for name, (is_folder, size) in sorted(entries.items()):
            path = f'{folder}/{name}'
            if is_folder:
                if depth < 3:
                    self.scan(path, depth + 1, status)
            elif name.lower().endswith('.iso'):
                if status:
                    status(f'Reading {path}')
                self.remember(path, self.iso_info)
            elif depth and '.' not in name and size > 0x1000 and packages < 8:
                packages += 1  # an arcade game or a game on demand: an STFS package
                self.remember(path, self.package_info)


def content_folder(console, title):
    return f'{console.folder}/saves/0000000000000000/{title}'


class Result:
    def __init__(self, package, state, detail, path=''):
        self.package, self.state, self.detail, self.path = package, state, detail, path

    def line(self):
        p = self.package
        where = f' -> {self.path}' if self.path else ''
        return f'[{self.state}] {p.title or "--------"} {p.summary} ({p.file}){where}: {self.detail}'


def plan_updates(packages):
    """The update to activate for each title (the newest), and the ones to keep off."""
    active, off = {}, []
    for p in packages:
        if p.kind == 'update':
            best = active.get(p.title)
            if best is None or p.version > best.version:
                if best:
                    off.append(best)
                active[p.title] = p
            else:
                off.append(p)
    return active, off


def install(console, packages, log=print, progress=None, status=None):
    """Installs the accepted packages; returns one Result per package (refused ones included).
    progress(done bytes, total bytes); status(text). The console must be connected and checked."""
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    results = [Result(p, 'refused', p.reason) for p in packages if p.reason]
    accepted = [p for p in packages if not p.reason]
    total = sum(p.size * 2 for p in accepted) or 1
    done = 0

    def advance(count):
        nonlocal done
        done += count
        if progress:
            progress(done, total)

    def place(p, folder):
        """Uploads p into folder (staged, read back, renamed). Returns (state, detail, path)."""
        nonlocal done
        target = f'{folder}/{p.file}'
        local_sha = p.sha256()
        console.makedirs(folder)
        existing = console.listing(folder) or {}
        if p.file in existing:
            if existing[p.file][1] == p.size and console.sha256(target) == local_sha:
                advance(p.size * 2)
                return 'already installed', f'same file already there (SHA-256 {local_sha[:16]})', target
            advance(p.size * 2)
            return 'refused', 'a different file with this name is already there; it was left alone', target
        staging = f'{console.folder}/content-installer-staging'  # nothing the core reads
        console.makedirs(staging)
        temporary = f'{staging}/{p.file}.{stamp}.part'
        if status:
            status(f'Uploading {p.summary}')
        console.upload(p.path, temporary, advance)
        if status:
            status(f'Checking {p.summary}')
        remote_sha = console.sha256(temporary, advance)
        if remote_sha != local_sha:
            return 'failed', f'the uploaded copy differs (SHA-256 {remote_sha[:16]}, expected ' \
                             f'{local_sha[:16]}); it was kept at {temporary}', ''
        console.move(temporary, target)
        moved = (console.listing(folder) or {}).get(p.file)
        if not moved or moved[1] != p.size:
            return 'failed', f'the rename into place could not be confirmed (check {staging})', ''
        return 'installed', f'SHA-256 {local_sha[:16]} verified', target

    editions = {}
    for p in [p for p in accepted if p.kind == 'update']:
        refusal, editions[p] = edition(console, p, status)
        if refusal:
            results.append(Result(p, 'refused', refusal))
            accepted.remove(p)
            advance(p.size * 2)
            log(results[-1].line())
    active, keep_off = plan_updates(accepted)
    for p in accepted:
        base = content_folder(console, p.title)
        try:
            if p.kind == 'dlc':
                results.append(Result(p, *place(p, f'{base}/00000002')))
            elif p in keep_off:
                state, detail, path = place(p, f'{base}/000B0000-off')
                if state == 'installed':
                    detail += f'; kept off, {active[p.title].file} (newer) is the active update'
                results.append(Result(p, state, f'{detail}; {editions[p]}', path))
            else:
                state, detail, path = activate_update(console, p, base, place, stamp)
                results.append(Result(p, state, f'{detail}; {editions[p]}', path))
        except ftplib.all_errors as error:
            results.append(Result(p, 'failed', f'FTP error: {error}'))
            try:
                console.connect()
            except ftplib.all_errors:
                break
        log(results[-1].line())
    for p in accepted:
        if not any(r.package is p for r in results):
            results.append(Result(p, 'failed', 'not attempted (connection lost)'))
    return results


def edition(console, p, status):
    """(refusal or None, note): the update's media ID against the game's on the console."""
    if status:
        status(f'Looking for game {p.title} on the console')
    games = console.find_games(p.title, status)
    known = [(path, media) for path, media in games if media]
    if p.media and any(media == p.media for _, media in known):
        return None, 'edition checked'
    if p.media and known:
        found = ', '.join(f'{media:08X} ({path.rsplit("/", 1)[-1]})' for path, media in known)
        return f'another edition: the update is for media {p.media:08X}, the game on the console has {found}', ''
    return None, 'edition not checked (' + ('game not found in assets/roms' if not games else
                                           'media ID of the game could not be read') + ')'


def activate_update(console, p, base, place, stamp):
    """Places p in 000B0000 and moves any other package there to 000B0000-off."""
    folder = f'{base}/000B0000'
    console.makedirs(folder)
    others = [name for name, (is_folder, _) in (console.listing(folder) or {}).items()
              if not is_folder and name != p.file]
    state, detail, path = place(p, folder)
    if state in ('installed', 'already installed') and others:
        off = f'{base}/000B0000-off'
        console.makedirs(off)
        kept = console.listing(off) or {}
        moved = []
        for name in others:
            target = name if name not in kept else f'{name}.{stamp}'
            console.move(f'{folder}/{name}', f'{off}/{target}')
            moved.append(target)
        detail += f'; moved the other update(s) to 000B0000-off: {", ".join(moved)}'
    return state, detail, path


def report(results, host):
    counts = {}
    for r in results:
        counts[r.state] = counts.get(r.state, 0) + 1
    lines = [f'PS5X360 content installer report, {datetime.datetime.now():%Y-%m-%d %H:%M}, console {host}',
             ', '.join(f'{n} {state}' for state, n in sorted(counts.items()))]
    return '\n'.join(lines + [r.line() for r in results])
