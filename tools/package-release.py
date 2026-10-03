"""Build the zip people install from: the PPSA50011 title folder (without games),
the user guide and the licences. Run after tools/build-engine-ps5.sh."""
import hashlib
import re
import json
import subprocess
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "dist/PPSA50011"
PATCHES = ROOT / ".deps/references/game-patches"


def main():
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?", version):
        raise SystemExit("Invalid release version in VERSION")
    notes = ROOT / "docs/releases" / ("v" + version + ".md")
    if not notes.is_file():
        raise SystemExit(f"Missing release notes: {notes}")
    output = ROOT / "dist" / ("PS5X360-v" + version + ".zip")
    # Package the current Canary build, rather than a stale executable staged by
    # the original-core build script.
    current = ROOT / "build/canary-game/eboot.bin"
    if not current.is_file():
        raise SystemExit("Build the current title with tools/build-canary-game.sh first")
    (APP / "eboot.bin").write_bytes(current.read_bytes())
    required = ["eboot.bin", "sce_module/libc.prx", "sce_sys/param.json", "sce_sys/icon0.png",
                "sce_sys/pic0.png", "sce_sys/pic1.png",
                "assets/sounds/move.raw", "assets/sounds/select.raw", "assets/sounds/back.raw",
                "assets/fonts/NotoSans-Regular.ttf", "assets/fonts/NotoSans-SemiBold.ttf", "assets/fonts/OFL.txt"]
    missing = [name for name in required if not (APP / name).is_file()]
    if missing:
        raise SystemExit(f"Missing from {APP}: {missing}")
    files = {name: APP / name for name in required}
    files["assets/roms/README.txt"] = APP / "assets/roms/README.txt"
    patch_files = sorted((APP / "assets/patches").glob("*.patch.toml"))
    for path in patch_files:
        files[f"assets/patches/{path.name}"] = path
    revision = ""
    if (PATCHES / ".git").exists():
        revision = subprocess.run(["git", "-C", str(PATCHES), "rev-parse", "HEAD"],
                                  capture_output=True, text=True).stdout.strip()
    patch_notice = (
        "Game patches from the Xenia Canary community repository\n"
        "https://github.com/xenia-canary/game-patches"
        + (f" (revision {revision})" if revision else "") + "\n"
        "Credit to their authors, named in each file. Every patch ships switched off.\n")
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, path in files.items():
            archive.write(path, "PPSA50011/" + name)
        archive.writestr("PPSA50011/assets/patches/ORIGEM.txt", patch_notice)
        archive.write(ROOT / "docs/INSTALLATION.md", "INSTALLATION.md")
        archive.write(ROOT / "docs/CREDITS.md", "CREDITS.md")
        archive.write(ROOT / "LICENSE", "LICENSE")
        archive.write(notes, "RELEASE_NOTES.md")
        archive.writestr("VERSION", version + "\n")
        archive.writestr("RELEASE.json", json.dumps({"version": version, "tag": "v" + version,
                            "executable_sha256": hashlib.sha256(current.read_bytes()).hexdigest()}, indent=2) + "\n")
        manifest = {name: {"bytes": path.stat().st_size,
                          "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                    for name, path in files.items()}
        archive.writestr("MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
        for path in sorted((ROOT / "licenses").rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(output) as archive:
        if archive.testzip():
            raise SystemExit("ZIP CRC validation failed")
        names = archive.namelist()
    if any("/roms/" in name and not name.endswith("README.txt") for name in names):
        raise SystemExit("Game data found in the release zip")
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(".zip.sha256").write_text(digest + "  " + output.name + "\n", encoding="utf-8")
    print(json.dumps({"version": version, "package": str(output), "files": len(names), "patch_files": len(patch_files),
                      "bytes": output.stat().st_size,
                      "sha256": digest}))


if __name__ == "__main__":
    main()
