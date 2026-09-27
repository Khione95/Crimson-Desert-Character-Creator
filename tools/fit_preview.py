"""Renders a fitted armor over the female body (front and side, shaded) to a
PNG, to judge a fit without starting the game.

Usage: python fit_preview.py [--khione] <out.png> <model.pac | game path regex> [...]
       each model gets a column; a local .pac is read from disk.
       --khione: body and armor as the Khione Body mod sizes them (its bone
       scales applied through each model's own bone weights).
"""
import os
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
import numpy as np

import armor_fit
import game_files
import pac

LIGHT = np.array([0.4, 0.5, -0.75])
LIGHT /= np.linalg.norm(LIGHT)
CROP = (0.95, 1.62)     # heights shown (m): hips to shoulders


KHIONE = None       # bone hash: (world position, scale) when --khione
NO_BODY = False     # --armor-only: the body is hidden under chest armor in game


def khione_bones():
    import body_shape
    import pab
    _, bones, _ = pab.read_pab(game_files.read(next(game_files.entries(r'1_pc/2_phw/phw_01\.pab$'))))
    by_name = {b['name']: b for b in bones}
    out = {}
    for name, (a, b, c) in body_shape.SHAPE.items():
        bone = by_name[name]
        world = np.array(bone['mats'][0:16]).reshape(4, 4)
        axes = world[:3, :3] / np.linalg.norm(world[:3, :3], axis=1, keepdims=True)
        along = int(np.argmax(np.abs(axes[:, 1])))
        across = [k for k in range(3) if k != along]
        s = np.ones(3)
        s[along], s[across[0]], s[across[1]] = a, b, c
        out[bone['hash']] = (world[3, :3], axes, s)
    return out


def mesh(data):
    model = pac.Model(data)
    hashes = pac.bone_list(model.data, model.sections[0], armor_fit.Fitter.known_bones()) if KHIONE else []
    pts, tris, base = [], [], 0
    for s in model.geometry(0):
        p = s['pos']
        if KHIONE:
            move = np.zeros_like(p)
            for k in range(6):
                for h, (pivot, axes, scale) in KHIONE.items():
                    if h not in hashes:
                        continue
                    on = (s['bones'][:, k] == hashes.index(h)) & (s['weights'][:, k] > 0)
                    local = (p[on] - pivot) @ axes.T
                    move[on] += s['weights'][on, k:k + 1] * (((local * scale) @ axes + pivot) - p[on])
            p = p + move
        pts.append(p)
        tris.append(s['tris'] + base)
        base += len(p)
    return np.concatenate(pts), np.concatenate(tris)


def draw(ax, layers, view):
    """view 'front' looks along +z (the body faces -z), 'side' along -x."""
    polys, colors, depth = [], [], []
    for (pts, tris), color in layers:
        if view == 'front':
            xy, d, flip = pts[:, [0, 1]] * [-1, 1], pts[:, 2], 1
        else:
            xy, d, flip = pts[:, [2, 1]], -pts[:, 0], 1
        t = tris[(pts[tris, 1].mean(1) > CROP[0]) & (pts[tris, 1].mean(1) < CROP[1])]
        a, b, c = pts[t[:, 0]], pts[t[:, 1]], pts[t[:, 2]]
        n = np.cross(b - a, c - a)
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        light = LIGHT if view == 'front' else np.array([0.75, 0.5, -0.4])
        shade = 0.35 + 0.65 * np.abs(n @ light) * flip
        polys.append(xy[t])
        colors.append(np.clip(np.array(color)[None, :] * shade[:, None], 0, 1))
        depth.append(d[t].mean(1))
    polys, colors, depth = np.concatenate(polys), np.concatenate(colors), np.concatenate(depth)
    order = np.argsort(-depth)
    ax.add_collection(PolyCollection(polys[order], facecolors=colors[order], edgecolors='none'))
    ax.set_xlim(-0.35, 0.35) if view == 'front' else ax.set_xlim(-0.3, 0.3)
    ax.set_ylim(*CROP)
    ax.set_aspect('equal')
    ax.axis('off')


def load(arg):
    if os.path.exists(arg):
        return open(arg, 'rb').read(), os.path.basename(arg)
    e = next(game_files.entries(arg))
    return game_files.read(e), e['path'].rsplit('/', 1)[1]


def main():
    global KHIONE
    args = sys.argv[1:]
    global NO_BODY
    while args[0].startswith('--'):
        if args[0] == '--khione':
            KHIONE = khione_bones()
        elif args[0] == '--armor-only':
            NO_BODY = True
        args = args[1:]
    out, models = args[0], args[1:]
    body = mesh(armor_fit.load(armor_fit.FEMALE_BODY))
    fig, axes = plt.subplots(2, len(models), figsize=(4 * len(models), 7.5), squeeze=False)
    for i, arg in enumerate(models):
        data, name = load(arg)
        try:
            layers = ([] if NO_BODY else [(body, (0.85, 0.72, 0.62))]) + [(mesh(data), (0.45, 0.55, 0.75))]
        except Exception as e:
            print('unreadable', name, '-', e)
            layers = [(body, (0.85, 0.72, 0.62))]
            name += ' (unreadable)'
        draw(axes[0][i], layers, 'front')
        draw(axes[1][i], layers, 'side')
        axes[0][i].set_title(name, fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=90)
    print('preview:', out)


if __name__ == '__main__':
    main()
