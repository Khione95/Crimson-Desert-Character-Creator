"""Reshapes male player armor for the female body.

1. Bones: every vertex is moved from the male skeleton's rest pose to the
   female one (v * inverse(male bind) * female bind, blended by its bone
   weights): arms, hands and shoulders go where a woman's are.
2. Shape: the male body is moved the same way and compared with the female
   body: at every point of the male body, how far the female surface lies
   (bust, waist, hips). Each armor vertex gets the difference at the body
   points nearest to it, so the armor follows the female shape and keeps its
   own thickness and details.

Usage: python armor_fit.py <male armor .pac path in the game> <out file>
       python armor_fit.py --batch <path regex> <out dir>   (keeps game paths)
       python armor_fit.py --list <file of paths> <out dir>
"""
import os
import re
import struct
import sys

import numpy as np

import game_files
import pac
import skeleton_helpers
from pab import read_pab

MALE_BODY = 'character/model/1_pc/1_phm/nude/cd_phm_00_nude_00_0001.pac'
FEMALE_BODY = 'character/model/1_pc/2_phw/nude/cd_phw_00_nude_00_0001.pac'

NEAREST = 8             # body points averaged per armor vertex
SMOOTHING = 10          # passes over the body's difference field
NORMAL_AGREEMENT = 0.3  # female points facing away are not matched
SKIN_GAP = 0.004        # armor pushed out of the female body keeps this gap
PUSH_REACH = 0.03       # only vertices this close to the skin are checked
SMOOTH_PASSES = 6       # evening out of neighbouring vertices' moves
MAX_MOVE = 0.15         # no vertex moves further than this
SLEEVE_TARGET = 0.97    # sleeves are lengthened to end here along the forearm (0 elbow, 1 wrist)
SLEEVE_MAX_ADD = 0.04   # by at most this much (m)
SLEEVE_RADIUS = 0.09    # vertices this close to the forearm's line count as sleeve
SLEEVE_START = 0.4      # the stretch starts here along the forearm


def load(path):
    return game_files.read(next(game_files.entries('^' + re.escape(path) + '$')))


def skeletons():
    """Male and female bind matrices by bone hash; the female one includes
    the male bones as helpers (skeleton_helpers.py)."""
    male_data = load(skeleton_helpers.SKELETONS['male'])
    female_data = load(skeleton_helpers.SKELETONS['female'])
    male = read_pab(male_data)[1]
    female_patched, _ = skeleton_helpers.add_helpers(female_data, male)
    female = read_pab(female_patched)[1]
    return ({b['hash']: skeleton_helpers.world(b) for b in male},
            {b['hash']: skeleton_helpers.world(b) for b in female})


def rebind(pos, bones, weights, bone_hashes, male, female):
    """Moves rest positions from the male skeleton to the female one, by how
    far each bone sits elsewhere. Only positions: the two skeletons orient
    some bones differently (axis setup, not pose), and following those
    rotations swung hanging parts far out."""
    offsets = []
    for h in bone_hashes:
        if h in male and h in female:
            offsets.append(female[h][3, :3] - male[h][3, :3])
        else:
            offsets.append(np.zeros(3))
    offsets = np.array(offsets) if offsets else np.zeros((1, 3))
    out = pos.copy()
    for k in range(bones.shape[1]):
        out += offsets[np.minimum(bones[:, k], len(offsets) - 1)] * weights[:, k:k + 1]
    return out


def smooth_moves(before, after, tris, passes=SMOOTH_PASSES):
    """Evens out how far neighbouring vertices moved (not their positions):
    a vertex that moved much more than those around it is pulled back, so
    parts shift as a whole instead of getting spikes."""
    move = after - before
    if not len(tris):
        return after
    edges = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
    edges = np.concatenate([edges, edges[:, ::-1]])
    count = np.bincount(edges[:, 0], minlength=len(move)).astype(np.float64)
    for _ in range(passes):
        total = np.zeros_like(move)
        np.add.at(total, edges[:, 0], move[edges[:, 1]])
        mean = np.where(count[:, None] > 0, total / np.maximum(count, 1)[:, None], move)
        move = 0.5 * move + 0.5 * mean
    size = np.linalg.norm(move, axis=1, keepdims=True)
    move *= np.minimum(1.0, MAX_MOVE / np.maximum(size, 1e-9))
    return before + move


def model_points(model, male, female, known, move):
    """All LOD 0 vertices of a model with smooth normals (moved to the
    female skeleton if asked)."""
    hashes = pac.bone_list(model.data, model.sections[0], known)
    pts, nrm, tris, base = [], [], [], 0
    for s in model.geometry(0):
        p = rebind(s['pos'], s['bones'], s['weights'], hashes, male, female) if move else s['pos']
        pts.append(p)
        tris.append(s['tris'] + base)
        base += len(p)
    pts = np.concatenate(pts)
    tris = np.concatenate(tris)
    return pts, vertex_normals(pts, tris), tris


def vertex_normals(pts, tris):
    n = np.zeros_like(pts)
    f = np.cross(pts[tris[:, 1]] - pts[tris[:, 0]], pts[tris[:, 2]] - pts[tris[:, 0]])
    for k in range(3):
        np.add.at(n, tris[:, k], f)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


def turn(normals, before, after):
    """Turns each normal by the rotation that takes before to after."""
    axis = np.cross(before, after)
    sin = np.linalg.norm(axis, axis=1, keepdims=True)
    cos = (before * after).sum(1, keepdims=True)
    k = axis / np.maximum(sin, 1e-12)
    out = normals * cos + np.cross(k, normals) * sin + k * (k * normals).sum(1, keepdims=True) * (1 - cos)
    keep = (sin[:, 0] < 1e-9) | ~np.isfinite(out).all(1)
    out[keep] = normals[keep]
    return out / np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-12)


def nearest(query, points, k, chunk=2048):
    """Indices and distances of the k nearest points (brute force)."""
    idx = np.zeros((len(query), k), dtype=np.int64)
    dist = np.zeros((len(query), k))
    pp = (points ** 2).sum(1)
    for a in range(0, len(query), chunk):
        q = query[a:a + chunk]
        d2 = (q ** 2).sum(1)[:, None] + pp[None, :] - 2 * q @ points.T
        part = np.argpartition(d2, k, axis=1)[:, :k]
        idx[a:a + chunk] = part
        dist[a:a + chunk] = np.sqrt(np.maximum(np.take_along_axis(d2, part, 1), 0))
    return idx, dist


def difference_field(male_pts, male_nrm, male_tris, female_pts, female_nrm):
    """For every male body vertex: the offset to the female surface."""
    idx, dist = nearest(male_pts, female_pts, 12)
    agree = (female_nrm[idx] * male_nrm[:, None, :]).sum(2) > NORMAL_AGREEMENT
    score = np.where(agree, dist, np.inf)
    best = idx[np.arange(len(idx)), score.argmin(1)]
    field = female_pts[best] - male_pts
    field[~agree.any(1)] = 0
    # Smooth over the male body's surface.
    neighbours = [set() for _ in range(len(male_pts))]
    for a, b, c in male_tris:
        neighbours[a].update((b, c))
        neighbours[b].update((a, c))
        neighbours[c].update((a, b))
    lists = [np.array(sorted(n)) if n else np.array([i]) for i, n in enumerate(neighbours)]
    for _ in range(SMOOTHING):
        field = np.array([0.5 * field[i] + 0.5 * field[n].mean(0) for i, n in enumerate(lists)])
    return field


class Fitter:
    def __init__(self):
        self.male, self.female = skeletons()
        self.known = set(self.male) | set(self.female)
        male_body = pac.Model(load(MALE_BODY))
        female_body = pac.Model(load(FEMALE_BODY))
        self.body_pts, body_nrm, body_tris = model_points(male_body, self.male, self.female, self.known, True)
        female_pts, female_nrm, _ = model_points(female_body, self.male, self.female, self.known, False)
        self.field = difference_field(self.body_pts, body_nrm, body_tris, female_pts, female_nrm)
        self.female_pts = female_pts
        self.female_nrm = female_nrm
        self.arms = self.forearms()

    def forearms(self):
        """Per side: elbow and wrist positions of the female skeleton and the
        female forearm's radius along it (for tapering lengthened sleeves)."""
        female_data = load(skeleton_helpers.SKELETONS['female'])
        by_name = {b['name']: skeleton_helpers.world(b)[3, :3] for b in read_pab(female_data)[1]}
        arms = {}
        for side, sign in (('L', 1), ('R', -1)):
            a, h = by_name[f'Bip01 {side} Forearm'], by_name[f'Bip01 {side} Hand']
            length = np.linalg.norm(h - a)
            axis = (h - a) / length
            rel = self.female_pts - a
            share = rel @ axis / length
            radius = np.linalg.norm(np.cross(rel, axis), axis=1)
            steps = np.linspace(0, 1.1, 23)
            profile = []
            for x in steps:
                m = (abs(share - x) < 0.03) & (radius < 0.07) & (self.female_pts[:, 0] * sign > 0.05)
                profile.append(np.median(radius[m]) if m.any() else np.nan)
            profile = np.array(profile)
            profile = np.interp(steps, steps[~np.isnan(profile)], profile[~np.isnan(profile)])
            profile = np.convolve(np.pad(profile, 2, mode='edge'), np.ones(5) / 5, mode='valid')
            arms[side] = (a, axis, length, sign, steps, profile)
        return arms

    def sleeve_reach(self, pts, side):
        """How far along the forearm a model's sleeve reaches (None if it has
        no sleeve ending on the forearm)."""
        a, axis, length, sign, _, _ = self.arms[side]
        rel = pts - a
        share = rel @ axis / length
        radius = np.linalg.norm(np.cross(rel, axis), axis=1)
        m = (radius < SLEEVE_RADIUS) & (share > 0) & (share < 1.2) & (pts[:, 0] * sign > 0.05)
        if m.sum() < 20:
            return None
        return float(share[m].max())

    def lengthen_sleeves(self, p, reach):
        """Stretches the lower part of each sleeve that ends before the wrist
        towards it, narrowing it with the forearm."""
        for side, r in reach.items():
            if r is None or r >= SLEEVE_TARGET or r < 0.6:
                continue
            a, axis, length, sign, steps, profile = self.arms[side]
            add = min(SLEEVE_TARGET - r, SLEEVE_MAX_ADD / length)
            rel = p - a
            share = rel @ axis / length
            radial = rel - np.outer(share * length, axis)
            radius = np.linalg.norm(radial, axis=1)
            m = (radius < SLEEVE_RADIUS) & (share > SLEEVE_START) & (share <= r + 0.02) & (p[:, 0] * sign > 0.05)
            if not m.any():
                continue
            t = np.clip((share[m] - SLEEVE_START) / (r - SLEEVE_START), 0, 1)
            new_share = share[m] + add * t * t
            scale = np.interp(new_share, steps, profile) / np.maximum(np.interp(share[m], steps, profile), 1e-4)
            p = p.copy()
            p[m] = a + np.outer(new_share * length, axis) + radial[m] * scale[:, None]
        return p

    def push_out(self, p):
        """Moves vertices that ended up inside the female body out of it."""
        idx, dist = nearest(p, self.female_pts, 4)
        f = self.female_pts[idx[:, 0]]
        n = self.female_nrm[idx[:, 0:4]].mean(1)
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        depth = ((p - f) * n).sum(1)
        inside = (depth < SKIN_GAP) & (dist[:, 0] < PUSH_REACH)
        p = p.copy()
        p[inside] += n[inside] * (SKIN_GAP - depth[inside])[:, None]
        return p

    def fit(self, data, glove=False):
        """Reshapes one model. glove: a glove (its sleeves are not lengthened)."""
        model = pac.Model(data)
        hashes = pac.bone_list(model.data, model.sections[0], self.known)
        lods = len(model.descs[0]['vcounts'])
        new = {}
        reach = None
        fitted = []
        for lod in range(lods):
            for s in model.geometry(lod):
                if not len(s['pos']):
                    continue
                p = rebind(s['pos'], s['bones'], s['weights'], hashes, self.male, self.female)
                idx, dist = nearest(p, self.body_pts, NEAREST)
                w = 1.0 / np.maximum(dist, 1e-3) ** 2
                w /= w.sum(1, keepdims=True)
                p = p + (self.field[idx] * w[:, :, None]).sum(1)
                p = smooth_moves(s['pos'], p, s['tris'])
                p = self.push_out(p)
                fitted.append((lod, s, p))
        # Sleeves: judged on the most detailed level, applied to every level.
        lod0 = [p for lod, _, p in fitted if lod == 0]
        if lod0 and not glove:
            allp = np.concatenate(lod0)
            reach = {side: self.sleeve_reach(allp, side) for side in self.arms}
        for lod, s, p in fitted:
            if reach:
                p = self.lengthen_sleeves(p, reach)
            new.setdefault(s['desc']['offset'], []).append((s, p))
        # A submesh's box covers all its LODs: widen it where the new shape
        # needs, then write every LOD's positions.
        for offset, parts in new.items():
            allp = np.concatenate([p for _, p in parts])
            mn = allp.min(0)
            ext = np.maximum(allp.max(0) - mn, 1e-6)
            struct.pack_into('<3f', model.data, offset + 3 + 8, *mn)
            struct.pack_into('<3f', model.data, offset + 3 + 20, *ext)
            for s, p in parts:
                s['desc']['bbox_min'], s['desc']['bbox_ext'] = tuple(mn), tuple(ext)
                model.set_positions(s, p)
                if len(s['tris']):
                    model.set_normals(s, turn(s['normal'], vertex_normals(s['pos'], s['tris']),
                                              vertex_normals(p, s['tris'])))
        return bytes(model.data)


def is_glove(path):
    name = path.rsplit('/', 1)[1]
    return '/11_hand/' in path or '_hand_' in name or name.startswith('cd_phm_00_hand')


def batch(pattern, out_dir):
    fitter = Fitter()
    done = failed = 0
    for entry in game_files.entries(pattern):
        path = os.path.join(out_dir, entry['path'])
        if os.path.exists(path):
            continue
        try:
            original = game_files.read(entry)
            out = fitter.fit(original, glove=is_glove(entry['path']))
            if out == original:
                raise ValueError('came out unchanged')
        except Exception as e:
            failed += 1
            print('skipped', entry['path'], '-', e)
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(out)
        done += 1
        if done % 25 == 0:
            print(done, 'fitted', flush=True)
    print(f'{done} fitted, {failed} skipped')


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == '--batch':
        batch(sys.argv[2], sys.argv[3])
        return
    if len(sys.argv) >= 4 and sys.argv[1] == '--list':
        paths = [l.strip() for l in open(sys.argv[2], encoding='utf-8') if l.strip()]
        batch('^(' + '|'.join(re.escape(p) for p in paths) + ')$', sys.argv[3])
        return
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    fitter = Fitter()
    out = fitter.fit(load(sys.argv[1]), glove=is_glove(sys.argv[1]))
    os.makedirs(os.path.dirname(os.path.abspath(sys.argv[2])), exist_ok=True)
    open(sys.argv[2], 'wb').write(out)
    print('written', sys.argv[2])


if __name__ == '__main__':
    main()
