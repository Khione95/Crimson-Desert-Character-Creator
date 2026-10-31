"""Part names across game versions.

The game update of 2026-10-30 renamed most player-character parts: the "cd_"
prefix went (cd_phm_00_head_00_0001 -> phm_00_head_00_0001; orc, dwarf and
goblin parts kept theirs, mostly), a few women's hair parts got an
"_itemhair" ending, and the parts' socket names lost "CD_" (CD_Hair -> Hair,
CD_TwoHandWeapon_Sword -> TwoHandWeapon_Sword). The mod's source files still
use the old names: they are read through new_name / rename_text.

game_data/part_renames.json: old name -> new name, made by make_renames.py
from the part tables of the two builds.

The player characters' copies of heads and eye models (private_eyes.py) are
marked in place, keeping the length (the copies' paths are swapped inside
binary files): cd_... -> zk_... / zd_... / zo_... as before, and a name
without the prefix gets the character's letter for its first digit
(phm_00_head_00_0001 -> phm_k0_head_00_0001). The plugin turns them back
(eyes.cpp).
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
RENAMES_FILE = os.path.join(HERE, 'game_data', 'part_renames.json')
_renames = None


# Chosen by hand. "No beard" is a beard option naming a model without a part
# (it shows nothing): the women's eyebrow model was one, but since the update
# it is a part (an eyebrow on the face) - a men's eyebrow model the game has
# without a part stands in (the plugin's NO_BEARD_MESH, menu.cpp).
OVERRIDES = {'cd_phw_00_eyebrow_00_0001': 'phm_00_eyebrow_00_0004',
             # Only its player version is left.
             'cd_phm_00_beard_00_0005_06': 'phm_00_beard_00_0005_06_player'}


def renames():
    global _renames
    if _renames is None:
        _renames = json.load(open(RENAMES_FILE, encoding='utf-8')) if os.path.exists(RENAMES_FILE) else {}
        _renames.update(OVERRIDES)
    return _renames


def new_name(name):
    return renames().get(name, name)


_NAME = re.compile(r'\bcd_[A-Za-z0-9_]+')
# Socket names (CD_Upperbody, CD_TwoHandWeapon_Sword); not file names in
# paths (.../CD_Itemicon_Barbershop_..., .../CD_PHM_00_Nude_Tattoo_...) or
# capitalised part names (CD_NHM_SO_UB_0013) or element names
# (<CD_Macduff_Emotion>), which the game kept.
_SOCKET = re.compile(r'(?<![A-Za-z0-9_/\\<])CD_(?=[A-Z][a-z])')


_PABC = re.compile(r'[A-Za-z0-9_/]+\.pabc')


def resolve_path(path, existing):
    """A skeleton variation path (1_pc/.../<name>.pabc) as the game has it now
    (existing: the game's paths, lower case): some kept "cd_" where their part
    lost it, some were renamed with their part."""
    folder, _, file = path.rpartition('/')
    stem = file[:-len('.pabc')]
    for candidate in (stem, renames().get(stem, stem), stem[3:] if stem.lower().startswith('cd_') else stem):
        full = f'{folder}/{candidate}.pabc' if folder else f'{candidate}.pabc'
        if full.lower() in existing:
            return full
    return None


def rename_text(text, pabc_paths=None):
    """Every old part name in a text file (XML) written the new way, and the
    "CD_" of socket names dropped. pabc_paths (the game's skeleton variation
    paths, lower case): .pabc paths are resolved against them (resolve_path);
    one the game no longer has is left as it was."""
    table = renames()
    if pabc_paths is not None:
        text = _PABC.sub(lambda m: resolve_path(m.group(0), pabc_paths) or m.group(0), text)
        # The resolved paths are final: protect them from the name pass.
        parts = re.split(r'([A-Za-z0-9_/]+\.pabc)', text)
        text = ''.join(part if i % 2 else _NAME.sub(lambda m: table.get(m.group(0), m.group(0)), part)
                       for i, part in enumerate(parts))
    else:
        text = _NAME.sub(lambda m: table.get(m.group(0), m.group(0)), text)
    return _SOCKET.sub('', text)


def race_code(name):
    """cd_phw_... / phw_... -> 'phw' (human woman), cd_pom_... / pom_... -> 'pom' (orc man), ..."""
    m = re.match(r'(?:cd_)?(p[a-z]{2})_', name or '')
    return m.group(1) if m else None


LETTERS = {'zk_': 'k', 'zd_': 'd', 'zo_': 'o'}
_UNPREFIXED = re.compile(r'(?i)p[a-z]{2}_0')


def copy_name(name, prefix):
    """A character's same-length copy of a part or file name (prefix zk_, zd_
    or zo_), or None if the name has neither form."""
    if name.startswith('cd_'):
        return prefix + name[3:]
    if _UNPREFIXED.match(name):
        return name[:4] + LETTERS[prefix] + name[5:]
    return None


def can_copy(name):
    return name.startswith('cd_') or bool(_UNPREFIXED.match(name))


_pabc_paths = None


def game_pabc_paths():
    """The game's skeleton variation paths (below character/binary/
    skeletonvariation/, lower case)."""
    global _pabc_paths
    if _pabc_paths is None:
        import game_files
        prefix = 'character/binary/skeletonvariation/'
        _pabc_paths = {e['path'][len(prefix):].lower() for e in game_files.entries(r'^character/binary/skeletonvariation/.*\.pabc$')}
    return _pabc_paths
