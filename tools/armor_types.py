"""Sorts armor parts into plate, leather and cloth by the English name of the
item that uses them, from the game's item data, into game_data/armor_types.txt
(one "part type item name" line each). armor_fit.py reads it: plates stay
stiff (no breast bone weights); leather and cloth move with the bust.

  item names: gamedata/stringtable/binary__/eng/item.paloc - "paloc", sizes,
    then LZ4: per entry u32 length + text key (digits), u32 length + text
  item data: gamedata/binarystaticinfo__/bin/iteminfo.staticinfobody - an
    item's text key as digits (u32 length 16 before it), and the parts it
    uses as name hashes (Jenkins hashlittle, seed 810718) after it

Usage: python armor_types.py
"""
import os
import re
import struct

import lz4.block

import game_files
import pappt

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'game_data', 'armor_types.txt')
MODELS_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'game_data', 'armor_model_types.txt')
SEED = 810718
REACH = 4000            # an item's parts come within this many bytes after its text key

TYPES = (('plate', ('Plate', 'Chain Mail', 'Mail')),
         ('leather', ('Leather',)),
         ('cloth', ('Cloth', 'Attire', 'Outfit', 'Robe', 'Garb', 'Uniform', 'Suit', 'Coat', 'Dress')))


def item_names():
    e = next(game_files.entries(r'stringtable/binary__/eng/item\.paloc$'))
    data = game_files.read(e)
    size = struct.unpack_from('<I', data, 12)[0]
    text = lz4.block.decompress(data[17:], uncompressed_size=size)
    names = {}
    for m in re.finditer(rb'\x10\x00\x00\x00(\d{16})', text):
        at = m.end()
        n = struct.unpack_from('<I', text, at)[0]
        if 0 < n < 200:
            names[m.group(1)] = text[at + 4:at + 4 + n].decode('utf-8', 'replace')
    return names


def armor_type(name):
    for kind, words in TYPES:
        if any(w in name for w in words):
            return kind
    return None


def main():
    names = item_names()
    data = game_files.read(next(game_files.entries(r'iteminfo\.staticinfobody$')))
    table = game_files.read(next(game_files.entries(r'partprefabtable\.pappt$')))
    parts = {game_files._hash_little(n.encode(), SEED): n for n, _, _ in pappt.records(table)[0]}
    # Items have a name and a description key; only names (short, no full stop) count.
    keys = [(m.start(), m.group(1)) for m in re.finditer(rb'\x10\x00\x00\x00(\d{16})', data)
            if m.group(1) in names and len(names[m.group(1)]) < 60 and '.' not in names[m.group(1)]]
    found = {}
    for i, (at, key) in enumerate(keys):
        name = names.get(key)
        kind = armor_type(name) if name else None
        if not kind:
            continue
        end = min(keys[i + 1][0] if i + 1 < len(keys) else len(data), at + REACH)
        for off in range(at, end - 3):
            part = parts.get(struct.unpack_from('<I', data, off)[0])
            if part and part not in found and ('_ub' in part or 'ub_' in part):
                found[part] = (kind, name)
    # Khione's own notes name the item of each part exactly: they win.
    user = os.path.join(os.path.dirname(OUT), 'armor_items_user.txt')
    for line in open(user, encoding='utf-8'):
        if line.startswith('#') or ' - ' not in line:
            continue
        name, part = (x.strip() for x in line.rsplit(' - ', 1))
        kind = armor_type(name)
        if kind:
            found[part] = (kind, name)

    with open(OUT, 'w', encoding='utf-8') as f:
        for part in sorted(found):
            f.write(f'{part} {found[part][0]} {found[part][1]}\n')

    # The models each part uses (named in its prefab), for armor_fit.py.
    prefabs = {e['path'].rsplit('/', 1)[1][:-len('.prefab')]: e
               for e in game_files.entries(r'^character/bin__/prefab/.*\.prefab$')}
    models = {}
    for part, (kind, _) in found.items():
        if part not in prefabs:
            continue
        for m in re.findall(rb'character/model/[ -~]+?\.pac', game_files.read(prefabs[part])):
            models.setdefault(m.decode(), set()).add(kind)
    with open(MODELS_OUT, 'w', encoding='utf-8') as f:
        for model in sorted(models):
            kinds = models[model]
            # A model shared by items of different types: the stiffest wins.
            kind = next(k for k in ('plate', 'leather', 'cloth') if k in kinds)
            f.write(f'{model} {kind}\n')
    print(len(models), 'models ->', MODELS_OUT)
    counts = {}
    for kind, _ in found.values():
        counts[kind] = counts.get(kind, 0) + 1
    print(len(found), 'chest parts:', counts, '->', OUT)


if __name__ == '__main__':
    main()
