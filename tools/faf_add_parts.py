"""Female Armor Fit 1.0.3 = the released 1.0.2 package with the necklaces
added (paz_add.py), its part table replaced by the shared one (Character
Creator's build, which now has the necklaces too), and the Dyes folder."""
import json
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import paz_add

HERE = os.path.dirname(os.path.abspath(__file__))  # (built 2026-10-17 from a scratch folder holding loose/, cc_table_before.pappt and the part lists)
STAGE = os.path.join(ROOT, 'armor_fit_stage')
VERSION = '1.0.3'
NAME = 'Female Armor Fit'
RULES = 'character/descriptors/conditionalpartprefab/conditionalpartprefab_femalefit.xml'
TABLE = 'character/bin__/partprefabtable.pappt'
OLD_ZIP = os.path.join(ROOT, 'dist', 'Female Armor Fit 1.0.2.zip')

shutil.rmtree(STAGE, ignore_errors=True)
os.makedirs(STAGE)
with zipfile.ZipFile(OLD_ZIP) as z:
    z.extractall(STAGE)
group = os.path.join(STAGE, NAME, '0036')

entries = paz_add.read_group(group)
stored, size, flags = entries[RULES]
old_rules = paz_add.decode(RULES, stored, size, flags).decode('utf-8-sig')
stored, size, flags = entries[TABLE]
old_table = paz_add.decode(TABLE, stored, size, flags)
before = open(os.path.join(HERE, 'cc_table_before.pappt'), 'rb').read()
print('released table = previous shared table:', old_table == before, len(old_table))

add = os.path.join(STAGE, 'add')
loose = os.path.join(HERE, 'loose')
for folder, _, files in os.walk(loose):
    for name in files:
        rel = os.path.relpath(os.path.join(folder, name), loose).replace('\\', '/')
        if rel in (RULES, TABLE):
            continue
        dst = os.path.join(add, *rel.split('/'))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(folder, name), dst)

new_rules = open(os.path.join(loose, *RULES.split('/')), encoding='utf-8-sig').read()
texts = re.findall(r'(<Condition SourcePartPrefab="(cd_[^"]+)">.*?</Condition>)', new_rules, re.S)
added = [t for t, s in texts if f'SourcePartPrefab="{s}"' not in old_rules]
rules = old_rules.rstrip('\n') + '\n' + '\n'.join(added) + '\n'
os.makedirs(os.path.join(add, *RULES.split('/')[:-1]), exist_ok=True)
open(os.path.join(add, *RULES.split('/')), 'w', encoding='utf-8-sig', newline='\n').write(rules)
shutil.copy2(os.path.join(ROOT, 'build', 'Character Creator', '0009', *TABLE.split('/')), os.path.join(add, *TABLE.split('/')))
print(len(added), 'rules added')

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
