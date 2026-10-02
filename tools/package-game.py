"""Package the experimental native game entry, never include game data."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--folder", action="store_true")
args = parser.parse_args()
app = ROOT / "dist/PPSA50011"
if args.folder:
    param = json.loads((ROOT / "build/native-runtime-stage/dist/PPSA50010/sce_sys/param.json").read_text())
    param["titleId"] = "PPSA50011"
    param["conceptId"] = "50011"
    param["contentId"] = "UP9000-PPSA50011_00-XBOX360PS5M80001"
    param["localizedParameters"]["en-US"]["titleName"] = "Xbox360PS5 Experimental Game Test"
    (app / "sce_sys/param.json").write_text(json.dumps(param, indent=2) + "\n")
    (app / "assets/roms/README.txt").write_text(
        "Put the extracted game here, with default.xex and its original data folders.\n"
        "Default guest path: /app0/assets/roms/default.xex\n"
        "For a subfolder, set the full /app0 path in assets/game.txt.\n"
        "No game, console BIOS or keys are included.\n")
    (app / "TEST_STATUS.txt").write_text(
        "EXPERIMENTAL BUILD - NOT YET HARDWARE VALIDATED.\n"
        "Real Xenia CPU/kernel/VFS/XMA/Xenos Vulkan and RADV are linked.\n"
        "Host lifecycle and Sonic module loading passed; game execution is unverified.\n"
        "Native log: /download0/xbox360ps5/engine.log\n"
        "Preserve the prior M4/M7 apps until this has been tested.\n")
else:
    tests = ET.parse(ROOT / "build/kernel-host/integration-tests.xml").getroot()
    if int(tests.attrib.get("failures", "1")) or int(tests.attrib.get("tests", "0")) < 16:
        raise SystemExit("Passing integrated host tests required")
    required = [app / name for name in ("eboot.bin", "sce_module/libc.prx", "sce_sys/param.json")]
    if any(not path.is_file() for path in required):
        raise SystemExit("Missing native application artifact")
    expected_libc = "e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"
    if hashlib.sha256(required[1].read_bytes()).hexdigest() != expected_libc:
        raise SystemExit("Unexpected companion libc")
    allowed = {"eboot.bin", "sce_module/libc.prx", "sce_sys/param.json", "sce_sys/icon0.png",
               "assets/roms/README.txt", "assets/game.txt", "TEST_STATUS.txt"}
    package_files = [path for path in sorted(app.rglob('*')) if path.is_file()]
    if any(path.relative_to(app).as_posix() not in allowed for path in package_files):
        raise SystemExit("Unexpected app contents: refusing to bundle game data or unknown files")
    records = {str(path.relative_to(app)).replace('\\', '/'): {
        "bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()
    } for path in package_files}
    receipt = {"title": "PPSA50011", "hardware_tested": False,
               "game_executed": False, "experimental": True, "host_suites": 16,
               "files": records, "dependencies": json.loads((ROOT / 'deps.json').read_text()),
               "radv_sha256": hashlib.sha256((ROOT / '.deps/references/PS5_Vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a').read_bytes()).hexdigest(),
               "elfs": {name: hashlib.sha256((ROOT / 'build/native-game' / name).read_bytes()).hexdigest()
                        for name in ('llvm-pie.elf', 'eboot.elf')}}
    (ROOT / "build/native-game/package-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    output = ROOT / "dist/Xbox360PS5-M8-experimental-game-test.zip"
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in package_files:
            archive.write(path, str(path.relative_to(app.parent)))
        for path in (ROOT / 'licenses').rglob('*'):
            if path.is_file(): archive.write(path, str(path.relative_to(ROOT)))
        archive.write(ROOT / 'build/native-game/package-receipt.json', 'package-receipt.json')
        archive.write(ROOT / 'docs/ENGINE_INTEGRATION.md', 'ENGINE_INTEGRATION.md')
        for directory in ('src', 'platform', 'include', 'cmake', 'tools', 'tooling'):
            for path in sorted((ROOT / directory).rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:
                    archive.write(path, 'source/' + path.relative_to(ROOT).as_posix())
        for name in ('CMakeLists.txt', 'build.ps1', 'deps.json', 'LICENSE', 'README.md'):
            path = ROOT / name
            if path.is_file(): archive.write(path, 'source/' + name)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip(): raise SystemExit("ZIP CRC validation failed")
    print(json.dumps({"package": str(output), "bytes": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                      "hardware_tested": False}))
