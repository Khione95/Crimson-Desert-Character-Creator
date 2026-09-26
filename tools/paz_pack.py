"""Packs a folder of game files into one archive group (0.paz + 0.pamt), the
format of the game's own groups. DMM mounts a mod that brings such a group
(in a group folder, e.g. 0009/) as a pre-built "standalone overlay" in one
go, instead of adding thousands of loose files one by one.

  0.pamt: u32 checksum of the rest (from byte 12), u32 archive count,
          u32 (the game's value), per archive: index, checksum, size;
          folder names, file names (linked name pieces: u32 parent, u8
          length, text), folders (path hash, name, first file, file count),
          files (name, offset, stored size, size, archive, flags)
  flags: low nibble compression (0 none, 1 per section, 2 LZ4), high
         nibble encryption (3 ChaCha20, key from the file name)
  checksums and path hashes: Jenkins hashlittle, seed 810718.

Folders are written in path order and each folder's files in name order
(the game finds files by name in order).

Usage: python paz_pack.py <folder with game paths> <out group folder>
"""
import os
import struct
import sys

import lz4.block

import game_files

SEED = 810718
UNKNOWN = 1628308018        # the third header value of the game's own indexes
ALIGN = 16


def pack_sections(data):
    """A model (PAR) with each section LZ4 compressed where that is smaller
    ("partial" compression, flags 1)."""
    out = bytearray(data[:0x50])
    pos = 0x50
    for i in range(8):
        stored, size = struct.unpack_from('<II', data, 0x10 + i * 8)
        if not size:
            continue
        raw = data[pos:pos + (stored or size)]
        if stored:
            raw = lz4.block.decompress(raw, uncompressed_size=size)
        pos += stored or size
        packed = lz4.block.compress(raw, store_size=False)
        if len(packed) < len(raw):
            out += packed
            struct.pack_into('<II', out, 0x10 + i * 8, len(packed), size)
        else:
            out += raw
            struct.pack_into('<II', out, 0x10 + i * 8, 0, size)
    return bytes(out)


def encode(path, data):
    """(stored bytes, flags) as the game's own groups keep such a file."""
    name = path.rsplit('/', 1)[1]
    if name.endswith('.pac') and data[:4] == b'PAR ':
        return pack_sections(data), 0x01
    packed = lz4.block.compress(data, store_size=False)
    if path.startswith('character/descriptors/') and name.endswith('.xml'):
        return game_files.chacha(packed, path), 0x32
    if len(packed) < len(data):
        return packed, 0x02
    return data, 0x00


def name_block(strings):
    """Each string as one piece (no parent); offsets by string."""
    block = bytearray()
    offsets = {}
    for s in strings:
        b = s.encode('utf-8')
        if len(b) > 255:
            raise ValueError(f'name too long: {s}')
        offsets[s] = len(block)
        block += struct.pack('<IB', 0xFFFFFFFF, len(b)) + b
    return bytes(block), offsets


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    files = {}
    for root, _, names in os.walk(src):
        for n in names:
            full = os.path.join(root, n)
            rel = os.path.relpath(full, src).replace(os.sep, '/')
            folder, name = rel.rsplit('/', 1)
            files.setdefault(folder, []).append((name, full))

    folders = sorted(files)
    dir_block, dir_off = name_block(folders)
    all_names = sorted({n for f in folders for n, _ in files[f]})
    names_block, name_off = name_block(all_names)

    os.makedirs(out, exist_ok=True)
    paz = bytearray()
    folder_rows, file_rows = [], []
    for folder in folders:
        first = len(file_rows)
        for name, full in sorted(files[folder]):
            data = open(full, 'rb').read()
            stored, flags = encode(f'{folder}/{name}', data)
            paz += b'\0' * (-len(paz) % ALIGN)
            file_rows.append((name_off[name], len(paz), len(stored), len(data), 0, flags))
            paz += stored
        folder_rows.append((game_files._hash_little(folder.encode(), SEED), dir_off[folder], first,
                            len(file_rows) - first))

    open(os.path.join(out, '0.paz'), 'wb').write(paz)
    body = bytearray(struct.pack('<III', 0, 1, UNKNOWN))
    body += struct.pack('<III', 0, game_files._hash_little(bytes(paz), SEED), len(paz))
    body += struct.pack('<I', len(dir_block)) + dir_block
    body += struct.pack('<I', len(names_block)) + names_block
    body += struct.pack('<I', len(folder_rows)) + b''.join(struct.pack('<IIII', *r) for r in folder_rows)
    body += struct.pack('<I', len(file_rows)) + b''.join(struct.pack('<IIIIHH', *r) for r in file_rows)
    struct.pack_into('<I', body, 0, game_files._hash_little(bytes(body[12:]), SEED))
    open(os.path.join(out, '0.pamt'), 'wb').write(body)
    print(f'{len(file_rows)} files in {len(folder_rows)} folders, {len(paz) / 1e6:.1f} MB archive')


if __name__ == '__main__':
    main()
