"""Reads the game's skeleton files (.pab): bone hash, name, parent and the
local rest transform (scale, rotation quaternion, translation)."""
import struct


def read_pab(data):
    count = struct.unpack_from('<H', data, 0x14)[0]
    bones = []
    pos = 0x16
    for _ in range(count):
        h, n = struct.unpack_from('<IB', data, pos)
        if n == 0 or pos + 5 + n + 300 > len(data):
            break
        name = data[pos + 5:pos + 5 + n].decode('latin-1')
        body = pos + 5 + n
        parent = struct.unpack_from('<i', data, body)[0]
        mats = struct.unpack_from('<64f', data, body + 4)
        tail = struct.unpack_from('<10f', data, body + 260)
        bones.append({'hash': h, 'name': name, 'parent': parent, 'mats': mats,
                      'scale': tail[0:3], 'rot': tail[3:7], 'pos': tail[7:10]})
        pos = body + 300
    return count, bones, pos
