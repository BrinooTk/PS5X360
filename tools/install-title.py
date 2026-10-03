"""Installs the title folder on a console that does not have it yet.

    python tools/install-title.py --host <console>            the emulator only
    python tools/install-title.py --host <console> --games    then the games in
                                                               dist/PPSA50011/assets/roms

The emulator's files are uploaded to a staging folder, checked by reading them
back, and published with one rename; ShadowMount is then asked to scan. Games
are large: they are sent after the title is usable, each file checked by size,
and files already complete on the console are skipped (the command can be run
again after an interruption).
"""
import argparse
import ftplib
import hashlib
import json
import uuid
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "dist/PPSA50011"
DESTINATION = "/data/homebrew/PPSA50011"


def connect(host):
    ftp = ftplib.FTP()
    ftp.connect(host, 2121, timeout=30)
    ftp.login()
    return ftp


def exists(ftp, path):
    try:
        ftp.retrlines(f"LIST {path}", lambda line: None)
        return True
    except ftplib.all_errors:
        return False


def mkdir(ftp, path):
    try:
        ftp.mkd(path)
    except ftplib.all_errors:
        pass


def remote_size(ftp, path):
    try:
        ftp.voidcmd("TYPE I")
        return ftp.size(path)
    except ftplib.all_errors:
        return None


def install(host):
    ftp = connect(host)
    if remote_size(ftp, DESTINATION + "/eboot.bin"):
        print("The title is already installed; use tools/console.py update")
        ftp.quit()
        return
    staging = "/data/homebrew/PPSA50011-upload-" + uuid.uuid4().hex[:8]
    mkdir(ftp, staging)
    count = 0
    for source in sorted(APP.rglob("*")):
        relative = source.relative_to(APP).as_posix()
        if relative.startswith("assets/roms/") and relative != "assets/roms/README.txt":
            continue
        target = staging + "/" + relative
        if source.is_dir():
            mkdir(ftp, target)
            continue
        with source.open("rb") as stream:
            ftp.storbinary("STOR " + target, stream, blocksize=1 << 20)
        # The server strips unmapped segments from executables as it stores
        # them (tools/console.py verifies those in detail); the rest must match.
        if relative not in ("eboot.bin", "sce_module/libc.prx"):
            digest = hashlib.sha256()
            ftp.retrbinary("RETR " + target, digest.update, blocksize=1 << 20)
            if digest.digest() != hashlib.sha256(source.read_bytes()).digest():
                raise RuntimeError(f"Remote bytes differ: {relative}")
        count += 1
    mkdir(ftp, staging + "/assets/roms")
    ftp.rename(staging, DESTINATION)
    ftp.quit()
    print(f"Installed {count} files in {DESTINATION}")
    request = Request(f"http://{host}:10101/api/v1/scan", data=b"{}",
                      headers={"Content-Type": "application/json"}, method="POST")
    with urlopen(request, timeout=30) as response:
        print("ShadowMount scan:", json.load(response))


def games(host):
    roms = APP / "assets/roms"
    ftp = connect(host)
    mkdir(ftp, DESTINATION + "/assets/roms")
    files = [path for path in sorted(roms.rglob("*")) if path.is_file()]
    total = sum(path.stat().st_size for path in files)
    sent = 0
    made = set()
    for source in files:
        relative = source.relative_to(roms).as_posix()
        target = f"{DESTINATION}/assets/roms/{relative}"
        folder = ""
        for part in relative.split("/")[:-1]:
            folder += "/" + part
            if folder not in made:
                mkdir(ftp, f"{DESTINATION}/assets/roms{folder}")
                made.add(folder)
        size = source.stat().st_size
        if remote_size(ftp, target) != size:
            for attempt in range(3):
                try:
                    with source.open("rb") as stream:
                        ftp.storbinary("STOR " + target, stream, blocksize=1 << 20)
                    break
                except ftplib.all_errors as error:
                    print(f"retry {relative}: {error}", flush=True)
                    try:
                        ftp.close()
                    except ftplib.all_errors:
                        pass
                    ftp = connect(host)
            if remote_size(ftp, target) != size:
                raise RuntimeError(f"Size differs after upload: {relative}")
        sent += size
        print(f"{sent * 100 // max(total, 1):3d}% {relative}", flush=True)
    ftp.quit()
    print(f"Games complete: {len(files)} files, {total / (1 << 30):.1f} GB")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--games", action="store_true")
    args = parser.parse_args()
    install(args.host)
    if args.games:
        games(args.host)


if __name__ == "__main__":
    main()
