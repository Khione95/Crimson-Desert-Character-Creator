"""Reads a game archive index (<group>/0.pamt): folders and their files."""
import struct


def _path(block, off):
    parts = []
    while off != 0xFFFFFFFF and off + 5 <= len(block) and len(parts) < 256:
        parent, n = struct.unpack_from('<IB', block, off)
        parts.append(block[off + 5:off + 5 + n].decode('utf-8', 'replace'))
        off = parent
    return ''.join(reversed(parts))


def read(path):
    d = open(path, 'rb').read()
    p = 4
    paz_count = struct.unpack_from('<I', d, p)[0]; p += 8 + 12 * paz_count
    n = struct.unpack_from('<I', d, p)[0]; dirs = d[p + 4:p + 4 + n]; p += 4 + n
    n = struct.unpack_from('<I', d, p)[0]; names = d[p + 4:p + 4 + n]; p += 4 + n
    n = struct.unpack_from('<I', d, p)[0]; p += 4
    folders = [struct.unpack_from('<IIII', d, p + 16 * i) for i in range(n)]; p += 16 * n
    n = struct.unpack_from('<I', d, p)[0]; p += 4
    files = [struct.unpack_from('<IIIIHH', d, p + 20 * i) for i in range(n)]
    out = {}
    for fhash, dir_off, first, count in folders:
        folder = _path(dirs, dir_off).replace(chr(92), '/').strip('/')
        out[folder] = {'hash': fhash, 'files': [(_path(names, f[0]),) + f[1:] for f in files[first:first + count]]}
    return out
