"""Reads files out of the game's archives, using the index Crimson Browser
Sharp keeps (cache/archive_index.json: archive, .paz number, offset, sizes,
compression and encryption of every file).

Compression: none, LZ4 whole-file or "partial"; encryption: none or ChaCha20
(key from the file name, as Crimson Browser Sharp does it).

Usage:
  python game_files.py find <regex>            lists matching paths
  python game_files.py get <regex> <out dir>   extracts matching files
"""
import mmap
import os
import re
import struct
import sys

import lz4.block

GAME = r'E:\SteamLibrary\steamapps\common\Crimson Desert'
INDEX = os.path.join(os.path.expanduser('~'), 'Desktop', r'Crimson Browser Sharp\cache\archive_index.json')

ENTRY = re.compile(rb'\{"ArchiveId":"(\d+)","EntryIndex":\d+,"RelativePath":"([^"]+)"[^{}]*?"PackedSize":(\d+),'
                   rb'"UnpackedSize":(\d+),"Offset":(\d+),"PazIndex":(\d+),"Compression":"(\w+)","Encryption":"(\w+)"')


def entries(pattern):
    rx = re.compile(pattern, re.I)
    with open(INDEX, 'rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
        for e in ENTRY.finditer(m):
            path = e.group(2).decode('utf-8', 'replace').replace('\\\\', '/')
            if rx.search(path):
                yield {'archive': e.group(1).decode(), 'path': path, 'packed': int(e.group(3)),
                       'size': int(e.group(4)), 'offset': int(e.group(5)), 'paz': int(e.group(6)),
                       'compression': e.group(7).decode(), 'encryption': e.group(8).decode()}


M32 = 0xFFFFFFFF


def _rol(v, n):
    return ((v << n) | (v >> (32 - n))) & M32


def _hash_little(data, init):
    a = b = c = (0xDEADBEEF + len(data) + init) & M32
    i, n = 0, len(data)
    while n > 12:
        a = (a + struct.unpack_from('<I', data, i)[0]) & M32
        b = (b + struct.unpack_from('<I', data, i + 4)[0]) & M32
        c = (c + struct.unpack_from('<I', data, i + 8)[0]) & M32
        a = (a - c) & M32; a ^= _rol(c, 4); c = (c + b) & M32
        b = (b - a) & M32; b ^= _rol(a, 6); a = (a + c) & M32
        c = (c - b) & M32; c ^= _rol(b, 8); b = (b + a) & M32
        a = (a - c) & M32; a ^= _rol(c, 16); c = (c + b) & M32
        b = (b - a) & M32; b ^= _rol(a, 19); a = (a + c) & M32
        c = (c - b) & M32; c ^= _rol(b, 4); b = (b + a) & M32
        i += 12; n -= 12
    tail = data[i:] + bytes(12)
    if n == 0:
        return c
    if n >= 9: c = (c + struct.unpack_from('<I', tail, 8)[0]) & M32
    if n >= 5: b = (b + struct.unpack_from('<I', tail, 4)[0]) & M32
    a = (a + struct.unpack_from('<I', tail, 0)[0]) & M32
    c ^= b; c = (c - _rol(b, 14)) & M32
    a ^= c; a = (a - _rol(c, 11)) & M32
    b ^= a; b = (b - _rol(a, 25)) & M32
    c ^= b; c = (c - _rol(b, 16)) & M32
    a ^= c; a = (a - _rol(c, 4)) & M32
    b ^= a; b = (b - _rol(a, 14)) & M32
    c ^= b; c = (c - _rol(b, 24)) & M32
    return c


def chacha(data, filename):
    import numpy as np
    h = _hash_little(os.path.basename(filename).lower().encode(), 810718)
    deltas = [0, 168430090, 202116108, 101058054, 235802126, 168430090, 101058054, 33686018]
    key = [(h ^ 0x60616263) ^ d for d in deltas]
    state = [1634760805, 857760878, 2036477234, 1797285236] + key + [h, h, h, h]
    out = bytearray(len(data))
    for block in range(0, len(data), 64):
        x = list(state)
        for _ in range(10):
            for a, b, c, d in ((0, 4, 8, 12), (1, 5, 9, 13), (2, 6, 10, 14), (3, 7, 11, 15),
                               (0, 5, 10, 15), (1, 6, 11, 12), (2, 7, 8, 13), (3, 4, 9, 14)):
                x[a] = (x[a] + x[b]) & M32; x[d] = _rol(x[d] ^ x[a], 16)
                x[c] = (x[c] + x[d]) & M32; x[b] = _rol(x[b] ^ x[c], 12)
                x[a] = (x[a] + x[b]) & M32; x[d] = _rol(x[d] ^ x[a], 8)
                x[c] = (x[c] + x[d]) & M32; x[b] = _rol(x[b] ^ x[c], 7)
        ks = struct.pack('<16I', *[(x[i] + state[i]) & M32 for i in range(16)])
        chunk = data[block:block + 64]
        out[block:block + len(chunk)] = bytes(p ^ k for p, k in zip(chunk, ks))
        state[12] = (state[12] + 1) & M32
    return bytes(out)


def read(entry):
    with open(os.path.join(GAME, entry['archive'], f'{entry["paz"]}.paz'), 'rb') as f:
        f.seek(entry['offset'])
        data = f.read(entry['packed'])
    if entry['encryption'] == 'ChaCha20':
        data = chacha(data, entry['path'])
    elif entry['encryption'] != 'None':
        raise ValueError(f'{entry["path"]} is encrypted ({entry["encryption"]})')
    if entry['compression'] == 'None' or entry['packed'] == entry['size']:
        return data
    if entry['compression'] == 'Partial':
        return unpack_sections(data)
    return lz4.block.decompress(data, uncompressed_size=entry['size'])


def unpack_sections(data):
    """"Partial" files (PAR models): a table of 8 sections at 0x10, each
    (stored size, size); a section with a stored size is LZ4 compressed.
    Returned with every section stored plain (stored size 0)."""
    out = bytearray(data[:0x50])
    pos = 0x50
    for i in range(8):
        stored, size = struct.unpack_from('<II', data, 0x10 + i * 8)
        if not size:
            continue
        if stored:
            out += lz4.block.decompress(data[pos:pos + stored], uncompressed_size=size)
            pos += stored
        else:
            out += data[pos:pos + size]
            pos += size
        struct.pack_into('<I', out, 0x10 + i * 8, 0)
    return bytes(out)


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'find':
        for e in entries(sys.argv[2]):
            print(f'{e["archive"]}/{e["paz"]} {e["compression"]:7} {e["encryption"]:8} {e["size"]:>9} {e["path"]}')
    elif len(sys.argv) >= 4 and sys.argv[1] == 'get':
        for e in entries(sys.argv[2]):
            out = os.path.join(sys.argv[3], e['path'])
            os.makedirs(os.path.dirname(out), exist_ok=True)
            try:
                open(out, 'wb').write(read(e))
                print('extracted', e['path'])
            except Exception as x:
                print('skipped', e['path'], '-', x)
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
