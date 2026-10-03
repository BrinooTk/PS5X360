"""Development loop with the console: replace the M8 executable, read its logs.

update: upload dist/PPSA50011/eboot.bin beside the installed one, verify it
        through FTP, keep the previous executable as eboot.bin.previous, swap.
logs:   save the kernel log (ShadowMount API) and print the title's lines.
covers: download box art for the games on the console and send it to
        assets/covers (when the console's DNS keeps the title offline).
"""
import argparse
import ftplib
import importlib.util
import json
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
FOLDER = "/data/homebrew/PPSA50011"


def deploy_module():
    spec = importlib.util.spec_from_file_location("deploy", ROOT / "tools/deploy-native-probe.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def remote_exists(ftp, path):
    try:
        ftp.voidcmd("TYPE I")
        size = ftp.size(path)
        # This server answers a missing file with size -1 (as unsigned).
        return size is not None and size < (1 << 60)
    except ftplib.all_errors:
        return False


def update(host, canary=False):
    # --canary: the executable built on the Xenia Canary core
    # (tools/build-canary-game.sh). The first time, the executable it replaces
    # (the original core's) is kept as eboot.bin.original for `restore`.
    source = ROOT / ("build/canary-game/eboot.bin" if canary else "dist/PPSA50011/eboot.bin")
    fself = source.read_bytes()
    elf = (ROOT / ("build/canary-game/eboot.elf" if canary else "build/native-game/eboot.elf")).read_bytes()
    ftp = ftplib.FTP()
    ftp.connect(host, 2121, timeout=25)
    ftp.login()
    try:
        with source.open("rb") as stream:
            ftp.storbinary(f"STOR {FOLDER}/eboot.bin.new", stream, blocksize=1024 * 1024)
        actual = bytearray()
        ftp.retrbinary(f"RETR {FOLDER}/eboot.bin.new", actual.extend, blocksize=1024 * 1024)
        print(deploy_module().verify_eboot(bytes(actual), fself, elf))
        try:
            ftp.delete(f"{FOLDER}/eboot.bin.previous")
        except (ftplib.error_perm, ftplib.error_reply):
            pass  # Absent, or this server's "226 File deleted" success reply.
        if canary and not remote_exists(ftp, f"{FOLDER}/eboot.bin.original"):
            ftp.rename(f"{FOLDER}/eboot.bin", f"{FOLDER}/eboot.bin.original")
            print("The original core's executable is kept as eboot.bin.original")
        else:
            ftp.rename(f"{FOLDER}/eboot.bin", f"{FOLDER}/eboot.bin.previous")
        ftp.rename(f"{FOLDER}/eboot.bin.new", f"{FOLDER}/eboot.bin")
        print("Installed executable replaced; previous kept as eboot.bin.previous")
        # Small support files of the title (fonts); never the games under assets/roms.
        assets = ROOT / "dist/PPSA50011/assets"
        uploaded = 0
        for source in sorted(assets.rglob("*")):
            relative = source.relative_to(assets).as_posix()
            if not source.is_file() or relative.split("/")[0] == "roms":
                continue
            folder = f"{FOLDER}/assets"
            for part in relative.split("/")[:-1]:
                folder += "/" + part
                try:
                    ftp.mkd(folder)
                except (ftplib.error_perm, ftplib.error_reply):
                    pass
            with source.open("rb") as stream:
                ftp.storbinary(f"STOR {FOLDER}/assets/{relative}", stream)
            uploaded += 1
        print(f"Uploaded {uploaded} support files (fonts, patches)")
        # The icon, the start-up picture and the title's name.
        for name in ("icon0.png", "pic0.png", "pic1.png", "param.json"):
            source = ROOT / "dist/PPSA50011/sce_sys" / name
            if source.is_file():
                with source.open("rb") as stream:
                    ftp.storbinary(f"STOR {FOLDER}/sce_sys/{name}", stream)
    finally:
        ftp.quit()


def restore(host):
    """Puts the original core's executable back (after update --canary)."""
    ftp = ftplib.FTP()
    ftp.connect(host, 2121, timeout=25)
    ftp.login()
    try:
        if not remote_exists(ftp, f"{FOLDER}/eboot.bin.original"):
            print("No eboot.bin.original on the console")
            return
        try:
            ftp.delete(f"{FOLDER}/eboot.bin.canary")
        except (ftplib.error_perm, ftplib.error_reply):
            pass
        ftp.rename(f"{FOLDER}/eboot.bin", f"{FOLDER}/eboot.bin.canary")
        ftp.rename(f"{FOLDER}/eboot.bin.original", f"{FOLDER}/eboot.bin")
        print("Original core restored; the Canary executable is kept as eboot.bin.canary")
    finally:
        ftp.quit()


def logs(host, everything):
    request = Request(f"http://{host}:10101/api/v1/kernel-log", data=b"{}",
                      headers={"Content-Type": "application/json"})
    with urlopen(request, timeout=20) as response:
        text = json.load(response).get("content", "")
    output = ROOT / "build/console-kernel.log"
    output.write_text(text, encoding="utf-8")
    lines = text.splitlines()
    wanted = ("[X360]", "eboot.bin", "PPSA50011", "# ")
    for line in lines:
        if everything or any(mark in line for mark in wanted):
            print(line[:300])
    print(f"-- {len(lines)} kernel log lines saved to {output}")


def api(host, route):
    request = Request(f"http://{host}:10101/api/v1/{route}", data=b"{}",
                      headers={"Content-Type": "application/json"})
    with urlopen(request, timeout=20) as response:
        return json.load(response).get("content", "")


def watch(host, seconds):
    """Wait for the next launch of the title, then collect its kernel-log lines
    until it stops or `seconds` pass. The kernel log is a small ring, so lines
    are accumulated across polls."""
    import time
    started = api(host, "debug-log").count("started: PPSA50011")
    while api(host, "debug-log").count("started: PPSA50011") == started:
        time.sleep(2)
    seen, ordered, text = set(), [], ""
    deadline = time.time() + seconds
    stopped_at = None
    while time.time() < deadline:
        text = api(host, "kernel-log")
        # Only this launch: the ring still holds the previous one.
        begin = max(text.rfind("[X360] BOOT thread-exit key"), text.rfind("[X360] BOOT main entered") - 200)
        for line in text[max(begin, 0):].splitlines():
            if ("[X360]" in line or line.startswith("# ") or "App Crash" in line) and line not in seen:
                seen.add(line)
                ordered.append(line)
        stops = api(host, "debug-log").count("game stopped: PPSA50011")
        if stopped_at is None and api(host, "debug-log").rfind("game stopped: PPSA50011") > \
                api(host, "debug-log").rfind("started: PPSA50011"):
            stopped_at = time.time()
        if stopped_at and time.time() - stopped_at > 4:
            break
        time.sleep(1.5)
    (ROOT / "build/console-kernel.log").write_text(text, encoding="utf-8")
    (ROOT / "build/console-title.log").write_text("\n".join(ordered) + "\n", encoding="utf-8")
    for line in ordered:
        if "[X360]" in line or line.startswith(("# reason", "# signal", "# rip", "# 0000")) or "App Crash" in line:
            print(line[:300])
    print("-- title", "stopped" if stopped_at else "still running", f"; {len(ordered)} lines in build/console-title.log")


def netlog(host, seconds, once_more=False):
    """Connect to the title's log stream as soon as it is up and save it until
    the title closes the connection (it exits or crashes) or `seconds` pass."""
    import socket
    import time
    import shutil
    from datetime import datetime, timezone
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = ROOT / f"build/console-net-{stamp}.log"
    latest = ROOT / "build/console-net.log"
    deadline = time.time() + seconds
    total = 0
    sessions = 0
    with output.open("wb") as file:
        # The title restarts between games of an unattended run: reconnect each time.
        while time.time() < deadline:
            try:
                stream = socket.create_connection((host, 9100), timeout=2)
            except OSError:
                time.sleep(0.3)
                continue
            sessions += 1
            stream.settimeout(5)
            while time.time() < deadline:
                try:
                    data = stream.recv(1 << 16)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not data:
                    break
                file.write(data)
                file.flush()
                total += len(data)
            stream.close()
            if not once_more:
                break
    shots = extract_shots(output)
    # A refused connection must not destroy the last useful diagnostic log.
    # Keep each capture, and preserve the previous latest before replacing it.
    if total:
        if latest.exists() and latest.stat().st_size:
            shutil.copy2(latest, ROOT / f"build/console-net-before-{stamp}.log")
        shutil.copy2(output, latest)
    print(f"-- {total} bytes of title log in {sessions} sessions saved to {output}; {shots} screenshots")


def extract_shots(log):
    """Screenshots sent as "[X360] SHOT <game> <tag> <base64 png>" lines."""
    import base64
    folder = ROOT / "build/console-shots"
    folder.mkdir(parents=True, exist_ok=True)
    count = 0
    for line in log.read_bytes().splitlines():
        if not line.startswith(b"[X360] SHOT "):
            continue
        parts = line.split(b" ", 4)
        if len(parts) < 5:
            continue
        try:
            png = base64.b64decode(parts[4])
        except ValueError:
            continue
        name = f"game{int(parts[2]) + 1:02d}-{parts[3].decode()}.png"
        (folder / name).write_bytes(png)
        count += 1
    return count


def ftp_read(ftp, path, offset, size):
    """size bytes of a file on the console from offset (FTP REST)."""
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
        ftp.retrbinary(f"RETR {path}", take, blocksize=65536, rest=offset or None)
    except Enough:
        try:
            ftp.abort()
        except ftplib.all_errors:
            pass
        # Some servers send the transfer-aborted replies late; reconnect clean.
        ftp.close()
        ftp.connect(ftp.host, ftp.port, timeout=25)
        ftp.login()
    return b"".join(chunks)[:size]


def xex_title_id(header):
    if header[:4] != b"XEX2":
        return None
    count = int.from_bytes(header[0x14:0x18], "big")
    for n in range(min(count, 64)):
        key = int.from_bytes(header[0x18 + n * 8:0x1C + n * 8], "big")
        if key == 0x00040006:
            offset = int.from_bytes(header[0x1C + n * 8:0x20 + n * 8], "big")
            title = int.from_bytes(header[offset + 12:offset + 16], "big")
            return f"{title:08X}" if title else None
    return None


def iso_title_id(ftp, path):
    """The title id of default.xex inside an Xbox 360 disc image (XDVDFS)."""
    for base in (0, 0x18300000, 0xFD90000, 0x2080000):
        volume = ftp_read(ftp, path, base + 32 * 2048, 2048)
        if volume[:20] != b"MICROSOFT*XBOX*MEDIA":
            continue
        root, root_size = int.from_bytes(volume[20:24], "little"), int.from_bytes(volume[24:28], "little")
        table = ftp_read(ftp, path, base + root * 2048, min(root_size, 1 << 20))
        pending = [0]
        while pending:
            at = pending.pop()
            if at + 14 > len(table):
                continue
            left, right = int.from_bytes(table[at:at + 2], "little"), int.from_bytes(table[at + 2:at + 4], "little")
            if left == 0xFFFF:
                continue
            name = table[at + 14:at + 14 + table[at + 13]].decode("latin-1").lower()
            if name == "default.xex":
                start = base + int.from_bytes(table[at + 4:at + 8], "little") * 2048
                return xex_title_id(ftp_read(ftp, path, start, 65536))
            pending += [x * 4 for x in (left, right) if x]
        return None
    return None


def covers(host):
    """Box art for every game on the console, downloaded on this computer and
    sent to the title's assets/covers: for consoles whose DNS (a jailbreak
    helper) keeps the title from reaching the cover service itself."""
    import urllib.request
    ftp = ftplib.FTP()
    ftp.connect(host, 2121, timeout=25)
    ftp.login()
    roms = f"{FOLDER}/assets/roms"
    ids = {}

    def walk(folder, depth):
        lines = []
        ftp.retrlines(f"LIST {folder}", lines.append)
        # Unix-style lines: permissions, links, owner, group, size, date (3 fields), name.
        listing = [(line.split(None, 8)[-1], "dir" if line.startswith("d") else "file")
                   for line in lines if len(line.split(None, 8)) == 9]
        for name, kind in listing:
            if name in (".", ".."):
                continue
            path = f"{folder}/{name}"
            lower = name.lower()
            if lower.endswith(".xex") and (lower == "default.xex" or depth > 0):
                title = xex_title_id(ftp_read(ftp, path, 0, 65536))
                if title:
                    ids.setdefault(title, path)
                    if lower == "default.xex":
                        return
            elif lower.endswith(".iso"):
                title = iso_title_id(ftp, path)
                if title:
                    ids.setdefault(title, path)
            elif kind == "dir" and depth < 3:
                walk(path, depth + 1)

    walk(roms, 0)
    try:
        ftp.mkd(f"{FOLDER}/assets/covers")
    except ftplib.all_errors:
        pass
    for title, where in sorted(ids.items()):
        base = "http://xboxunity.net/Resources/Lib"
        info = urllib.request.urlopen(f"{base}/CoverInfo.php?titleid={title}", timeout=20).read().decode("utf-8", "replace")
        best = None
        for entry in json.loads(info or "{}").get("Covers", []):
            score = (entry.get("Official") == "1") * 1000 + int(entry.get("Rating") or 0)
            if best is None or score > best[0]:
                best = (score, entry["CoverID"])
        if not best:
            print(f"{title}: no cover on XboxUnity ({where})")
            continue
        image = urllib.request.urlopen(f"{base}/Cover.php?size=large&cid={best[1]}", timeout=30).read()
        from io import BytesIO
        ftp.storbinary(f"STOR {FOLDER}/assets/covers/{title}.jpg", BytesIO(image))
        print(f"{title}: cover sent ({len(image)} bytes) for {where}")
    ftp.quit()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("update", "restore", "logs", "watch", "netlog", "covers"))
    parser.add_argument("--seconds", type=int, default=90)
    parser.add_argument("--host", required=True)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--canary", action="store_true", help="update: the Xenia Canary core build")
    parser.add_argument("--follow", action="store_true", help="netlog: keep reconnecting until --seconds pass")
    args = parser.parse_args()
    if args.command == "update":
        update(args.host, args.canary)
    elif args.command == "restore":
        restore(args.host)
    elif args.command == "watch":
        watch(args.host, args.seconds)
    elif args.command == "covers":
        covers(args.host)
    elif args.command == "netlog":
        netlog(args.host, args.seconds, args.follow)
    else:
        logs(args.host, args.all)


if __name__ == "__main__":
    main()
