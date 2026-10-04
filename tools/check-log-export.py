"""Regression checks for visible logs and sandbox export, without a console."""
import contextlib
import ftplib
import importlib.util
import io
from pathlib import Path
import tempfile
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location("console_logs", Path(__file__).with_name("console.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FTP:
    def __init__(self, files, mounted=True, writable=True):
        self.files, self.mounted, self.writable = files, mounted, writable
    def connect(self, *args, **kwargs): pass
    def login(self): pass
    def quit(self): pass
    def mkd(self, path): pass
    def retrlines(self, command, output):
        folder = command.removeprefix("LIST ")
        if folder == "/mnt/sandbox":
            if not self.mounted: raise ftplib.error_perm("550 Not mounted")
            output("drwxrwxrwx 2 0 0 0 Oct 03 12:00 PPSA50011_000")
            return
        names = [p[len(folder)+1:] for p in self.files if p.startswith(folder + "/")]
        names = [name for name in names if "/" not in name]
        if not names: raise ftplib.error_perm("550 Missing")
        for name in names: output("-rw-r--r-- 1 0 0 10 Oct 03 12:00 " + name)
    def retrbinary(self, command, output, **kwargs):
        output(self.files[command.removeprefix("RETR ")])
    def storbinary(self, command, stream, **kwargs):
        if not self.writable: raise ftplib.error_perm("550 Read only")
        self.files[command.removeprefix("STOR ")] = stream.read()


def run(fake):
    with tempfile.TemporaryDirectory(prefix="ps5x360-log-export-") as directory:
        with patch.object(module, "ROOT", Path(directory)), patch.object(module.ftplib, "FTP", return_value=fake):
            with contextlib.redirect_stdout(io.StringIO()): module.game_logs("fixture")
        archives = list(Path(directory).glob("build/*.zip"))
        assert len(archives) == 1
        with zipfile.ZipFile(archives[0]) as archive:
            assert archive.testzip() is None
            return {name: archive.read(name) for name in archive.namelist() if name.endswith(".log")}


visible = "/data/homebrew/PPSA50011/logs"
storage = "/mnt/sandbox/PPSA50011_000/download0/xbox360ps5"
fake = FTP({storage + "/LOGS/Game-Halo 3-session.log": b"Halo session", storage + "/boot.log": b"Boot"})
saved = run(fake)
assert fake.files[visible + "/Game-Halo 3-session.log"] == b"Halo session"
assert fake.files[visible + "/PPSA50011_000-boot.log"] == b"Boot"
assert len(saved) == 2
fake.mounted = False
fake.files = {path: data for path, data in fake.files.items() if path.startswith(visible)}
assert len(run(fake)) == 2
fake = FTP({storage + "/engine.log": b"Legacy game", storage + "/boot.log": b"Legacy boot"}, writable=False)
saved = run(fake)
assert len(saved) == 2 and b"Legacy game" in saved.values()
print("PASS: per-game console export, separate boot, closed-title retrieval, legacy logs and export failure preserve downloads")
