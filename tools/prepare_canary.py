"""Fetches the pinned Xenia Canary and applies the Xbox360PS5 patch to it.

    python3 tools/prepare_canary.py            check out the pinned revision,
                                               fetch its submodules, apply the patch
    python3 tools/prepare_canary.py --export   write the working tree's changes
                                               back to patches/canary/xbox360ps5.patch

The emulator core is Canary's own tree (.deps/xenia-canary) built with its own
CMake files (see canary/CMakeLists.txt); everything the PS5 needs changed in it
is in the one patch, each change marked "Xbox360PS5" in the source.
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / ".deps/xenia-canary"
PATCH = ROOT / "patches/canary/xbox360ps5.patch"
# Written by Canary's build (compiled shaders, its in-tree snappy configure).
GENERATED = ("src/xenia/gpu/shaders/bytecode/", "CMakeCache.txt", "CMakeFiles/", "Makefile", "cmake_install.cmake")


def git(*args, **kwargs):
    return subprocess.run(["git", "-C", str(SOURCE), "-c", "core.autocrlf=false", "-c", "core.safecrlf=false", *args],
                          check=True, text=True, capture_output=True, **kwargs).stdout


def changed():
    names = [line[3:] for line in git("status", "--porcelain").splitlines() if not line.startswith("??")]
    return [name for name in names if not name.startswith(GENERATED)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--export", action="store_true")
    args = parser.parse_args()
    dep = json.loads((ROOT / "deps.json").read_text())["xenia_canary"]
    if args.export:
        PATCH.parent.mkdir(parents=True, exist_ok=True)
        PATCH.write_text(git("diff", "--", *changed()), encoding="utf-8", newline="\n")
        print(f"{PATCH.relative_to(ROOT)}: {len(changed())} files")
        return
    if not SOURCE.exists():
        subprocess.check_call(["git", "clone", "--filter=blob:none", "--no-checkout", dep["url"], str(SOURCE)])
    if subprocess.run(["git", "-C", str(SOURCE), "cat-file", "-e", dep["revision"]],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        git("fetch", "origin", dep["revision"])
    # Back to the pinned tree, then the patch: the result does not depend on
    # what an earlier version of the patch left behind.
    names = changed()
    if names:
        git("checkout", dep["revision"], "--", *names)
    git("checkout", "--detach", dep["revision"])
    paths = git("config", "-f", ".gitmodules", "--get-regexp", "path").splitlines()
    submodules = [line.split()[1] for line in paths if line.split()[1] not in dep["submodules_excluded"]]
    subprocess.check_call(["git", "-C", str(SOURCE), "submodule", "update", "--init", "--depth", "1", "--", *submodules])
    git("apply", "--whitespace=nowarn", str(PATCH))
    print(f"Xenia Canary {dep['revision'][:8]} with {PATCH.name} ({len(changed())} files changed)")


if __name__ == "__main__":
    main()
