"""Builds the optional "Female Armor Fit" download: male armor reshaped for
female bodies, used only when a woman wears it (men keep the original).

Steps (the first is slow and only needed after a game update or a change to
armor_fit.py):
  1. python armor_fit.py --list game_data/female_fit_models.txt ../armor_fit
     reshapes the models wearable armor parts use (about 30 min; run several
     at once on parts of the list to go faster)
  2. python make_armor_release.py
     copies of those parts (armor_female_pack.py), packed into one archive
     group (paz_pack.py), zipped as dist/Female Armor Fit <version>.zip:

  Female Armor Fit/
    mod.json
    0036/0.paz, 0036/0.pamt      a pre-built group: DMM mounts it in seconds

The package replaces the game's part table (partprefabtable.pappt) and the
player skeletons, so it is tied to the game version it was built from.
"""
import json
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
# Its own version, apart from Character Creator's (1.0 shipped with CC 9.1.0;
# 1.0.1 has the part table with CC's private heads, see private_eyes.py).
VERSION = '1.0.2'

# Not under build/: build_data.py clears that folder.
FITTED = os.path.join(ROOT, 'armor_fit')
PARTS = os.path.join(HERE, 'game_data', 'female_fit_parts.txt')
STAGE = os.path.join(ROOT, 'armor_fit_stage')
OUT = os.path.join(ROOT, 'dist')
NAME = 'Female Armor Fit'
GROUP = '0036'


def write_zip(package):
    os.makedirs(OUT, exist_ok=True)
    archive = os.path.join(OUT, f'{NAME} {VERSION}.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for folder, _, files in os.walk(package):
            for name in sorted(files):
                if name.startswith('.'):
                    continue            # DMM's own marker files
                path = os.path.join(folder, name)
                z.write(path, os.path.join(NAME, os.path.relpath(path, package)))
    print('release:', archive, f'{os.path.getsize(archive) / 1e6:.1f} MB')


def main():
    # --package <folder>: zip an already built package (e.g. the one in DMM's
    # mods folder) with a fresh mod.json.
    if len(sys.argv) >= 3 and sys.argv[1] == '--package':
        write_mod_json(sys.argv[2])
        write_zip(sys.argv[2])
        return
    if not os.path.isdir(FITTED):
        raise SystemExit(f'missing {FITTED} - run armor_fit.py --list first (see the top of this file)')
    loose = os.path.join(STAGE, 'loose')
    package = os.path.join(STAGE, NAME)
    shutil.rmtree(STAGE, ignore_errors=True)
    subprocess.check_call([sys.executable, os.path.join(HERE, 'armor_female_pack.py'), FITTED, loose, PARTS])
    subprocess.check_call([sys.executable, os.path.join(HERE, 'paz_pack.py'), loose, os.path.join(package, GROUP)])
    write_mod_json(package)
    write_zip(package)


def write_mod_json(package):
    info = {'modinfo': {
        'title': NAME,
        'version': VERSION,
        'author': 'Khione',
        'description': 'Male armor reshaped for female bodies (bust, waist, proportions), used only when a woman '
                       'wears it - men keep the original. Part of Character Creator.',
        'nexus_url': 'https://www.nexusmods.com/crimsondesert/mods/837',
    }}
    with open(os.path.join(package, 'mod.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, indent=2)


if __name__ == "__main__":
    main()
