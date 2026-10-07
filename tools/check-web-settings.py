"""Drives the settings page's server (platform/ps5/web_settings.cpp, built for the host) over real sockets:
the page, the key every other request needs, the state, a change handed to the owner of the settings,
malformed changes, the log listing and download with names that must be refused, and the ZIP of every log
or of one game's logs (opened and verified with Python's zipfile)."""
import io
import json
import os
import shutil
import zipfile
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / "build/web-settings-check"
logs = out / "logs"
shutil.rmtree(logs, ignore_errors=True)
logs.mkdir(parents=True, exist_ok=True)
sample = "Game-Sample-00000000-20261006-000000-UTC-0.log"
repeating = "".join(f"w> 0100000C Readback: {n * 4096} bytes copied back to 1F702000 ({n} of them not zero), mode full\n"
                    for n in range(6000)).encode()
files = {
    sample: b"PS5X360 sample log\n",
    "boot.log": b"[X360] BOOT main entered\n[X360] CRASH signal=6\n" * 40,
    "Game-Left 4 Dead 2-5EF98836-20261006-173317-UTC-0.log": repeating,
    "Game-Left 4 Dead 2-5EF98836-20261006-173317-UTC-0.log.part1": b"an older part of the same session\n",
    "Game-Left 4 Dead 2 [RF]-5EF98836-20261006-173600-UTC-0.log": b"listed under another name, the same game\n",
    "Game-Bakugan\u2122 DOTC-A1B2C3D4-20261006-150000-UTC-0.log": b"a name outside ASCII\n",
    "Game-Noise-0BADF00D-20261006-160000-UTC-0.log": os.urandom(70000),
    "Game-Empty-0000E0E0-20261006-170000-UTC-0.log": b"",
}
for age, (name, data) in enumerate(files.items()):
    (logs / name).write_bytes(data)
    os.utime(logs / name, (1790000000 + age * 60, 1790000000 + age * 60))
(logs / "notes.bin").write_text("not a log\n")
(out / "secret.txt").write_text("outside the folder\n")
(out / "harness.cpp").write_text(r'''
#include "xbox360ps5/web_settings.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
int main(int, char** argv) {
  using namespace xbox360ps5;
  if (!StartWebSettings(argv[1])) { std::puts("START FAILED"); return 1; }
  PublishWebState("{\"sample\":" + JsonText("a \"quoted\" text") + "}");
  std::printf("KEY %s\nADDRESS %s\n", WebSettingsKey().c_str(), WebSettingsAddress().c_str());
  std::fflush(stdout);
  for (int n = 0; n < 150; ++n) {
    for (const auto& change : TakeWebChanges())
      std::printf("CHANGE scope=%s key=%s value=%d\n", change.scope.c_str(), change.key.c_str(), change.value);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
''')
subprocess.run(["clang++-18", "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined", "-I" + str(root / "include"),
                str(out / "harness.cpp"), str(root / "platform/ps5/web_settings.cpp"), "-lpthread", "-o", str(out / "harness")],
               check=True)
server = subprocess.Popen([str(out / "harness"), str(logs)], stdout=subprocess.PIPE, text=True)
key = server.stdout.readline().split()[1]
address = server.stdout.readline().split()
port = 8360
if len(address) > 1:
    port = int(address[1].split(":")[2].split("/")[0])
base = f"http://127.0.0.1:{port}"


def ask_bytes(path, data=None, given=key, method=None):
    request = urllib.request.Request(base + path, data=data, method=method)
    if given is not None:
        request.add_header("X-Key", given)
    try:
        with urllib.request.urlopen(request, timeout=20) as reply:
            return reply.status, reply.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read()


def ask(path, data=None, given=key, method=None):
    status, body = ask_bytes(path, data, given, method)
    return status, body.decode()


def archive(game):
    status, body = ask_bytes("/logs.zip?game=" + game)
    assert status == 200, (game, status)
    opened = zipfile.ZipFile(io.BytesIO(body))
    assert opened.testzip() is None, game
    for name in opened.namelist():
        assert opened.read(name) == files[name], name
    return opened


try:
    status, page = ask("/", given=None)
    assert status == 200 and "PS5X360" in page and "/api/state" in page, status
    assert ask("/api/state", given=None)[0] == 403 and ask("/api/state", given="000000x")[0] == 403
    status, state = ask("/api/state")
    assert status == 200 and json.loads(state) == {"sample": 'a "quoted" text'}, state
    assert ask("/api/state?k=" + key, given=None)[0] == 200
    assert ask("/api/set", b"scope=&key=image_filter&value=2")[0] == 200
    assert ask("/api/set", b"scope=545407F2&key=vsync&value=-1")[0] == 200
    assert ask("/api/set", b"scope=&key=image_filter&value=2", given="nope")[0] == 403
    for bad in (b"scope=&key=&value=1", b"scope=../x&key=a&value=1", b"scope=&key=a%2Fb&value=1", b"scope=&key=a&value=999",
                b"scope=123456789&key=a&value=1"):
        assert ask("/api/set", bad)[0] == 400, bad
    status, listing = ask("/api/logs")
    listing = json.loads(listing)
    names = [entry["name"] for entry in listing["files"]]
    # Newest first; a rotated part is not offered one by one, it goes in the archives.
    assert names == [name for name in reversed(files) if not name.endswith(".part1")], names
    assert listing["count"] == len(files) and listing["size"] == sum(len(data) for data in files.values()), listing
    games = {game["id"]: game for game in listing["games"]}
    assert games["5EF98836"]["count"] == 3 and games["5EF98836"]["name"] == "Left 4 Dead 2 [RF]", games["5EF98836"]
    assert games["A1B2C3D4"]["name"] == "Bakugan\u2122 DOTC" and listing["games"][0]["id"] == "0000E0E0", listing["games"]
    status, text = ask("/logs/" + sample.replace(" ", "%20"))
    assert status == 200 and text == "PS5X360 sample log\n"
    whole = archive("all")
    assert sorted(whole.namelist()) == sorted(files), whole.namelist()
    packed = whole.getinfo("Game-Left 4 Dead 2-5EF98836-20261006-173317-UTC-0.log")
    assert packed.compress_type == zipfile.ZIP_DEFLATED and packed.compress_size * 4 < packed.file_size, (packed.compress_size, packed.file_size)
    assert whole.getinfo("Game-Noise-0BADF00D-20261006-160000-UTC-0.log").compress_type == zipfile.ZIP_STORED
    one = archive("5EF98836")
    assert sorted(one.namelist()) == sorted(name for name in files if "5EF98836" in name or name == "boot.log"), one.namelist()
    assert sorted(archive("FFFFFFFF").namelist()) == ["boot.log"]
    for refused in ("/logs.zip", "/logs.zip?game=", "/logs.zip?game=..%2F..%2Fx", "/logs.zip?game=5EF9883", "/logs.zip?game=5EF98836X"):
        assert ask_bytes(refused)[0] == 400, refused
    assert ask_bytes("/logs.zip?game=all", given=None)[0] == 403
    for refused in ("/logs/notes.bin", "/logs/..%2Fsecret.txt", "/logs/../secret.txt", "/logs/%2e%2e%2fsecret.txt", "/logs/.hidden.txt"):
        assert ask(refused)[0] == 404, refused
    assert ask("/logs/" + sample, given=None)[0] == 403
    assert ask("/nothing")[0] == 404
    time.sleep(0.5)
finally:
    server.terminate()
lines = server.stdout.read().splitlines()
assert lines == ["CHANGE scope= key=image_filter value=2", "CHANGE scope=545407F2 key=vsync value=-1"], lines
print("PASS: page without the key; state, changes and logs only with it; 5 malformed changes and 5 file names refused; "
      "2 changes handed over in order; ZIP of every log and of one game's logs opened and compared file by file "
      f"({packed.file_size} bytes of log text packed to {packed.compress_size})")
