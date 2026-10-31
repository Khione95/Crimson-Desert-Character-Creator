"""The Barber edition's game files, on top of the shared package.

Mesh lists: the options the plugin would undo hidden from the barber
(ShowInGame="False"), as it keeps each character fitting (FitCharacter in
menu.cpp) and has no editor to choose another gender or race:

  Body      only the character's own race and gender (the skeleton's)
  Head      only heads of the character's gender (any race)
  Beard     none for Damiane; for the men, no women's entries
  Whiskers  as Head: Face Details, another head mixed into the chosen one

Options stay at their numbers (saves remember options by number), only their
ShowInGame changes. Hair is left as it is: every style fits everyone.

Lengths: a barber slider beside a mesh list only works when the list's option
names it (DecorationParamIndex, with MinValue the slider's floor), as most of
the game's own options do. Every hair, beard and eyebrow names its lengths,
with the whole range open as in the Anywhere editor.

Option pictures: the mannequin renders (icon_render.py) as the game's DDS, in
the lists and in the type palette. The old k_ pictures go.

Settings: rows 184+ in decorationparam_*.xml for Face and Body Shape, of the
game's own inert kind (Method="Reserved"). The game keeps their values and the
barber shows their sliders; barbertabs.cpp moves them onto the bones. 0..100
with 50 the middle; a save from before them is set to 50 by the plugin (Min 1
did not help: the game raises the 0s to it, and they looked like a choice).
Then the eye colour, a type list with a palette of its own: each colour's iris
as a picture (EYE_COLOURS in eyes.cpp). Then an empty setting, for the colour
sub-tab a type list needs; gender and race, type lists with the mannequin's heads
(the women's file shows women's heads for the races); height.

Tab icons: barber_icons.py, named in ui/xml/texture/cd_item_icon.xml.

Usage: python barber_lists.py <customizationmeta folder>   (rewrites the package around it)
"""
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import barber_icons  # noqa: E402
import game_files  # noqa: E402
import icon_meshes  # noqa: E402
import icon_render  # noqa: E402
import names as part_names  # noqa: E402

# Each character's own race and gender (RACE_CODES in menu.cpp).
OWN = {'meshparam_example_kliff.xml': 'phm', 'meshparam_example_damian.xml': 'phw', 'meshparam_example_oongka.xml': 'pom'}
BODY, HEAD, HAIR, BEARD, WHISKERS, EYEBROWS = 0, 1, 2, 3, 5, 6
LENGTHS = {HAIR: ('0 1 2', '1 1 1'), BEARD: ('29', '1'), EYEBROWS: ('47', '1')}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DDS_CACHE = os.path.join(ROOT, 'cache', 'barber_dds')
PICTURES = 'ui/texture/image/customizeimage/'
META_PATH = 'character/descriptors/customizationmeta/'
FACE_SLIDERS, BODY_SLIDERS = 24, 19
FIRST_ROW = 184


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
    if slot in (HEAD, WHISKERS):
        return len(race) == 3 and race[2] == own[2]
    if slot == BEARD:
        return own[2] == 'm' and race != 'phw'
    return True


def hide(tag):
    if re.search(r'ShowInGame\s*=\s*"False"', tag, re.I):
        return tag
    if re.search(r'ShowInGame\s*=', tag):
        return re.sub(r'ShowInGame\s*=\s*"[^"]*"', 'ShowInGame="False"', tag)
    return tag[:-1].rstrip() + ' ShowInGame="False">'


def with_lengths(tag, slot):
    """A MeshSet's opening tag naming the slot's length settings."""
    if slot not in LENGTHS:
        return tag
    tag = re.sub(r'\s*(DecorationParamIndex|MinValue)\s*=\s*"[^"]*"', '', tag)
    return tag[:-1].rstrip() + f' DecorationParamIndex="{LENGTHS[slot][0]}" MinValue="{LENGTHS[slot][1]}">'


def picture(part, package, used):
    """The render of a game part as a DDS in the package: its path, or None."""
    rendered = os.path.join(icon_render.OUT, part + '.png')
    if not os.path.exists(rendered):
        return None
    name = 'cc_' + part.lower() + '.dds'
    cached = os.path.join(DDS_CACHE, name)
    if not os.path.exists(cached) or os.path.getmtime(cached) < os.path.getmtime(rendered):
        os.makedirs(DDS_CACHE, exist_ok=True)
        Image.open(rendered).convert('RGBA').resize((256, 256), Image.LANCZOS).save(cached, pixel_format='DXT5')
    if name not in used:
        used.add(name)
        with open(cached, 'rb') as f:
            data = f.read()
        with open(os.path.join(package, '0012', *PICTURES.split('/'), name), 'wb') as f:
            f.write(data)
    return PICTURES + name


def with_picture(body, package, used):
    """A MeshSet's body with its first MeshList showing its render."""
    names = re.findall(r'MeshFileName="([^"]*)"', body)
    path = picture(icon_render.original(names[0]), package, used) if names else None
    if not path:
        return body

    def first(m):
        tag = re.sub(r'\s*IconPath\s*=\s*"[^"]*"', '', m.group(0))
        return tag[:-2].rstrip() + f' IconPath="{path}"/>'

    return re.sub(r'<MeshList\b[^>]*/>', first, body, count=1)


def rewrite(text, own, package, used):
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
            return with_lengths(tag if now or not was else hide(tag), slot) + with_picture(body, package, used) + '</MeshSet>'

        return pd.group(1) + re.sub(r'(<MeshSet\b[^>]*>)(.*?)</MeshSet>', meshset, pd.group(3), flags=re.S) + '</ParamDesc>'

    # Comments hold example ParamDescs: they are kept as they are.
    parts = re.split(r'(<!--.*?-->)', text, flags=re.S)
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(r'(<ParamDesc\s+Index="(\d+)"[^>]*>)(.*?)</ParamDesc>', param, parts[i], flags=re.S)
    return ''.join(parts), counts


def game_text(path):
    raw = game_files.read(next(game_files.entries('(?i)^' + re.escape(path) + '$')))
    return raw.startswith(b'\xef\xbb\xbf'), raw.decode('utf-8-sig')


def write_text(path, bom, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write((b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))


def shape_rows():
    keys = [f'ccFace{i}' for i in range(FACE_SLIDERS)] + [f'ccBody{i}' for i in range(BODY_SLIDERS)]
    return ''.join(f'\t<ParamDesc Index="{FIRST_ROW + i}" Min="0" Max="100" Default="50" UIKey="{key}" PartName="Head" BlendSlot="5" MaskIndex="-1" '
                   f'Method="Reserved" MethodValue="Reserved"/>\n' for i, key in enumerate(keys))


def eye_row(palette, colours):
    index = FIRST_ROW + FACE_SLIDERS + BODY_SLIDERS
    return (f'\t<ParamDesc Index="{index}" Min="0" Max="{colours - 1}" Default="0" UIKey="ccEyeColour" PartName="Head" BlendSlot="5" MaskIndex="-1" '
            f'Method="Reserved" MethodValue="Reserved" PaletteIndex="{palette}"/>\n')


def identity_rows(gender, race):
    first = FIRST_ROW + FACE_SLIDERS + BODY_SLIDERS + 1
    reserved = 'PartName="Head" BlendSlot="5" MaskIndex="-1" Method="Reserved" MethodValue="Reserved"'
    return (f'\t<ParamDesc Index="{first}" Min="0" Max="100" Default="0" UIKey="ccNone" {reserved}/>\n'
            f'\t<ParamDesc Index="{first + 1}" Min="0" Max="2" Default="0" UIKey="ccGender" {reserved} PaletteIndex="{gender}"/>\n'
            f'\t<ParamDesc Index="{first + 2}" Min="0" Max="4" Default="0" UIKey="ccRace" {reserved} PaletteIndex="{race}"/>\n'
            f'\t<ParamDesc Index="{first + 3}" Min="0" Max="100" Default="50" UIKey="ccHeight" {reserved}/>\n')


def mannequin(code):
    """The render of a race and gender's head."""
    head = icon_render.MANNEQUINS.get(code, (None,))[0]
    if not head:
        head = next(p for p in sorted(f[:-4] for f in os.listdir(icon_render.OUT)) if '_head_' in p and p.replace('cd_', '').startswith(code))
    return head


def identity_palettes(text, package, used):
    """Gender, and race for men and for women, added to the texture palette: their
    numbers and the new text."""
    next_index = 1 + max(int(i) for i in re.findall(r'<TexturePalette\s+Index="(\d+)"', re.sub(r'<!--.*?-->', '', text, flags=re.S)))
    pictures = os.path.join(package, '0012', *PICTURES.split('/'))
    own = PICTURES + 'cc_identity_own.dds'
    barber_icons.head().save(os.path.join(pictures, 'cc_identity_own.dds'), pixel_format='DXT5')
    choices = {'gender': ['phm', 'phw'], 'race_m': ['phm', 'pom', 'pdm', 'pgm'], 'race_w': ['phw', 'pow', 'pdw', 'pgw']}
    numbers = {}
    for key, codes in choices.items():
        lines = f'\t<Texture IsTiling="false" File="{own}" IconPath="{own}"/>\n'
        for code in codes:
            path = picture(icon_render.original(mannequin(code)), package, used)
            lines += f'\t<Texture IsTiling="false" File="{path}" IconPath="{path}"/>\n'
        text = text.rstrip() + f'\n<TexturePalette Index="{next_index}" NormalTexture="false">\n{lines}</TexturePalette>\n'
        numbers[key] = next_index
        next_index += 1
    print('gender and race palettes:', numbers)
    return numbers, text


def eye_colours():
    """The iris textures of EYE_COLOURS in eyes.cpp, in order (None: the game's own)."""
    with open(os.path.join(ROOT, 'CharacterCreator', 'CharacterCreator', 'eyes.cpp'), encoding='utf-8') as f:
        return [None if t == 'NULL' else t.strip('"') for t in re.findall(r'\{ L"[^"]+",\s*(NULL|"[^"]+"),', f.read())]


def iris_picture(texture, package):
    """An iris texture as an option's picture: the disc alone, filling the icon."""
    loose = os.path.join(package, '0009', *texture.split('/'))
    im = Image.open(loose) if os.path.exists(loose) else icon_meshes.texture(texture)
    im = im.convert('RGBA')
    w, h = im.size
    r = int(w * 0.29)
    im = im.crop((w // 2 - r, h // 2 - r, w // 2 + r, h // 2 + r)).resize((256, 256), Image.LANCZOS)
    im.putalpha(ImageChops.multiply(im.getchannel('A'), disc_mask()))
    return im


def disc_mask():
    big = Image.new('L', (1024, 1024), 0)
    ImageDraw.Draw(big).ellipse((16, 16, 1008, 1008), fill=255)
    return big.resize((256, 256), Image.LANCZOS)


def eye_palette(text, package):
    """The eye colours' palette, added to the texture palette: its number and the new text."""
    os.makedirs(DDS_CACHE, exist_ok=True)
    palette = 1 + max(int(i) for i in re.findall(r'<TexturePalette\s+Index="(\d+)"', re.sub(r'<!--.*?-->', '', text, flags=re.S)))
    colours = eye_colours()
    pictures = os.path.join(package, '0012', *PICTURES.split('/'))
    lines = ''
    for i, texture in enumerate(colours):
        name = f'cc_eye_{i:02d}.dds'
        if texture:
            iris_picture(texture, package).save(os.path.join(pictures, name), pixel_format='DXT5')
        else:
            barber_icons.eyes().save(os.path.join(pictures, name), pixel_format='DXT5')
        lines += f'\t<Texture IsTiling="false" File="{PICTURES}{name}" IconPath="{PICTURES}{name}"/>\n'
    text = text.rstrip() + f'\n<TexturePalette Index="{palette}" NormalTexture="false">\n{lines}</TexturePalette>\n'
    print('eye colours:', len(colours), 'in palette', palette)
    return palette, len(colours), text


def add_settings(folder, palette, colours, identity):
    for name in ('decorationparam_example.xml', 'decorationparam_player.xml', 'decorationparam_player_phw.xml'):
        bom, text = game_text(META_PATH + name)
        indexes = [int(i) for i in re.findall(r'<ParamDesc\s+Index="(\d+)"', re.sub(r'<!--.*?-->', '', text, flags=re.S))]
        if max(indexes) != FIRST_ROW - 1 or len(indexes) != FIRST_ROW:
            raise SystemExit(f'{name}: the game has {len(indexes)} rows up to {max(indexes)}, not {FIRST_ROW}: new rows need another start')
        end = text.rindex('</DecorationParam>')
        race = identity['race_w'] if name.endswith('_phw.xml') else identity['race_m']
        write_text(os.path.join(folder, name), bom, text[:end] + shape_rows() + eye_row(palette, colours) + identity_rows(identity['gender'], race) + text[end:])
        print(name, 'rows', FIRST_ROW, '-', FIRST_ROW + FACE_SLIDERS + BODY_SLIDERS + 4)


def palette_pictures(folder, package, used):
    bom, text = game_text(META_PATH + 'customizationtexturepalette.xml')
    counts = [0, 0]

    def texture(m):
        tag = m.group(0)
        f = re.search(r'File="([^"]*)"', tag).group(1)
        if not f:
            return tag
        base = os.path.basename(f)[:-4]
        path = icon_render.game_picture(base)
        if path:
            counts[0] += 1
        else:
            path = picture('type_' + base.lower(), package, used)
            if not path:
                return tag
            counts[1] += 1
        # Scars and dirt name no picture at all.
        if 'IconPath=' in tag:
            return re.sub(r'IconPath="[^"]*"', f'IconPath="{path}"', tag)
        return tag[:-2].rstrip() + f' IconPath="{path}"/>'

    parts = re.split(r'(<!--.*?-->)', text, flags=re.S)
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(r'<Texture\b[^>]*File="[^"]*"[^>]*/>', texture, parts[i])
    palette, colours, text = eye_palette(''.join(parts), package)
    identity, text = identity_palettes(text, package, used)
    write_text(os.path.join(folder, 'customizationtexturepalette.xml'), bom, text)
    print('type pictures:', counts[0], 'of the game,', counts[1], 'rendered')
    return palette, colours, identity


def tab_icons(package):
    icons = os.path.join(package, '0012', 'ui', 'texture', 'icon')
    os.makedirs(icons, exist_ok=True)
    made = os.path.join(DDS_CACHE, 'tabs')
    barber_icons.main(made)
    entries = ''
    for name in barber_icons.NAMES.values():
        with open(os.path.join(made, name + '.dds'), 'rb') as f:
            data = f.read()
        with open(os.path.join(icons, name.lower() + '.dds'), 'wb') as f:
            f.write(data)
        entries += f'<Texture Name="{name}" Filename="UI/texture/icon/{name}.dds" Type="Image" GetRect="0,0,256,256"/>\n'
    bom, text = game_text('ui/xml/texture/cd_item_icon.xml')
    at = text.index('<Texture Name="ItemIcon_ItemGroup_makeup_Head"')
    write_text(os.path.join(package, '0012', 'ui', 'xml', 'texture', 'cd_item_icon.xml'), bom, text[:at] + entries + text[at:])


def main(folder):
    package = folder
    for _ in range(4):
        package = os.path.dirname(package)
    pictures = os.path.join(package, '0012', *PICTURES.split('/'))
    os.makedirs(pictures, exist_ok=True)
    used = set()

    for name, own in OWN.items():
        path = os.path.join(folder, name)
        raw = open(path, 'rb').read()
        bom = raw.startswith(b'\xef\xbb\xbf')
        text, counts = rewrite(raw.decode('utf-8-sig'), own, package, used)
        open(path, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))
        print(name, ', '.join(f'{part_names_slot(s)} {c[1]}/{c[0]}' for s, c in sorted(counts.items())), '(shown at the barber / before)')

    palette, colours, identity = palette_pictures(folder, package, used)
    add_settings(folder, palette, colours, identity)
    tab_icons(package)

    # The pictures still named somewhere stay; the old k_ ones go.
    named = set()
    for name in os.listdir(folder):
        if name.endswith('.xml'):
            with open(os.path.join(folder, name), encoding='utf-8-sig', errors='replace') as f:
                named |= {os.path.basename(p).lower() for p in re.findall(r'IconPath="([^"]*)"', f.read())}
    removed = 0
    for name in os.listdir(pictures):
        if name.lower() not in named and not name.lower().startswith('cd_'):
            os.remove(os.path.join(pictures, name))
            removed += 1
    print(len(used), 'rendered pictures in the package,', removed, 'old ones removed,', len(os.listdir(pictures)), 'left')


def part_names_slot(slot):
    return {0: 'body', 1: 'head', 2: 'hair', 3: 'beard', 4: 'mustache', 5: 'whiskers', 6: 'eyebrows'}.get(slot, str(slot))


if __name__ == '__main__':
    main(sys.argv[1])
