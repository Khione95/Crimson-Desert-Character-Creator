"""Female Armor Fit for a new game version: the released package's reshaped
models (zz_....pac) and rules, the rest made again from the game as it is now
(as armor_female_pack.py made it): each copied part's prefab is the game's
with its reshaped models' paths swapped for the copies (same length), the
models' material (.pac_xml) and physics (.hkx) files are the game's, the
rules name the game's female bodies, and the part table is Character
Creator's shared one (build_data.py: the game's with these copies and the
head copies).

The game update of 2026-10-30 renamed the parts' socket names in the prefabs
(CD_Upperbody -> Upperbody, ...) and the female bodies (cd_phw_00_nude_... ->
phw_00_nude_...): copies of the old prefabs no longer fit.

With a fitted folder (armor_fit.py --list of the released fitter, the
models at their game paths) the copies are made from those fits instead of
the released ones (after the 2026-10-30 update the game's models are stored
unpacked; the released copies were packed), and every other male armor part
in RESHAPE_SLOTS with a fitted model gets a female copy too.

Usage: python faf_port.py <released group (0036)> <version> [fitted folder]
       writes armor_fit_stage/ (Female Armor Fit, Female Armor Fit Dyes),
       dist/Female Armor Fit <version>.zip and game_data/faf_rules_<version>.xml
       (Character Creator's shared part table is made from it: build it again
       after a new part was added, then run this again)
"""
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import game_files
import skeleton_helpers
import paz_add

ROOT = os.path.join(HERE, '..')
STAGE = os.path.join(ROOT, 'armor_fit_stage')
COMMON_RULES = 'character/descriptors/conditionalpartprefab/conditionalpartprefab_common.xml'
NAME = 'Female Armor Fit'
RULES = 'character/descriptors/conditionalpartprefab/conditionalpartprefab_femalefit.xml'
TABLE = 'character/bin__/partprefabtable.pappt'
SHARED_TABLE = os.path.join(ROOT, 'build', 'Character Creator', '0009', *TABLE.split('/'))
# The slots whose male parts get reshaped copies (glasses, earrings, rings,
# weapons do not follow the body).
RESHAPE_SLOTS = {'ub', 'lb', 'hand', 'foot', 'hel', 'cloak', 'bag', 'shoulder', 'belt', 'sho', 'vest', 'jacket', 'necklace'}
FEMALE_BODIES = re.compile(r'^character/bin__/prefab/1_pc/[^/]+/nude/(cd_)?p[hgod]w_00_nude[^/]*\.prefab$')


def renamed(path):
    folder, name = path.rsplit('/', 1)
    return f'{folder}/zz_{name[3:]}' if name.startswith('cd_') else None


def related(model_path, kind):
    sub = model_path[len('character/model/'):-len('.pac')]
    return f'character/modelproperty/{sub}.pac_xml' if kind == 'property' else f'character/bin__/meshphysics/{sub}.hkx'


def game_rule_sources():
    """The parts the game's own conditional rules swap (one rule per part)."""
    names = set()
    for e in game_files.entries(r'^character/descriptors/conditionalpartprefab/[^/]+\.xml$'):
        names.update(re.findall(r'SourcePartPrefab="([^"]+)"', game_files.read(e).decode('utf-8', 'replace')))
    return names


def women_versions(have):
    """The game's own women's version for every armor part that has one and no
    rule yet (the user's rule: what Damiane can wear, women wear): Kliff's part
    -> Damiane's variant of the same item (faf_native_female's pairing). Parts
    the game's own rules name keep those."""
    import faf_native_female
    named = game_rule_sources()
    women = faf_native_female.women_parts()
    added = [(s, t) for s, t in sorted(women.items()) if s not in have and s not in named and s != t]
    clash = sorted(s for s in have if s in named)
    print(f'{len(added)} more parts to the game\'s own women\'s version ({len(women)} have one; '
          f'{sum(1 for s in women if s in named and s not in have)} left to the game\'s rules)'
          + (f'; the game\'s rules now also name {len(clash)} of the package\'s parts: {clash[:10]}' if clash else ''))
    return added


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    released, version = sys.argv[1], sys.argv[2]
    fitted = sys.argv[3] if len(sys.argv) > 3 else None
    old = paz_add.read_group(released)
    rules_text = paz_add.decode(RULES, *old[RULES]).decode('utf-8-sig')
    pairs = re.findall(r'<Condition SourcePartPrefab="([^"]+)">\s*<If Type="Match" TargetPartPrefab="([^"]+)"', rules_text)
    pairs += women_versions({s for s, _ in pairs})

    index = {e['path']: e for e in game_files.entries(
        r'^character/(bin__/prefab/.*\.prefab|modelproperty/.*\.pac_xml|bin__/meshphysics/.*\.hkx)$')}
    prefab_of = {p.rsplit('/', 1)[1][:-len('.prefab')]: p for p in index if p.endswith('.prefab')}

    def models_of(part):
        path = prefab_of.get(part)
        return sorted(set(m.decode() for m in re.findall(rb'character/model/[ -~]+?\.pac', game_files.read(index[path])))) if path else []

    # The reshaped models: copy path -> where its bytes come from.
    if fitted:
        source_of = {}
        for folder, _, files in os.walk(fitted):
            for name in files:
                game_path = os.path.relpath(os.path.join(folder, name), fitted).replace(os.sep, '/')
                if renamed(game_path):
                    source_of[renamed(game_path)] = os.path.join(folder, name)
        # Parts whose models no longer come out changed lose their copy; the
        # other male armor parts with a fitted model get one.
        dropped = [s for s, t in pairs if t.startswith('zz_') and not any(renamed(m) in source_of for m in models_of(s))]
        pairs = [(s, t) for s, t in pairs if s not in dropped]
        import faf_native_female
        have, named = {s for s, _ in pairs}, game_rule_sources()
        parts = sorted({p for p, w in faf_native_female.variants() if faf_native_female.KLIFF in w})
        new = [(p, 'zz_' + p[3:]) for p in parts if p.startswith('cd_') and p not in have and p not in named
               and faf_native_female.slot(p) in RESHAPE_SLOTS and any(renamed(m) in source_of for m in models_of(p))]
        pairs += new
        print(f'{len(source_of)} fitted models; {len(new)} more parts reshaped; '
              f'{len(dropped)} parts without a changed model dropped{": " + ", ".join(dropped[:10]) if dropped else ""}')
    else:
        source_of = {p: None for p in old if p.endswith('.pac') and '/zz_' in p}
    reshaped = set(source_of)

    loose = os.path.join(STAGE, 'loose')
    shutil.rmtree(STAGE, ignore_errors=True)

    def write(path, data):
        full = os.path.join(loose, *path.split('/'))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        open(full, 'wb').write(data)

    used, copied, kept_old, no_prefab = set(), 0, [], []
    for source, target in pairs:
        if not target.startswith('zz_'):
            continue
        path = prefab_of.get(source)
        if not path:
            no_prefab.append(source)
            continue
        data = game_files.read(index[path])
        for m in sorted(set(m.decode() for m in re.findall(rb'character/model/[ -~]+?\.pac', data))):
            copy = renamed(m)
            if copy and copy in reshaped:
                data = data.replace(m.encode(), copy.encode())
                used.add((m, copy))
        write(renamed(path), data)
        copied += 1

    for m, copy in sorted(used):
        if source_of[copy]:
            write(copy, open(source_of[copy], 'rb').read())
        else:
            stored, size, flags = old[copy]
            write(copy, paz_add.decode(copy, stored, size, flags))
        for kind in ('property', 'physics'):
            src, dst = related(m, kind), related(copy, kind)
            if src in index:
                write(dst, game_files.read(index[src]))
            elif dst in old:
                write(dst, paz_add.decode(dst, *old[dst]))
                kept_old.append(dst)

    # The game's female bodies (those its part table has: some old prefab
    # files are still in the archives under their old names).
    import pappt
    table = {n for n, _, _ in pappt.records(open(SHARED_TABLE, 'rb').read())[0]}
    bodies = sorted(n for n in (p.rsplit('/', 1)[1][:-len('.prefab')] for p in index if FEMALE_BODIES.match(p)) if n in table)
    lines = ['<PartPrefabGroup Name="FemaleFitBodies">']
    lines += [f'\t<PartPrefab Name="{b}" WholeWord="True"/>' for b in bodies]
    lines += ['</PartPrefabGroup>', '']
    for source, target in pairs:
        lines += [f'<Condition SourcePartPrefab="{source}">',
                  f'\t<If Type="Match" TargetPartPrefab="{target}" MatchPartPrefabGroup="FemaleFitBodies"/>',
                  '</Condition>']
    # At the end of the game's common rules, not a file of their own: the game lists
    # the folder from its archives, so it never reads a new file DMM 4.5 serves.
    common = game_files.read(next(game_files.entries('^' + re.escape(COMMON_RULES) + '$'))).decode('utf-8-sig')
    # The skeletons with the other gender's bones: without them armor made for
    # one body has wobbly arms on the other (skeleton_helpers.py).
    for path, data in skeleton_helpers.patched_skeletons().items():
        write(path, data)
    write(COMMON_RULES, ('﻿' + common.rstrip().replace('\r\n', '\n') + '\n\n<!-- Female Armor Fit -->\n' + '\n'.join(lines) + '\n').encode('utf-8'))
    # For Character Creator's shared part table (build_data.py ARMOR_FIT_RULES).
    open(os.path.join(HERE, 'game_data', f'faf_rules_{version}.xml'), 'wb').write(('﻿' + '\n'.join(lines) + '\n').encode('utf-8'))
    shutil.copy2(SHARED_TABLE, os.path.join(loose, *TABLE.split('/')))
    print(f'{copied} parts, {len(used)} reshaped models, {len(bodies)} female bodies, '
          f'{len(pairs) - copied} rules to the game\'s own women\'s parts'
          + (f'; old material/physics kept for {len(kept_old)}' if kept_old else '')
          + (f'; no prefab for {no_prefab}' if no_prefab else ''))

    package = os.path.join(STAGE, NAME)
    # Loose files: DMM 4.5 serves them as they are (a packed group it has to unpack first).
    shutil.copytree(loose, os.path.join(package, '0009'))
    info = json.loads(open(os.path.join(os.path.dirname(released), 'mod.json'), encoding='utf-8').read())
    info['modinfo']['version'] = version
    json.dump(info, open(os.path.join(package, 'mod.json'), 'w', encoding='utf-8'), indent=2)

    dyes = os.path.join(STAGE, NAME + ' Dyes')
    subprocess.check_call([sys.executable, os.path.join(HERE, 'armor_dyes.py'), os.path.join(package, '0009'), os.path.join(dyes, '0008')])
    old_dyes = os.path.join(os.path.dirname(os.path.dirname(released)), NAME + ' Dyes', 'mod.json')
    info = json.loads(open(old_dyes, encoding='utf-8').read())
    info['modinfo']['version'] = version
    json.dump(info, open(os.path.join(dyes, 'mod.json'), 'w', encoding='utf-8'), indent=2)

    archive = os.path.join(ROOT, 'dist', f'{NAME} {version}.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for top in (package, dyes):
            for folder, _, files in os.walk(top):
                for name in sorted(files):
                    full = os.path.join(folder, name)
                    z.write(full, os.path.join(os.path.basename(top), os.path.relpath(full, top)))
    print('release:', archive, f'{os.path.getsize(archive) / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
