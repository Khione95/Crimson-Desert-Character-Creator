"""Reads meshparam_example_*.xml files (the game's per-character mesh lists)."""
import re

SLOT_NAMES = {0: 'Body', 1: 'Head', 2: 'Hair', 3: 'Beard', 4: 'Mustache', 5: 'Whiskers', 6: 'Eyebrows'}


def strip_comments(text):
    return re.sub(r'<!--.*?-->', '', text, flags=re.S)


def parse(path):
    """Returns {slot: [MeshSet dict, ...]} in file order.

    A MeshSet dict holds its attributes (without Index), the raw XML of its
    MeshList children and the list of mesh file names.
    """
    text = strip_comments(open(path, encoding='utf-8-sig', errors='replace').read())
    result = {}
    for pd in re.finditer(r'<ParamDesc\s+Index="(\d+)"([^>]*)>(.*?)</ParamDesc>', text, re.S):
        slot = int(pd.group(1))
        sets = []
        for ms in re.finditer(r'<MeshSet\b([^>]*)>(.*?)</MeshSet>', pd.group(3), re.S):
            attrs = dict(re.findall(r'(\w+)\s*=\s*"([^"]*)"', ms.group(1)))
            attrs.pop('Index', None)
            lists = re.findall(r'<MeshList\b[^>]*/>', ms.group(2))
            names = [re.search(r'MeshFileName="([^"]*)"', l).group(1) for l in lists if 'MeshFileName' in l]
            icons = [m.group(1) for l in lists for m in [re.search(r'IconPath="([^"]*)"', l)] if m]
            sets.append({'attrs': attrs, 'lists': lists, 'names': names, 'icon': icons[0] if icons else ''})
        result[slot] = {'header': pd.group(2), 'sets': sets}
    return result


def mesh_key(meshset):
    """Two MeshSets are the same option if they load the same meshes."""
    return (tuple(meshset['names']), meshset['attrs'].get('SkeletonVariation', ''))


def race_code(meshset):
    """cd_phw_... -> 'phw' (human woman), cd_pom_... -> 'pom' (orc man), ..."""
    for n in meshset['names']:
        m = re.match(r'cd_(p[a-z]{2})_', n)
        if m:
            return m.group(1)
    return ''
