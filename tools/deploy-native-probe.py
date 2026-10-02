"""Publish the new M1 title only after FTP verification; never replace a title."""
import argparse
import ftplib
import hashlib
import json
from pathlib import Path
import struct
import uuid
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]


def exists(ftp, path):
    original = ftp.pwd()
    try:
        ftp.cwd(path)
        return True
    except ftplib.error_perm:
        return False
    finally:
        ftp.cwd(original)


def mkdir(ftp, path):
    if not exists(ftp, path):
        ftp.mkd(path)


def verify_eboot(actual, fself, elf):
    if actual == fself:
        return "full FSELF bytes verified"
    # The console FTP service may expose a decrypted ELF for a SELF file.
    # Validate all mapped bytes, ELF identity, and the entire program-header table.
    if not actual.startswith(b"\x7fELF") or len(actual) != len(elf) or actual[:64] != elf[:64]:
        raise RuntimeError("Unexpected eboot representation")
    phoff = struct.unpack_from("<Q", elf, 32)[0]
    phsize, phnum = struct.unpack_from("<HH", elf, 54)
    if actual[phoff:phoff+phsize*phnum] != elf[phoff:phoff+phsize*phnum]:
        raise RuntimeError("Remote program headers differ")
    headers = [struct.unpack_from("<IIQQQQQQ", elf, phoff+i*phsize) for i in range(phnum)]
    loads = [(h[2], h[2] + h[5]) for h in headers if h[0] == 1]
    stripped = 0
    for i, header in enumerate(headers):
        kind, _, offset, _, _, size, _, _ = header
        if actual[offset:offset+size] != elf[offset:offset+size]:
            # ftpsrv projects SELF to ELF and zeros unmapped SDK/version notes.
            # Never permit this for code/data, procparam, imports, TLS or dynamic tables.
            unmapped = not any(offset < end and offset + size > start for start, end in loads)
            if kind in (4, 0x6FFFFF01) and unmapped and not any(actual[offset:offset+size]):
                stripped += 1
            else:
                raise RuntimeError(f"Remote eboot segment {i} differs")
    return f"ELF headers and mapped/dynamic segments verified; FTP stripped {stripped} unmapped metadata segments"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--cpu-translation", action="store_true")
    parser.add_argument("--runtime", action="store_true")
    parser.add_argument("--game", action="store_true")
    args = parser.parse_args()
    if sum((args.runtime, args.cpu_translation, args.game)) > 1:
        raise RuntimeError("Choose one native stage")
    title_id = "PPSA50011" if args.game else "PPSA50010" if args.runtime else "PPSA50009" if args.cpu_translation else "PPSA50008"
    stage = ROOT / ("build/native-runtime-stage" if args.runtime else "build/native-cpu-stage" if args.cpu_translation else "build/native-stage")
    app = stage / "dist" / title_id
    image_dir = stage / "build"
    if args.game:
        stage = ROOT / "build/native-game"
        app = ROOT / "dist/PPSA50011"
        image_dir = stage
        manifest = json.loads((stage / "package-receipt.json").read_text())
        for name, record in manifest["files"].items():
            if hashlib.sha256((app / name).read_bytes()).hexdigest() != record["sha256"]:
                raise RuntimeError(f"Local package bytes changed: {name}")
    param = json.loads((app / "sce_sys/param.json").read_text())
    if param["titleId"] != title_id:
        raise RuntimeError("Probe identity mismatch")
    destination = "/data/homebrew/" + title_id
    staging = "/data/Xbox360PS5/" + ("M8" if args.game else "M7" if args.runtime else "M4" if args.cpu_translation else "M1") + "-upload-" + uuid.uuid4().hex[:8]
    ftp = ftplib.FTP()
    ftp.connect(args.host, 2121, timeout=25)
    ftp.login()
    verified = {}
    try:
        if exists(ftp, destination):
            raise RuntimeError(f"{title_id} already exists; refusing overwrite")
        mkdir(ftp, "/data/Xbox360PS5")
        mkdir(ftp, staging)
        for source in sorted(app.rglob("*")):
            relative = source.relative_to(app).as_posix()
            target = staging + "/" + relative
            if source.is_dir():
                mkdir(ftp, target)
                continue
            with source.open("rb") as stream:
                ftp.storbinary("STOR " + target, stream, blocksize=1024 * 1024)
            if relative in ("eboot.bin", "sce_module/libc.prx"):
                actual = bytearray()
                ftp.retrbinary("RETR " + target, actual.extend, blocksize=1024 * 1024)
                elf_name = "eboot.elf" if relative == "eboot.bin" else "libc.elf"
                detail = verify_eboot(bytes(actual), source.read_bytes(),
                    (image_dir / elf_name).read_bytes())
            else:
                remote_hash = hashlib.sha256()
                ftp.retrbinary("RETR " + target, remote_hash.update, blocksize=1024 * 1024)
                with source.open("rb") as stream:
                    local_hash = hashlib.file_digest(stream, "sha256")
                if remote_hash.digest() != local_hash.digest():
                    raise RuntimeError(f"Remote bytes differ: {relative}")
                detail = "SHA256 " + remote_hash.hexdigest()
            verified[relative] = detail
            print(relative, detail, flush=True)
        # Atomic publication of a new, previously absent title folder.
        if exists(ftp, destination):
            raise RuntimeError("Destination appeared while uploading; staging retained")
        ftp.rename(staging, destination)
        print("Published:", destination)
    finally:
        ftp.quit()
    request = Request(f"http://{args.host}:10101/api/v1/scan",
                      data=b'{"reset_attempts":true}', headers={"Content-Type":"application/json"})
    with urlopen(request, timeout=15) as response:
        scan = json.load(response)
    receipt = {"host": args.host, "title_id": title_id, "folder": destination,
               "verified": verified, "scan": scan, "hardware_tested": False}
    (ROOT / "dist" / ("game-deployment-receipt.json" if args.game else "runtime-deployment-receipt.json" if args.runtime else "cpu-deployment-receipt.json" if args.cpu_translation else "deployment-receipt.json")).write_text(json.dumps(receipt, indent=2) + "\n")
    if scan.get("status") != 0:
        raise RuntimeError(f"ShadowMount scan rejected: {scan}")
    print("ShadowMount scan queued:", scan)


if __name__ == "__main__":
    main()
