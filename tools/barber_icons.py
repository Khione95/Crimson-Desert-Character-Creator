"""The Barber edition's new tab icons, made from the game's own so they match:
pale cracked stone on the dark disc.

  Head  the makeup tab's head, without the brush: its left half mirrored
  Body  the body tattoo tab's figure, with the tattoo lines filled in
  Eyes  the eyeliner tab's eye inside a plain almond, without the wings
  Identity  two of that head, side by side (Gender and Race)

  python tools/barber_icons.py <out folder>   writes <name>.dds and a sheet.png
"""
import io
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_files  # noqa: E402

SIZE = 256
NAMES = {'head': 'ItemIcon_ItemGroup_makeup_cc_Head', 'body': 'ItemIcon_ItemGroup_makeup_cc_Body',
         'eyes': 'ItemIcon_ItemGroup_makeup_cc_Eyes', 'identity': 'ItemIcon_ItemGroup_makeup_cc_Identity'}


def game_icon(name):
    entry = next(game_files.entries(rf'(?i)^ui/texture/icon/itemicon_itemgroup_makeup_{name}\.dds$'))
    return np.array(Image.open(io.BytesIO(game_files.read(entry))).convert('RGBA')).astype(np.float32)


def stone_mask(icon):
    """The bright figure, apart from the disc."""
    return (icon[..., :3].mean(axis=2) * (icon[..., 3] / 255)) > 110


def disc_only(icon):
    """The picture with its figure taken out: the dark disc, from a ring of it."""
    disc = icon.copy()
    figure = np.array(Image.fromarray((stone_mask(icon) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(9))) > 0
    outside = icon[~figure & (icon[..., 3] > 40)]
    disc[figure] = np.median(outside, axis=0)
    return disc


def box_blur(a, radius):
    for axis in (0, 1):
        pad = [(0, 0), (0, 0)]
        pad[axis] = (radius + 1, radius)
        total = np.cumsum(np.pad(a, pad, mode='edge'), axis=axis)
        a = (np.take(total, range(2 * radius + 1, total.shape[axis]), axis=axis) - np.take(total, range(total.shape[axis] - 2 * radius - 1), axis=axis)) / (2 * radius + 1)
    return a


def compose(icon, figure):
    """The figure (a soft 0..255 mask) in stone from `icon`, on its disc. Stone
    missing under the mask is filled from the nearest stone, so cracks carry on."""
    stone = icon.copy()
    known = stone_mask(icon)
    filled = stone[..., :3] * known[..., None]
    weight = known.astype(np.float32)

    for radius in (3, 7, 15, 31, 63):
        blur = lambda a: box_blur(a, radius)
        w = blur(weight)
        c = np.stack([blur(filled[..., i]) for i in range(3)], axis=2)
        spread = c / np.maximum(w[..., None], 1e-3)
        missing = (weight < 0.5) & (w > 0.02)
        filled[missing] = spread[missing]
        weight[missing] = 1

    disc = disc_only(icon)
    a = (figure / 255.0)[..., None]
    out = disc.copy()
    out[..., :3] = disc[..., :3] * (1 - a) + filled * a
    out[..., 3] = np.maximum(disc[..., 3], figure)
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), 'RGBA')


def smooth(mask, grow=0):
    im = Image.fromarray((mask * 255).astype(np.uint8))
    if grow:
        im = im.filter(ImageFilter.MaxFilter(grow * 2 + 1)).filter(ImageFilter.MinFilter(grow * 2 + 1))
    return np.array(im.filter(ImageFilter.GaussianBlur(0.8))).astype(np.float32)


def head():
    icon = game_icon('head')
    # The figure's centre line: the head's top is clear of the brush.
    mask = stone_mask(icon)
    xs = np.where(mask[:SIZE // 3].any(axis=0))[0]
    middle = (xs[0] + xs[-1] + 1) // 2
    out = icon.copy()
    width = min(middle, SIZE - middle)
    out[:, middle:middle + width] = icon[:, middle - width:middle][:, ::-1]
    disc = disc_only(icon)
    out[:, :middle - width] = disc[:, :middle - width]
    out[:, middle + width:] = disc[:, middle + width:]
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), 'RGBA')


def body():
    icon = game_icon('tattoo_body')
    mask = stone_mask(icon)
    # Closing bridges the tattoo lines; holes left inside are filled too.
    closed = smooth(mask, grow=6) > 127
    outside = np.zeros_like(closed)
    todo = [(0, 0)]
    while todo:
        y, x = todo.pop()
        if 0 <= y < SIZE and 0 <= x < SIZE and not closed[y, x] and not outside[y, x]:
            outside[y, x] = True
            todo += [(y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)]
    figure = ~outside
    return compose(icon, smooth(figure))


def eyes():
    icon = game_icon('eyeline')
    mask = stone_mask(icon)
    ys, xs = np.where(mask)
    cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
    # A plain almond around the iris, drawn at 4x; the iris is the game's.
    scale = 4
    half_w, half_h = 86, 50
    points = []
    for t in np.linspace(-1, 1, 80):
        points.append((cx + t * half_w, cy - half_h * (1 - t * t) ** 1.0))
    for t in np.linspace(1, -1, 80):
        points.append((cx + t * half_w, cy + half_h * (1 - t * t) ** 1.0))
    almond = Image.new('L', (SIZE * scale, SIZE * scale), 0)
    d = ImageDraw.Draw(almond)
    d.polygon([(x * scale, y * scale) for x, y in points], fill=255)
    inner = Image.new('L', almond.size, 0)
    d = ImageDraw.Draw(inner)
    k = 0.80
    d.polygon([(cx * scale + (x - cx) * k * scale, cy * scale + (y - cy) * k * scale) for x, y in points], fill=255)
    outline = np.array(almond).astype(np.float32) - np.array(inner).astype(np.float32)
    r = 38
    iris = Image.new('L', almond.size, 0)
    d = ImageDraw.Draw(iris)
    d.ellipse([(cx - r) * scale, (cy - r) * scale, (cx + r) * scale, (cy + r) * scale], fill=255)
    r = 15
    d.ellipse([(cx - r) * scale, (cy - r) * scale, (cx + r) * scale, (cy + r) * scale], fill=0)
    # Only inside the almond, with a dark gap between the iris and the outline.
    iris = np.array(iris).astype(np.float32) * (np.array(inner).astype(np.float32) / 255)
    figure = np.maximum(outline, iris)
    figure = np.array(Image.fromarray(figure.astype(np.uint8)).resize((SIZE, SIZE), Image.LANCZOS)).astype(np.float32)
    return compose(game_icon('head'), figure)


def identity():
    one = np.array(head()).astype(np.float32)
    out = Image.fromarray(np.clip(disc_only(game_icon('head')), 0, 255).astype(np.uint8), 'RGBA')
    mask = Image.fromarray((smooth(stone_mask(one)) / 255 * 255).astype(np.uint8))
    figure = Image.fromarray(one.astype(np.uint8), 'RGBA')
    width = int(SIZE * 0.62)
    figure, mask = figure.resize((width, width), Image.LANCZOS), mask.resize((width, width), Image.LANCZOS)
    y = (SIZE - width) // 2 + 6
    out.paste(figure, (SIZE // 2 - width + 22, y), mask)
    out.paste(figure, (SIZE // 2 - 22, y), mask)
    return out


def main(folder):
    os.makedirs(folder, exist_ok=True)
    sheet = Image.new('RGB', (SIZE * 4, SIZE), (24, 24, 24))
    for i, (key, make) in enumerate((('head', head), ('body', body), ('eyes', eyes), ('identity', identity))):
        icon = make()
        icon.save(os.path.join(folder, NAMES[key] + '.dds'), pixel_format='DXT5')
        sheet.paste(icon, (SIZE * i, 0), icon)
    return sheet


if __name__ == '__main__':
    main(sys.argv[1]).save(os.path.join(sys.argv[1], 'sheet.png'))
