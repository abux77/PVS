#!/usr/bin/env python3
"""Convert OLD column-order PVS files into a NEW folder; originals stay intact.
Run once on old files only: python3 tools/migrate_keypad.py OLD_FOLDER NEW_FOLDER
"""
from pathlib import Path
import argparse

LABELS = '123456789*0#'
OLD_TO_NEW = (0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11)
NEW_TO_OLD = tuple(OLD_TO_NEW.index(i) for i in range(12))
HEADER = '''# Printed keypad:
#   1 2 3 A
#   4 5 6 B
#   7 8 9 C
#   * 0 # D
# Phrase order: 1,2,3,4,5,6,7,8,9,*,0,#.
# A=Sample B=Preset/bank C=Chromatic D=Voice; A+D=USB edit.
# Comments label the NEXT phrase; non-comment line order assigns keys.
# S: ordinary text; P: SAM phonemes.
'''

def convert(name, text):
    if name.startswith('phrases_'):
        phrases = [line for line in text.splitlines() if line.strip() and not line.lstrip().startswith('#')]
        if len(phrases) != 12:
            raise ValueError(f'{name}: expected 12 phrases; fill missing old slots before converting')
        return HEADER + '\n' + '\n\n'.join(
            f'# Key {LABELS[i]}\n{phrases[old]}' for i, old in enumerate(NEW_TO_OLD)) + '\n'
    if name.startswith('locks_') or name == 'presets.txt':
        rows = []
        for line in text.splitlines():
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            first, rest = line.split(',', 1)
            old = int(first.strip()) - 1
            if old not in range(12):
                raise ValueError(f'{name}: invalid old key number {first}')
            new = OLD_TO_NEW[old]
            rows.append((new, LABELS[new] + ',' + rest))
        fields = 'flags,' if name.startswith('locks_') else ''
        return '# key,' + fields + 'speed,pitch,mouth,throat,sample_rate,bit_depth\n' + ''.join(row + '\n' for _, row in sorted(rows))
    if name == 'startup_state.txt':
        lines = [line for line in text.splitlines() if line.strip() and not line.lstrip().startswith('#')]
        if lines:
            fields = lines[0].split(',')
            if len(fields) > 1 and 1 <= int(fields[1]) <= 12:
                fields[1] = str(OLD_TO_NEW[int(fields[1]) - 1] + 1)
            return ','.join(fields) + '\n'
    return text

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    if args.destination.exists():
        parser.error('destination must be a new folder; existing files are never overwritten')
    converted = {p.name: convert(p.name, p.read_text()) for p in args.source.glob('*.txt')}
    args.destination.mkdir(parents=True)
    for name, text in converted.items():
        (args.destination / name).write_text(text)
    print(f'Converted {len(converted)} files into {args.destination}; originals unchanged.')

if __name__ == '__main__':
    main()
