"""Builds "Equip All Armor": Kliff, Damiane and Oongka can wear each other's
armor. Weapons, tools, horse, pet and robot gear are left as they are.

A DMM v3 JSON mod with one rule, applied by DMM to the game's current item
data (nothing is copied from it): every item whose equipment type
(equip_type_info, a key of equiptypeinfo = hashlittle(lower-case type name,
810718)) is an armor type gets an empty wearer list (tribe_gender_list) on
all its variants (prefab_data_list[*]), so any character can wear it.

Output: dist/Equip All Armor <version>.zip (a folder with the JSON), and
build/Equip All Armor.json, which make_release.py puts in Character Creator's
folder.
Usage: python make_equip_all.py
"""
import json
import os
import zipfile

import game_files

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'dist')
NAME = 'Equip All Armor'
TITLE = 'Character Creator Enhanced - Equip All Armor'
VERSION = '1.0'

ARMOR_TYPES = ['Helm', 'Upperbody', 'Hand', 'Foot', 'Cloak', 'Mask', 'Glass', 'BackPack',
               'Earring', 'Necklace', 'Ring', 'Bracelet']


def type_key(name):
    return game_files._hash_little(name.lower().encode(), 810718)


def build_mod():
    return {
        'format': 3, 'format_minor': 1,
        'modinfo': {
            'author': 'Khione', 'title': TITLE, 'version': VERSION,
            'description': "Kliff, Damiane and Oongka can wear each other's armor (helmets, chest, gloves, "
                           'boots, cloaks, masks, glasses, backpacks, jewellery). Weapons are unchanged.',
        },
        'targets': [{'file': 'iteminfo.pabgb', 'intents': [{
            'field': 'prefab_data_list[*].tribe_gender_list',
            'match': {'equip_type_info': [type_key(n) for n in ARMOR_TYPES]},
            'new': [], 'op': 'set'}]}],
    }


def write_json(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(build_mod(), f, indent=2)


def main():
    mod = build_mod()
    write_json(os.path.join(HERE, '..', 'build', f'{NAME}.json'))
    os.makedirs(OUT, exist_ok=True)
    archive = os.path.join(OUT, f'{NAME} {VERSION}.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(f'{NAME}/{NAME}.json', json.dumps(mod, indent=2))
    print('release:', archive)


if __name__ == '__main__':
    main()
