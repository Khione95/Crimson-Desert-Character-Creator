"""Replaces one file in a group made by paz_pack.py (0.paz + 0.pamt), without
rebuilding the rest: the new file is added at the end of 0.paz and its row in
0.pamt points there (the old bytes stay unused).

Used to give an already released package (Female Armor Fit) the part table
Character Creator Eyes ships, so both packages bring the same table.

Usage: python paz_replace.py <group folder> <game path> <new file>
       e.g. paz_replace.py ".../Female Armor Fit/0036" character/bin__/partprefabtable.pappt table.pappt
"""
import os
import struct
import sys

import game_files
from paz_pack import ALIGN, SEED, encode


def read_names(block):
    """Offset -> text for a name block of single pieces (paz_pack.name_block)."""
    names, pos = {}, 0
    while pos < len(block):
        parent, length = struct.unpack_from('<IB', block, pos)
        names[pos] = block[pos + 5:pos + 5 + length].decode('utf-8')
        pos += 5 + length
    return names


def replace(group, game_path, data):
    pamt_path, paz_path = os.path.join(group, '0.pamt'), os.path.join(group, '0.paz')
    body = bytearray(open(pamt_path, 'rb').read())
    if struct.unpack_from('<I', body, 4)[0] != 1:
        raise SystemExit('only groups with one archive (as paz_pack.py makes them)')
    pos = 24
    size, = struct.unpack_from('<I', body, pos)
    folders = read_names(body[pos + 4:pos + 4 + size])
    pos += 4 + size
    size, = struct.unpack_from('<I', body, pos)
    files = read_names(body[pos + 4:pos + 4 + size])
    pos += 4 + size
    count, = struct.unpack_from('<I', body, pos)
    folder_rows = [struct.unpack_from('<IIII', body, pos + 4 + i * 16) for i in range(count)]
    pos += 4 + count * 16
    file_table = pos + 4

    folder, name = game_path.rsplit('/', 1)
    row = None
    for _, name_off, first, n in folder_rows:
        if folders[name_off] == folder:
            for i in range(first, first + n):
                at = file_table + i * 20
                if files[struct.unpack_from('<I', body, at)[0]] == name:
                    row = at
    if row is None:
        raise SystemExit(f'{game_path} is not in {group}')

    paz = bytearray(open(paz_path, 'rb').read())
    paz += b'\0' * (-len(paz) % ALIGN)
    stored, flags = encode(game_path, data)
    name_off, _, _, _, archive, _ = struct.unpack_from('<IIIIHH', body, row)
    struct.pack_into('<IIIIHH', body, row, name_off, len(paz), len(stored), len(data), archive, flags)
    paz += stored
    open(paz_path, 'wb').write(paz)
    struct.pack_into('<II', body, 16, game_files._hash_little(bytes(paz), SEED), len(paz))
    struct.pack_into('<I', body, 0, game_files._hash_little(bytes(body[12:]), SEED))
    open(pamt_path, 'wb').write(body)
    print(f'{game_path} replaced ({len(data)} bytes)')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    replace(sys.argv[1], sys.argv[2], open(sys.argv[3], 'rb').read())
