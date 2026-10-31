"""Private face and body shapes for the player characters, so the Face Shape
and Body Shape sliders do not reach NPCs (as private_eyes.py does for the eye
colour).

A head's face shape and a body's shape are skeleton variations (.pabc),
named in the lists (SkeletonVariation of slots 1 and 5, the head and face
details, and slot 0, the body). The game loads each file once and shares it:
an NPC with the same head or body used the same shape, and the sliders
(faceshape.cpp, which change the loaded shape) reached them too. Each player
character's lists name their own copy:

  <shape>_zk.pabc Kliff    <shape>_zd.pabc Damiane    <shape>_zo.pabc Oongka

A copy is the game's file with one mark, so the plugin can tell the
character's copy from the game's in memory: a bone moved sideways by 0.5 mm
for Kliff, 1 mm for Damiane, 1.5 mm for Oongka - for a face shape
B_Forehead_00_C (in every head, no slider moves it; the game overwrites the
tongue bones of a loaded shape, so a mark there never shows), for a body the
pelvis weapon point (Bip_Weapon_Attach_In_Pelvis, which the game leaves as
loaded; it adjusts other bones of a loaded body). Only shapes with the
character's mark are changed.

Run on the built package (build_data.py calls it; running it again leaves
the copies as they are).
Usage: python private_shapes.py <package 0009 dir> <meshparam dir>
"""
import os
import re
import struct
import sys

import game_files
import pab

FOLDER = 'character/binary/skeletonvariation/'
SKELETON = 'character/model/1_pc/1_phm/phm_01.pab'
TAG_BONE = 'B_Forehead_00_C'
BODY_TAG_BONE = 'Bip_Weapon_Attach_In_Pelvis'
TAG_STEP = 0.0005           # metres along the bone's local z, per character
CHARACTERS = (('meshparam_example_kliff.xml', 'zk', 1), ('meshparam_example_damian.xml', 'zd', 2),
              ('meshparam_example_oongka.xml', 'zo', 3))
HEAD_SLOTS = (1, 5)
BODY_SLOT = 0
COPY = re.compile(r'_z[kdo]\.pabc$', re.I)


def original_path(path):
    """The game's shape a copy was made from (the path itself if it is none)."""
    return COPY.sub('.pabc', path)


def copy_path(path, mark):
    return path[:-len('.pabc')] + f'_{mark}.pabc'


def tag_bone_hash(name=TAG_BONE):
    bones = pab.read_pab(game_files.read(next(game_files.entries('^' + re.escape(SKELETON) + '$'))))[1]
    return [b['hash'] for b in bones if b['name'] == name][0]


def marked(data, bone, steps):
    """The shape with the tag bone moved sideways by steps * TAG_STEP, or None
    if the shape has no record for it. Records (version 4: 196 bytes, 5: 392):
    hash, world matrix 16, local matrix 16, scale 3, rotation 4, position 3,
    shape 3, inverse 3 (version 5 repeats the floats after one more value)."""
    size = {0x34: 196, 0x35: 392}[data[4]]
    count = struct.unpack_from('<I', data, 16)[0]
    out = bytearray(data)
    for i in range(count):
        at = 20 + size * i
        if struct.unpack_from('<I', data, at)[0] != bone:
            continue
        for block in ((0,) if size == 196 else (0, 49)):
            for index in (block + 30, block + 41):      # the local matrix's last row, the position: z
                where = at + 4 + index * 4
                struct.pack_into('<f', out, where, struct.unpack_from('<f', out, where)[0] + steps * TAG_STEP)
        return bytes(out)
    return None


def build(out_dir, meshparam_dir):
    available = {e['path'][len(FOLDER):].lower(): e for e in game_files.entries('^' + FOLDER + r'.*\.pabc$')}
    face_bone, body_bone = tag_bone_hash(), tag_bone_hash(BODY_TAG_BONE)
    written = skipped = 0
    for name, mark, steps in CHARACTERS:
        path = os.path.join(meshparam_dir, name)
        raw = open(path, 'rb').read()
        text = raw.decode('utf-8-sig')
        done = {}

        def copy(m, bone):
            nonlocal written, skipped
            shape = original_path(m.group(1))
            key = shape.lower()
            if key not in done:
                data = game_files.read(available[key]) if key in available else None
                new = marked(data, bone, steps) if data else None
                if new is None:
                    done[key] = None
                    skipped += 1
                else:
                    target = os.path.join(out_dir, *(FOLDER + copy_path(shape, mark)).split('/'))
                    os.makedirs(os.path.dirname(target), exist_ok=True)
                    open(target, 'wb').write(new)
                    done[key] = copy_path(shape, mark)
                    written += 1
            return f'SkeletonVariation="{done[key] or shape}"'

        for slot in HEAD_SLOTS + (BODY_SLOT,):
            bone = body_bone if slot == BODY_SLOT else face_bone
            pd = re.search(rf'<ParamDesc\s+Index="{slot}".*?</ParamDesc>', text, re.S)
            block = re.sub(r'\bSkeletonVariation="([^"]+)"', lambda m: copy(m, bone), pd.group(0))
            text = text[:pd.start()] + block + text[pd.end():]
        open(path, 'wb').write((b'\xef\xbb\xbf' if raw[:3] == b'\xef\xbb\xbf' else b'') + text.encode('utf-8'))
    print(f'{written} face and body shape copies ({skipped} shapes without the tag bone keep the game\'s file)')


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    build(sys.argv[1], sys.argv[2])


if __name__ == '__main__':
    main()
