"""Reads a part's models and textures for icon_render_blender.py: every option's
icon is drawn from its own model, on one mannequin, in the style of the game's
barber icons.

A model (character/model/...<name>.pac) gives per submesh: positions, normals, UVs,
triangles, and from its .pac_xml the shader and textures (the alpha of a hair's
base colour is the strands' cut-out; skin takes the normal map). Written to
<out>/<name>.npz + .json, the textures as .png beside them.

Usage: python icon_meshes.py <out dir> <part> ...
"""
import io
import json
import os
import re
import struct
import sys

import lz4.block
import numpy as np
from PIL import Image

import game_files
import pac
from iris_colours import raw

_paths = {}
_names = {}


def _load_index():
    if not _paths:
        for e in game_files.entries(r'^character/(model|modelproperty|texture|bin__/prefab)/'):
            path = e['path'].lower()
            _paths[path] = e
            _names.setdefault(path[path.rfind('/') + 1:], []).append(e)


def find(path):
    """The game's file by its path, any case (None if it has none). The index is read once."""
    _load_index()
    return _paths.get(path.lower())


def find_name(name, folder):
    """The game's file by its file name under a folder, any case (None if it has none)."""
    _load_index()
    return next((e for e in _names.get(name.lower(), []) if e['path'].lower().startswith(folder)), None)


BLOCK = {b'DXT1': 8, b'DXT3': 16, b'DXT5': 16, b'ATI1': 8, b'BC4U': 8, b'ATI2': 16, b'BC5U': 16}
DX10_BLOCK = {71: 8, 72: 8, 74: 16, 77: 16, 78: 16, 80: 8, 83: 16, 98: 16, 99: 16}


def texture(path):
    """The game's texture, its first mip, as an RGBA image (packed mips: see iris_colours)."""
    data = raw(find(path))
    fourcc = data[84:88]
    dx10 = fourcc == b'DX10'
    block = DX10_BLOCK[struct.unpack_from('<I', data, 128)[0]] if dx10 else BLOCK[fourcc]
    height, width = struct.unpack_from('<II', data, 12)
    start = 148 if dx10 else 128
    size = max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * block
    packed = struct.unpack_from('<I', data, 32)[0]
    body = data[start:start + size] if not packed or packed == size else lz4.block.decompress(data[start:start + packed], uncompressed_size=size)
    header = bytearray(data[:start])
    struct.pack_into('<11I', header, 32, *[0] * 11)
    struct.pack_into('<I', header, 28, 1)
    im = Image.open(io.BytesIO(bytes(header) + body))
    im.load()
    return im.convert('RGBA')


def materials(path):
    """Per material in order: (shader, base colour texture, normal map)."""
    entry = find(path)
    if not entry:
        return []
    text = game_files.read(entry).decode('utf-8-sig', 'replace')
    out = []
    for m in re.finditer(r'<SkinnedMeshMaterialWrapper\b([^>]*)>(.*?)</SkinnedMeshMaterialWrapper>', text, re.S):
        shader = re.search(r'_materialName="(\w+)"', m.group(0))
        base = re.search(r'StringItemID="_baseColorTexture"[^>]*>\s*<ResourceReferencePath_ITexture[^>]*_path="([^"]*)"', m.group(2))
        normal = re.search(r'StringItemID="_normalTexture"[^>]*>\s*<ResourceReferencePath_ITexture[^>]*_path="([^"]*)"', m.group(2))
        out.append((shader.group(1) if shader else '', base.group(1) if base else '', normal.group(1) if normal else ''))
    return out


def normal_png(path, out_dir):
    """A head's or body's normal map for Blender: the third axis worked out from
    the two the game stores."""
    png = os.path.basename(path).lower().replace('.dds', '.png')
    where = os.path.join(out_dir, png)
    if not os.path.exists(where):
        im = texture(path)
        im.thumbnail((1024, 1024))
        a = np.asarray(im.convert('RGB'), dtype=np.float32) / 255 * 2 - 1
        z = np.sqrt(np.clip(1 - a[..., 0] ** 2 - a[..., 1] ** 2, 0, 1))
        rgb = np.stack([a[..., 0], a[..., 1], z], 2)
        Image.fromarray(((rgb + 1) / 2 * 255).astype(np.uint8), 'RGB').save(where)
    return png


def part_models(name):
    """The models a part's prefab names (underwear, stubble and teeth left out:
    icons show the mannequin), or the model of that name."""
    prefab = find_name(name + '.prefab', 'character/bin__/prefab/')
    if prefab:
        paths = re.findall(rb'character/model/[\w/.-]+?\.pac', game_files.read(prefab))
        kept = [p.decode() for p in paths if not re.search(rb'underwear|_uw_|_fuzz_|_tooth_', p)]
        if kept:
            return list(dict.fromkeys(kept))
    model = find_name(name + '.pac', 'character/model/')
    if not model:
        raise KeyError(name)
    return [model['path']]


def export_part(name, out_dir, hair=False):
    """Writes each model of the part; returns their names for the Blender side.
    hair: all of it is hair, whatever its shaders (braids and ribbons have others)."""
    models = part_models(name)
    for path in models:
        export(os.path.basename(path)[:-len('.pac')], out_dir, path, hair)
    return [os.path.basename(p)[:-len('.pac')].lower() for p in models]


def bounds(name, out_dir):
    """A written model's box without its eye cover: (min, max)."""
    subs = json.load(open(os.path.join(out_dir, name.lower() + '.json')))
    data = np.load(os.path.join(out_dir, name.lower() + '.npz'))
    points = [data[f'pos{i}'] for i, s in enumerate(subs) if 'eyecover' not in (s['shader'] + s['name']).lower() and len(data[f'pos{i}'])]
    points = np.vstack(points)
    return points.min(0), points.max(0)


def export(name, out_dir, path=None, hair=False):
    """Writes the model's arrays and textures; returns its submeshes' description."""
    target = os.path.join(out_dir, name.lower() + '.json')
    if os.path.exists(target):
        subs = json.load(open(target))
        if not hair or all(s['texture'] or 'hair' in s for s in subs):
            return subs
    entry = find(path) if path else find_name(name + '.pac', 'character/model/')
    if not entry:
        raise KeyError(name)
    model = pac.Model(game_files.read(entry))
    geometry = model.geometry(0)
    mats = materials(entry['path'].replace('character/model/', 'character/modelproperty/').replace('.pac', '.pac_xml'))
    arrays, subs = {}, []
    for i, g in enumerate(geometry):
        at = g['voffset']
        count = len(g['pos'])
        if len(g['tris']) and g['tris'].max() >= count:
            raise ValueError(f'{name}: triangles past the vertices')
        vertex = np.frombuffer(bytes(model.data[at:at + count * pac.STRIDE]), dtype=np.uint8).reshape(count, pac.STRIDE)
        arrays[f'pos{i}'] = g['pos'].astype(np.float32)
        arrays[f'normal{i}'] = g['normal'].astype(np.float32)
        arrays[f'uv{i}'] = vertex[:, 8:12].copy().view('<f2').astype(np.float32)
        arrays[f'tris{i}'] = g['tris'].astype(np.int32)
        shader, base, normal = mats[i] if i < len(mats) else ('', '', '')
        png = ''
        bump = ''
        if not hair and normal and shader != 'SkinnedMeshHair' and 'eyecover' not in shader.lower() and find(normal):
            bump = normal_png(normal, out_dir)
        if base and (hair or shader == 'SkinnedMeshHair') and find(base):
            png = os.path.basename(base).lower().replace('.dds', '.png')
            where = os.path.join(out_dir, png)
            if not os.path.exists(where):
                im = texture(base)
                im.thumbnail((1024, 1024))
                im.save(where)
        subs.append({'name': g['desc']['name'], 'shader': shader, 'texture': png, 'normal': bump, 'hair': hair})
    np.savez(os.path.join(out_dir, name.lower() + '.npz'), **arrays)
    json.dump(subs, open(target, 'w'))
    return subs


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for name in sys.argv[2:]:
        print(name, export_part(name, sys.argv[1]))


if __name__ == '__main__':
    main()
