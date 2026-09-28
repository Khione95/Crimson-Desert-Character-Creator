"""Dye slots for Female Armor Fit's reshaped models.

The game's dye slots per model (gamedata/binarystaticinfo__/bin/
partprefabdyeslotinfo.staticinfoheader / .staticinfobody):

  header: u16 count, then (u32 key, u32 offset in the body) per entry
  body:   per entry u32 key, ... the part's name, its dye slots (materials),
          and the model path the entry belongs to
  key:    hashlittle(model path, 810718), as the archive paths are hashed

The reshaped models are copies named zz_... (same length as cd_...), so they
had no entry and women could not dye the armor. Each gets a copy of its
original's entry, keyed and pointing at the copy.

Usage: python armor_dyes.py <Female Armor Fit group folder> <out folder>
       (writes <out>/gamedata/binarystaticinfo__/bin/partprefabdyeslotinfo.*)
"""
import os
import struct
import sys

import game_files
from paz_add import read_group

SEED = 810718
BASE = 'gamedata/binarystaticinfo__/bin/partprefabdyeslotinfo'


def entries(header, body):
    count, = struct.unpack_from('<H', header, 0)
    rows = [struct.unpack_from('<II', header, 2 + i * 8) for i in range(count)]
    ends = sorted({o for _, o in rows} | {len(body)})
    following = {o: ends[ends.index(o) + 1] for o in ends[:-1]}
    return {key: body[offset:following[offset]] for key, offset in rows}, rows


def build(group, out):
    header = game_files.read(next(game_files.entries('^' + BASE + r'\.staticinfoheader$')))
    body = game_files.read(next(game_files.entries('^' + BASE + r'\.staticinfobody$')))
    by_key, rows = entries(header, body)

    models = sorted(p for p in read_group(group) if p.startswith('character/model/') and
                    p.rsplit('/', 1)[1].startswith('zz_') and p.endswith('.pac'))
    new_body = bytearray(body)
    new_rows = list(rows)
    added = missing = 0
    for copy in models:
        folder, name = copy.rsplit('/', 1)
        original = f'{folder}/cd_{name[3:]}'
        key = game_files._hash_little(original.encode(), SEED)
        record = by_key.get(key)
        if record is None or original.encode() not in record:
            missing += 1        # a model without dye slots (not dyeable in the game either)
            continue
        copy_key = game_files._hash_little(copy.encode(), SEED)
        if copy_key in by_key:
            continue
        clone = bytearray(record.replace(original.encode(), copy.encode()))
        struct.pack_into('<I', clone, 0, copy_key)
        # The part's name (u32 length and text after the key and 9 bytes)
        # becomes the copy's (zz_... as the copied prefab is named).
        name_len, = struct.unpack_from('<I', clone, 13)
        if clone[17:20] == b'cd_':
            clone[17:20] = b'zz_'
        new_rows.append((copy_key, len(new_body)))
        new_body += clone
        added += 1

    if len(new_rows) > 0xFFFF:
        raise SystemExit(f'{len(new_rows)} entries, more than the header can count')
    new_header = struct.pack('<H', len(new_rows)) + b''.join(struct.pack('<II', k, o) for k, o in new_rows)
    target = os.path.join(out, *BASE.split('/'))
    os.makedirs(os.path.dirname(target), exist_ok=True)
    open(target + '.staticinfoheader', 'wb').write(new_header)
    open(target + '.staticinfobody', 'wb').write(new_body)
    print(f'{len(models)} reshaped models: {added} dye entries added, {missing} without dye slots')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    build(sys.argv[1], sys.argv[2])
