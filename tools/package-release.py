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
    output = ROOT / "dist/PPSA50011.zip"
    # Package the current Canary build, rather than a stale executable staged by
    # the original-core build script.
    current = ROOT / "build/canary-game/eboot.bin"
    if not current.is_file():
        raise SystemExit("Build the current title with tools/build-canary-game.sh first")
    if ("PS5X360 v" + version).encode() not in current.read_bytes():
        raise SystemExit("Compiled executable version does not match VERSION")
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
        archive.write(ROOT / "docs/INSTALLATION.md", "PPSA50011/INSTALLATION.md")
        archive.write(ROOT / "docs/CREDITS.md", "PPSA50011/CREDITS.md")
        archive.write(ROOT / "LICENSE", "PPSA50011/LICENSE")
        archive.write(notes, "PPSA50011/RELEASE_NOTES.md")
        archive.write(ROOT / "native/autolog/README.md", "PPSA50011/AUTOLOG.md")
        archive.writestr("PPSA50011/VERSION", version + "\n")
        archive.writestr("PPSA50011/RELEASE.json", json.dumps({"version": version, "tag": "v" + version,
                            "executable_sha256": hashlib.sha256(current.read_bytes()).hexdigest()}, indent=2) + "\n")
        manifest = {name: {"bytes": path.stat().st_size,
                          "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                    for name, path in files.items()}
        archive.writestr("PPSA50011/MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
        for path in sorted((ROOT / "licenses").rglob("*")):
            if path.is_file():
                archive.write(path, "PPSA50011/" + path.relative_to(ROOT).as_posix())
        # Keep complete corresponding collector source and licenses available
        # without adding a third release download. AutoLog itself stays optional.
        collector_source = ROOT / "dist/PS5X360-AutoLog-ELF-v1.0.9-preview.zip"
        if not collector_source.is_file():
            raise SystemExit("Missing corresponding AutoLog source archive")
        archive.write(collector_source, "PPSA50011/licenses/AutoLog-source-v1.0.9.zip")
    with zipfile.ZipFile(output) as archive:
        if archive.testzip():
            raise SystemExit("ZIP CRC validation failed")
        names = archive.namelist()
        if any(not name.startswith("PPSA50011/") for name in names):
            raise SystemExit("Release must contain only the PPSA50011 folder")
        for name, record in manifest.items():
            data = archive.read("PPSA50011/" + name)
            if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
                raise SystemExit(f"Manifest verification failed: {name}")
    if any("/roms/" in name and not name.endswith("README.txt") for name in names):
        raise SystemExit("Game data found in the release zip")
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(".zip.sha256").write_text(digest + "  " + output.name + "\n", encoding="utf-8")
    print(json.dumps({"version": version, "package": str(output), "files": len(names), "patch_files": len(patch_files),
                      "bytes": output.stat().st_size,
                      "sha256": digest}))


if __name__ == "__main__":
    main()
