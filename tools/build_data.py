"""Builds the unified Character Creator package from the per-race folders.

Input : the unpacked mod, one folder per race and gender
        (Human Male, Human Female, Orc Male, ...), each with 0009/ and 0012/.
Output: build/Character Creator/        game data for the mod manager
          0009/...  shared files + the merged mesh list, written for Kliff,
                    Damiane and Oongka (meshparam_example_kliff/damian/oongka.xml)
          0012/...  icons and UI
        build/bin64/CharacterCreator/   data read by the .asi at runtime
          menu.txt  every mesh option and palette colour, for the editor
          icons/    the option icons as PNG

Mesh options are merged so every race's bodies and heads are in one list.
The order is fixed (by race, then the order in the source files), so an
option keeps its index between builds and saved profiles stay valid.

Usage: python build_data.py [source folder] [output folder] [--identity "Human Male"]
"""
import colorsys
import os
import re
import shutil
import sys

from meshparam import SLOT_NAMES, mesh_key, parse, race_code, strip_comments

# The mod's source folders sit on the Desktop.
DESKTOP = os.path.join(os.path.expanduser('~'), 'Desktop')

SOURCE =os.path.join(DESKTOP, r'Crimson Mod Folders Unpacked\Character Creator\Character Creator')
OUTPUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build')

META = r'0009\character\descriptors\customizationmeta'
MESHPARAM = META + r'\meshparam_example_kliff.xml'
PALETTE = META + r'\customizationcolorpalette.xml'
ICONS = r'0012\ui\texture\image\customizeimage'
DECORATION = META + r'\decorationparam_player.xml'
CUSTOMIZATION_FOLDER = r'0009\character\descriptors\customization'

# Unmodified game files. The colour palette file is built from the game's own
# (with the mod's extra colours appended, see build_palette).
ORIGINAL_META = os.path.join(DESKTOP, r'Crimson Browser Sharp\extract', META)

# Texture palettes (tattoo, scar and dirt types). Only used to know how many
# types there are; the game has its own copy.
TEXTURE_PALETTE = os.path.join(DESKTOP, r'CC DATA SLINKY\charactercustomizationextender (1)'
                               r'\charactercustomizationextender\files\character\descriptors\customizationmeta'
                               r'\customizationtexturepalette.xml')

# Folder that provides every file which is the same for all races.
BASE_FOLDER = 'Human Male'

# The race and gender the character loads as. Its base character file
# (body, head, scale, customization) must match the skeleton: female
# identities also get Female Animations.field.json, male ones must not.
DEFAULT_IDENTITY = 'Human Male'
APPEARANCE = r'0009\character\appearance\1_pc\1_phm\cd_phm_macduff\cd_phm_macduff_00000.app_xml'
FEMALE_ANIMATIONS = 'Female Animations.field.json'
GENDERS = ['Male', 'Female']
RACES = ['Human', 'Orc', 'Dwarf', 'Goblin']

# Merge order. Human first, so the options the game had before keep index 0..n.
RACE_ORDER = ['phm', 'phw', 'pom', 'pow', 'pdm', 'pdw', 'pgm', 'pgw', 'ptm', '']
FOLDER_ORDER = ['Human Male', 'Human Female', 'Orc Male', 'Orc Female',
                'Dwarf Male', 'Dwarf Female', 'Goblin Male', 'Goblin Female']

VERSION = '9.1.9'
# Body scales that replace the source's: its goblin woman (0.50) was about half
# a goblin man's height; 0.88 as for the goblin man.
BASE_SCALE_FIXES = {'Goblin Female': '0.88'}
# Female Armor Fit's part table, from its released package (see private_eyes.py).
ARMOR_FIT_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'armor_fit_released',
                               'character', 'bin__', 'partprefabtable.pappt')
DESCRIPTION = ('In-game appearance editor for Kliff, Damiane and Oongka (F6 / F7 / F8): gender, race, body, '
               'head, hair, beard, eyebrows, eye colour, tattoos, scars and paint. Needs CharacterCreator.asi.')

ICON_SIZE = 128
MAX_OPTIONS = 255   # the game stores a mesh choice in one byte


def merged_mesh_lists(source):
    """{slot: [MeshSet, ...]} with every race's options, duplicates removed."""
    seen = {}
    per_slot = {}
    headers = {}

    for folder in FOLDER_ORDER:
        path = os.path.join(source, folder, MESHPARAM)
        if not os.path.exists(path):
            print(f'  skipping {folder}: no meshparam file')
            continue

        for slot, desc in parse(path).items():
            headers.setdefault(slot, desc['header'])
            for ms in desc['sets']:
                key = (slot, mesh_key(ms))
                if key not in seen:
                    seen[key] = True
                    per_slot.setdefault(slot, []).append(ms)

    for slot, sets in per_slot.items():
        order = {code: i for i, code in enumerate(RACE_ORDER)}
        sets.sort(key=lambda ms: order.get(race_code(ms), len(order)))   # stable: keeps file order within a race
        if len(sets) > MAX_OPTIONS:
            raise SystemExit(f'{SLOT_NAMES[slot]} has {len(sets)} options, more than the game can store ({MAX_OPTIONS})')

    return per_slot, headers


def add_character_meshes(per_slot, headers):
    """Appends the meshes of the game's own Kliff, Damiane and Oongka lists
    that the race folders do not have (Oongka's own body and head, ...), at
    the end so no existing option moves. Returns their names."""
    added = []
    for name in ('kliff', 'damian', 'oongka'):
        path = os.path.join(ORIGINAL_META, f'meshparam_example_{name}.xml')
        if not os.path.exists(path):
            print(f'  game file {os.path.basename(path)} not extracted - its own meshes may be missing')
            continue
        for slot, desc in parse(path).items():
            headers.setdefault(slot, desc['header'])
            sets = per_slot.setdefault(slot, [])
            known = {mesh_key(ms) for ms in sets}
            for ms in desc['sets']:
                if mesh_key(ms) not in known and ms['names']:
                    sets.append(ms)
                    known.add(mesh_key(ms))
                    added.append(ms['names'][0])
            if len(sets) > MAX_OPTIONS:
                raise SystemExit(f'{SLOT_NAMES[slot]} has {len(sets)} options, more than the game can store ({MAX_OPTIONS})')

    # Oongka's own head is only named in his base character file (the game's
    # list for him has no heads), so it is added by hand, to the head slot and
    # to slot 5 (which the mod's lists also fill with heads).
    for slot, mesh, skeleton, icon in EXTRA_MESHES:
        ms = {'attrs': {'SkeletonVariation': skeleton, 'UseSkeletonVariation': 'True'},
              'lists': [f'<MeshList MeshFileName="{mesh}" IconPath="{icon}"/>'], 'names': [mesh], 'icon': icon}
        sets = per_slot.setdefault(slot, [])
        if mesh_key(ms) not in {mesh_key(s) for s in sets}:
            sets.append(ms)
            added.append(mesh)
    return added


def add_all_hair_and_beards(per_slot):
    """Appends every player hair and beard the game has (tools/game_data/
    hair_beard_prefabs.txt) that the lists do not have yet, at the end so no
    existing option moves. The editor offers them to every race and gender."""
    folder = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'game_data')
    path = os.path.join(folder, 'hair_beard_prefabs.txt')
    added = []
    if not os.path.exists(path):
        print('  hair_beard_prefabs.txt missing - only the hair and beards of the mod\'s lists')
        return added
    # Only prefabs with a model of their own (hair_beard_models.txt, the .pac
    # files of the game's file list). The others are pieces that borrow another
    # hair's parts and are never worn alone; cd_phw_00_hair_00_0504_02 crashed
    # the game when worn.
    models = {l.strip() for l in open(os.path.join(folder, 'hair_beard_models.txt'), encoding='utf-8') if l.strip()}
    skipped = []
    for line in open(path, encoding='utf-8'):
        parts = line.split()
        if len(parts) != 2 or line.startswith('#'):
            continue
        slot, name = int(parts[0]), parts[1]
        sets = per_slot.setdefault(slot, [])
        if any(ms['names'] and ms['names'][0] == name for ms in sets):
            continue
        if name not in models:
            skipped.append(name)
            continue
        sets.append({'attrs': {}, 'lists': [f'<MeshList MeshFileName="{name}"/>'], 'names': [name], 'icon': ''})
        added.append(name)
        if len(sets) > MAX_OPTIONS:
            raise SystemExit(f'{SLOT_NAMES[slot]} has {len(sets)} options, more than the game can store ({MAX_OPTIONS})')
    if skipped:
        print(f'  left out {len(skipped)} hair/beard prefabs without a model of their own:', ', '.join(skipped))
    return added


EXTRA_MESHES = [(slot, 'cd_pom_00_head_00_0001_oongka', '1_pc/5_pom/head/head/cd_pom_oongka_head_0001.pabc',
                 'ui/texture/image/customizeimage/k_cd_pom_00_head_00_0001.dds') for slot in (1, 5)]


# Slots where "none" must be possible. The game does not treat 255 as none in
# a slot that has options: it falls back to the slot's Default (option 0, a
# beard). A "nothing" option added at the end and made the Default crashed the
# game on load, both without any mesh and with the game's empty belt prefab,
# so it is off unless --empty is given.
EMPTY_SLOTS = (3, 5)
EMPTY_PREFAB = 'cd_phm_00_bag_belt_empty'


def add_empty_options(per_slot, headers):
    for slot in EMPTY_SLOTS:
        sets = per_slot.setdefault(slot, [])
        sets.append({'attrs': {'ShowInGame': 'False'}, 'lists': [f'<MeshList MeshFileName="{EMPTY_PREFAB}"/>'],
                     'names': [], 'icon': ''})
        header = headers.get(slot, '')
        header, n = re.subn(r'Default\s*=\s*"\d+"', f'Default="{len(sets) - 1}"', header)
        headers[slot] = header if n else header + f' Default="{len(sets) - 1}"'


def write_meshparam(path, per_slot, headers, template):
    """Writes the merged list, keeping the comment block of the original file."""
    head = template[:template.index('<MeshParam>')]
    out = [head.rstrip() + '\n\n<MeshParam>\n']

    for slot in sorted(set(per_slot) | set(headers)):
        out.append(f'\t<ParamDesc Index="{slot}"{headers.get(slot, "")}>\n')
        for i, ms in enumerate(per_slot.get(slot, [])):
            attrs = ''.join(f' {k}="{v}"' for k, v in ms['attrs'].items())
            out.append(f'\t\t<MeshSet Index="{i}"{attrs}>\n')
            for l in ms['lists']:
                out.append(f'\t\t\t{l}\n')
            out.append('\t\t</MeshSet>\n')
        out.append('\t</ParamDesc>\n\n')

    out.append('</MeshParam>\n')

    with open(path, 'w', encoding='utf-8-sig', newline='\r\n') as f:
        f.write(''.join(out))


# The mod's own skin colours: added to its palette 54 (which the old version
# used for skin). The game's skin colour setting uses palette 52, so they are
# appended there instead - the decoration file stays the game's own, and the
# existing skin colours keep their numbers.
SKIN_PALETTE = 52
MOD_SKIN_SOURCE = 54


def palette_color_lines(text, index):
    """The <Color .../> elements of a palette, as written."""
    m = re.search(r'<PaletteInfo\s+PaletteIndex="' + str(index) + r'"[^>]*>(.*?)</PaletteInfo>', strip_comments(text), re.S)
    return re.findall(r'<Color\s[^>]*/>', m.group(1)) if m else []


# The mod's extra colours, appended to the game's own palettes: (palette the
# colours go to, palette of the mod's file they come from).
EXTRA_COLOURS = [(50, 50), (54, 54), (SKIN_PALETTE, MOD_SKIN_SOURCE)]


def build_palette(out_path, mod_path, original_path):
    """The game's own colour palette file with the mod's extra colours appended.

    The old mod's palette file is a copy of an older game version (palettes 0
    and 2 lack colours the game added since, and the skin palette 52 differs),
    so it is not used as the base: every colour of the game keeps its number
    and only the mod's own colours are added at the end."""
    if not os.path.exists(original_path):
        raise SystemExit(f'the game\'s colour palette is needed: {original_path}')
    key = lambda line: tuple(re.findall(r'(\w)="(\d+)"', line))
    game = open(original_path, encoding='utf-8-sig', errors='replace').read()
    mod = open(mod_path, encoding='utf-8-sig', errors='replace').read()
    text = game
    counts = {}
    for target, source in EXTRA_COLOURS:
        game_source = {key(l) for l in palette_color_lines(game, source)}
        extra = [l for l in palette_color_lines(mod, source) if key(l) not in game_source]
        m = re.search(r'(<PaletteInfo\s+PaletteIndex="' + str(target) + r'"[^>]*>.*?)(</PaletteInfo>)', text, re.S)
        if not m:
            raise SystemExit(f'palette {target} is not in the game\'s colour palette file')
        existing = {key(l) for l in re.findall(r'<Color\s[^>]*/>', strip_comments(m.group(1)))}
        extra = [l for l in extra if key(l) not in existing]
        if len(existing) + len(extra) > 255:
            raise SystemExit(f'palette {target} would have {len(existing) + len(extra)} colours, more than a setting can choose (255)')
        block = ''.join(f'\t{l} <!-- Character Creator -->\n' for l in extra)
        text = text[:m.end(1)] + block + text[m.end(1):]
        counts[target] = (len(existing), len(extra))
    with open(out_path, 'w', encoding='utf-8-sig', newline='\r\n') as f:
        f.write(text.replace('\r\n', '\n'))
    return counts


def read_palettes(path):
    """{palette index: [(r, g, b), ...]}"""
    text = strip_comments(open(path, encoding='utf-8-sig', errors='replace').read())
    palettes = {}
    for p in re.finditer(r'<PaletteInfo\s+PaletteIndex="(\d+)"[^>]*>(.*?)</PaletteInfo>', text, re.S):
        colors = [tuple(int(c) for c in m) for m in
                  re.findall(r'<Color\s+r="(\d+)"\s+g="(\d+)"\s+b="(\d+)"', p.group(2))]
        palettes[int(p.group(1))] = colors
    return palettes


def read_base_character(path):
    """Values of a race's base character file (cd_phm_macduff_00000.app_xml)
    that the plugin swaps in when the player loads as that race and gender."""
    text = strip_comments(open(path, encoding='utf-8-sig', errors='replace').read())

    def attr(element, name):
        m = re.search(r'<' + element + r'\b.*?<Prefab\b[^>]*\b' + name + r'="([^"]*)"', text, re.S) \
            if element in ('Nude', 'Head', 'Hair') else \
            re.search(r'<' + element + r'\b[^>]*\b' + name + r'="([^"]*)"', text, re.S)
        return m.group(1) if m else ''

    return [attr('Customization', 'CustomizationFile'),
            attr('Nude', 'Name'), attr('Nude', 'CharacterScale'),
            attr('Head', 'Name'), attr('Head', 'HeadScale'),
            attr('Hair', 'Name')]


TEXTURE_PALETTE_FILE = 'customizationtexturepalette.xml'

# The characters' starting appearance values (the game's customization files,
# kept in tools/game_data).
CUSTOMIZATION_FILES = ['cd_phm_macduff_customization.paccd_xml', 'cd_phw_damian_customization.paccd_xml',
                       'cd_phm_oongka_customization.paccd_xml']

# The three playable characters share the merged mesh list. Each copy gets a
# few hidden whisker options at the end (Kliff 0, Damiane 1, Oongka 2), so the
# plugin can tell from a character's option count whose look it is.
CHARACTERS = [('kliff', 0), ('damian', 1), ('oongka', 2)]
MARKER_SLOT = 5

# Their base character files. The plugin overwrites values in place when the
# character loads as another race, so the scale values are padded to leave
# room for any other race's value.
BASE_FILES = [APPEARANCE,
              r'0009\character\appearance\1_pc\2_phw\cd_phw_damian\cd_phw_damian_00000.app_xml',
              r'0009\character\appearance\1_pc\1_phm\cd_phm_oongka\cd_phm_oongka_00000.app_xml']
SCALE_WIDTH = 8


def pad_scales(path):
    """Pads CharacterScale / HeadScale to SCALE_WIDTH characters (1.02 -> 1.020000)."""
    text = open(path, encoding='utf-8-sig', errors='replace').read()

    def pad(m):
        value = m.group(2)
        if '.' not in value:
            value += '.'
        return f'{m.group(1)}="{value.ljust(SCALE_WIDTH, "0")}"'

    text = re.sub(r'\b(CharacterScale|HeadScale)="([0-9.]+)"', pad, text)
    with open(path, 'w', encoding='utf-8-sig', newline='\r\n') as f:
        f.write(text.replace('\r\n', '\n'))


def character_order(per_slot, name, own_meshes):
    """The merged options in the order one character's list uses.

    The save stores mesh choices as positions in the character's own list, so
    that list's options come first, in the game's order. Slots the game's
    list leaves empty (Damiane's body and head, Oongka's head: they only come
    from the base character file) start with the character's own mesh, which
    the save's 0 has always meant. Kliff keeps the merged order, which the mod's
    earlier lists (and saved profiles) use."""
    if name == 'kliff':
        return {slot: list(sets) for slot, sets in per_slot.items()}

    path = os.path.join(ORIGINAL_META, f'meshparam_example_{name}.xml')
    game = parse(path) if os.path.exists(path) else {}
    order = {}
    for slot, sets in per_slot.items():
        by_key = {mesh_key(ms): ms for ms in sets}
        first = [by_key[mesh_key(ms)] for ms in game.get(slot, {}).get('sets', []) if mesh_key(ms) in by_key]
        if not first and slot in own_meshes:
            first = [ms for ms in sets if ms['names'] and ms['names'][0] == own_meshes[slot]][:1]
        picked = {id(ms) for ms in first}
        order[slot] = first + [ms for ms in sets if id(ms) not in picked]
    return order


def with_marker(per_slot, extra):
    """Copy of the merged lists with `extra` hidden duplicates of the last
    whisker option (never shown, never chosen)."""
    result = dict(per_slot)
    sets = list(per_slot.get(MARKER_SLOT, []))
    if extra and not sets:
        raise SystemExit('no whisker options to build the character marker from')
    for _ in range(extra):
        marker = dict(sets[-1])
        marker['attrs'] = dict(marker['attrs'], ShowInGame='False')
        sets.append(marker)
    if len(sets) > MAX_OPTIONS:
        raise SystemExit('whisker list too long for the character marker')
    result[MARKER_SLOT] = sets
    return result


def read_decoration_params(path):
    """[(index, uikey, min, max, default, palette or -1), ...] for active params."""
    text = strip_comments(open(path, encoding='utf-8-sig', errors='replace').read())
    params = []
    for m in re.finditer(r'<ParamDesc\s+([^>]*?)/?>', text):
        a = dict(re.findall(r'(\w+)\s*=\s*"([^"]*)"', m.group(1)))
        if 'Index' not in a:
            continue
        params.append((int(a['Index']), a.get('UIKey', '-'), int(a.get('Min', 0)), int(a.get('Max', 100)),
                       int(a.get('Default', 0)), int(a.get('PaletteIndex', -1))))
    return params


def read_texture_palettes(path):
    """{palette index: [texture file, ...]}"""
    if not os.path.exists(path):
        print('  texture palette not found, tattoo types will use the parameter range')
        return {}
    text = strip_comments(open(path, encoding='utf-8-sig', errors='replace').read())
    result = {}
    for m in re.finditer(r'<TexturePalette\s+Index="(\d+)"[^>]*>(.*?)</TexturePalette>', text, re.S):
        result[int(m.group(1))] = re.findall(r'File="([^"]*)"', m.group(2))
    return result


def color_name(rgb):
    """A readable shade name such as 'Honey' or 'Ash', from hue and lightness."""
    r, g, b = (c / 255 for c in rgb)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    if s < 0.12:
        return 'Black' if l < 0.15 else 'Charcoal' if l < 0.35 else 'Ash' if l < 0.6 else 'Silver' if l < 0.85 else 'White'
    hue = h * 360
    if l < 0.25:
        return 'Espresso' if hue < 50 else 'Forest' if hue < 170 else 'Midnight' if hue < 260 else 'Plum'
    names = [(15, 'Crimson'), (30, 'Copper'), (45, 'Auburn' if l < 0.45 else 'Ginger'),
             (60, 'Honey' if l > 0.5 else 'Chestnut'), (75, 'Blonde' if l > 0.6 else 'Mocha'),
             (160, 'Moss'), (200, 'Teal'), (255, 'Azure'), (290, 'Violet'), (330, 'Rose'), (360, 'Crimson')]
    for limit, name in names:
        if hue < limit:
            return name
    return 'Crimson'


def convert_icon(src, dst):
    from PIL import Image
    with Image.open(src) as im:
        im = im.convert('RGBA')
        im.thumbnail((ICON_SIZE, ICON_SIZE))
        im.save(dst, optimize=True)


def pack_runtime_data(folder, out_path):
    """Packs menu.txt and the icons into one file, built into CharacterCreator.asi
    as a resource: the plugin unpacks it into bin64/CharacterCreator, so the
    .asi is all a mod manager has to install.

    Format: b'CCDATA1\0', version (NUL-terminated), file count (uint32), then per
    file: name length (uint16), name (UTF-8, '/' separators), size (uint32), bytes."""
    import struct
    files = []
    for root, _, names in os.walk(folder):
        for n in sorted(names):
            full = os.path.join(root, n)
            files.append((os.path.relpath(full, folder).replace(os.sep, '/'), full))
    with open(out_path, 'wb') as f:
        f.write(b'CCDATA1\0' + VERSION.encode() + b'\0' + struct.pack('<I', len(files)))
        for name, full in files:
            data = open(full, 'rb').read()
            encoded = name.encode('utf-8')
            f.write(struct.pack('<H', len(encoded)) + encoded + struct.pack('<I', len(data)) + data)
    print(f'  packed {len(files)} files for the plugin ({os.path.getsize(out_path) // 1024} KB)')


# Damiane's shield (one-handed, right hand) is carried from the back to the
# hand by an entry only the women's player description has; a male Damiane
# (who takes the men's descriptions) kept it on her back. The men's player
# descriptions get the same entry, and the dock override that hides it.
MALE_DESCRIPTIONS = ['phm_description_player_001.xml', 'phm_description_player_kliff.xml']
DESCRIPTION_FOLDER = r'0009\character\descriptors\characterdescription'
SHIELD_R = ('<PartInOutSocket PartName="CD_MainWeapon_Shield_R" InSocketBone="Spine2_B_Shield_Socket" '
            'OutSocketBone="RHand_Socket" InChildSocketBone="Spine2_B_Shield_ChildSocket" '
            'OutChildSocketBone="Basic_ChildSocket" BagSocketBone="Bag_Shield_Socket"/>')
SHIELD_R_OUT = '<PartInOutSocket PartName="CD_MainWeapon_Shield_R" Visible="Out"/>'


def male_shield_descriptions(game_out):
    import game_files
    folder = os.path.join(game_out, DESCRIPTION_FOLDER)
    os.makedirs(folder, exist_ok=True)
    for name in MALE_DESCRIPTIONS:
        entry = next(game_files.entries('characterdescription/' + re.escape(name) + '$'))
        text = game_files.read(entry).decode('utf-8-sig')
        lines = text.split(chr(13) + chr(10))
        out = []
        for line in lines:
            out.append(line)
            indent = line[:len(line) - len(line.lstrip())]
            if 'PartName="CD_MainWeapon_Shield_L"' in line:
                out.append(indent + (SHIELD_R if 'InSocketBone' in line else SHIELD_R_OUT))
        with open(os.path.join(folder, name), 'w', encoding='utf-8-sig', newline='') as f:
            f.write((chr(13) + chr(10)).join(out))
        print('  male description with Damiane shield entry:', name)


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Build the unified Character Creator package.')
    parser.add_argument('source', nargs='?', default=SOURCE)
    parser.add_argument('output', nargs='?', default=OUTPUT)
    parser.add_argument('--empty', action='store_true',
                        help='add "nothing" beard / slot 5 options (crashes the game when loaded - kept for research)')
    parser.add_argument('--identity', default=DEFAULT_IDENTITY,
                        help='race and gender the character loads as, e.g. "Human Male"')
    args = parser.parse_args()

    identity = args.identity
    source = args.source
    output = os.path.abspath(args.output)
    game_out = os.path.join(output, 'Character Creator')
    runtime_out = os.path.join(output, 'bin64', 'CharacterCreator')
    base = os.path.join(source, BASE_FOLDER)

    if os.path.exists(output):
        shutil.rmtree(output)

    print('copying shared game files from', BASE_FOLDER)
    for sub in ('0009', '0012'):
        shutil.copytree(os.path.join(base, sub), os.path.join(game_out, sub))
    # The game's own decorationparam is used: the mod's copy swapped the hair
    # and skin colour names, so the barber's hair colour changed the skin.
    os.remove(os.path.join(game_out, DECORATION))
    os.remove(os.path.join(game_out, META, 'decorationparam_player_damian.xml'))
    # The mod's Oongka base file names a copy of its own, a file the game does
    # not have (the mod manager only replaces existing files): Oongka then had
    # no appearance values at all. He uses the game's player file, as without
    # the mod.
    os.remove(os.path.join(game_out, META, 'decorationparam_player_oongka.xml'))
    # The game's own starting-look files (customization/*.paccd_xml) are used
    # too; the mod's copies only existed to match its decorationparam.
    shutil.rmtree(os.path.join(game_out, CUSTOMIZATION_FOLDER))
    # The old barber screen change (ui/xml/.../barbershopview.html) belonged to
    # the barber-based plugin; the editor does not need it.
    old_ui = os.path.join(game_out, '0012', 'ui', 'xml')
    if os.path.exists(old_ui):
        shutil.rmtree(old_ui)

    race_name, gender_name = identity.split()
    female = gender_name == 'Female'
    print('character loads as', identity)
    shutil.copy2(os.path.join(source, identity, APPEARANCE), os.path.join(game_out, APPEARANCE))
    for path in BASE_FILES:
        pad_scales(os.path.join(game_out, path))
    oongka = os.path.join(game_out, BASE_FILES[2])
    text = open(oongka, encoding='utf-8-sig').read()
    with open(oongka, 'w', encoding='utf-8-sig', newline='\r\n') as f:
        f.write(text.replace('decorationparam_player_oongka.xml', 'decorationparam_player.xml').replace('\r\n', '\n'))

    for f in os.listdir(source):
        # The old barber-based plugin is replaced by CharacterCreator.asi, and
        # the female animations only belong with a female base character.
        if not os.path.isfile(os.path.join(source, f)) or f.lower().endswith('.asi'):
            continue
        if f == FEMALE_ANIMATIONS and not female:
            continue
        shutil.copy2(os.path.join(source, f), game_out)

    # The release's version and description.
    import json
    info_path = os.path.join(game_out, 'mod.json')
    info = json.load(open(info_path, encoding='utf-8')) if os.path.exists(info_path) else {'modinfo': {}}
    info['modinfo'].update({'title': 'Character Creator Enhanced', 'version': VERSION, 'author': 'Khione',
                            'description': DESCRIPTION, 'nexus_url': 'https://www.nexusmods.com/crimsondesert/mods/837'})
    with open(info_path, 'w', encoding='utf-8') as f:
        json.dump(info, f, indent=2)

    print('merging mesh lists')
    per_slot, headers = merged_mesh_lists(source)
    added = add_character_meshes(per_slot, headers)
    added += add_all_hair_and_beards(per_slot)
    if args.empty:
        add_empty_options(per_slot, headers)
    if added:
        print('  added the characters\' own meshes:', ', '.join(added))
    template = open(os.path.join(base, MESHPARAM), encoding='utf-8-sig', errors='replace').read()
    orders = []
    for ch, (name, extra) in enumerate(CHARACTERS):
        body, head, hair = [read_base_character(os.path.join(game_out, BASE_FILES[ch]))[i] for i in (1, 3, 5)]
        order = character_order(per_slot, name, {0: body, 1: head, 2: hair, MARKER_SLOT: head})
        orders.append({slot: {id(ms): i for i, ms in enumerate(sets)} for slot, sets in order.items()})
        write_meshparam(os.path.join(game_out, META, f'meshparam_example_{name}.xml'),
                        with_marker(order, extra), headers, template)
    for slot in sorted(per_slot):
        print(f'  {SLOT_NAMES[slot]:9s} {len(per_slot[slot])} options')

    counts = build_palette(os.path.join(game_out, PALETTE), os.path.join(base, PALETTE),
                           os.path.join(ORIGINAL_META, os.path.basename(PALETTE)))
    for palette, (game, extra) in sorted(counts.items()):
        print(f'  colour palette {palette}: {game} of the game + {extra} of the mod')


    print('writing menu data')
    os.makedirs(os.path.join(runtime_out, 'icons'))
    icon_dir = os.path.join(base, ICONS)
    converted = {}
    lines = ['# Character Creator menu data. Generated by tools/build_data.py - do not edit.',
             '# mesh <slot> <Kliff option> <race> <shown 0/1> <icon or -> <mesh file> <Damiane option> <Oongka option>',
             '# color <palette> <index> <r> <g> <b> <name>',
             '# param <index> <uikey> <min> <max> <default> <palette or -1>',
             '# texture <palette> <index> <file or ->',
             '# loaded <gender 0 male / 1 female> <race 0 human / 1 orc / 2 dwarf / 3 goblin>',
             f'loaded {GENDERS.index(gender_name)} {RACES.index(race_name)}',
             '# marker <slot> <kliff option count>: Damiane has one more option, Oongka two',
             f'marker {MARKER_SLOT} {len(per_slot.get(MARKER_SLOT, []))}']

    # Heads need a prefab: a few in the game's lists only have a model and
    # show no face (cd_phm_00_head_00_4003, ...). Not offered.
    import game_files
    head_prefabs = {e['path'].rsplit('/', 1)[1][:-len('.prefab')]
                    for e in game_files.entries(r'^character/bin__/prefab/.*\.prefab$')}

    for slot in sorted(per_slot):
        for i, ms in enumerate(per_slot[slot]):
            icon = '-'
            src_name = os.path.basename(ms['icon'].replace('/', os.sep))
            src = os.path.join(icon_dir, src_name)
            if src_name and os.path.exists(src):
                icon = converted.get(src_name)
                if not icon:
                    icon = os.path.splitext(src_name)[0].lower() + '.png'
                    convert_icon(src, os.path.join(runtime_out, 'icons', icon))
                    converted[src_name] = icon
            shown = 0 if ms['attrs'].get('ShowInGame', 'True').lower() == 'false' else 1
            name = ms['names'][0] if ms['names'] else '-'
            # The game's own lists have an eyebrow part among the heads, hair
            # and beards (cd_phw_00_eyebrow_00_0001): not offered.
            if '_eyebrow_' in name or (slot in (1, 5) and ('_head' not in name or name not in head_prefabs)):
                shown = 0
            lines.append(f'mesh {slot} {i} {race_code(ms) or "-"} {shown} {icon} {name} '
                         f'{orders[1][slot][id(ms)]} {orders[2][slot][id(ms)]}')

    for index, colors in sorted(read_palettes(os.path.join(game_out, PALETTE)).items()):
        counts = {}
        for i, rgb in enumerate(colors):
            shade = color_name(rgb)
            counts[shade] = counts.get(shade, 0) + 1
            lines.append(f'color {index} {i} {rgb[0]} {rgb[1]} {rgb[2]} {shade} {counts[shade]}')

    for index, key, lo, hi, default, palette in read_decoration_params(os.path.join(ORIGINAL_META, os.path.basename(DECORATION))):
        lines.append(f'param {index} {key} {lo} {hi} {default} {palette}')

    lines.append('# base <gender> <race> <customization file> <body> <scale> <head> <head scale> <hair>   ("-" = empty)')
    for folder in FOLDER_ORDER:
        path = os.path.join(source, folder, APPEARANCE)
        if not os.path.exists(path):
            continue
        race, gender = folder.split()
        values = [v if v else '-' for v in read_base_character(path)]
        values[2] = BASE_SCALE_FIXES.get(folder, values[2])
        lines.append(f'base {GENDERS.index(gender)} {RACES.index(race)} ' + ' '.join(values))

    # The part of each head that holds its eyes (see tools/head_eyes.txt): the
    # plugin rebuilds a head through one with other eyes, so they reload.
    head_eyes = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'head_eyes.txt')
    if os.path.exists(head_eyes):
        lines.append('# eyes <head mesh> <part holding its eyes>')
        for l in open(head_eyes, encoding='utf-8'):
            parts = l.split()
            if len(parts) == 2 and not l.startswith('#') and parts[1] not in ('-', '?'):
                lines.append(f'eyes {parts[0]} {parts[1]}')
    else:
        print('  head_eyes.txt missing - eye colour changes may not reload every head')

    # Each character's starting appearance values (their customization file),
    # for characters the game has not given values yet (see GameCreateValues).
    lines.append('# start <character> <250 values, one per decoration index>')
    params = read_decoration_params(os.path.join(ORIGINAL_META, os.path.basename(DECORATION)))
    defaults = {index: default for index, key, lo, hi, default, palette in params}
    by_key = {}
    for index, key, lo, hi, default, palette in params:
        by_key.setdefault(key, index)
    for ch, name in enumerate(CUSTOMIZATION_FILES):
        values = [defaults.get(i, 0) for i in range(250)]
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'game_data', name)
        if os.path.exists(path):
            for key, value in re.findall(r'<Data\s+UIKey="([^"]+)"\s+Value="(\d+)"', open(path, encoding='utf-8-sig').read()):
                if key in by_key and by_key[key] < 250:
                    values[by_key[key]] = min(int(value), 255)
        else:
            print(f'  {name} missing - {ch} starts from the default values')
        lines.append(f'start {ch} ' + ' '.join(str(v) for v in values))

    # Each character's own look (their base character file in the package),
    # used instead of the race's base when they are playing as themselves.
    lines.append('# own <character 0 Kliff / 1 Damiane / 2 Oongka> <same values as base>')
    for ch, path in enumerate(BASE_FILES):
        values = [v if v else '-' for v in read_base_character(os.path.join(game_out, path))]
        lines.append(f'own {ch} ' + ' '.join(values))

    # Type counts (tattoos, scars...) come from the game's texture palette, or
    # Slinky's older copy if the game's has not been extracted.
    game_palette = os.path.join(ORIGINAL_META, TEXTURE_PALETTE_FILE)
    for index, files in sorted(read_texture_palettes(game_palette if os.path.exists(game_palette) else TEXTURE_PALETTE).items()):
        for i, f in enumerate(files):
            lines.append(f'texture {index} {i} {os.path.basename(f) or "-"}')

    with open(os.path.join(runtime_out, 'menu.txt'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')

    print(f'  {len(converted)} icons converted')

    male_shield_descriptions(game_out)

    # The player characters' own heads and eyes (tools/private_eyes.py), loose
    # in the package's 0009: the head lists name them and the plugin gets the
    # list of copied heads. Their part table is Female Armor Fit's (the game's
    # with the armor copies) plus the heads, so both packages ship the same
    # table.
    import private_eyes
    print('private eyes:')
    private_eyes.build(os.path.join(game_out, '0009'), os.path.join(game_out, META),
                       os.path.join(runtime_out, 'private_heads.txt'), menu=os.path.join(runtime_out, 'menu.txt'),
                       table_path=ARMOR_FIT_TABLE if os.path.exists(ARMOR_FIT_TABLE) else None)
    # The table's size, for the plugin to tell whether the game loads this
    # table or another mod's (parttable.cpp).
    table = os.path.join(game_out, '0009', 'character', 'bin__', 'partprefabtable.pappt')
    with open(os.path.join(runtime_out, 'parttable.txt'), 'w') as f:
        f.write(f'{os.path.getsize(table)}'+chr(10))
    pack_runtime_data(runtime_out, os.path.join(output, 'CharacterCreator.data'))
    print('done:', output)


if __name__ == '__main__':
    main()
