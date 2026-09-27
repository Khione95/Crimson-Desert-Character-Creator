"""Private eyes for the player characters, so a chosen eye colour does not
reach NPCs.

The eye colour swaps the iris texture while the game reads an eye model's
material file (eyes.cpp). The game reads each file once and shares it, and
NPC heads use the same eye files as the player heads: they got the colour
too. Each player character gets copies of the eye models under its own
prefix, and copies of the heads that use them:

  zk_... Kliff    zd_... Damiane    zo_... Oongka     (instead of cd_...)

Per head:        character/bin__/prefab/<dir>/<prefix><name>.prefab
                 (the eye models' paths replaced by the character's copies)
Per eye model:   character/model/<dir>/<prefix><name>.pac
                 character/modelproperty/<dir>/<prefix><name>.pac_xml
                 ("eye" in the name written "eie", see model_copy_name)
Per face shape:  character/binary/skeletonvariation/<dir>/<prefix><name>.pabc
Part table:      character/bin__/partprefabtable.pappt with the head copies

The copies' iris texture paths are marked with the prefix (zk_phm_00_eye_iris_
... instead of cd_phm_00_eye_iris_...): the plugin points them at the chosen
colour's texture, or back at the original, while the game reads them - only
these files, NPC eyes are never touched. It also gives each character the
copy of their head.

Also rewrites each character's head lists (slots 1 and 5 of
meshparam_example_kliff/damian/oongka.xml in <meshparam dir>) to their copies, and writes the copied heads to
<list file> for the plugin.

Usage: python private_eyes.py <out dir> <meshparam dir> <list file> [head name ...]
       (default: every head the editor offers, see build/bin64/CharacterCreator/menu.txt)
"""
import os
import re
import sys

import game_files
import pappt

HERE = os.path.dirname(os.path.abspath(__file__))
MENU = os.path.join(HERE, '..', 'build', 'bin64', 'CharacterCreator', 'menu.txt')
TABLE = 'character/bin__/partprefabtable.pappt'
IRIS = b'character/texture/cd_phm_00_eye_iris_'
PREFIXES = {'zk_': 'Kliff', 'zd_': 'Damiane', 'zo_': 'Oongka'}
HEAD_LISTS = {'zk_': 'meshparam_example_kliff.xml', 'zd_': 'meshparam_example_damian.xml',
              'zo_': 'meshparam_example_oongka.xml'}
HEAD_SLOTS = (1, 5)


def copy_name(path, prefix):
    folder, name = path.rsplit('/', 1)
    return f'{folder}/{prefix}{name[3:]}' if name.startswith('cd_') else None


def model_copy_name(path, prefix):
    """An eye model's copy: DMM sends loose files named ...eye... down its
    own eye route, where the game then fails to load them (a crash), so the
    copies say "eie" instead (same length)."""
    name = copy_name(path, prefix)
    folder, file = name.rsplit('/', 1)
    return f'{folder}/{file.replace("eye", "eie")}'


def write(out_dir, path, data):
    full = os.path.join(out_dir, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    open(full, 'wb').write(data)


def offered_heads(menu=MENU):
    return sorted({line.split()[6] for line in open(menu, encoding='utf-8') if line.startswith('mesh 1 ')})


def build(out_dir, meshparam_dir, list_file, heads=None, menu=MENU, table_path=None):
    """table_path: the part table to add the copies to (default the game's).
    Female Armor Fit's table (the game's with its armor copies) makes one
    table both packages can ship."""
    heads = heads or offered_heads(menu)
    index = {e['path']: e for e in game_files.entries(
        r'^character/(bin__/prefab/.*/head/.*\.prefab|modelproperty/.*/head/.*\.pac_xml)$')}
    prefab_of = {p.rsplit('/', 1)[1][:-len('.prefab')]: p for p in index if p.endswith('.prefab')}

    has_iris = {}
    copies = {}
    eye_models = set()
    for head in heads:
        path = prefab_of.get(head)
        if not path or not head.startswith('cd_'):
            print('skipped', head, '(no prefab)')
            continue
        data = game_files.read(index[path])
        models = sorted(set(m.decode() for m in re.findall(rb'character/model/[ -~]+?\.pac', data)))
        eyes = []
        for m in models:
            prop = 'character/modelproperty/' + m[len('character/model/'):-len('.pac')] + '.pac_xml'
            if m not in has_iris:
                has_iris[m] = prop in index and IRIS in game_files.read(index[prop])
            if has_iris[m] and m.rsplit('/', 1)[1].startswith('cd_'):
                eyes.append((m, prop))
        if not eyes:
            print('skipped', head, '(no eyes with an iris)')
            continue
        for prefix in PREFIXES:
            new = data
            for m, prop in eyes:
                new = new.replace(m.encode(), model_copy_name(m, prefix).encode())
                eye_models.add((m, prop, prefix))
            write(out_dir, copy_name(path, prefix), new)
            copies[head] = copies.get(head, []) + [prefix + head[3:]]

    models = {}
    for m, prop, prefix in sorted(eye_models):
        if m not in models:
            models[m] = next(game_files.entries('^' + re.escape(m) + '$'))
        write(out_dir, model_copy_name(m, prefix), game_files.read(models[m]))
        # The iris paths are marked with the character's prefix (same length):
        # the plugin points them at the chosen colour, or back at the
        # original texture - these copies need the plugin.
        marked = game_files.read(index[prop]).replace(IRIS, IRIS.replace(b'/cd_', b'/' + prefix.encode()))
        write(out_dir, model_copy_name(prop, prefix), marked)

    table = (open(table_path, 'rb').read() if table_path else
             game_files.read(next(game_files.entries('^' + re.escape(TABLE) + '$'))))
    added_total = 0
    for prefix in PREFIXES:
        table, added = pappt.add_copies(table, {h: prefix + h[3:] for h in copies})
        added_total += added
    write(out_dir, TABLE, table)

    # The face shape (skeleton variation) of a head named in a character's
    # file is found by the head's name: each copy gets its own.
    shapes = {e['path'].rsplit('/', 1)[1][:-len('.pabc')]: e for e in game_files.entries(
        r'^character/binary/skeletonvariation/.*/head/.*\.pabc$')}
    shape_count = 0
    for head in copies:
        e = shapes.get(head)
        if e:
            data = game_files.read(e)
            for prefix in PREFIXES:
                write(out_dir, copy_name(e['path'], prefix), data)
            shape_count += 1

    # Each character's head lists name their own copies: the Head entry
    # (ParamDesc Index="1") and slot 5, which the plugin sets to match the
    # head (menu.cpp) - the head the character is finally built with.
    for prefix, name in HEAD_LISTS.items():
        path = os.path.join(meshparam_dir, name)
        raw = open(path, 'rb').read()
        text = raw.decode('utf-8-sig')
        for index in HEAD_SLOTS:
            head = re.search(rf'<ParamDesc Index="{index}".*?</ParamDesc>', text, re.S)
            block = re.sub(r'MeshFileName="cd_([^"]+)"',
                           lambda m: f'MeshFileName="{prefix}{m.group(1)}"' if 'cd_' + m.group(1) in copies else m.group(0),
                           head.group(0))
            text = text[:head.start()] + block + text[head.end():]
        open(path, 'wb').write((b'\xef\xbb\xbf' if raw[:3] == b'\xef\xbb\xbf' else b'') + text.encode('utf-8'))
    open(list_file, 'w', encoding='utf-8').write(''.join(h + chr(10) for h in sorted(copies)))
    print(f'{len(copies)} heads x {len(PREFIXES)} characters ({added_total} added to the part table), '
          f'{len(models)} eye models x {len(PREFIXES)}, {shape_count} face shapes x {len(PREFIXES)}')


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    build(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:])


if __name__ == '__main__':
    main()
