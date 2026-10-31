"""The Barber edition's mesh lists: the package's lists with the options the
editor would undo hidden from the barber (ShowInGame="False"), as the plugin
keeps each character fitting (FitCharacter in menu.cpp) and has no editor to
choose another gender, race or face details:

  Body      only the character's own race and gender (the skeleton's)
  Head      only heads of the character's gender (any race)
  Beard     none for Damiane; for the men, no women's entries
  Whiskers  none: the mod's face details, kept matching the head

Options stay at their numbers (saves remember options by number), only their
ShowInGame changes. Hair is left as it is: every style fits everyone.

Usage: python barber_lists.py <customizationmeta folder>   (rewrites it)
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import names as part_names  # noqa: E402

# Each character's own race and gender (RACE_CODES in menu.cpp).
OWN = {'meshparam_example_kliff.xml': 'phm', 'meshparam_example_damian.xml': 'phw', 'meshparam_example_oongka.xml': 'pom'}
BODY, HEAD, BEARD, WHISKERS = 0, 1, 3, 5


def race_of(body):
    """The race code of a MeshSet's meshes (the characters' own copies,
    zk_/zd_/zo_..., are named after cd_...)."""
    for name in re.findall(r'MeshFileName="([^"]*)"', body):
        code = part_names.race_code(re.sub(r'^z[kdo]_', 'cd_', name))
        if code:
            return code
    return ''


def shown(slot, race, own):
    if slot == BODY:
        return race == own
    if slot == HEAD:
        return len(race) == 3 and race[2] == own[2]
    if slot == BEARD:
        return own[2] == 'm' and race != 'phw'
    if slot == WHISKERS:
        return False
    return True


def hide(tag):
    if re.search(r'ShowInGame\s*=\s*"False"', tag, re.I):
        return tag
    if re.search(r'ShowInGame\s*=', tag):
        return re.sub(r'ShowInGame\s*=\s*"[^"]*"', 'ShowInGame="False"', tag)
    return tag[:-1].rstrip() + ' ShowInGame="False">'


def rewrite(text, own):
    counts = {}

    def param(pd):
        slot = int(pd.group(2))

        def meshset(ms):
            tag, body = ms.group(1), ms.group(2)
            was = not re.search(r'ShowInGame\s*=\s*"False"', tag, re.I)
            now = was and shown(slot, race_of(body), own)
            c = counts.setdefault(slot, [0, 0])
            c[0] += was
            c[1] += now
            return (tag if now or not was else hide(tag)) + body + '</MeshSet>'

        return pd.group(1) + re.sub(r'(<MeshSet\b[^>]*>)(.*?)</MeshSet>', meshset, pd.group(3), flags=re.S) + '</ParamDesc>'

    # Comments hold example ParamDescs: they are kept as they are.
    parts = re.split(r'(<!--.*?-->)', text, flags=re.S)
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(r'(<ParamDesc\s+Index="(\d+)"[^>]*>)(.*?)</ParamDesc>', param, parts[i], flags=re.S)
    return ''.join(parts), counts


def main(folder):
    for name, own in OWN.items():
        path = os.path.join(folder, name)
        raw = open(path, 'rb').read()
        bom = raw.startswith(b'\xef\xbb\xbf')
        text, counts = rewrite(raw.decode('utf-8-sig'), own)
        open(path, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))
        print(name, ', '.join(f'{part_names_slot(s)} {c[1]}/{c[0]}' for s, c in sorted(counts.items())), '(shown at the barber / before)')


def part_names_slot(slot):
    return {0: 'body', 1: 'head', 2: 'hair', 3: 'beard', 4: 'mustache', 5: 'whiskers', 6: 'eyebrows'}.get(slot, str(slot))


if __name__ == '__main__':
    main(sys.argv[1])
