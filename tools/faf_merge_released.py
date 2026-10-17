"""Adds the reshaped necklaces (loose/, from armor_female_pack.py at 3b1b085)
to the released Female Armor Fit files (DLLCrimson/armor_fit_released)."""
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pappt

HERE = os.path.dirname(os.path.abspath(__file__))  # (built 2026-10-17 from a scratch folder holding loose/, cc_table_before.pappt and the part lists)
LOOSE = os.path.join(HERE, 'loose')
REL = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'armor_fit_released')
RULES = 'character/descriptors/conditionalpartprefab/conditionalpartprefab_femalefit.xml'
TABLE = 'character/bin__/partprefabtable.pappt'

parts = [l.strip() for l in open(os.path.join(HERE, 'necklace_parts.txt')) if l.strip()]

# 1. the copies' files
copied = 0
for folder, _, files in os.walk(LOOSE):
    for name in files:
        rel = os.path.relpath(os.path.join(folder, name), LOOSE).replace('\\', '/')
        if rel in (RULES, TABLE):
            continue
        dst = os.path.join(REL, rel)
        if os.path.exists(dst):
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(folder, name), dst)
        copied += 1

# 2. the rules: the necklaces' Condition blocks appended
new_rules = open(os.path.join(LOOSE, RULES), encoding='utf-8-sig').read()
blocks = re.findall(r'<Condition SourcePartPrefab="(cd_[^"]+)">.*?</Condition>', new_rules, re.S)
texts = re.findall(r'(<Condition SourcePartPrefab="cd_[^"]+">.*?</Condition>)', new_rules, re.S)
old_rules = open(os.path.join(REL, RULES), encoding='utf-8-sig').read()
added_rules = [t for s, t in zip(blocks, texts) if f'SourcePartPrefab="{s}"' not in old_rules]
out = old_rules.rstrip('\n') + '\n' + '\n'.join(added_rules) + '\n'
open(os.path.join(REL, RULES), 'w', encoding='utf-8-sig', newline='\n').write(out)

# 3. the part table: the copies added
table = open(os.path.join(REL, TABLE), 'rb').read()
names = [s for s in blocks]
table, added = pappt.add_copies(table, {s: 'zz_' + s[3:] for s in names})
open(os.path.join(REL, TABLE), 'wb').write(table)

print(f'{copied} files copied, {len(added_rules)} rules added, {added} parts added to the table')
