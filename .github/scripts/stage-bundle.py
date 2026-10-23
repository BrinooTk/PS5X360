"""The ready-to-install bundle from a CI build: dist/PPSA50011/ and dist/PPSA50011.zip.

The same PPSA50011 folder tools/package-release.py zips (eboot.bin, sce_module/libc.prx,
sce_sys, the sounds, fonts and community game patches, the guide, notices and a SHA-256
manifest), staged from the build's outputs instead of a folder prepared by hand:

  sce_sys/param.json   Castation's, with this title's ids and name, as tools/native-stage.py
                       and tools/package-game.py --folder make it
  assets/patches       xenia-canary/game-patches at the pinned revision, every patch off
  assets/roms          the README tools/package-game.py --folder writes, no games

Left out, as a CI build cannot make them: licenses/AutoLog-source-v1.0.9.zip (the optional
AutoLog collector's archive, packaged with the owner's credentials checked) and the per-game
assets/presets.txt the published releases add. Game data is never included.

    python3 .github/scripts/stage-bundle.py --eboot build/canary-game/eboot.bin \\
        --libc .deps/references/PS5_Vulkan/runtime/libc.prx --castation .deps/castation \\
        --patches .deps/references/game-patches
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TITLE = "PPSA50011"
# The runtime module tools/package-game.py accepts (the clean-room libc.prx).
LIBC_SHA256 = "e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"
# tools/package-game.py --folder's text.
ROMS_README = (
    "Put each game in its own folder here: an extracted game (default.xex and its\n"
    "data folders), an .iso image, or a GOD/STFS package. The library lists them.\n"
    "To start one game directly, write its full /app0 path in assets/game.txt.\n"
    "No game, console BIOS or keys are included.\n")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def revision(repository: Path) -> str:
    return subprocess.run(["git", "-C", str(repository), "rev-parse", "HEAD"],
                          capture_output=True, text=True, check=True).stdout.strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--eboot", type=Path, required=True)
    parser.add_argument("--libc", type=Path, required=True)
    parser.add_argument("--castation", type=Path, required=True, help="a Castation checkout (its sce_sys/param.json)")
    parser.add_argument("--patches", type=Path, required=True, help="a xenia-canary/game-patches checkout")
    parser.add_argument("--build", default="", help="what built it, for RELEASE.json (a CI run's address)")
    args = parser.parse_args()

    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?", version):
        raise SystemExit("Invalid release version in VERSION")
    eboot = args.eboot.read_bytes()
    if ("PS5X360 v" + version).encode() not in eboot:
        raise SystemExit("Compiled executable version does not match VERSION")
    libc = args.libc.read_bytes()
    if sha256(libc) != LIBC_SHA256:
        raise SystemExit(f"{args.libc} is not the runtime module tools/package-game.py accepts")

    param = json.loads((args.castation / "sce_sys/param.json").read_text(encoding="utf-8"))
    param.update(titleId=TITLE, conceptId="50011", contentId="UP9000-PPSA50011_00-XBOX360PS5M80001",
                 contentVersion="00.001.000", masterVersion="00.01")
    param["localizedParameters"]["en-US"]["titleName"] = "PS5X360"

    app = ROOT / "dist" / TITLE
    shutil.rmtree(app, ignore_errors=True)
    files = {
        "eboot.bin": eboot,
        "sce_module/libc.prx": libc,
        "sce_sys/param.json": (json.dumps(param, indent=2) + "\n").encode(),
        "assets/roms/README.txt": ROMS_README.encode(),
    }
    for name in ("icon0.png", "pic0.png", "pic1.png"):
        files["sce_sys/" + name] = (ROOT / "assets/branding" / name).read_bytes()
    for name in ("move.raw", "select.raw", "back.raw"):
        files["assets/sounds/" + name] = (ROOT / "assets/sounds" / name).read_bytes()
    for name in ("NotoSans-Regular.ttf", "NotoSans-SemiBold.ttf", "OFL.txt"):
        files["assets/fonts/" + name] = (ROOT / "assets/fonts" / name).read_bytes()
    patch_files = sorted((args.patches / "patches").glob("*.patch.toml"))
    if not patch_files:
        raise SystemExit(f"No .patch.toml files in {args.patches / 'patches'}")
    for path in patch_files:
        files["assets/patches/" + path.name] = path.read_bytes()
    for name, data in files.items():
        (app / name).parent.mkdir(parents=True, exist_ok=True)
        (app / name).write_bytes(data)

    patch_revision = revision(args.patches)
    patch_notice = ("Game patches from the Xenia Canary community repository\n"
                    f"https://github.com/xenia-canary/game-patches (revision {patch_revision})\n"
                    "Credit to their authors, named in each file. Every patch ships switched off.\n")
    notes = ROOT / "docs/releases" / ("v" + version + ".md")
    release = {"version": version, "tag": "v" + version, "executable_sha256": sha256(eboot),
               "source_commit": revision(ROOT), "built_by": args.build or "local",
               "hardware_gameplay": "unverified", "game_patches": patch_revision,
               "not_included": ["licenses/AutoLog-source-v1.0.9.zip", "assets/presets.txt"]}
    manifest = {name: {"bytes": len(data), "sha256": sha256(data)} for name, data in files.items()}
    extras = {
        "INSTALLATION.md": ROOT / "docs/INSTALLATION.md",
        "CREDITS.md": ROOT / "docs/CREDITS.md",
        "LICENSE": ROOT / "LICENSE",
        "AUTOLOG.md": ROOT / "native/autolog/README.md",
    }
    if notes.is_file():
        extras["RELEASE_NOTES.md"] = notes
    output = ROOT / "dist" / (TITLE + ".zip")
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for name in files:
            archive.write(app / name, f"{TITLE}/{name}")
        archive.writestr(f"{TITLE}/assets/patches/ORIGEM.txt", patch_notice)
        for name, path in extras.items():
            archive.write(path, f"{TITLE}/{name}")
        archive.writestr(f"{TITLE}/VERSION", version + "\n")
        archive.writestr(f"{TITLE}/RELEASE.json", json.dumps(release, indent=2) + "\n")
        archive.writestr(f"{TITLE}/MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
        for path in sorted((ROOT / "licenses").rglob("*")):
            if path.is_file():
                archive.write(path, f"{TITLE}/" + path.relative_to(ROOT).as_posix())
    # The checks tools/package-release.py makes of its zip.
    with zipfile.ZipFile(output) as archive:
        if archive.testzip():
            raise SystemExit("ZIP CRC validation failed")
        names = archive.namelist()
        if any(not name.startswith(TITLE + "/") for name in names):
            raise SystemExit(f"The bundle must contain only the {TITLE} folder")
        for name, record in manifest.items():
            data = archive.read(f"{TITLE}/{name}")
            if len(data) != record["bytes"] or sha256(data) != record["sha256"]:
                raise SystemExit(f"Manifest verification failed: {name}")
    if any("/roms/" in name and not name.endswith("README.txt") for name in names):
        raise SystemExit("Game data found in the bundle")
    digest = sha256(output.read_bytes())
    output.with_suffix(".zip.sha256").write_text(f"{digest}  {output.name}\n", encoding="utf-8")
    print(json.dumps({"version": version, "package": str(output), "files": len(names),
                      "patch_files": len(patch_files), "bytes": output.stat().st_size, "sha256": digest}))


if __name__ == "__main__":
    main()
