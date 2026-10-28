#!/usr/bin/env python3
"""Symbolize a Magical Blocks crash report (logs/crash-*.txt).

The Windows build links with -Wl,-Map=build-win64/MagicalBlocks.map. The map is
kept LOCAL (it is not part of the release packages). Keep the map from the exact build that
was shipped: offsets only match the same binary.

Usage: tools/symbolize_crash.py crash-YYYYMMDD-HHMMSS.txt [path/to/MagicalBlocks.map]
Every "MagicalBlocks.exe+0xOFFSET" in the report gets "<symbol>+0x.. (<object>)" appended.
"""
import bisect
import os
import re
import sys

SYM_RE = re.compile(r'^([0-9a-fA-F]{8,16})\s+([0-9a-fA-F]+)\s+(\d+)\s{9,}(\S.*)$')
OBJ_RE = re.compile(r'^([0-9a-fA-F]{8,16})\s+([0-9a-fA-F]+)\s+(\d+)\s{9}(\S.*)$')
REF_RE = re.compile(r'(MagicalBlocks\.exe)\+0x([0-9a-fA-F]+)')


def load_map(path):
    syms = {}
    obj = ''
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            m = SYM_RE.match(line)
            if m:
                name = m.group(4).strip()
                # Object lines ("file.o:(.text)") have fewer leading spaces than symbol lines.
                if ':(' in name and name.endswith(')'):
                    obj = os.path.basename(name.split(':(')[0])
                    continue
                addr = int(m.group(1), 16)
                syms.setdefault(addr, (name, obj))
                continue
            m = OBJ_RE.match(line)
            if m and ':(' in m.group(4):
                obj = os.path.basename(m.group(4).split(':(')[0])
    addrs = sorted(syms)
    return addrs, [syms[a] for a in addrs]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    report = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    map_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '..', 'build-win64', 'MagicalBlocks.map')
    addrs, info = load_map(map_path)
    if not addrs:
        print(f'no symbols found in {map_path}', file=sys.stderr)
        return 1

    def resolve(match):
        off = int(match.group(2), 16)
        i = bisect.bisect_right(addrs, off) - 1
        if i < 0:
            return match.group(0)
        name, obj = info[i]
        return f'{match.group(0)}  [{name}+0x{off - addrs[i]:x}{" (" + obj + ")" if obj else ""}]'

    with open(report, encoding='utf-8', errors='replace') as f:
        for line in f:
            print(REF_RE.sub(resolve, line.rstrip('\n')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
