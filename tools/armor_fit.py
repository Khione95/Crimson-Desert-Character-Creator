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
BUST_FULL = 0.08        # armor this close in front of the chest takes the full bust shape
BUST_REACH = 0.15       # fading out until this far (straps, pouches further out stay)
BUST_DRAPE = 0.01       # cloth hangs this far down from the bust before meeting the body
BUST_BRIDGE = 0.006     # and spans this far sideways (the cleavage)
BUST_HUG = 0.2          # over the bust, armor keeps this share of its distance from the chest
                        # (the game's own male and female Dark Marksman plates: nearly skin tight)
PLATE_HUG = 0.3         # plates keep a little more of their distance (they are rigid)
BUST_TUCK = 0.04        # armor moves back by at most this much (under the bust)
BUST_SMOOTH = 2         # passes evening out the forward move between neighbours
BUST_BONE_FULL = 0.015  # armor this close to the bust follows the breast bones as the body does
BUST_BONE_REACH = 0.05  # fading out until this far
ARM_BONE_WORDS = ('Arm', 'Clavicle', 'Hand', 'Elbow', 'Finger', 'Shoulder')
ARM_SHARE = 0.1         # armor with this much of its weight on arm bones takes no breast weight (further out, the bones' scaling would push it out too far)


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


def front_depth(pts, x0, y0, cell, shape):
    """The body's front surface (it faces -z) on a grid over x and y: the
    smallest z per cell, holes filled from the neighbours, lightly smoothed."""
    ix = ((pts[:, 0] - x0) / cell).astype(int)
    iy = ((pts[:, 1] - y0) / cell).astype(int)
    ok = (ix >= 0) & (ix < shape[0]) & (iy >= 0) & (iy < shape[1])
    grid = np.full(shape, np.inf)
    np.minimum.at(grid, (ix[ok], iy[ok]), pts[ok, 2])
    for _ in range(4):
        pad = np.pad(grid, 1, constant_values=np.inf)
        around = np.min([pad[1 + dx:shape[0] + 1 + dx, 1 + dy:shape[1] + 1 + dy]
                         for dx in (-1, 0, 1) for dy in (-1, 0, 1)], axis=0)
        grid = np.where(np.isfinite(grid), grid, around)
    for _ in range(2):
        pad = np.pad(grid, 1, mode='edge')
        grid = np.mean([pad[1 + dx:shape[0] + 1 + dx, 1 + dy:shape[1] + 1 + dy]
                        for dx in (-1, 0, 1) for dy in (-1, 0, 1)], axis=0)
    return grid


class Bust:
    """How far the female bust stands out in front of the male chest (both on
    the female skeleton), on a grid over x and y. The body difference field
    is smoothed to keep armor free of spikes, which flattens the bust; this
    puts it back on the armor's front, unsmoothed."""
    X0, Y0, CELL = -0.22, 1.12, 0.005
    SHAPE = (88, 90)            # 44 cm wide, 45 cm high: the chest
    EDGE = 0.03                 # the effect fades out over this much at the sides

    def __init__(self, male_pts, female_pts):
        self.male = front_depth(male_pts, self.X0, self.Y0, self.CELL, self.SHAPE)
        female = front_depth(female_pts, self.X0, self.Y0, self.CELL, self.SHAPE)
        # Cloth does not follow the skin under and between the breasts: it
        # hangs from the bust and spans the cleavage.
        drop, across = int(BUST_DRAPE / self.CELL), int(BUST_BRIDGE / self.CELL)
        pad = np.pad(female, ((across, across), (0, drop)), mode='edge')
        draped = np.min([pad[across + dx:across + dx + self.SHAPE[0], dy:dy + self.SHAPE[1]]
                         for dx in range(-across, across + 1) for dy in range(0, drop + 1)], axis=0)
        for _ in range(3):
            pad = np.pad(draped, 1, mode='edge')
            draped = np.mean([pad[1 + dx:self.SHAPE[0] + 1 + dx, 1 + dy:self.SHAPE[1] + 1 + dy]
                              for dx in (-1, 0, 1) for dy in (-1, 0, 1)], axis=0)
        self.female = draped
        self.bulge = np.clip(self.male - self.female, 0, None)
        self.bulge[~np.isfinite(self.bulge)] = 0

    def lookup(self, grid, p):
        ix = np.clip(((p[:, 0] - self.X0) / self.CELL).astype(int), 0, self.SHAPE[0] - 1)
        iy = np.clip(((p[:, 1] - self.Y0) / self.CELL).astype(int), 0, self.SHAPE[1] - 1)
        inside = ((p[:, 0] >= self.X0) & (p[:, 0] < self.X0 + self.SHAPE[0] * self.CELL) &
                  (p[:, 1] >= self.Y0) & (p[:, 1] < self.Y0 + self.SHAPE[1] * self.CELL))
        return grid[ix, iy], inside

    def shape(self, p, rebound, tris, hug=None):
        """Brings the armor's front over the bust: a vertex in front of the
        chest ends up as far in front of the female bust as it stood in front
        of the male chest, or a share of it (layers keep their order). It
        moves back too, up to a limit: the female chest goes in under the
        bust, which gives it its round shape. Every limit fades, and the move
        is evened out between neighbours."""
        _, inside = self.lookup(self.bulge, p)
        male_front, _ = self.lookup(self.male, rebound)
        female_front, _ = self.lookup(self.female, p)
        clearance = male_front - rebound[:, 2]          # how far in front of the male chest
        side = np.clip((self.X0 + self.SHAPE[0] * self.CELL - np.abs(p[:, 0])) / self.EDGE, 0, 1)
        fade = (np.clip((BUST_REACH - clearance) / (BUST_REACH - BUST_FULL), 0, 1) *
                np.clip((clearance + 0.03) / 0.02, 0, 1) * inside * side)
        hug = BUST_HUG if hug is None else hug
        target = female_front - np.maximum(SKIN_GAP + (clearance - SKIN_GAP) * hug, SKIN_GAP)
        lift = np.clip(p[:, 2] - target, -BUST_TUCK, None) * fade
        lift[~np.isfinite(lift)] = 0            # outside the body (empty grid cells)
        if len(tris):
            edges = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
            edges = np.concatenate([edges, edges[:, ::-1]])
            count = np.maximum(np.bincount(edges[:, 0], minlength=len(p)), 1)
            for _ in range(BUST_SMOOTH):
                total = np.bincount(edges[:, 0], weights=lift[edges[:, 1]], minlength=len(p))
                lift = 0.5 * lift + 0.5 * total / count     # plain averaging: keeps the cleavage
        out = p.copy()
        out[:, 2] -= lift
        return out


class Fitter:
    @staticmethod
    def known_bones():
        male, female = skeletons()
        return set(male) | set(female)

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
        self.bust = Bust(self.body_pts, female_pts)
        self.chest = self.chest_shares(female_body)
        names = {b['hash']: b['name'] for b in read_pab(load(skeleton_helpers.SKELETONS['female']))[1] +
                 read_pab(load(skeleton_helpers.SKELETONS['male']))[1]}
        self.arm_bones = {h for h, n in names.items() if any(k in n for k in ARM_BONE_WORDS)}
        self.arms = self.forearms()

    def chest_shares(self, body):
        """Per side: the breast bone's hash and, per female body vertex, the
        share of its weight on that bone (and the one under it)."""
        names = {b['hash']: b['name'] for b in read_pab(load(skeleton_helpers.SKELETONS['female']))[1]}
        hashes = pac.bone_list(body.data, body.sections[0], self.known)
        out = {}
        for side in ('L', 'R'):
            wanted = {f'Bip01 {side} Chest', f'Bip01 {side} Chest_sub'}
            share = []
            for s in body.geometry(0):
                on = np.array([names.get(hashes[b], '') in wanted if b < len(hashes) else False
                               for b in range(max(len(hashes), 1))])
                share.append((s['weights'] * on[np.minimum(s['bones'], len(on) - 1)]).sum(1))
            bone = next(h for h, n in names.items() if n == f'Bip01 {side} Chest')
            out[side] = (bone, np.concatenate(share))
        return out

    def bust_weights(self, model, s, hashes, p):
        """Ties the armor's bust to the breast bones as the body is, so it
        follows them (a body mod that sizes the bust through them)."""
        idx, dist = nearest(p, self.female_pts, 4)
        near = np.clip(1 - (dist[:, 0] - BUST_BONE_FULL) / (BUST_BONE_REACH - BUST_BONE_FULL), 0, 1)
        # Sleeves hang beside the bust: armor on the arm bones stays off the
        # breast bones (it would wobble with them).
        on_arm = np.array([h in self.arm_bones for h in hashes] + [False])
        arm = (s['weights'] * on_arm[np.minimum(s['bones'], len(hashes))]).sum(1)
        near *= np.clip(1 - arm / ARM_SHARE, 0, 1)
        bones = s['bones'].copy()
        _, raw = model.raw_skin(s)
        total = np.maximum(raw.sum(1, keepdims=True), 1)
        w = raw / total
        changed = np.zeros(len(w), bool)
        for side, (bone, share) in self.chest.items():
            if bone not in hashes:
                continue
            slot_bone = hashes.index(bone)
            want = share[idx].mean(1) * near
            want[np.abs(total[:, 0] - 255) > 2] = 0     # odd byte totals (leftovers in unused slots): left alone
            for k in np.flatnonzero(want > 0.01):
                t = want[k]
                w[k] *= 1 - t
                have = np.flatnonzero((bones[k] == slot_bone) & (w[k] > 0))
                if len(have):
                    w[k, have[0]] += t
                else:
                    slot = int(np.argmin(w[k]))
                    if w[k, slot] > 0:
                        w[k, int(np.argmax(w[k]))] += w[k, slot]
                    bones[k, slot], w[k, slot] = slot_bone, t
                changed[k] = True
        if not changed.any():
            return
        out = np.floor(w * total).astype(np.int64)
        out[np.arange(len(out)), out.argmax(1)] += (total[:, 0] - out.sum(1))
        out = np.where(changed[:, None], np.clip(out, 0, 255), raw)
        bones = np.where(changed[:, None], bones, s['bones'])
        model.set_skin(s, bones, out)

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

    def fit(self, data, glove=False, kind=None):
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
                rebound = rebind(s['pos'], s['bones'], s['weights'], hashes, self.male, self.female)
                idx, dist = nearest(rebound, self.body_pts, NEAREST)
                w = 1.0 / np.maximum(dist, 1e-3) ** 2
                w /= w.sum(1, keepdims=True)
                p = rebound + (self.field[idx] * w[:, :, None]).sum(1)
                p = smooth_moves(s['pos'], p, s['tris'])
                p = self.bust.shape(p, rebound, s['tris'], PLATE_HUG if kind == 'plate' else BUST_HUG)
                p = self.push_out(p)
                if kind != 'plate':         # plates stay stiff; leather and cloth move with the bust
                    self.bust_weights(model, s, hashes, p)
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


def armor_types():
    """Model path -> plate, leather or cloth (armor_types.py)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'game_data', 'armor_model_types.txt')
    if not os.path.exists(path):
        return {}
    return dict(line.split() for line in open(path, encoding='utf-8') if line.strip())


TYPES = armor_types()


def armor_kind(path):
    """The type of the item a model belongs to (None: not known)."""
    path = path.replace(chr(92), '/')
    return TYPES.get(path[path.find('character/'):])


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
            out = fitter.fit(original, glove=is_glove(entry['path']), kind=armor_kind(entry['path']))
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
    out = fitter.fit(load(sys.argv[1]), glove=is_glove(sys.argv[1]), kind=armor_kind(sys.argv[1]))
    os.makedirs(os.path.dirname(os.path.abspath(sys.argv[2])), exist_ok=True)
    open(sys.argv[2], 'wb').write(out)
    print('written', sys.argv[2])


if __name__ == '__main__':
    main()
