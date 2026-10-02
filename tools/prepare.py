"""Fetch pinned inputs and generate a PS5 platform overlay without editing Xenia."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    dep = json.loads((ROOT / "deps.json").read_text())["xenia"]
    source = ROOT / ".deps/xenia"
    if args.fetch:
        if not source.exists():
            subprocess.check_call(["git", "clone", "--no-checkout", dep["url"], str(source)])
        if run("git", "-C", str(source), "status", "--porcelain"):
            raise SystemExit("Xenia has local changes; refusing checkout")
        if subprocess.run(["git", "-C", str(source), "cat-file", "-e", dep["revision"]],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            subprocess.check_call(["git", "-C", str(source), "fetch", "origin", dep["revision"]])
        subprocess.check_call(["git", "-C", str(source), "checkout", "--detach", dep["revision"]])
        subprocess.check_call(["git", "-C", str(source), "submodule", "update", "--init",
                               "--depth", "1", *dep["submodules"]])
    actual = run("git", "-C", str(source), "rev-parse", "HEAD")
    if actual != dep["revision"]:
        raise SystemExit(f"Wrong Xenia revision: {actual}")
    platform = (source / "src/xenia/base/platform.h").read_text()
    marker = "#if defined(TARGET_OS_MAC) && TARGET_OS_MAC"
    if platform.count(marker) != 1:
        raise SystemExit("Platform overlay anchor changed")
    platform = platform.replace(marker,
        "#if defined(__PROSPERO__)\n#define XE_PLATFORM_PS5 1\n#elif defined(TARGET_OS_MAC) && TARGET_OS_MAC")
    target = ROOT / "build/generated/xenia/base/platform.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text() != platform:
        target.write_text(platform)
    print(f"Pinned Xenia {actual}; PS5 overlay generated")


if __name__ == "__main__":
    main()
