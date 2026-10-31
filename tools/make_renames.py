"""Writes game_data/part_renames.json (names.py): the parts of an old part
table that the game's current one has under another name - the "cd_" prefix
dropped, or (a few women's hair parts) "_itemhair" added.

Usage: python make_renames.py <old partprefabtable.pappt>
       (an old table: e.g. a Character Creator package built before the update)
"""
import json
import sys

import game_files
import pappt
from names import RENAMES_FILE


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    old = [n for n, _, _ in pappt.records(open(sys.argv[1], 'rb').read())[0]]
    new = {n for n, _, _ in pappt.records(game_files.read(next(game_files.entries(r'^character/bin__/partprefabtable\.pappt$'))))[0]}
    out = {}
    for n in old:
        if n in new or not n.startswith('cd_'):
            continue
        for candidate in (n[3:], n[3:] + '_itemhair', n + '_itemhair'):
            if candidate in new:
                out[n] = candidate
                break
    json.dump(out, open(RENAMES_FILE, 'w', encoding='utf-8'), indent=0, sort_keys=True)
    print(f'{len(out)} renamed parts -> {RENAMES_FILE}')


if __name__ == '__main__':
    main()
