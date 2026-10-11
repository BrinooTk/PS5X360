"""Check catalogue completeness, printf arguments and the launcher's glyph range.

Run from any directory with Python 3. No PS5 SDK or third-party packages needed.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STRING = r'"(?:\\.|[^"\\])*"'
ENTRY = re.compile(r'^\s*\{\s*(' + STRING + r')\s*,\s*(' + STRING + r')\s*,\s*('
                   + STRING + r')\s*,\s*(' + STRING + r')\s*\},\s*$')
FORMAT = re.compile(r'%(?:%|(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn])')


def main():
    text = (ROOT / 'include/xbox360ps5/i18n.hpp').read_text(encoding='utf-8')
    table = text.split('static constexpr Entry entries[] = {', 1)[1].split('\n  };', 1)[0]
    entries = []
    errors = []
    for line in table.splitlines():
        if not line.strip():
            continue
        match = ENTRY.fullmatch(line)
        if not match:
            errors.append('Incomplete or malformed catalogue entry: ' + line.strip())
            continue
        entries.append(tuple(json.loads(value) for value in match.groups()))
    assert entries, 'Empty translation catalogue'
    seen = {}
    for pt, en, es, fr in entries:
        if not all((pt, en, es, fr)):
            errors.append('Empty translation: ' + pt)
        if pt in seen and seen[pt] != (en, es, fr):
            errors.append('Conflicting duplicate key: ' + pt)
        seen[pt] = (en, es, fr)
        # Argument order and type must match: Tr() is passed directly to snprintf.
        if FORMAT.findall(fr) != FORMAT.findall(pt):
            errors.append('French printf arguments differ: ' + pt)
        for pattern in (r'^\s*', r'\s*$'):
            if re.search(pattern, pt)[0] != re.search(pattern, fr)[0]:
                errors.append('French concatenation spacing differs: ' + pt)
        for char in fr:
            code = ord(char)
            if not (0x20 <= code <= 0x24F or 0x2000 <= code <= 0x206F):
                errors.append(f'French character outside the font atlas: U+{code:04X} in {pt}')
    # Direct Tr calls must be covered. Dynamic labels/choices need contextual review too.
    for folder in ('src', 'platform'):
        for path in (ROOT / folder).rglob('*.cpp'):
            for match in re.finditer(r'\bTr\((' + STRING + r')\)', path.read_text(encoding='utf-8')):
                key = json.loads(match[1])
                if key not in seen:
                    errors.append(f'Missing catalogue key in {path.relative_to(ROOT)}: {key}')
    assert not errors, '\n'.join(errors)
    duplicates = len(entries) - len(seen)
    print(f'PASS: {len(entries)} entries ({len(seen)} unique keys, {duplicates} matching duplicates); '
          'French text, printf arguments, concatenated spacing, glyph ranges and literal Tr calls')


if __name__ == '__main__':
    main()
