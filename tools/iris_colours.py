"""The eye colours the game has no texture for: it has no green iris at all and
barely a blue one. Each is the game's most detailed iris (phm_00_eye_iris_0003,
a light one) tinted by its brightness. DXT5 keeps a block's colours as two
RGB565 endpoints, so only those change: the detail stays as the game has it.

Written plain (no packed mips) to character/texture/ in the package. The names
have no "eye" in them: DMM sends loose files named so down its own route, where
the game fails to load them (private_eyes.py).

Usage: python iris_colours.py <package 0009 dir>
"""
import os
import struct
import sys

import lz4.block

import game_files

SOURCE = 'character/texture/phm_00_eye_iris_0003.dds'
# name in character/texture/: the colour of the iris at full brightness
COLOURS = {'cc_iris_green.dds': (86, 190, 92), 'cc_iris_blue.dds': (82, 148, 240)}


def raw(entry):
    with open(os.path.join(game_files.GAME, entry['archive'], f'{entry["paz"]}.paz'), 'rb') as f:
        f.seek(entry['offset'])
        data = f.read(entry['packed'])
    return game_files.chacha(data, entry['path']) if entry['encryption'] == 'ChaCha20' else data


def plain_mips(data):
    """The header (its packed sizes cleared) and the mips unpacked."""
    if data[84:88] != b'DXT5':
        raise SystemExit('the source iris is not DXT5')
    height, width = struct.unpack_from('<II', data, 12)
    mips = max(1, struct.unpack_from('<I', data, 28)[0])
    stored = struct.unpack_from('<11I', data, 32)
    pos = 128
    body = bytearray()
    for mip in range(mips):
        size = max(1, (max(1, width >> mip) + 3) // 4) * max(1, (max(1, height >> mip) + 3) // 4) * 16
        packed = stored[mip] if mip < 11 and stored[mip] else size
        chunk = data[pos:pos + packed]
        if len(chunk) != packed:
            raise SystemExit(f'the source iris ends in mip {mip}')
        body += chunk if packed == size else lz4.block.decompress(chunk, uncompressed_size=size)
        pos += packed
    header = bytearray(data[:128])
    for i in range(11):
        struct.pack_into('<I', header, 32 + i * 4, 0)
    return bytes(header), body


def tinted(endpoint, colour):
    r, g, b = (endpoint >> 11 & 31) * 255 // 31, (endpoint >> 5 & 63) * 255 // 63, (endpoint & 31) * 255 // 31
    # Lifted a little: the iris is darker than the colours it should give.
    light = min(1.0, (0.30 * r + 0.59 * g + 0.11 * b) / 255 * 1.35)
    r, g, b = [int(c * light) for c in colour]
    return (r * 31 // 255) << 11 | (g * 63 // 255) << 5 | b * 31 // 255


def tint(body, colour):
    out = bytearray(body)
    # A block: 8 bytes of alpha, then the two colour endpoints.
    for at in range(8, len(out), 16):
        a, b = struct.unpack_from('<HH', out, at)
        struct.pack_into('<HH', out, at, tinted(a, colour), tinted(b, colour))
    return bytes(out)


def build(out_dir):
    header, body = plain_mips(raw(next(game_files.entries('^' + SOURCE + '$'))))
    folder = os.path.join(out_dir, 'character', 'texture')
    os.makedirs(folder, exist_ok=True)
    for name, colour in COLOURS.items():
        with open(os.path.join(folder, name), 'wb') as f:
            f.write(header + tint(body, colour))
    print(f'{len(COLOURS)} iris colours of their own ({len(header) + len(body)} bytes each)')


if __name__ == '__main__':
    build(sys.argv[1])
