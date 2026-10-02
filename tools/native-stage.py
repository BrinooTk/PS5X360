"""Stage a minimal native app using the validated local title toolchain."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
RUNTIME = WORKSPACE / "Castation/native-ps5"
BOILERPLATE = WORKSPACE / ".deps/upstream/ps5-native-app-boilerplate"
RUNTIME_PIN = "94dfef7"
BOILERPLATE_PIN = "470695e0c557f99ff2df0e36e4df713c5e636526"
STAGE = ROOT / "build/native-stage"


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def write(relative, data):
    target = STAGE / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def icon():
    # Original diagnostic icon, independent of any emulator artwork.
    rows = bytearray()
    for y in range(512):
        rows.append(0)
        for x in range(512):
            cross = 85 < x < 427 and 85 < y < 427 and (
                abs(x - y) < 38 or abs(x + y - 511) < 38)
            rows.extend((92, 230, 124, 255) if cross else (14, 24, 30, 255))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 512, 512, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def main():
    runtime_revision = git(RUNTIME, "rev-parse", RUNTIME_PIN).decode().strip()
    paths = git(RUNTIME, "ls-tree", "-r", "--name-only", runtime_revision,
                "tools", "tooling").decode().splitlines()
    for path in paths:
        write(path, git(RUNTIME, "show", f"{runtime_revision}:{path}"))
    # Use the original small renderer and retain its licence/header.
    renderer = git(BOILERPLATE, "show", f"{BOILERPLATE_PIN}:src/demo_renderer.cpp").decode().replace("\r\n", "\n")
    old_loop = "    for (;;)\n        (void)sceKernelUsleep(1000000);\n}\n} // namespace ps5::demo\n"
    if renderer.count(old_loop) != 1:
        raise SystemExit("Renderer loop anchor changed")
    renderer = renderer.replace(old_loop, """    unsigned next = 1;
    std::int64_t flips = 1;
    for (;;) {
        Canvas &canvas = next == 0 ? first : second;
        draw(canvas);
        flush_range(next == 0 ? mapped : second_frame, frame_bytes);
        if (sceVideoOutSubmitFlip(video, static_cast<std::int32_t>(next), 1, ++flips) < 0)
            halt("M1: flip failed");
        (void)sceVideoOutWaitVblank(video);
        next ^= 1;
        (void)sceKernelUsleep(33000);
    }
}
} // namespace ps5::demo
""")
    write("src/demo_renderer.cpp", renderer.encode())
    write("src/demo_renderer.hpp", git(BOILERPLATE, "show", f"{BOILERPLATE_PIN}:src/demo_renderer.hpp"))
    write("src/main.cpp", (ROOT / "platform/ps5/native_probe.cpp").read_bytes())
    write("include/probe_cases.hpp", (ROOT / "include/probe_cases.hpp").read_bytes())
    write("include/xenia/base/platform.h", (ROOT / "build/generated/xenia/base/platform.h").read_bytes())
    write(".local/decoder/libxenia_ppc_decoder.a", (ROOT / "build/ps5/libxenia_ppc_decoder.a").read_bytes())
    for name in ("libc.prx", "libc.prx.sha256"):
        write("runtime/" + name, (RUNTIME / "runtime" / name).read_bytes())
    # Sharing the already installed SDK/cache avoids fetching or modifying it.
    for name, source in (("native", RUNTIME / ".deps/native"), ("xenia", ROOT / ".deps/xenia")):
        target = STAGE / ".deps" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.symlink_to(source, target_is_directory=True)
        elif target.resolve() != source.resolve():
            raise SystemExit(f"Unexpected dependency path: {target}")
    param = json.loads(git(RUNTIME, "show", f"{runtime_revision}:sce_sys/param.json"))
    param.update(titleId="PPSA50008", conceptId="50008",
                 contentId="UP9000-PPSA50008_00-XBOX360PS5PROBE1",
                 contentVersion="00.001.000", masterVersion="00.01")
    param["localizedParameters"]["en-US"]["titleName"] = "Xbox360PS5 Platform Test"
    write("sce_sys/param.json", (json.dumps(param, indent=2) + "\n").encode())
    write("sce_sys/icon0.png", icon())
    receipt = {"runtime": runtime_revision, "boilerplate_renderer": BOILERPLATE_PIN,
               "native_main_sha256": hashlib.sha256((ROOT / "platform/ps5/native_probe.cpp").read_bytes()).hexdigest()}
    write("stage-receipt.json", (json.dumps(receipt, indent=2) + "\n").encode())
    print(f"Native probe staged at {STAGE}")


if __name__ == "__main__":
    main()
