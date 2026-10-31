"""Renders the icon of every option the menu shows, from its own model, on a
mannequin of its race (icon_meshes.py, then Blender with icon_render_blender.py).

Run after build_data.py (reads build/bin64/CharacterCreator/menu.txt). Icons
already there are kept, so it can stop and go on. On the CPU at low priority:
the game can stay open.

Writes cache/icon_renders/<part>.png; build_data.py takes them for the menu.
Usage: python icon_render.py [how many at most]
"""
import collections
import json
import os
import re
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

import game_files
import icon_meshes as M

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
MENU = os.path.join(ROOT, 'build', 'bin64', 'CharacterCreator', 'menu.txt')
MESHES = os.path.join(ROOT, 'cache', 'icon_meshes')
OUT = os.path.join(ROOT, 'cache', 'icon_renders')
BLENDER = r'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe'
SCRIPT = os.path.join(HERE, 'icon_render_blender.py')
BATCH = 25
BELOW_NORMAL = 0x00004000

# The mannequins chosen by hand (the camera was set on the man's): head, body.
MANNEQUINS = {
    'phm': ('phm_00_head_00_0001_macduff', 'phm_00_nude_01_0002_macduff'),
    'phw': ('phw_00_head_00_0111', 'phw_00_nude_00_0001_damian'),
    'pom': ('cd_pom_00_head_00_0001_oongka', 'pom_00_nude_00_0001_oongka'),
}


def original(name):
    """The game's part a menu name stands for (the characters' own copies: names.py)."""
    name = name.lower()
    if re.match(r'z[kdo]_', name):
        return 'cd_' + name[3:]
    if re.match(r'p[a-z]+_[kdo]0', name) and len(name) > 4 and name[3] == '_' and name[4] in 'kdo':
        return name[:4] + '0' + name[5:]
    return name


def race_of(part):
    m = re.match(r'(?:cd_)?(p[a-z]+?)_', part)
    return m.group(1) if m else 'phm'


def kind_of(part):
    for word, kind in (('_hair_', 'hair'), ('_beard_', 'beard'), ('_nude_', 'body'), ('_eyebrow_', 'eyebrow'), ('_head_', 'head')):
        if word in part:
            return kind
    return None


def shown_parts():
    parts = {}
    for line in open(MENU, encoding='utf-8', errors='replace'):
        p = line.split()
        if len(p) >= 7 and p[0] == 'mesh' and p[4] == '1':
            part = original(p[6])
            kind = kind_of(part)
            if kind:
                parts[part] = kind
    return parts


def eyes(race):
    found = []
    for side in ('eyeleft', 'eyeright'):
        name = f'{race}_00_{side}_00_0001'
        if M.find_name(name + '.pac', 'character/model/'):
            M.export(name, MESHES)
            found.append(name)
    return found


def decal_colour(name):
    """As the game's makeup icons have them; tattoos, scars and dirt dark."""
    for word, colour in (('lip', (0.45, 0.02, 0.03)), ('eye', (0.0, 0.55, 0.75)), ('cheek', (0.45, 0.25, 0.75)),
                         ('highlighter', (0.45, 0.25, 0.75)), ('shading', (0.45, 0.25, 0.75)), ('freckle', (0.12, 0.10, 0.10))):
        if word in name:
            return colour
    return (0.015, 0.015, 0.017)


def decal_mask(path, png):
    """How strongly a texture paints, from its most telling channel; False if it
    is one flat colour."""
    im = M.texture(path)
    im.thumbnail((1024, 1024))
    a = np.asarray(im, dtype=np.float32) / 255
    channel = a[..., int(np.argmax([a[..., i].std() for i in (3, 0, 1, 2)]) + 3) % 4]
    if channel.std() < 0.004:
        return None
    border = np.concatenate([channel[0], channel[-1], channel[:, 0], channel[:, -1]])
    away = np.abs(channel - np.median(border))
    away = np.clip(away / max(float(np.percentile(away, 99.8)), 0.02), 0, 1)
    Image.fromarray((away * 255).astype(np.uint8), 'L').save(os.path.join(MESHES, png))
    return away


def head_box(part):
    """A head part's models, its centre and height."""
    models = M.export_part(part, MESHES)
    heads = [m for m in models if '_eye' not in m and '_eyelash' not in m] or models
    lo, hi = M.bounds(heads[0], MESHES)
    return models, [float(x) for x in (lo + hi) / 2], float(hi[1] - lo[1])


def paints(model, mask):
    """Whether the mask paints anything on the model."""
    subs = json.load(open(os.path.join(MESHES, model + '.json')))
    data = np.load(os.path.join(MESHES, model + '.npz'))
    h, w = mask.shape
    weights = []
    for i, s in enumerate(subs):
        if 'eyecover' in (s['shader'] + s['name']).lower() or s['texture'] or not len(data[f'pos{i}']):
            continue
        uv = data[f'uv{i}']
        x = (uv[:, 0] % 1 * (w - 1)).astype(int)
        y = (uv[:, 1] % 1 * (h - 1)).astype(int)
        weights.append(mask[y, x])
    return (np.concatenate(weights) > 0.25).sum() >= 3


_pictures = {}


def game_picture(name):
    """The game's barber picture of a texture, if it has one (its list does not
    always name it): a path, or None."""
    if not _pictures:
        for e in game_files.entries(r'^ui/texture/image/customizeimage/'):
            _pictures[os.path.basename(e['path']).lower()] = e['path']
    return _pictures.get(f'cd_itemicon_barbershop_{name.lower()}.dds')


def flat_tattoo(path, out):
    """A tattoo the game has no picture for, as its pictures are: the design
    alone on nothing, filling the icon, in white (its alpha is the design) or its
    own colours if it has any. A solid fill: a swatch of its colour."""
    im = M.texture(path)
    im.thumbnail((1024, 1024))
    a = np.asarray(im, dtype=np.float32) / 255
    colourful = a[..., :3].std(axis=(0, 1)).max() > 0.03
    if a[..., 3].min() > 0.85 and not colourful:
        icon = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
        ImageDraw.Draw(icon).rectangle((24, 24, 231, 231), fill=tuple(int(x * 255) for x in a[0, 0, :3]) + (255,), outline=(110, 110, 110, 255), width=3)
        icon.save(out)
        return True
    rows, cols = np.where(a[..., 3] > 0.15)
    if not len(rows):
        return False
    side = max(rows.max() - rows.min(), cols.max() - cols.min()) + 1
    pad = int(side * 0.06) + 1
    box = (max(0, cols.min() - pad), max(0, rows.min() - pad), cols.max() + pad + 1, rows.max() + pad + 1)
    cut = im.crop(box)
    if not colourful:
        # Full strength: some textures paint at half, the game's pictures are solid white.
        design = np.asarray(cut.getchannel('A'), dtype=np.float32) / 255
        top = float(np.median(design[design > 0.1])) if (design > 0.1).any() else 1.0
        strong = Image.fromarray((np.clip(design / max(top, 0.05), 0, 1) * 255).astype(np.uint8), 'L')
        white = Image.new('L', cut.size, 235)
        cut = Image.merge('RGBA', (white, white, white, strong))
    k = 256 / max(cut.size)
    cut = cut.resize((max(1, int(cut.width * k)), max(1, int(cut.height * k))), Image.LANCZOS)
    icon = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    icon.paste(cut, ((256 - cut.width) // 2, (256 - cut.height) // 2))
    icon.save(out)
    return True


def decal_jobs():
    """Types the game has no picture for: tattoos flat as its pictures are, the
    rest (scars, dirt, makeup) painted on the mannequin, seen by the one camera."""
    man_height = head_box(MANNEQUINS['phm'][0])[2]
    text = game_files.read(next(game_files.entries(r'^character/descriptors/customizationmeta/customizationtexturepalette\.xml$'))).decode('utf-8-sig', 'replace')
    jobs = {}
    for t in re.findall(r'<Texture\b[^>]*>', re.sub(r'<!--.*?-->', '', text, flags=re.S)):
        path = re.search(r'File="([^"]*)"', t).group(1)
        icon = re.search(r'IconPath="([^"]*)"', t)
        if not path or (icon and icon.group(1).lower().startswith('ui/') and 'empty_image' not in icon.group(1).lower()):
            continue
        name = os.path.basename(path)[:-4].lower()
        out = os.path.join(OUT, 'type_' + name + '.png')
        if name in jobs or os.path.exists(out) or not M.find(path) or game_picture(name):
            continue
        if 'tattoo' in name:
            if not flat_tattoo(path, out):
                print('no design in', name, flush=True)
            continue
        female = 'phw' in name or 'pow' in name
        head, body = MANNEQUINS['phw' if female else 'phm']
        on_body = 'nude' in name
        png = 'decal_' + name + '.png'
        mask = decal_mask(path, png)
        if mask is None:
            print('one flat colour', name, flush=True)
            continue
        skin = M.export_part(head, MESHES) + M.export_part(body, MESHES) + eyes('phw' if female else 'phm')
        model = M.export_part(body, MESHES)[0] if on_body else M.export_part(head, MESHES)[0]
        if not paints(model, mask):
            print('paints nothing', name, flush=True)
            continue
        job = {'out': out, 'skin': list(dict.fromkeys(skin)), 'hair': [],
               'decal': {'model': model, 'png': png, 'colour': decal_colour(name)}}
        if on_body:
            lo, hi = M.bounds(model, MESHES)
            job.update(view='body', center=[float(x) for x in (lo + hi) / 2], height=float(hi[1] - lo[1]))
        else:
            _, center, height = head_box(head)
            job.update(view='face', center=center, scale=height / man_height)
            if 'makeup' in name:
                job.update(view='makeup_eye' if 'eye' in name else 'makeup_face', scale=1)
                if 'eye' in name:
                    # As the game's pictures: the eye on the viewer's right.
                    boxes = [M.bounds(e.lower(), MESHES) for e in eyes('phw' if female else 'phm')]
                    lo, hi = max(boxes, key=lambda b: b[0][0] + b[1][0])
                    job['center'] = [float(x) for x in (lo + hi) / 2]
        jobs[name] = job
    return list(jobs.values())


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    os.makedirs(MESHES, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    parts = shown_parts()
    print('options to draw', collections.Counter(parts.values()).most_common(), flush=True)
    mannequins = dict(MANNEQUINS)
    for race in sorted({race_of(p) for p in parts}):
        if race not in mannequins:
            head = next((p for p, k in sorted(parts.items()) if k == 'head' and race_of(p) == race), None)
            body = next((p for p, k in sorted(parts.items()) if k == 'body' and race_of(p) == race), None)
            mannequins[race] = (head, body) if head else mannequins['phw' if race.endswith('w') else 'phm']
    print('mannequins', mannequins, flush=True)

    _, man_center, man_height = head_box(MANNEQUINS['phm'][0])
    references = {'face': man_center, 'makeup_face': man_center, 'makeup_eye': [0.0, 1.6875, -0.1105]}

    jobs, failed = [], []
    for part, kind in sorted(parts.items()):
        out = os.path.join(OUT, part + '.png')
        if os.path.exists(out):
            continue
        race = race_of(part)
        head, body = mannequins[race]
        try:
            job = {'out': out, 'skin': eyes(race), 'hair': []}
            if kind == 'body':
                job['view'] = 'body'
                job['skin'] += M.export_part(part, MESHES)
                models = [m for m in M.part_models(part)]
                lo, hi = M.bounds(os.path.basename(models[0])[:-len('.pac')], MESHES)
                job['center'] = [float(x) for x in (lo + hi) / 2]
                job['height'] = float(hi[1] - lo[1])
                job['skin'] += head_box(head)[0]
            elif kind == 'head':
                job['view'] = 'face'
                models, center, height = head_box(part)
                job['skin'] += models
                job['center'], job['scale'] = center, height / man_height
                if body:
                    job['skin'] += M.export_part(body, MESHES)
            else:
                job['view'] = 'face'
                models, center, height = head_box(head)
                job['skin'] += models
                job['center'], job['scale'] = center, height / man_height
                if body:
                    job['skin'] += M.export_part(body, MESHES)
                job['hair'] += M.export_part(part, MESHES, True)
            job['skin'] = list(dict.fromkeys(job['skin']))
            jobs.append(job)
        except Exception as ex:
            failed.append((part, repr(ex)[:100]))
            print('cannot read', part, repr(ex)[:100], flush=True)
    jobs += decal_jobs()
    if limit:
        jobs = jobs[:limit]
    print('to render', len(jobs), 'unreadable', len(failed), flush=True)
    json.dump(failed, open(os.path.join(OUT, '_unreadable.json'), 'w'), indent=1)

    config = os.path.join(MESHES, '_jobs.json')

    def run(batch):
        json.dump({'meshes': MESHES, 'threads': 6, 'samples': 28, 'size': 256, 'references': references, 'jobs': batch}, open(config, 'w'))
        r = subprocess.run([BLENDER, '-b', '--python-exit-code', '1', '-P', SCRIPT, '--', config], capture_output=True, text=True, errors='replace', creationflags=BELOW_NORMAL)
        return r.returncode

    done = 0
    for at in range(0, len(jobs), BATCH):
        batch = jobs[at:at + BATCH]
        if run(batch):
            print('a batch stopped: one by one', flush=True)
            for job in batch:
                if not os.path.exists(job['out']) and run([job]):
                    print('cannot render', job['out'], flush=True)
                    failed.append((os.path.basename(job['out']), 'render'))
        done += len(batch)
        print(f'{done}/{len(jobs)}', flush=True)
    json.dump(failed, open(os.path.join(OUT, '_unreadable.json'), 'w'), indent=1)
    print('finished', 'failed', len(failed), flush=True)


if __name__ == '__main__':
    main()
