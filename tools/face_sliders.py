"""Face Shape sliders: writes CharacterCreator/faceshape_data.inc.

Every head of the mod's lists is skinned to the same flat face rig: about
205 bones, all children of B_face_com (phm_01.pab / phw_01.pab, the same
names for men and women). A head's face shape is its skeleton variation
(.pabc): a local transform per face bone. The game keeps the loaded shape
in memory as 0x90-byte records (local matrix, scale, rotation, position,
..., the bone's name hash at +0x80) and applies it when the head is built.
A slider moves a group of those bones: offsets at +100, worked out here.

  Sliders   soft brushes on the reference skeleton (positions in cm from
            B_face_com; x the character's left, y up, z back): every bone
            within a brush's radius moves along its direction, the most at
            the centre. Brushes off the middle are mirrored for the other
            side. Offsets are written in B_face_com's space, per gender.
  Prints    each listed head's face shape (.pabc) by the positions of three
            bones, so the plugin finds the shape in memory; and for each
            character's head (slot 1) and face details (slot 5) options,
            which print they use. With the tag bone's position: the lists
            name each character's own copy of a shape (private_shapes.py),
            marked by that bone moved a little.

Run after build_data.py (it reads the built lists).
Usage: python face_sliders.py
"""
import os
import re
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_files  # noqa: E402
import pab  # noqa: E402
import private_shapes  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
META = os.path.join(ROOT, 'build', 'Character Creator', '0009', 'character', 'descriptors', 'customizationmeta')
OUT = os.path.join(ROOT, 'CharacterCreator', 'CharacterCreator', 'faceshape_data.inc')
SKELETONS = ('character/model/1_pc/1_phm/phm_01.pab', 'character/model/1_pc/2_phw/phw_01.pab')   # men, women
LISTS = ('meshparam_example_kliff.xml', 'meshparam_example_damian.xml', 'meshparam_example_oongka.xml')
PRINT_BONES = ('B_Chin_04_L', 'B_Forehead_06_C', 'B_Eyeside_11_L')
NOT_MOVED = re.compile(r'^(B_Tooth|B_Tongue|B_NeckCenter|Bip01)')

# (page, name, brushes); a brush: (centre cm, radius cm, direction, cm at +100)
# or a special kind: ('width', share at +100), ('outward', centre, radius, cm),
# ('around', centre, radius, cm, skip eyeballs).
SLIDERS = [
    ('Face', 'Face width', [('width', 0.12)]),
    ('Face', 'Jaw width', [((5.5, -7.0, -8.0), 5.0, (1, 0, 0), 1.2)]),
    ('Face', 'Jaw height', [((5.0, -8.0, -8.0), 4.5, (0, -1, 0), 1.0)]),
    ('Face', 'Cheekbone width', [((6.5, 0.0, -10.0), 3.5, (1, 0, 0), 0.8)]),
    ('Face', 'Cheekbone height', [((6.5, 0.0, -10.0), 3.5, (0, 1, 0), 0.8)]),
    ('Face', 'Cheek fullness', [((4.5, -3.0, -13.0), 3.0, (0.4, 0, -1), 0.6)]),
    ('Chin', 'Chin length', [((0.0, -10.0, -14.0), 4.5, (0, -1, 0), 1.5)]),
    ('Chin', 'Chin width', [('outward', (0.0, -9.0, -14.0), 4.0, 0.8)]),
    ('Chin', 'Chin forward', [((0.0, -9.5, -14.0), 4.0, (0, 0, -1), 1.2)]),
    ('Nose', 'Nose length', [((0.0, -2.0, -18.0), 2.8, (0, 0, -1), 1.0)]),
    ('Nose', 'Nose height', [((0.0, -2.0, -17.0), 3.0, (0, 1, 0), 0.8)]),
    ('Nose', 'Nose width', [((1.6, -2.5, -15.5), 2.0, (1, 0, 0), 0.6)]),
    ('Nose', 'Nose bridge', [((0.0, 1.0, -16.8), 2.2, (0, 0, -1), 0.6)]),
    ('Mouth', 'Mouth width', [((2.4, -5.7, -14.4), 1.8, (1, 0, 0), 0.7)]),
    ('Mouth', 'Mouth height', [((0.0, -5.6, -15.0), 3.3, (0, 1, 0), 0.8)]),
    ('Mouth', 'Upper lip', [((0.0, -4.9, -16.0), 2.0, (0, 0.3, -1), 0.4)]),
    ('Mouth', 'Lower lip', [((0.0, -6.4, -15.8), 2.0, (0, -0.3, -1), 0.4)]),
    ('Eyes', 'Eye spacing', [((3.2, 2.0, -14.0), 2.8, (1, 0, 0), 0.6)]),
    ('Eyes', 'Eye height', [((3.2, 2.0, -14.0), 2.8, (0, 1, 0), 0.6)]),
    ('Eyes', 'Eye size', [('around', (3.2, 1.9, -14.0), 2.4, 0.35, True)]),
    ('Eyes', 'Eye depth', [((3.2, 2.0, -14.0), 2.8, (0, 0, 1), 0.5)]),
    ('Brows', 'Brow height', [((4.0, 4.4, -14.0), 3.0, (0, 1, 0), 0.7)]),
    ('Brows', 'Brow depth', [((4.0, 4.4, -14.0), 3.0, (0, 0, -1), 0.6)]),
    ('Brows', 'Ears out', [((9.0, 0.0, -4.0), 4.0, (1, 0, 0.3), 0.8)]),
]


# Body Shape sliders: bone scale on the body's shape (+0x40 and the local
# matrix's rows; its shape scale is not read again in play). Only bones
# without children, so a slider does not grow everything below a bone.
# (page, name, men's bones, women's bones, mode, amount at +100)
#   mode 'all': every axis; 'across up' / 'across side': the two axes across
#   the bone's length (along the axis nearest to up / to the side)
BODY_SLIDERS = [
    ('Chest', 'Chest / bust size', ['Bip01 L Chest', 'Bip01 R Chest'], ['Bip01 L Chest', 'Bip01 R Chest'], 'all', 0.6),
    ('Chest', 'Bust shape (women)', [], ['Bip01 L Chest_sub', 'Bip01 R Chest_sub'], 'all', 0.6),
    ('Chest', 'Bust sides (women)', [], ['Bip01 L Chest Side', 'Bip01 R Chest Side'], 'all', 0.6),
    ('Chest', 'Upper chest', ['Bip01 Spine2_Sub'], ['Bip01 L Chest_Muscle', 'Bip01 R Chest_Muscle'], 'all', 0.4),
    ('Arms', 'Shoulders', ['Bip01 L Clavicle_Up', 'Bip01 R Clavicle_Up', 'Bip01 L Clavicle_Front', 'Bip01 R Clavicle_Front',
                           'Bip01 L Clavicle_Back', 'Bip01 R Clavicle_Back', 'Bip01 L ClavicleTwist_Back', 'Bip01 R ClavicleTwist_Back'],
     ['Bip01 L Clavicle_Up', 'Bip01 R Clavicle_Up', 'Bip01 L Clavicle_Front', 'Bip01 R Clavicle_Front', 'Bip01 L Clavicle_Back',
      'Bip01 R Clavicle_Back', 'Bip01 L ClavicleTwist_Back', 'Bip01 R ClavicleTwist_Back'], 'all', 0.4),
    ('Arms', 'Upper arms', ['Bip01 L UpperBMuscle', 'Bip01 R UpperBMuscle', 'Bip01 L UpperFMuscle', 'Bip01 R UpperFMuscle',
                            'Bip01 L UpperFMuscle_sub', 'Bip01 R UpperFMuscle_sub'],
     ['Bip01 L UpperBMuscle', 'Bip01 R UpperBMuscle', 'Bip01 L UpperFMuscle', 'Bip01 R UpperFMuscle', 'Bip01 L UpperFMuscle_sub',
      'Bip01 R UpperFMuscle_sub'], 'across side', 0.5),
    ('Arms', 'Neck', ['Bip01 Neck_Sub'], ['Bip01 Neck_Sub', 'Bip01 Neck_Twist', 'Bip01 L Neck Back', 'Bip01 R Neck Back'], 'across up', 0.3),
    ('Torso', 'Waist', ['Bip01 Spine_Sub', 'Bip01 Spine1_Sub'], ['Bip01 Spine_Sub', 'Bip01 Spine1_Sub'], 'across up', 0.25),
    ('Torso', 'Belly', ['Bip01 Spine_Front'], ['Bip01 Spine_Front', 'Bip01 Spine1_Front'], 'all', 1.0),
    ('Torso', 'Hips', ['Bip01 L Pelvis_Sub', 'Bip01 R Pelvis_Sub'], ['Bip01 L Pelvis_Sub', 'Bip01 R Pelvis_Sub'], 'all', 0.3),
    ('Torso', 'Glutes', ['Bip01 L Hip', 'Bip01 R Hip'], ['Bip01 L Hip', 'Bip01 R Hip'], 'all', 0.5),
    ('Legs', 'Thighs', ['Bip01 L Thigh_Front', 'Bip01 R Thigh_Front', 'Bip01 L Thigh_Side', 'Bip01 R Thigh_Side',
                        'Bip01 L Thigh_In', 'Bip01 R Thigh_In'],
     ['Bip01 L Thigh_Front', 'Bip01 R Thigh_Front', 'Bip01 L Thigh_Side', 'Bip01 R Thigh_Side', 'Bip01 L Thigh_In',
      'Bip01 R Thigh_In'], 'across up', 0.35),
    ('Legs', 'Calves', ['Bip01 L Calf_Back_L', 'Bip01 R Calf_Back_L', 'Bip01 L Calf_Back_R', 'Bip01 R Calf_Back_R'],
     ['Bip01 L Calf_Back_L', 'Bip01 R Calf_Back_L', 'Bip01 L Calf_Back_R', 'Bip01 R Calf_Back_R'], 'across up', 0.8),
    # At the end: profiles keep sliders by number.
    ('Arms', 'Biceps', ['Bip01 L UpperFMuscle', 'Bip01 R UpperFMuscle', 'Bip01 L UpperFMuscle_sub', 'Bip01 R UpperFMuscle_sub'],
     ['Bip01 L UpperFMuscle', 'Bip01 R UpperFMuscle', 'Bip01 L UpperFMuscle_sub', 'Bip01 R UpperFMuscle_sub'], 'across side', 0.8),
    ('Arms', 'Triceps', ['Bip01 L UpperBMuscle', 'Bip01 R UpperBMuscle'], ['Bip01 L UpperBMuscle', 'Bip01 R UpperBMuscle'], 'across side', 0.8),
    ('Arms', 'Forearms', ['Bip01 L ForearmTwist_Side', 'Bip01 R ForearmTwist_Side', 'Bip01 L ForeTwist1', 'Bip01 R ForeTwist1',
                          'Bip01 L ForeTwist2', 'Bip01 R ForeTwist2'],
     ['Bip01 L ForearmTwist_Side', 'Bip01 R ForearmTwist_Side', 'Bip01 L ForeTwist01', 'Bip01 R ForeTwist01', 'Bip01 L ForeTwist02',
      'Bip01 R ForeTwist02', 'Bip01 L ForeTwist03', 'Bip01 R ForeTwist03'], 'across side', 0.5),
    ('Legs', 'Quads', ['Bip01 L Thigh_Front', 'Bip01 R Thigh_Front'], ['Bip01 L Thigh_Front', 'Bip01 R Thigh_Front'], 'across up', 0.8),
    ('Legs', 'Outer thighs', ['Bip01 L Thigh_Side', 'Bip01 R Thigh_Side'], ['Bip01 L Thigh_Side', 'Bip01 R Thigh_Side'], 'across up', 0.8),
    ('Legs', 'Inner thighs', ['Bip01 L Thigh_In', 'Bip01 R Thigh_In'], ['Bip01 L Thigh_In', 'Bip01 R Thigh_In'], 'across up', 0.8),
]
# Bones with children used anyway, each child a part that should follow: the
# men's Neck_Sub carries the shoulder tops (Clavicle_Up), the waist's
# Spine_Sub / Spine1_Sub the belly fronts, a woman's Chest the bust's front
# (Chest_sub, which the Bust shape slider scales on its own).
CHILDREN_ALLOWED = {'Bip01 Neck_Sub', 'Bip01 Spine_Sub', 'Bip01 Spine1_Sub', 'Bip01 L Chest', 'Bip01 R Chest'}
APPEARANCES = ('character/appearance/1_pc/1_phm/cd_phm_macduff/cd_phm_macduff_00000.app_xml',
               'character/appearance/1_pc/2_phw/cd_phw_damian/cd_phw_damian_00000.app_xml',
               'character/appearance/1_pc/1_phm/cd_phm_oongka/cd_phm_oongka_00000.app_xml')


def body_moves(path):
    """Per body slider and bone: the scale added on each local axis at +100."""
    bones = pab.read_pab(read_one(path))[1]
    by = {b['name']: b for b in bones}
    children = {}
    for b in bones:
        children[b['parent']] = children.get(b['parent'], 0) + 1
    women = '2_phw' in path
    rows = []
    for slider, (_, _, men_bones, women_bones, mode, amount) in enumerate(BODY_SLIDERS):
        for name in (women_bones if women else men_bones):
            b = by[name]
            assert name in CHILDREN_ALLOWED or not children.get(bones.index(b)), f'{name} has children'
            axes = np.array(b['mats']).reshape(4, 4, 4)[0][:3, :3]
            axes = axes / np.linalg.norm(axes, axis=1, keepdims=True)
            if mode == 'all':
                d = [amount] * 3
            else:
                along = int(np.argmax(np.abs(axes[:, 1 if mode == 'across up' else 0])))
                d = [0.0 if a == along else amount for a in range(3)]
            rows.append((slider, b['hash'], *d))
    return rows


def read_one(path):
    return game_files.read(next(game_files.entries('^' + re.escape(path) + '$')))


def falloff(d, r):
    return (1 - (d / r) ** 2) ** 2 if d < r else 0.0


def rig(path):
    """{name: (hash, position cm from B_face_com)} and B_face_com's axes."""
    bones = pab.read_pab(read_one(path))[1]
    centre = [b for b in bones if b['name'] == 'B_face_com'][0]
    index = bones.index(centre)
    world = np.array(centre['mats']).reshape(4, 4, 4)[0]
    axes = world[:3, :3] / np.linalg.norm(world[:3, :3], axis=1, keepdims=True)
    out = {}
    for b in bones:
        if b['parent'] != index or NOT_MOVED.match(b['name']):
            continue
        w = np.array(b['mats']).reshape(4, 4, 4)
        p = (w[0][3][:3] - world[3][:3]) * 100
        # The local position is the world offset seen along B_face_com's axes.
        assert np.allclose(axes @ (p / 100), w[2][3][:3], atol=2e-3), b['name']
        out[b['name']] = (b['hash'], p)
    return out, axes


def moves_for(bones, axes):
    rows = []
    for slider, (_, _, brushes) in enumerate(SLIDERS):
        total = {}
        for brush in brushes:
            for name, (h, p) in bones.items():
                x = p[0]
                if brush[0] == 'width':
                    d = np.array([x * brush[1], 0, 0])
                elif brush[0] == 'outward':
                    _, c, r, cm = brush
                    w = falloff(np.linalg.norm(p - np.array(c)), r) * min(abs(x) / 2.5, 1.0)
                    d = np.array([np.sign(x) * cm * w, 0, 0])
                elif brush[0] == 'around':
                    _, c, r, cm, skip_eyes = brush
                    c = np.array(c) * (np.sign(x) if x else 1, 1, 1)
                    if skip_eyes and name.startswith('B_Eyeball'):
                        continue
                    off = (p - c) * (1, 1, 0)
                    n = np.linalg.norm(off)
                    w = falloff(np.linalg.norm(p - c), r)
                    d = off / n * cm * w if n > 1e-6 else np.zeros(3)
                else:
                    c, r, direction, cm = brush
                    c, direction = np.array(c, float), np.array(direction, float)
                    direction = direction / np.linalg.norm(direction)
                    if c[0] and x < 0:                # the brush on the other side
                        c[0], direction[0] = -c[0], -direction[0]
                    d = direction * cm * falloff(np.linalg.norm(p - c), r)
                if np.linalg.norm(d) > 0.01:
                    total[h] = total.get(h, np.zeros(3)) + d
        for h, d in sorted(total.items()):
            local = axes @ (d / 100)                  # cm -> m, in B_face_com's space
            rows.append((slider, h, *local))
    return rows


def pabc_records(data):
    size = {0x34: 196, 0x35: 392}[data[4]]
    count = struct.unpack_from('<I', data, 16)[0]
    out = {}
    for i in range(count):
        at = 20 + size * i
        h = struct.unpack_from('<I', data, at)[0]
        out[h] = struct.unpack_from('<3f', data, at + 4 + 39 * 4)
    return out


def main():
    men, men_axes = rig(SKELETONS[0])
    women, women_axes = rig(SKELETONS[1])
    print_hashes = [men[n][0] for n in PRINT_BONES]
    tag_hash = private_shapes.tag_bone_hash()
    assert print_hashes == [women[n][0] for n in PRINT_BONES]
    # Every bone a face shape has a record for (the plugin walks a loaded
    # shape's records by them): B_face_com, the teeth and the tongue too.
    shape_bones = set()

    prints, by_path, option_print = [], {}, []
    skeletonvariations = game_files.entries(r'^character/binary/skeletonvariation/.*\.pabc$')
    available = {e['path'][len('character/binary/skeletonvariation/'):].lower() for e in skeletonvariations}
    for name in LISTS:
        text = re.sub(r'<!--.*?-->', '', open(os.path.join(META, name), encoding='utf-8-sig').read(), flags=re.S)
        table = []
        for slot in (1, 5):
            row = [-1] * 256
            pd = re.search(r'<ParamDesc\s+Index="%d"[^>]*>(.*?)</ParamDesc>' % slot, text, re.S)
            for ms in re.finditer(r'<MeshSet\s+Index="(\d+)"([^>]*)>', pd.group(1) if pd else ''):
                sv = re.search(r'SkeletonVariation="([^"]+)"', ms.group(2))
                path = private_shapes.original_path(sv.group(1)).lower() if sv else None
                if not path or path not in available:
                    continue
                if path not in by_path:
                    recs = pabc_records(read_one('character/binary/skeletonvariation/' + path))
                    shape_bones.update(recs)
                    if not all(h in recs for h in print_hashes + [tag_hash]):
                        by_path[path] = -1
                        continue
                    gender = 1 if re.search(r'/\d+_p[a-z]w/', '/' + path) else 0
                    by_path[path] = len(prints)
                    prints.append((path, gender, [recs[h] for h in print_hashes], recs[tag_hash]))
                row[int(ms.group(1))] = by_path[path]
            table.append(row)
        option_print.append(table)

    out = ['// Generated by tools/face_sliders.py from the game\'s skeletons and the',
           '// package\'s lists - do not edit.', '',
           f'static const int FACE_SLIDER_COUNT = {len(SLIDERS)};', '',
           '// Page and name of each slider.',
           'static const wchar_t* const FACE_SLIDER_PAGES[FACE_SLIDER_COUNT] = {',
           '    ' + ', '.join(f'L"{p}"' for p, _, _ in SLIDERS) + ' };',
           'static const wchar_t* const FACE_SLIDER_NAMES[FACE_SLIDER_COUNT] = {',
           '    ' + ', '.join(f'L"{n}"' for _, n, _ in SLIDERS) + ' };', '',
           '// Each slider\'s bone offsets at +100 (metres, B_face_com\'s space).']
    for label, (bones, axes) in (('MEN', (men, men_axes)), ('WOMEN', (women, women_axes))):
        rows = moves_for(bones, axes)
        out.append(f'static const FaceMove FACE_MOVES_{label}[] = {{')
        out += [f'    {{ {s}, 0x{h:08X}u, {dx:.6f}f, {dy:.6f}f, {dz:.6f}f }},' for s, h, dx, dy, dz in rows]
        out.append('};')
        print(f'{label.lower()}: {len(rows)} bone offsets')
    face_hashes = sorted(shape_bones)
    out += ['', '// The bones face shapes have records for (name hashes).',
            f'static const uint32_t FACE_BONES[] = {{']
    out += ['    ' + ', '.join(f'0x{h:08X}u' for h in face_hashes[i:i + 8]) + ',' for i in range(0, len(face_hashes), 8)]
    out += ['};', '', '// A face shape is told by the positions of these bones (' + ', '.join(PRINT_BONES) + ').',
            'static const uint32_t FACE_PRINT_BONES[3] = { ' + ', '.join(f'0x{h:08X}u' for h in print_hashes) + ' };',
            '',
            f"// Each character's copy of a shape is marked: {private_shapes.TAG_BONE} moved along z by",
            "// FACE_TAG_STEP x 1 (Kliff), 2 (Damiane), 3 (Oongka); the game's own shape has 0.",
            f'static const uint32_t FACE_TAG_BONE = 0x{tag_hash:08X}u;',
            f'static const float FACE_TAG_STEP = {private_shapes.TAG_STEP}f;', '',
            "// gender, the print bones' positions, the tag bone's (unmarked)",
            'static const FacePrint FACE_PRINTS[] = {']
    for path, gender, pos, tag in prints:
        vals = ', '.join(f'{v:.6f}f' for p in pos for v in p)
        tags = ', '.join(f'{v:.6f}f' for v in tag)
        out.append(f'    {{ {gender}, {{ {vals} }}, {{ {tags} }} }},   // {path}')
    out += ['};', '', '// Which print each character\'s head (slot 1) and face details (slot 5) option uses (-1: none).',
            'static const int16_t FACE_OPTION_PRINT[3][2][256] = {']
    for table in option_print:
        out.append('    {')
        for row in table:
            out.append('        { ' + ', '.join(str(v) for v in row) + ' },')
        out.append('    },')
    out.append('};')
    out += body_data(available)
    with open(OUT, 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')
    print(f'{len(SLIDERS)} sliders, {len(face_hashes)} face bones, {len(prints)} face shapes -> {OUT}')


def body_data(available):
    """The Body Shape sliders' part of faceshape_data.inc: the sliders' bone
    scales per gender, each listed body's shape (gender, its mark bone's
    unmarked position, its records), which one each character's body options
    use (slot 0), and each character's own body option (Damiane's and
    Oongka's body comes from their appearance file, slot 0 empty: the plugin
    sets their own option so their copy is used)."""
    tag_hash = private_shapes.tag_bone_hash(private_shapes.BODY_TAG_BONE)
    prints, by_path, option_print, own, away, bones = [], {}, [], [], [], set()
    for name, appearance in zip(LISTS, APPEARANCES):
        text = re.sub(r'<!--.*?-->', '', open(os.path.join(META, name), encoding='utf-8-sig').read(), flags=re.S)
        body = re.search(r'<Nude>\s*<Prefab Name="([^"]+)"', read_one(appearance).decode('utf-8-sig')).group(1)
        row, mine, sets = [-1] * 256, -1, []
        pd = re.search(r'<ParamDesc\s+Index="0"[^>]*>(.*?)</ParamDesc>', text, re.S)
        for ms in re.finditer(r'<MeshSet\s+Index="(\d+)"([^>]*)>(.*?)</MeshSet>', pd.group(1), re.S):
            if mine < 0 and re.search(r'MeshFileName="%s"' % re.escape(body), ms.group(3)):
                mine = int(ms.group(1))
            sv = re.search(r'\bSkeletonVariation="([^"]+)"', ms.group(2))
            path = private_shapes.original_path(sv.group(1)).lower() if sv else None
            if not path or path not in available:
                continue
            if path not in by_path:
                data = read_one('character/binary/skeletonvariation/' + path)
                recs = pabc_records(data)
                bones.update(recs)
                if tag_hash not in recs:
                    by_path[path] = -1
                    continue
                gender = 1 if re.search(r'/\d+_p[a-z]w/', '/' + path) else 0
                by_path[path] = len(prints)
                prints.append((path, gender, recs[tag_hash], struct.unpack_from('<I', data, 16)[0]))
            row[int(ms.group(1))] = by_path[path]
            sets.append((int(ms.group(1)), path, re.findall(r'MeshFileName="([^"]+)"', ms.group(3))))
        option_print.append(row)
        own.append(mine)
        # Another body of the same skeleton, for loading the marked copy of their own.
        mine_set = next((s for s in sets if s[0] == mine), None)
        away.append(next((s[0] for s in sets if mine_set and s[0] != mine and row[s[0]] >= 0 and s[2] != mine_set[2]
                          and s[1].rsplit('/', 1)[0] == mine_set[1].rsplit('/', 1)[0]), -1))
    out = ['', '// ---------------------------------------------------------------------------',
           '// Body Shape', '// ---------------------------------------------------------------------------', '',
           f'static const int BODY_SLIDER_COUNT = {len(BODY_SLIDERS)};',
           'static const wchar_t* const BODY_SLIDER_PAGES[BODY_SLIDER_COUNT] = {',
           '    ' + ', '.join(f'L"{s[0]}"' for s in BODY_SLIDERS) + ' };',
           'static const wchar_t* const BODY_SLIDER_NAMES[BODY_SLIDER_COUNT] = {',
           '    ' + ', '.join(f'L"{s[1]}"' for s in BODY_SLIDERS) + ' };', '',
           "// Each slider's scale added on the bone's local axes at +100.",
           'static const BodyMove BODY_MOVES_MEN[] = {']
    counts = []
    for label, skeleton in (('MEN', SKELETONS[0]), ('WOMEN', SKELETONS[1])):
        if label == 'WOMEN':
            out.append('static const BodyMove BODY_MOVES_WOMEN[] = {')
        rows = body_moves(skeleton)
        counts.append(len(rows))
        out += [f'    {{ {s}, 0x{h:08X}u, {x:.3f}f, {y:.3f}f, {z:.3f}f }},' for s, h, x, y, z in rows]
        out.append('};')
    out += ['', '// The bones body shapes have records for (name hashes).', 'static const uint32_t BODY_BONES[] = {']
    ordered = sorted(bones)
    out += ['    ' + ', '.join(f'0x{h:08X}u' for h in ordered[i:i + 8]) + ',' for i in range(0, len(ordered), 8)]
    out += ['};', '',
            f"// Each character's copy of a body is marked: {private_shapes.BODY_TAG_BONE} moved along z",
            '// by FACE_TAG_STEP x the mark (the game leaves that bone as loaded).',
            f'static const uint32_t BODY_TAG_BONE = 0x{tag_hash:08X}u;', '',
            "// gender, the tag bone's position (unmarked), the number of records",
            'static const BodyPrint BODY_PRINTS[] = {']
    for path, gender, tag, count in prints:
        out.append(f'    {{ {gender}, {{ {", ".join(f"{v:.6f}f" for v in tag)} }}, {count} }},   // {path}')
    out += ['};', '', "// Which print each character's body option (slot 0) uses (-1: none).",
            'static const int16_t BODY_OPTION_PRINT[3][256] = {']
    out += ['    { ' + ', '.join(str(v) for v in row) + ' },' for row in option_print]
    out += ['};', '', "// Each character's own body (their appearance file's) in their list.",
            'static const int BODY_OWN_OPTION[3] = { ' + ', '.join(str(v) for v in own) + ' };',
            'static const int BODY_AWAY_OPTION[3] = { ' + ', '.join(str(v) for v in away) + ' };']
    print(f'body: {len(BODY_SLIDERS)} sliders, {counts[0]} / {counts[1]} bone scales (men / women), '
          f'{len(prints)} body shapes, own options {own}, away {away}')
    return out


if __name__ == '__main__':
    main()
