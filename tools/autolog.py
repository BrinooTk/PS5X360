"""Opt-in PC collector for PS5X360 logs. Standard library only; HTTPS relay.

Reads the existing FTP log folder; never modifies console files. Stability is a
snapshot heuristic, not proof that a game exited. Queue survives PC restarts.
"""
import argparse
from collections import deque
from contextlib import contextmanager
import ftplib
import getpass
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import sqlite3
import time
import urllib.request
from urllib.parse import urlsplit

MAX_REPORT = 2 * 1024 * 1024
MAGIC = "PS5X360 diagnostic report v1\n"
HOME = Path(os.environ.get("LOCALAPPDATA", Path.home())) / "PS5X360-AutoLog"
LOG_FOLDER = "/data/homebrew/PPSA50011/logs"
FILE = re.compile(r"^Game-[^/\\\r\n]+-\d{8}-\d{6}-UTC-\d+(?:\.part[123])?\.log$")


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def redact(text):
    # Remove lines carrying sensitive fields and source/library paths entirely.
    text = re.sub(r"(?im)^.*(?:source:|authorization:|webhook|password|token|secret|game_path|roms/|roms\\).*$",
                  "[private field removed]", text)
    text = re.sub(r"(?i)https?://\S+", "[URL]", text)
    text = re.sub(r"(?i)\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b", "[MAC]", text)
    def address(match):
        try:
            ipaddress.ip_address(match.group(0))
            return "[IP]"
        except ValueError:
            return match.group(0)
    text = re.sub(r"\b(?:\d{1,3}\.){3}\d{1,3}\b", address, text)
    text = re.sub(r"(?<![\w:])(?:[0-9A-Fa-f]{0,4}:){2,}[0-9A-Fa-f:.]*(?![\w:])", address, text)
    text = re.sub(r"(?i)([a-z]:\\Users\\)[^\\\r\n]+", r"\1[USER]", text)
    text = re.sub(r"/home/[^/\s]+", "/home/[USER]", text)
    return text


def build_report(name, sections):
    # Include only log segments of this session. Boot logs aren't reliably linked
    # to session IDs in existing builds, so do not attach an unrelated boot.log.
    first = sections[0][1].decode("utf-8", "replace")
    game = re.search(r"^Game: (.*)$", first, re.M)
    build = re.search(r"^PS5X360 (.*)$", first, re.M)
    result = MAGIC + "Game: " + (game[1] if game else "Unknown") + "\n"
    result += "Build: " + (build[1] if build else "Unknown") + "\n"
    result += "Capture: stable log snapshot; session exit not confirmed\n"
    result += "Collector: PC AutoLog 1.0; IPs, URLs, credentials and source paths redacted\n"
    for segment, content in sections:
        result += "\n--- " + segment + " ---\n" + content.decode("utf-8", "replace")
    # Leave space for a truncation marker; never truncate before redaction.
    data = redact(result).encode("utf-8")
    if len(data) > MAX_REPORT:
        data = data[:128*1024] + b"\n[report middle omitted]\n" + data[-(MAX_REPORT-129*1024):]
    return data


class Queue:
    def __init__(self, path):
        self.db = sqlite3.connect(path)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("CREATE TABLE IF NOT EXISTS reports (id TEXT PRIMARY KEY, body BLOB, attempts INTEGER DEFAULT 0, retry REAL DEFAULT 0)")
        self.db.execute("CREATE TABLE IF NOT EXISTS files (name TEXT PRIMARY KEY, signature TEXT)")
        self.db.commit()

    def seen(self, name, signature):
        return self.db.execute("SELECT 1 FROM files WHERE name=? AND signature=?", (name, signature)).fetchone() is not None

    def add(self, name, signature, body):
        if self.db.execute("SELECT COUNT(*) FROM reports").fetchone()[0] >= 20:
            return False  # Backpressure: source logs remain on the PS5.
        identifier = hashlib.sha256(body).hexdigest()
        with self.db:
            self.db.execute("INSERT OR IGNORE INTO reports (id,body) VALUES (?,?)", (identifier, body))
            self.db.execute("INSERT OR REPLACE INTO files VALUES (?,?)", (name, signature))
        return True

    def upload_one(self, config, send=None, now=None):
        now = time.time() if now is None else now
        row = self.db.execute("SELECT id,body,attempts FROM reports WHERE retry<=? ORDER BY rowid LIMIT 1", (now,)).fetchone()
        if row is None or not config.get("enabled"):
            return False
        identifier, body, attempts = row
        try:
            reply = (send or upload)(config, identifier, body)
            if reply.strip() != "accepted " + identifier:
                raise ValueError("Relay did not confirm report")
        except Exception:
            with self.db:
                self.db.execute("UPDATE reports SET attempts=?,retry=? WHERE id=?",
                                (attempts+1, now + min(1800, 30 * 2**min(attempts, 6)), identifier))
            print("Delivery pending; saved locally for retry.", flush=True)
            return False
        with self.db:
            self.db.execute("DELETE FROM reports WHERE id=?", (identifier,))
        print("Report delivered: " + identifier[:16], flush=True)
        return True


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def validate(config):
    host = ipaddress.ip_address(config["host"])
    if not host.is_private:
        raise ValueError("Use the PS5's LAN IP address")
    url = urlsplit(config["endpoint"])
    if url.scheme != "https" or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError("Use the relay HTTPS URL without credentials or query parameters")
    if url.path != "/v1/reports":
        raise ValueError("Endpoint must end in /v1/reports")
    if len(config.get("upload_token", "")) < 24:
        raise ValueError("Upload token must have at least 24 characters")


def upload(config, identifier, body):
    validate(config)
    request = urllib.request.Request(config["endpoint"], data=body, headers={
        "Authorization": "Bearer " + config["upload_token"],
        "Content-Type": "text/plain; charset=utf-8", "X-Report-ID": identifier,
        "User-Agent": "PS5X360-AutoLog/1.0 (+https://github.com/BrinooTk/PS5X360)"
    })
    # Default TLS validates both certificate chain and host; redirects disallowed.
    with urllib.request.build_opener(NoRedirect()).open(request, timeout=30) as response:
        if response.status != 200:
            raise ValueError("Delivery pending")
        return response.read(256).decode("ascii")


def remote_entries(ftp):
    # PS5 FTP payloads commonly implement LIST but not NLST/MLSD/MDTM.
    lines = []
    ftp.retrlines("LIST", lines.append)
    entries = {}
    for line in lines:
        fields = line.split(None, 8)
        if len(fields) == 9 and line.startswith("-") and FILE.fullmatch(fields[8]):
            entries[fields[8]] = ":".join(fields[4:8])
    return entries


def remote_signature(ftp, name):
    return remote_entries(ftp)[name]


def read_bounded(ftp, name):
    head = bytearray()
    tail = deque()
    total = 0
    # Keep initial context plus latest diagnostics, bounded even for verbose logs.
    def consume(chunk):
        nonlocal total
        total += len(chunk)
        if total > 9*1024*1024:
            raise ValueError("Unexpected log size")
        if len(head) < 64*1024:
            head.extend(chunk[:64*1024-len(head)])
        tail.append(chunk)
        while sum(map(len, tail)) > 384*1024 + 8192:
            tail.popleft()
    ftp.retrbinary("RETR " + name, consume, blocksize=8192)
    if total <= 64*1024:
        return bytes(head)
    if total <= sum(map(len, tail)):
        return b"".join(tail)
    return bytes(head) + b"\n[segment middle omitted]\n" + b"".join(tail)


def collect(config, queue, observations, now=None):
    now = time.monotonic() if now is None else now
    with ftplib.FTP() as ftp:
        ftp.connect(config["host"], int(config.get("port", 2121)), timeout=15)
        ftp.login()  # Existing PS5 anonymous FTP; LAN only. Never expose FTP publicly.
        ftp.cwd(LOG_FOLDER)
        entries = remote_entries(ftp)
        names = sorted(entries)
        bases = [n for n in names if ".part" not in n and not n.startswith("Game-Launcher-")]
        # Timestamp is near the end; do not sort by game name.
        bases.sort(key=lambda n: re.search(r"\d{8}-\d{6}-UTC-\d+", n)[0])
        for base in bases[-30:]:
            segments = [base] + [base[:-4]+f".part{i}.log" for i in (3,2,1) if base[:-4]+f".part{i}.log" in names]
            signatures = [entries[n] for n in segments]
            signature = "|".join(signatures)
            if queue.seen(base, signature):
                continue
            previous = observations.get(base)
            if not previous or previous[0] != signature:
                observations[base] = (signature, now)
                continue
            if now - previous[1] < 120:
                continue
            sections = [(n, read_bounded(ftp, n)) for n in segments]
            # A log may grow or rotate during transfer; don't send a mixed snapshot.
            after = remote_entries(ftp)
            if signatures != [after.get(n) for n in segments]:
                observations.pop(base, None)
                continue
            if not queue.add(base, signature, build_report(base, sections)):
                print("Queue full; console logs kept for later collection.", flush=True)
                break


@contextmanager
def singleton(home):
    with (home / "collector.lock").open("a+b") as stream:
        stream.seek(0); stream.write(b"0"); stream.flush(); stream.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def configure(home):
    print("Automatic game-log snapshots go to the developer's private Discord relay.")
    print("The PC must remain running. PS5 logs are preserved. No games/saves are uploaded.")
    config = {"host":input("PS5 LAN IP: ").strip(), "port":2121,
              "endpoint":input("Relay HTTPS URL ending in /v1/reports: ").strip(),
              "upload_token":getpass.getpass("Upload token (not the Discord webhook): "),
              "enabled": False}
    validate(config)
    config["enabled"] = input("Enable automatic uploads? Type YES: ").strip() == "YES"
    atomic_json(home / "config.json", config)
    print("Saved locally. Run AutoLog to collect. Edit enabled=false to stop uploads.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configure", action="store_true")
    parser.add_argument("--enable", action="store_true", help="enable the existing configuration with an interactive consent prompt")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--home", type=Path, default=HOME)
    args = parser.parse_args()
    args.home.mkdir(parents=True, exist_ok=True)
    if args.configure:
        configure(args.home)
        return
    if args.enable:
        path = args.home / "config.json"
        if not path.exists():
            raise SystemExit("Run with --configure first.")
        config = json.loads(path.read_text(encoding="utf-8"))
        validate(config)
        print("Send game diagnostic snapshots automatically to your configured private Discord relay.")
        print("The PC must stay on; no games or saves are uploaded. Local logs remain on the PS5.")
        if input("Enable automatic uploads? Type YES: ").strip() == "YES":
            config["enabled"] = True
            atomic_json(path, config)
            pause_file = args.home / "no-log-upload"
            if pause_file.exists():
                pause_file.unlink()
            print("Automatic uploads enabled. Run Start AutoLog.bat.")
        else:
            print("Configuration unchanged.")
        return
    if not (args.home / "config.json").exists():
        raise SystemExit("Run with --configure first.")
    with singleton(args.home):
        queue = Queue(args.home / "outbox.sqlite3")
        observations = {}
        while True:
            config = json.loads((args.home / "config.json").read_text(encoding="utf-8"))
            validate(config)
            if config.get("enabled") and not (args.home / "no-log-upload").exists():
                try:
                    collect(config, queue, observations)
                except Exception as error:
                    # Do not print server errors/URLs which might include credentials.
                    print("PS5 collection pending: " + type(error).__name__, flush=True)
                # Re-read consent after FTP, which may have taken a while.
                current = json.loads((args.home / "config.json").read_text(encoding="utf-8"))
                if current.get("enabled") and not (args.home / "no-log-upload").exists():
                    validate(current)
                    queue.upload_one(current)
            if args.once:
                break
            time.sleep(30)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("AutoLog stopped. Pending reports remain saved locally.")
