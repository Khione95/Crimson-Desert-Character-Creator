"""Builds the female-only armor package: for every wearable male armor part
whose models armor_fit.py reshaped, a copy named zz_... (instead of cd_...)
that uses the reshaped models (named zz_..., see PREFIX), and one conditional part file that swaps a
part for its copy while the wearer has a female body. Men keep the original.

Per part (prefab):   character/bin__/prefab/<dir>/zz_<name>.prefab
Per model:           character/model/<dir>/zz_<name>.pac          (reshaped)
                     character/modelproperty/<dir>/zz_<name>.pac_xml (materials, copied)
                     character/bin__/meshphysics/<dir>/zz_<name>.hkx (physics, copied, if any)
Rules:               character/descriptors/conditionalpartprefab/conditionalpartprefab_femalefit.xml
Part table:          character/bin__/partprefabtable.pappt (the game's table with
                     the copies added: parts not in it are ignored)

The new names have the same length as the old, so the prefab only needs its
path strings replaced in place.

Usage: python armor_female_pack.py <reshaped models dir (game paths)> <out dir> [prefab list]
"""
import os
import re
import sys

import game_files
import pappt

FEMALE_BODIES = re.compile(r'^character/bin__/prefab/1_pc/[^/]+/nude/cd_p[hgod]w_00_nude.*\.prefab$')
TABLE = 'character/bin__/partprefabtable.pappt'

# Copies are named zz_... instead of cd_...: DMM adds new files at the end
# of their folder in the archive index, and the game finds files there by
# name in sorted order. cf_ copies came after fs_ files in some folders,
# were not found and crashed the game; nothing in the game sorts after zz_.
PREFIX = 'zz_'
FEMALE_NAME = re.compile(r'(_|/)(phw|nhw|pow|pgw|pdw|ptw|ppdw)(_|/)|/2_phw/')
RULES = 'character/descriptors/conditionalpartprefab/conditionalpartprefab_femalefit.xml'


def renamed(path):
    """character/.../cd_x.ext -> character/.../zz_x.ext (same length)."""
    folder, name = path.rsplit('/', 1)
    if not name.startswith('cd_'):
        return None
    return f'{folder}/{PREFIX}{name[3:]}'


def related(model_path, kind):
    sub = model_path[len('character/model/'):-len('.pac')]
    if kind == 'property':
        return f'character/modelproperty/{sub}.pac_xml'
    return f'character/bin__/meshphysics/{sub}.hkx'


def write(out_dir, path, data):
    full = os.path.join(out_dir, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    open(full, 'wb').write(data)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    fitted_dir, out_dir = sys.argv[1], sys.argv[2]
    wanted = set(open(sys.argv[3]).read().split()) if len(sys.argv) > 3 else None
    base = lambda n: re.sub(r'(_index\d+)+$', '', n)
    wanted_base = {base(n) for n in wanted} if wanted else None

    # One pass over the game's index for everything needed.
    index = {e['path']: e for e in game_files.entries(
        r'^character/(bin__/prefab/.*\.prefab|modelproperty/.*\.pac_xml|bin__/meshphysics/.*\.hkx)$')}

    # A part may have only one rule: parts the game's own rule files already
    # name are left out (a second rule stops the game from starting).
    taken = set()
    for entry in game_files.entries(r'^character/descriptors/conditionalpartprefab/.*\.xml$'):
        text = game_files.read(entry).decode('utf-8', 'replace')
        taken.update(re.findall(r'SourcePartPrefab="([^"]+)"', text))
    taken = {t.lower() for t in taken}

    copied_models = {}
    rules = []
    for path, entry in sorted(index.items()):
        if not path.endswith('.prefab'):
            continue
        name = path.rsplit('/', 1)[1][:-len('.prefab')]
        if FEMALE_NAME.search(path) or not name.startswith('cd_'):
            continue
        if wanted_base is not None and base(name) not in wanted_base:
            continue
        if name.lower() in taken:
            continue
        try:
            data = game_files.read(entry)
        except Exception:
            continue
        models = sorted(set(m.decode() for m in re.findall(rb'character/model/[ -~]+?\.pac', data)))
        fitted = [m for m in models if os.path.exists(os.path.join(fitted_dir, m)) and renamed(m)]
        if not fitted:
            continue

        new = data
        for m in fitted:
            new = new.replace(m.encode(), renamed(m).encode())
            if m in copied_models:
                continue
            write(out_dir, renamed(m), open(os.path.join(fitted_dir, m), 'rb').read())
            for kind in ('property', 'physics'):
                src = related(m, kind)
                if src in index:
                    write(out_dir, related(renamed(m), kind), game_files.read(index[src]))
            copied_models[m] = True

        write(out_dir, renamed(path), new)
        rules.append((name, PREFIX + name[3:]))

    # Exact names: the partial match did not take.
    bodies = sorted(p.rsplit('/', 1)[1][:-len('.prefab')] for p in index if FEMALE_BODIES.match(p))
    lines = ['<PartPrefabGroup Name="FemaleFitBodies">']
    lines += [f'\t<PartPrefab Name="{b}" WholeWord="True"/>' for b in bodies]
    lines += ['</PartPrefabGroup>', '']
    for source, target in rules:
        lines += [f'<Condition SourcePartPrefab="{source}">',
                  f'\t<If Type="Match" TargetPartPrefab="{target}" MatchPartPrefabGroup="FemaleFitBodies"/>',
                  '</Condition>']
    write(out_dir, RULES, ('﻿' + '\n'.join(lines) + '\n').encode('utf-8'))
    table, added = pappt.add_copies(game_files.read(next(game_files.entries('^' + re.escape(TABLE) + '$'))),
                                    dict(rules))
    write(out_dir, TABLE, table)
    print(f'{len(rules)} parts ({added} added to the part table), {len(copied_models)} models, {len(bodies)} female bodies')


if __name__ == '__main__':
    main()
