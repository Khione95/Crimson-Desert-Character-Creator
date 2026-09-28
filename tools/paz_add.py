"""Adds (or replaces) files in a group made by paz_pack.py (0.paz + 0.pamt),
keeping the files already in it as they are stored: the group is written
again with the new files among the old ones, in the order the game needs
(folders by path, each folder's files by name).

Used to give an already released package (Female Armor Fit) files it did
not have (the dye slots of its reshaped models, see armor_dyes.py).

Usage: python paz_add.py <group folder> <folder with game paths>
"""
import os
import struct
import sys

import game_files
from paz_pack import ALIGN, SEED, UNKNOWN, encode, name_block
from paz_replace import read_names


def read_group(group):
    """{game path: (stored bytes, size, flags)}"""
    body = open(os.path.join(group, '0.pamt'), 'rb').read()
    paz = open(os.path.join(group, '0.paz'), 'rb').read()
    pos = 24
    size, = struct.unpack_from('<I', body, pos)
    folders = read_names(body[pos + 4:pos + 4 + size])
    pos += 4 + size
    size, = struct.unpack_from('<I', body, pos)
    files = read_names(body[pos + 4:pos + 4 + size])
    pos += 4 + size
    count, = struct.unpack_from('<I', body, pos)
    folder_rows = [struct.unpack_from('<IIII', body, pos + 4 + i * 16) for i in range(count)]
    table = pos + 4 + count * 16 + 4
    out = {}
    for _, name_off, first, n in folder_rows:
        for i in range(first, first + n):
            name, offset, stored, real, _, flags = struct.unpack_from('<IIIIHH', body, table + i * 20)
            out[f'{folders[name_off]}/{files[name]}'] = (paz[offset:offset + stored], real, flags)
    return out


def decode(path, stored, size, flags):
    """The file's contents (for files the tools need to read back)."""
    import lz4.block
    data = stored
    if flags >> 4 == 3:
        data = game_files.chacha(data, path)
    if flags & 0xF == 2:
        data = lz4.block.decompress(data, uncompressed_size=size)
    return data


def write_group(group, entries):
    by_folder = {}
    for path, value in entries.items():
        folder, name = path.rsplit('/', 1)
        by_folder.setdefault(folder, []).append((name, value))

    folders = sorted(by_folder)
    dir_block, dir_off = name_block(folders)
    all_names = sorted({n for f in folders for n, _ in by_folder[f]})
    names_block, name_off = name_block(all_names)

    paz = bytearray()
    folder_rows, file_rows = [], []
    for folder in folders:
        first = len(file_rows)
        for name, (stored, size, flags) in sorted(by_folder[folder]):
            paz += b'\0' * (-len(paz) % ALIGN)
            file_rows.append((name_off[name], len(paz), len(stored), size, 0, flags))
            paz += stored
        folder_rows.append((game_files._hash_little(folder.encode(), SEED), dir_off[folder], first,
                            len(file_rows) - first))

    open(os.path.join(group, '0.paz'), 'wb').write(paz)
    body = bytearray(struct.pack('<III', 0, 1, UNKNOWN))
    body += struct.pack('<III', 0, game_files._hash_little(bytes(paz), SEED), len(paz))
    body += struct.pack('<I', len(dir_block)) + dir_block
    body += struct.pack('<I', len(names_block)) + names_block
    body += struct.pack('<I', len(folder_rows)) + b''.join(struct.pack('<IIII', *r) for r in folder_rows)
    body += struct.pack('<I', len(file_rows)) + b''.join(struct.pack('<IIIIHH', *r) for r in file_rows)
    struct.pack_into('<I', body, 0, game_files._hash_little(bytes(body[12:]), SEED))
    open(os.path.join(group, '0.pamt'), 'wb').write(body)
    print(f'{len(file_rows)} files in {len(folder_rows)} folders, {len(paz) / 1e6:.1f} MB archive')


def add(group, src):
    entries = read_group(group)
    added = 0
    for root, _, names in os.walk(src):
        for n in names:
            full = os.path.join(root, n)
            path = os.path.relpath(full, src).replace(os.sep, '/')
            data = open(full, 'rb').read()
            stored, flags = encode(path, data)
            entries[path] = (stored, len(data), flags)
            added += 1
    write_group(group, entries)
    print(f'{added} files added or replaced')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    add(sys.argv[1], sys.argv[2])
