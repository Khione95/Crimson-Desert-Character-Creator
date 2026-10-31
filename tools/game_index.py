"""Builds the archive index game_files.py reads (the format Crimson Browser
Sharp's cache/archive_index.json has) from the game's own group indexes, so
the tools can read the game again right after a game update.

  <group>/0.pamt: u32 ?, u32 archive count, u32 ?, 12 bytes per archive,
  then the folder and file name blocks (paz_replace.read_names), u32 folder
  count and 16 bytes per folder (?, name, first file, file count), u32 ?, and
  u32 file count and 20 bytes per file (name, offset, stored size, size,
  u16 .paz number, u8 flags - low 4 bits the compression: 0 none,
  1 sections, 2 LZ4; high 4 the encryption: 3 ChaCha20 - and u8 the
  extension), then the extensions (u32 size, zero-ended, the first empty).
  Names are pieces: a varint distance back to the parent piece (0: none),
  a length byte, the text. (Layout of the 2026-10-30 update.)

Usage: python game_index.py [output]   (default: cache/archive_index.json
next to the tools; game_files.py uses it when it is there)
"""
import json
import os
import struct
import sys


HERE = os.path.dirname(os.path.abspath(__file__))
GAME = r'E:\SteamLibrary\steamapps\common\Crimson Desert'
OUT = os.path.join(HERE, '..', 'cache', 'archive_index.json')
COMPRESSION = {0: 'None', 1: 'Partial', 2: 'Lz4'}
ENCRYPTION = {0: 'None', 3: 'ChaCha20'}


def varint(block, pos):
    value = shift = 0
    while True:
        b = block[pos]
        pos += 1
        value |= (b & 0x7F) << shift
        if not b & 0x80:
            return value, pos
        shift += 7


def read_names(block):
    """Offset -> full name. Each piece: the distance back to its parent piece
    (a varint, 0: none), a length byte and the text; a name is its parent's
    name and its own text. (Before the 2026-10-30 update: a u32 parent offset,
    FFFFFFFF for none.)"""
    pieces, pos = {}, 0
    while pos < len(block):
        start = pos
        back, pos = varint(block, pos)
        length = block[pos]
        pieces[start] = (start - back if back else None, block[pos + 1:pos + 1 + length])
        pos += 1 + length
    names = {}
    for off in pieces:
        text, at = b'', off
        for _ in range(256):
            parent, piece = pieces[at]
            text = piece + text
            if parent is None or parent not in pieces:
                break
            at = parent
        names[off] = text.decode('utf-8', 'replace')
    return names


def group_entries(group):
    body = open(os.path.join(GAME, group, '0.pamt'), 'rb').read()
    archives, = struct.unpack_from('<I', body, 4)
    pos = 12 + archives * 12
    size, = struct.unpack_from('<I', body, pos)
    folders = read_names(body[pos + 4:pos + 4 + size])
    pos += 4 + size
    size, = struct.unpack_from('<I', body, pos)
    files = read_names(body[pos + 4:pos + 4 + size])
    pos += 4 + size
    count, = struct.unpack_from('<I', body, pos)
    rows = [struct.unpack_from('<IIII', body, pos + 4 + i * 16) for i in range(count)]
    table = pos + 4 + count * 16
    file_count, = struct.unpack_from('<I', body, table)
    # The extensions: u32 size, then zero-ended names, the first one empty.
    end = table + 4 + file_count * 20
    size, = struct.unpack_from('<I', body, end)
    extensions = body[end + 4:end + 4 + size].split(b'\0')
    for _, folder, first, n in rows:
        for i in range(first, first + n):
            name, offset, stored, real, paz, flags, ext = struct.unpack_from('<IIIIHBB', body, table + 4 + i * 20)
            suffix = extensions[ext].decode('ascii', 'replace') if ext < len(extensions) else ''
            # A file in the root folder: no folder name (FFFFFFFF).
            path = f'{folders[folder]}/{files[name]}' if folder in folders else files[name]
            yield i, path + suffix, offset, stored, real, paz, flags


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else OUT
    os.makedirs(os.path.dirname(out), exist_ok=True)
    count = 0
    with open(out, 'w', encoding='utf-8') as f:
        f.write('{"Version":2,"Entries":[\n')
        for group in sorted(os.listdir(GAME)):
            if not (len(group) == 4 and group.isdigit() and os.path.exists(os.path.join(GAME, group, '0.pamt'))):
                continue
            for i, path, offset, stored, real, paz, flags in group_entries(group):
                f.write(json.dumps({
                    'ArchiveId': group, 'EntryIndex': i, 'RelativePath': path,
                    'PackedSize': stored, 'UnpackedSize': real, 'Offset': offset, 'PazIndex': paz,
                    'Compression': COMPRESSION.get(flags & 0xF, f'Unknown{flags & 0xF}'),
                    'Encryption': ENCRYPTION.get(flags >> 4, f'Unknown{flags >> 4}')}, separators=(',', ':')) + ',\n')
                count += 1
        f.write('{}]}\n')
    print(f'{count} files indexed -> {out}')


if __name__ == '__main__':
    main()
