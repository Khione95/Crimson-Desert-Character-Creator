"""Female Armor Fit 1.0.4: armor that the game already makes in a women's
version uses that one, not the reshaped copy. Starts from the 1.0.3 package
(armor_fit_stage, from faf_add_parts.py) and only changes its rules: a part
that an item gives Kliff, where the same item has a variant for Damiane (all
such items agreeing on one), is swapped to Damiane's part instead of its zz_
copy (women_parts).

Usage: python faf_native_female.py"""
import collections
import json
import os
import re
import shutil
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import game_files
import pappt
import paz_add
from armor_types import SEED

ROOT = os.path.join(HERE, '..')
STAGE = os.path.join(ROOT, 'armor_fit_stage')
VERSION = '1.0.4'
NAME = 'Female Armor Fit'
RULES = 'character/descriptors/conditionalpartprefab/conditionalpartprefab_femalefit.xml'
# The game's item data: each variant of an item is a part's name hash
# followed by who wears it (the wearer types: Kliff, Damiane, Oongka); an
# item's variant for Damiane comes right after its variant for Kliff.
KLIFF, DAMIANE = 0xFC66D914, 0x26BE971F
WEARER_REACH = 24       # the wearer types come within this many bytes after the part
WEARERS = (KLIFF, DAMIANE, 0x87D08287)
SLOTS = ('hand', 'foot', 'hel', 'ub', 'lb', 'cloak', 'mask', 'glass', 'necklace', 'bag', 'shoulder', 'belt', 'earring', 'ring')


def slot(part):
    for t in re.split(r'[_\d]+', part):
        if t in SLOTS:
            return t
    return None


def variants():
    """The item data's parts in order, each with its wearer types (those
    with none are left out)."""
    data = game_files.read(next(game_files.entries(r'iteminfo\.staticinfobody$')))
    table = game_files.read(next(game_files.entries(r'partprefabtable\.pappt$')))
    parts = {game_files._hash_little(n.encode(), SEED): n for n, _, _ in pappt.records(table)[0]}
    out = []
    for off in range(len(data) - 3):
        part = parts.get(struct.unpack_from('<I', data, off)[0])
        if part:
            wearers = {struct.unpack_from('<I', data, o)[0] for o in range(off + 4, off + 4 + WEARER_REACH)} & set(WEARERS)
            if wearers:
                out.append((part, wearers))
    return out


def women_parts(sources=None):
    """Kliff's part -> the women's part: Damiane's variant right after each
    variant that gives the part to Kliff, of the same slot, all agreeing
    (for the parts in sources, or every part when it is None)."""
    found = collections.defaultdict(list)
    v = variants()
    for i, (part, wearers) in enumerate(v):
        if (sources is None or part in sources) and KLIFF in wearers:
            nxt = v[i + 1] if i + 1 < len(v) else (None, set())
            ok = DAMIANE in nxt[1] and KLIFF not in nxt[1] and slot(nxt[0]) == slot(part)
            found[part].append(nxt[0] if ok else None)
    return {p: w[0] for p, w in found.items() if None not in w and len(set(w)) == 1}


def main():
    group = os.path.join(STAGE, NAME, '0036')
    entries = paz_add.read_group(group)
    stored, size, flags = entries[RULES]
    rules = paz_add.decode(RULES, stored, size, flags).decode('utf-8-sig')
    sources = set(re.findall(r'<Condition SourcePartPrefab="([^"]+)">', rules))
    swap = women_parts(sources)

    def target(m):
        s = m.group(1)
        return m.group(0).replace(f'TargetPartPrefab="zz_{s[3:]}"', f'TargetPartPrefab="{swap[s]}"') if s in swap else m.group(0)

    new = re.sub(r'<Condition SourcePartPrefab="([^"]+)">.*?</Condition>', target, rules, flags=re.S)
    changed = sum(1 for s in swap if f'TargetPartPrefab="{swap[s]}"' in new)
    for s in sorted(swap):
        print(f'{s} -> {swap[s]}')
    print(f'{changed} of {len(sources)} parts use the game\'s women\'s version')

    add = os.path.join(STAGE, 'add')
    shutil.rmtree(add, ignore_errors=True)
    os.makedirs(os.path.join(add, *RULES.split('/')[:-1]))
    open(os.path.join(add, *RULES.split('/')), 'w', encoding='utf-8-sig', newline='\n').write(new)
    paz_add.add(group, add)
    shutil.rmtree(add)

    for top in (NAME, NAME + ' Dyes'):
        p = os.path.join(STAGE, top, 'mod.json')
        info = json.load(open(p, encoding='utf-8'))
        info['modinfo']['version'] = VERSION
        json.dump(info, open(p, 'w', encoding='utf-8'), indent=2)

    archive = os.path.join(ROOT, 'dist', f'{NAME} {VERSION}.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for top in (NAME, NAME + ' Dyes'):
            base = os.path.join(STAGE, top)
            for folder, _, files in os.walk(base):
                for name in sorted(files):
                    path = os.path.join(folder, name)
                    z.write(path, os.path.join(top, os.path.relpath(path, base)))
    print('release:', archive, f'{os.path.getsize(archive) / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
