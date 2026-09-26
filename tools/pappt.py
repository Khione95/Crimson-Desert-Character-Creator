"""Reads and writes the game's part prefab table (character/bin__/partprefabtable.pappt):
every part prefab an item or a conditional part rule can use.

  u64 0, u32 count, then per part:
    str name, str folder, str sockets file, u8 flag, str body tag, u8,
    u8 slot count, per slot: str slot name, u8
  then u32 count and (str name, str folder) per head/hair set.
A str is a length byte (with the terminating zero) and the text.
"""
import struct


def _str(d, p):
    n = d[p]
    return d[p + 1:p + n].rstrip(b'\0').decode('latin-1'), p + 1 + n


def records(d):
    """(name, start, end) of every part record, and where the table ends."""
    count = struct.unpack_from('<I', d, 8)[0]
    p = 12
    out = []
    for _ in range(count):
        start = p
        name, p = _str(d, p)
        _, p = _str(d, p)          # folder
        _, p = _str(d, p)          # sockets
        p += 1
        _, p = _str(d, p)          # body tag
        p += 1
        slots = d[p]
        p += 1
        for _ in range(slots):
            _, p = _str(d, p)
            p += 1
        out.append((name, start, p))
    return out, p


def add_copies(d, copies):
    """copies: {existing name: new name of the same length}. Each new part is
    a copy of the existing record under the new name, added to the table."""
    recs, end = records(d)
    by_name = {n: (s, e) for n, s, e in recs}
    new = bytearray()
    added = 0
    for old, name in copies.items():
        if old not in by_name or name in by_name or len(old) != len(name):
            continue
        s, e = by_name[old]
        rec = bytearray(d[s:e])
        rec[1:1 + len(name)] = name.encode('latin-1')
        new += rec
        added += 1
    out = bytearray(d[:end]) + new + d[end:]
    struct.pack_into('<I', out, 8, len(recs) + added)
    return bytes(out), added
