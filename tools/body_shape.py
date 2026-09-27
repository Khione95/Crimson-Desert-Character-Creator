"""Reshapes Damiane's second body (cd_phw_00_nude_02_0001_damian, the "body 4"
option) through its skeleton variation (.pabc), so armor, which hangs on the
same bones, follows the new shape.

Only that body uses cd_phw_00_nude_02_0001.pabc, so the file is changed in
place. Her default body, cd_phw_00_nude_00_0001_damian, takes no variation at
all in play (neither a changed prefab data nor a file named after it had any
effect; tested 2026-09-26), so her appearance file is changed to give her the
second body by default - the two meshes differ by 2 cm at most. Character
Creator keeps her own appearance file when she is played as herself.

  .pabc: "PAR " version (4: 196-byte bone records, 5: 392), 11 bytes, u32
  count, then per bone: u32 name hash and floats - world matrix (16, row
  major, rows are the bone's axes), local matrix (16), local scale (3),
  rotation (4), position (3), shape scale (3) and its inverse (3); version 5
  repeats that block after one more value. The shape scale sizes the skin on
  that bone and is passed on to its children unless the inverse is set (the
  game shrinks Damiane's ears, eyes and teeth this way); it shows in the
  world matrix rows.

Usage: python body_shape.py      writes the DMM mod to ../body_shape_stage
"""
import json
import os
import shutil
import struct
import sys

import numpy as np

import game_files
import pab

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
STAGE = os.path.join(ROOT, 'body_shape_stage')
TITLE = 'Khione Body'
VERSION = '1.0'
MOD = os.path.join(STAGE, TITLE)

APPEARANCE = 'character/appearance/1_pc/2_phw/cd_phw_damian/cd_phw_damian_00000.app_xml'
DEFAULT_BODY = 'cd_phw_00_nude_00_0001_damian'
SHAPED_BODY = 'cd_phw_00_nude_02_0001_damian'
VARIATION = 'character/binary/skeletonvariation/1_pc/2_phw/nude/cd_phw_00_nude_02_0001.pabc'

# Bone: (along the bone, across, across) - the "along" axis is the bone axis
# nearest to up; a round part has the same value three times.
SHAPE = {
    'Bip01 L Chest': (1.32, 1.32, 1.32),        # bust
    'Bip01 R Chest': (1.32, 1.32, 1.32),
    'Bip01 L Chest_sub': (1.08, 1.08, 1.08),
    'Bip01 R Chest_sub': (1.08, 1.08, 1.08),
    'Bip01 Spine1_Sub': (1.0, 0.95, 0.95),      # upper waist
    'Bip01 Spine_Sub': (1.0, 0.88, 0.90),       # waist
    'Bip01 L Hip': (1.18, 1.18, 1.18),          # glutes and hip sides
    'Bip01 R Hip': (1.18, 1.18, 1.18),
    'Bip01 L ThighTwist': (1.0, 1.06, 1.06),    # thighs
    'Bip01 R ThighTwist': (1.0, 1.06, 1.06),
}


def reshape(data, bones):
    size = {0x34: 196, 0x35: 392}[data[4]]
    floats = (size - 4) // 4
    blocks = (0,) if size == 196 else (0, 49)
    count = struct.unpack_from('<I', data, 16)[0]
    by_name = {b['name']: b['hash'] for b in bones}
    wanted = {by_name[n]: s for n, s in SHAPE.items()}
    out = bytearray(data)
    done = set()
    for i in range(count):
        at = 20 + size * i
        h = struct.unpack_from('<I', out, at)[0]
        if h not in wanted:
            continue
        f = np.array(struct.unpack_from(f'<{floats}f', out, at + 4), dtype=np.float64)
        axes = f[0:16].reshape(4, 4)[:3, :3]
        axes = axes / np.linalg.norm(axes, axis=1, keepdims=True)
        along = int(np.argmax(np.abs(axes[:, 1])))
        across = [a for a in range(3) if a != along]
        s = np.ones(3)
        s[along], s[across[0]], s[across[1]] = wanted[h]
        for b in blocks:
            world = f[b:b + 16].reshape(4, 4)
            world[:3, :3] *= s[:, None]
            f[b:b + 16] = world.ravel()
            f[b + 42:b + 45] *= s
            f[b + 45:b + 48] /= s          # children keep their own size
        struct.pack_into(f'<{floats}f', out, at + 4, *f.astype(np.float32))
        done.add(h)
    missing = [n for n in SHAPE if by_name[n] not in done]
    if missing:
        raise SystemExit(f'not in the skeleton variation: {missing}')
    return bytes(out)


def main():
    _, bones, _ = pab.read_pab(game_files.read(next(game_files.entries(r'1_pc/2_phw/phw_01\.pab$'))))
    source = next(game_files.entries('^' + VARIATION.replace('.', r'\.') + '$'))

    # Loose files at their game paths (two small files; DMM mounts them as
    # an overlay): easier to look into than an archive.
    shutil.rmtree(MOD, ignore_errors=True)

    target = os.path.join(MOD, VARIATION)
    os.makedirs(os.path.dirname(target))
    open(target, 'wb').write(reshape(game_files.read(source), bones))

    appearance = next(game_files.entries('^' + APPEARANCE.replace('.', r'\.') + '$'))
    raw = game_files.read(appearance)
    old = f'Prefab Name="{DEFAULT_BODY}"'.encode()
    if old not in raw:
        raise SystemExit(f'her appearance file no longer names {DEFAULT_BODY}')
    path = os.path.join(MOD, APPEARANCE)
    os.makedirs(os.path.dirname(path))
    open(path, 'wb').write(raw.replace(old, f'Prefab Name="{SHAPED_BODY}"'.encode()))

    json.dump({'modinfo': {'title': TITLE, 'version': VERSION, 'author': 'Khione',
                           'description': "A new shape for Damiane, made with bone edits: every armor and "
                                          "outfit follows it, no armor conversions needed."}},
              open(os.path.join(MOD, 'mod.json'), 'w'), indent=2)
    print('mod:', MOD)


if __name__ == '__main__':
    main()
