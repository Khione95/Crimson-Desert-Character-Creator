"""Adds the other gender's bones to the player skeletons, so armor made for
one body follows the other.

An armor model lists the bones its vertices follow by name hash. Male and
female skeletons name some bones differently (Finger1 Joint / Finger1 joint,
Elbow_In / Elbow In), have different forearm twist bones (ForeTwist1-2 /
ForeTwist01-03), and each has a few of its own (beard, chest helpers). A bone
an armor needs that the body lacks leaves those vertices behind: stretched,
wobbly sleeves.

Each missing bone is appended to the skeleton as a helper:
  - with an equivalent on this body: a child of it with no offset, so it moves
    exactly with it;
  - without one: under its usual parent, at its usual offset.
Existing bones keep their numbers, so animations are unaffected.

Usage: python skeleton_helpers.py <out dir>
"""
import math
import os
import re
import struct
import sys

import numpy as np

import game_files
from pab import read_pab

SKELETONS = {
    'male': 'character/model/1_pc/1_phm/phm_01.pab',
    'female': 'character/model/1_pc/2_phw/phw_01.pab',
}

# Bones armor uses that the other body lacks (found by scanning every player
# armor model; see the lists this script prints).
FOREARM_TWISTS = ('ForeTwist1', 'ForeTwist2', 'ForeTwist01', 'ForeTwist02', 'ForeTwist03')


def normal(name):
    return re.sub(r'[\s_]', '', name).lower()


def world(bone):
    return np.array(bone['mats'][0:16]).reshape(4, 4)


def local_matrix(scale, rot, pos):
    x, y, z, w = rot
    r = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
        [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
        [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)],
    ])
    m = np.identity(4)
    m[0:3, 0:3] = r * np.array(scale)[:, None]
    m[3, 0:3] = pos
    return m


def forearm_share(bones, bone):
    """Where a bone sits along its forearm: 0 at the elbow, 1 at the wrist."""
    side = bone['name'].split()[1]
    by = {b['name']: b for b in bones}
    a = world(by[f'Bip01 {side} Forearm'])[3, :3]
    h = world(by[f'Bip01 {side} Hand'])[3, :3]
    p = world(bone)[3, :3]
    return float(np.dot(p - a, h - a) / np.dot(h - a, h - a))


def equivalent(bone, source, target):
    """The target bone that plays this bone's part, or None."""
    by_normal = {normal(b['name']): b for b in target}
    if normal(bone['name']) in by_normal:
        return by_normal[normal(bone['name'])]
    words = bone['name'].split()
    if len(words) == 3 and words[2] in FOREARM_TWISTS:
        share = forearm_share(source, bone)
        twists = [b for b in target if b['name'].startswith(f'Bip01 {words[1]} ForeTwist')]
        return min(twists, key=lambda b: abs(forearm_share(target, b) - share))
    return None


def add_helpers(data, source):
    count, target, end = read_pab(data)
    names = {b['name'] for b in target}
    index = {b['name']: i for i, b in enumerate(target)}
    tail = data[end:]
    parents2 = list(struct.unpack_from(f'<{count}i', tail, 0))
    flags = list(tail[4 * count:5 * count])
    rest = tail[5 * count:]

    records = []
    report = []
    for bone in source:
        if bone['name'] in names:
            continue
        match = equivalent(bone, source, target)
        if match is not None:
            parent = index[match['name']]
            scale, rot, pos = (1.0, 1.0, 1.0), (0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0)
            flag = flags[parent]
            report.append(f'{bone["name"]} -> follows {match["name"]}')
        else:
            p = bone['parent']
            while p >= 0 and source[p]['name'] not in index:
                p = source[p]['parent']
            if p < 0:
                report.append(f'{bone["name"]} - skipped (no parent)')
                continue
            parent = index[source[p]['name']]
            scale, rot, pos = bone['scale'], bone['rot'], bone['pos']
            flag = 0
            report.append(f'{bone["name"]} -> under {source[p]["name"]}')

        loc = local_matrix(scale, rot, pos)
        w = loc @ world(target[parent])
        mats = np.concatenate([w.ravel(), np.linalg.inv(w).ravel(), loc.ravel(), np.linalg.inv(loc).ravel()])
        name = bone['name'].encode('latin-1')
        records.append(struct.pack('<IB', bone['hash'], len(name)) + name + struct.pack('<i', parent) +
                       struct.pack('<64f', *mats) + struct.pack('<10f', *scale, *rot, *pos))

        new = {'name': bone['name'], 'hash': bone['hash'], 'parent': parent, 'mats': tuple(mats),
               'scale': scale, 'rot': rot, 'pos': pos}
        index[bone['name']] = len(target)
        target.append(new)
        names.add(bone['name'])
        parents2.append(parent)
        flags.append(flag)

    total = count + len(records)
    out = bytearray(data[:end])
    struct.pack_into('<H', out, 0x14, total)
    out += b''.join(records)
    out += struct.pack(f'<{total}i', *parents2)
    out += bytes(flags)
    out += rest
    return bytes(out), report


def load(path):
    entry = next(game_files.entries('^' + re.escape(path) + '$'))
    return game_files.read(entry)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    out_dir = sys.argv[1]
    data = {k: load(p) for k, p in SKELETONS.items()}
    bones = {k: read_pab(d)[1] for k, d in data.items()}
    for body, other in (('female', 'male'), ('male', 'female')):
        patched, report = add_helpers(data[body], bones[other])
        check = read_pab(patched)
        path = os.path.join(out_dir, SKELETONS[body])
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(patched)
        print(f'{body}: {len(bones[body])} -> {check[0]} bones ({len(report)} helpers), {path}')
        for line in report:
            print('  ' + line)


if __name__ == '__main__':
    main()
