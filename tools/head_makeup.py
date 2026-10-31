"""The makeup mask on the human heads the editor offers, so face makeup
(lipstick, eyeliner, eyeshadow, blush ...) shows on them.

Face makeup is drawn by the head's skin material (SkinnedMeshSkinWrinkle) where
its _faceMaskTexture says. Since the 2026-10-30 update the game's barber has
makeup, but only four heads have the mask: Damiane's own (phw_00_head_00_0111),
phw_00_head_00_0552, the man's phm_00_head_00_0604 (with the women's mask:
human heads share the layout) and a dwarf's. The other human heads get the
women's mask, after _skinDetailMaskTexture as in the game's heads.

The files are the game's own (shared with NPCs): no NPC's customization sets
face makeup (none of the 1,641 in character/binary/customization), so only the
player characters show it.

Usage: python head_makeup.py <out dir> [menu.txt]"""
import os
import re
import sys

import game_files

HERE = os.path.dirname(os.path.abspath(__file__))
MENU = os.path.join(HERE, '..', 'build', 'bin64', 'CharacterCreator', 'menu.txt')
MASK = 'character/texture/phw_00_head_makeup_0001_m.dds'
ITEM_ID = '4165971661029374'    # the game's own heads' _faceMaskTexture
HUMAN = ('phm', 'phw')

SKIN = re.compile(r'(<Material Name="_resourceMaterial" _materialName="SkinnedMeshSkin\w*">.*?<Vector Name="_parameters">)(.*?)(\r?\n\t*</Vector>)', re.S)
PARAM = re.compile(r'<MaterialParameter\w+ StringItemID="(\w+)"[^>]*?Index="(\d+)"')


def offered_heads(menu):
    heads = set()
    for line in open(menu, encoding='utf-8'):
        part = line.split()
        if line.startswith('mesh 1 ') and part[3] in HUMAN:
            heads.add(part[6])
    return sorted(heads)


def with_mask(text):
    """The pac_xml text with the mask in every skin material that lacks it, or
    None when there is nothing to add."""
    added = 0
    nl = '\r\n' if '\r\n' in text else '\n'

    def material(m):
        nonlocal added
        params = m.group(2)
        if '"_faceMaskTexture"' in params:
            return m.group(0)
        names = [p.group(1) for p in PARAM.finditer(params)]
        after = next((n for n in ('_skinDetailMaskTexture', '_maskTexture', '_normalTexture', '_baseColorTexture') if n in names), None)
        if after is None:
            return m.group(0)
        # The element after which it goes: up to its end tag.
        start = params.index(f'StringItemID="{after}"')
        end = params.index('</MaterialParameterTexture>', start) + len('</MaterialParameterTexture>')
        indent = re.search(r'\n(\t*)<MaterialParameter', params).group(1)
        mask = (f'{nl}{indent}<MaterialParameterTexture StringItemID="_faceMaskTexture" ItemID="{ITEM_ID}" '
                f'_name="_faceMaskTexture" Index="0">'
                f'{nl}{indent}\t<ResourceReferencePath_ITexture Name="_value" _path="{MASK}"/>'
                f'{nl}{indent}</MaterialParameterTexture>')
        params = params[:end] + mask + params[end:]
        # The parameters numbered again in order.
        count = iter(range(1000))
        params = re.sub(r'(<MaterialParameter\w+ [^>]*?Index=")\d+(")', lambda p: p.group(1) + str(next(count)) + p.group(2), params)
        added += 1
        return m.group(1) + params + m.group(3)

    new = SKIN.sub(material, text)
    return new if added else None


def build(out_dir, menu=MENU):
    index = {e['path']: e for e in game_files.entries(
        r'^character/(bin__/prefab/.*/head/.*\.prefab|modelproperty/.*/head/.*\.pac_xml)$')}
    prefab_of = {p.rsplit('/', 1)[1][:-len('.prefab')]: p for p in index if p.endswith('.prefab')}
    done, had, written = set(), 0, 0
    for head in offered_heads(menu):
        path = prefab_of.get(head)
        if not path:
            continue
        for m in sorted(set(x.decode() for x in re.findall(rb'character/model/[ -~]+?\.pac', game_files.read(index[path])))):
            prop = 'character/modelproperty/' + m[len('character/model/'):-len('.pac')] + '.pac_xml'
            if prop in done or prop not in index:
                continue
            done.add(prop)
            raw = game_files.read(index[prop])
            text = raw.decode('utf-8-sig')
            if 'SkinnedMeshSkin' not in text:
                continue
            new = with_mask(text)
            if new is None:
                had += 1
                continue
            out = os.path.join(out_dir, *prop.split('/'))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            bom = b'\xef\xbb\xbf' if raw[:3] == b'\xef\xbb\xbf' else b''
            open(out, 'wb').write(bom + new.encode('utf-8'))
            written += 1
    print(f'makeup mask: {written} head materials given it, {had} had it')
    return written


if __name__ == '__main__':
    build(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else MENU)
