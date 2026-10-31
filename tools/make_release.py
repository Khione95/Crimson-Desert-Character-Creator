"""Assembles the release zips from the build output, one per edition:

  Character Creator Enhanced - Anywhere   the editor (F6 / F7 / F8), Release x64
  Character Creator Enhanced - Barber      no editor: the game's barber shows the
                                          package's lists (Release x64 built with
                                          /p:CcBarber=1, barber_lists.py)

  Character Creator Enhanced - <edition>/   the mod (the zip's only folder)
    CharacterCreator.asi               the plugin; DMM puts it into the game's
                                       bin64 (it carries its menu data and icons
                                       and unpacks them there)
    mod.json, README.txt, THIRD_PARTY.txt
    Equip All Armor.json               every character can wear each other's
                                       armor (make_equip_all.py)
    Character Creator Enhanced/        the game files (0009, 0012)

DMM 3.x: a mod with an asi and no JSON beside it goes down DMM's plugin
import, which fails with os error 32. With a JSON beside the asi (an empty
field file before, now Equip All Armor.json) it imports like 8.x did. Game folders
beside the asi are taken for plugin data, so they sit one level down.

Run tools/build_data.py, then build the plugin (Release x64, and again with
/p:CcBarber=1) - in that order: the plugin embeds build/CharacterCreator.data.
Usage: python make_release.py
"""
import json
import os
import shutil
import sys
import zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_data import VERSION  # noqa: E402
import barber_lists  # noqa: E402

BUILDS = os.path.join(ROOT, 'CharacterCreator', 'CharacterCreator', 'x64')
PACKAGE = os.path.join(ROOT, 'build', 'Character Creator')
DATA = os.path.join(ROOT, 'build', 'CharacterCreator.data')
DOCS = os.path.join(ROOT, 'release')
OUT = os.path.join(ROOT, 'dist')
META = os.path.join('0009', 'character', 'descriptors', 'customizationmeta')

# edition: (plugin build, readme, mod.json description)
EDITIONS = {
    'Anywhere': ('Release', 'README.txt',
                 'In-game appearance editor for Kliff, Damiane and Oongka (F6 / F7 / F8): gender, race, body, head, hair, '
                 'beard, eyebrows, eye colour, tattoos, scars and paint. Needs CharacterCreator.asi.'),
    'Barber': ('ReleaseBarber', 'README_Barber.txt',
               "Every option of Character Creator in the game's own barber screen, for Kliff, Damiane and Oongka: "
               'gender, race, head, body, face and body shape, hair, beard, eyes, tattoos, scars, paint. Needs CharacterCreator.asi.'),
}


def main():
    for edition in EDITIONS:
        make(edition)


def make(edition):
    build, readme, description = EDITIONS[edition]
    plugin = os.path.join(BUILDS, build, 'CharacterCreator.dll')
    for path in (plugin, PACKAGE, DATA, os.path.join(DOCS, readme)):
        if not os.path.exists(path):
            raise SystemExit(f'missing {path} - build the plugin and run build_data.py first')
    if os.path.getmtime(plugin) < os.path.getmtime(DATA):
        raise SystemExit(f'the {edition} plugin is older than build/CharacterCreator.data - rebuild it (Rebuild, so the data is embedded again)')

    # Only this release's own files: dist also holds the other mods' zips.
    stage = os.path.join(OUT, 'stage')
    if os.path.exists(stage):
        shutil.rmtree(stage)

    name = f'Character Creator Enhanced - {edition}'
    package = os.path.join(stage, name)
    # No appearance files: DMM 4.5 leaves them out (the lists' hidden old parts
    # make them look outdated to it); the plugin takes the starting beard out.
    shutil.copytree(PACKAGE, os.path.join(package, 'Character Creator Enhanced'), ignore=shutil.ignore_patterns('mod.json', '*.app_xml'))
    if edition == 'Barber':
        barber_lists.main(os.path.join(package, 'Character Creator Enhanced', META))

    info = json.load(open(os.path.join(PACKAGE, 'mod.json'), encoding='utf-8'))
    info['modinfo']['title'] = name
    info['modinfo']['description'] = description
    with open(os.path.join(package, 'mod.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, indent=2)

    shutil.copy2(plugin, os.path.join(package, 'CharacterCreator.asi'))
    shutil.copy2(os.path.join(DOCS, readme), os.path.join(package, 'README.txt'))
    shutil.copy2(os.path.join(DOCS, 'THIRD_PARTY.txt'), package)

    # Every character can wear each other's armor (make_equip_all.py). The
    # JSON beside the asi also keeps DMM 3.x from taking the mod down its
    # plugin import (see above), as the empty field file did before.
    import make_equip_all
    make_equip_all.write_json(os.path.join(package, 'Equip All Armor.json'))

    # Files only, without entries for the folders (as Windows' own zips are).
    archive = os.path.join(OUT, f'{name} {VERSION}.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for folder, _, files in os.walk(stage):
            for name in sorted(files):
                path = os.path.join(folder, name)
                z.write(path, os.path.relpath(path, stage).replace(os.sep, '/'))
    print('release:', archive)


if __name__ == '__main__':
    main()
