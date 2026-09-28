"""Assembles the release zip from the build output.

  Character Creator/                   the mod (the zip's only folder)
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

Run tools/build_data.py, then build the plugin (Release x64) - in that order:
the plugin embeds build/CharacterCreator.data.
Usage: python make_release.py
"""
import os
import shutil
import sys
import zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_data import VERSION  # noqa: E402

PLUGIN = os.path.join(ROOT, 'CharacterCreator', 'CharacterCreator', 'x64', 'Release', 'CharacterCreator.dll')
PACKAGE = os.path.join(ROOT, 'build', 'Character Creator')
DATA = os.path.join(ROOT, 'build', 'CharacterCreator.data')
DOCS = os.path.join(ROOT, 'release')
OUT = os.path.join(ROOT, 'dist')


def main():
    for path in (PLUGIN, PACKAGE, DATA):
        if not os.path.exists(path):
            raise SystemExit(f'missing {path} - build the plugin and run build_data.py first')
    if os.path.getmtime(PLUGIN) < os.path.getmtime(DATA):
        raise SystemExit('the plugin is older than build/CharacterCreator.data - rebuild the plugin (Rebuild, so the data is embedded again)')

    # Only this release's own files: dist also holds the other mods' zips.
    stage = os.path.join(OUT, 'stage')
    if os.path.exists(stage):
        shutil.rmtree(stage)

    package = os.path.join(stage, 'Character Creator')
    shutil.copytree(PACKAGE, os.path.join(package, 'Character Creator Enhanced'), ignore=shutil.ignore_patterns('mod.json'))
    shutil.copy2(os.path.join(PACKAGE, 'mod.json'), package)
    shutil.copy2(PLUGIN, os.path.join(package, 'CharacterCreator.asi'))
    for doc in ('README.txt', 'THIRD_PARTY.txt'):
        shutil.copy2(os.path.join(DOCS, doc), package)

    # Every character can wear each other's armor (make_equip_all.py). The
    # JSON beside the asi also keeps DMM 3.x from taking the mod down its
    # plugin import (see above), as the empty field file did before.
    import make_equip_all
    make_equip_all.write_json(os.path.join(package, 'Equip All Armor.json'))

    # Files only, without entries for the folders (as Windows' own zips are).
    archive = os.path.join(OUT, f'Character Creator {VERSION}.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for folder, _, files in os.walk(stage):
            for name in sorted(files):
                path = os.path.join(folder, name)
                z.write(path, os.path.relpath(path, stage).replace(os.sep, '/'))
    print('release:', archive)


if __name__ == '__main__':
    main()
