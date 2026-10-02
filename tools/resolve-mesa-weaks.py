"""Preserve ELF weak-undefined/null semantics for Mesa's optional dispatch entries."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
PREFIXES = {"annotate", "ctx", "radv", "rmv", "rra", "sqtt", "threaded", "utrace", "vk", "wsi"}
def collect(text):
    result = set()
    for line in text.splitlines():
        parts = line.split()
        if not parts or parts[0] not in ("w", "v"):
            continue
        if len(parts) != 2 or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", parts[1]):
            raise ValueError("Unexpected weak symbol encoding")
        name = parts[1]
        if name.split("_", 1)[0] not in PREFIXES:
            raise ValueError("Unexpected weak reference outside Mesa: " + name)
        result.add(name)
    return sorted(result)
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    names = collect(subprocess.check_output(["llvm-nm-18", "-u", str(args.elf)], text=True))
    # Strong symbols are never altered. The driver was linked whole already:
    # these names have no implementation in the selected Mesa archive.
    args.output.with_suffix('.rsp').write_text(''.join('--defsym=' + name + '=0\n' for name in names))
    args.output.with_suffix('.map').write_text('{ local:\n' + ''.join('  ' + name + ';\n' for name in names) + '};\n')
    report = {"weak_optional_null_entries": len(names), "strong_symbols_changed": 0,
              "prefixes": dict(Counter(name.split('_', 1)[0] for name in names))}
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
if __name__ == '__main__':
    main()
