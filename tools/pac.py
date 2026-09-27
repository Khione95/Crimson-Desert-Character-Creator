"""Reads the game's skinned models (.pac): submeshes with positions, normals,
bone slots and triangles, per level of detail. Layout as Crimson Browser
Sharp reads it:

  0x10  8 sections: (stored size or 0, size); data from 0x50
  section 0      descriptors: per submesh a record starting with 1, then
                 floats (?, ?, bbox min xyz, bbox extent xyz), vertex counts
                 (u16 per LOD) at +40, index counts (u32 per LOD) at +44+2n,
                 found by the LOD pattern (n, 0, 1, .. n-1) at +35
  section 1..4   geometry of one LOD: vertices (40 bytes) then u16 indices

Vertex (40 bytes): u16 x, y, z scaled into the submesh's box (/32767),
+8 half u, v, +16 normal 10:10:10 (bits 0-9 z, 10-19 x, 20-29 y), +20 six
bone numbers (index into the model's bone list), 10 bits each, three in each
of two u32, +28 six weights (bytes, adding up to 255).
"""
import struct

import numpy as np

STRIDE = 40
PATTERNS = [  # (pattern, stored lod count, vertex count offset, index count offset)
    (bytes([4, 0, 1, 2, 3]), 4, 40, 48),
    (bytes([3, 0, 1, 1, 2]), 3, 40, 46),
    (bytes([3, 0, 1, 2]), 3, 40, 46),
    (bytes([2, 0, 1]), 2, 40, 44),
]


def sections(data):
    out = {}
    pos = 0x50
    for i in range(8):
        stored, size = struct.unpack_from('<II', data, 0x10 + i * 8)
        if size:
            out[i] = (pos, size)
            pos += stored if stored else size
    return out


def descriptors(data, sec0):
    off, size = sec0
    region = data[off:off + size]
    found = {}
    for pat, lods, vco, ico in PATTERNS:
        start = 0
        while True:
            i = region.find(pat, start)
            if i < 0:
                break
            start = i + 1
            if pat == bytes([3, 0, 1, 2]) and i > 0 and region[i - 1] == 4:
                continue
            if pat == bytes([2, 0, 1]) and i > 0 and region[i - 1] in (3, 4):
                continue
            d = i - 35
            if d < 0 or d in found or region[d] != 1 or d + ico + lods * 4 > len(region):
                continue
            floats = struct.unpack_from('<8f', region, d + 3)
            vcounts = list(struct.unpack_from(f'<{lods}H', region, d + vco))
            icounts = list(struct.unpack_from(f'<{lods}I', region, d + ico))
            if not any(vcounts) or max(vcounts) > 200000 or max(icounts) > 20000000:
                continue
            found[d] = {'offset': off + d, 'bbox_min': floats[2:5], 'bbox_ext': floats[5:8],
                        'vcounts': vcounts, 'icounts': icounts, 'name': name_before(region, d)}
    return [found[k] for k in sorted(found)]


def name_before(region, d):
    for j in range(1, 200):
        p = d - j
        if p < 0:
            break
        if region[p] == j - 1 and j > 1:
            s = region[p + 1:d]
            if all(32 <= c < 127 for c in s):
                return s.decode()
    return '?'


def bone_list(data, sec0, known):
    """The model's bone name hashes (the list bone numbers index into): a u16
    count followed by that many u32 hashes, most of them known bones (cloth
    and helper bones of the model may not be in the player skeletons)."""
    off, size = sec0
    region = bytes(data[off:off + size])
    known_arr = np.array(sorted(known), dtype=np.uint32)
    best = []
    for a in range(4):
        arr = np.frombuffer(region, dtype='<u4', count=(len(region) - a) // 4, offset=a)
        hit = np.isin(arr, known_arr)
        for i in np.flatnonzero(hit):
            start = a + 4 * int(i)
            if start < 2:
                continue
            count = struct.unpack_from('<H', region, start - 2)[0]
            if count <= len(best) or count < 3 or i + count > len(arr):
                continue
            if hit[i:i + count].mean() >= 0.6:
                best = [int(x) for x in arr[i:i + count]]
    return best

class Model:
    def __init__(self, data):
        self.data = bytearray(data)
        self.sections = sections(data)
        self.lod_count = data[self.sections[0][0] + 4]
        self.descs = descriptors(data, self.sections[0])
        # A pattern match without a name can be a false descriptor: dropped
        # when the most detailed level only adds up without it.
        if any(d['name'] == '?' for d in self.descs):
            real = [d for d in self.descs if d['name'] != '?']
            lod0 = self.sections.get(max(k for k in self.sections if k))
            if real and lod0 and self.fits(real, 0, lod0[1]) and not self.fits(self.descs, 0, lod0[1]):
                self.descs = real
        # A submesh may store fewer levels of detail than the model has (it
        # is left out of the lowest ones): its missing levels hold nothing.
        levels = max([len(d['vcounts']) for d in self.descs] + [self.lod_count])
        for d in self.descs:
            d['vcounts'] += [0] * (levels - len(d['vcounts']))
            d['icounts'] += [0] * (levels - len(d['icounts']))

    @staticmethod
    def fits(descs, lod, size):
        need = sum(d['vcounts'][lod] for d in descs if lod < len(d['vcounts'])) * STRIDE
        need += sum(d['icounts'][lod] for d in descs if lod < len(d['icounts'])) * 2
        return need == size

    def lod_section(self, lod):
        """Geometry section of a LOD: the lowest detail is section 1."""
        n = max(d_lods for d_lods in [len(self.descs[0]['vcounts'])])
        return self.sections.get(n - lod) or self.sections.get(self.lod_count - lod)

    def geometry(self, lod=0):
        """Per submesh: vertex offsets (absolute), positions, normals,
        bone slots, weights and triangles."""
        section = self.lod_section(lod)
        if section is None:
            return []
        off, size = section
        nverts = sum(d['vcounts'][lod] for d in self.descs)
        nidx = sum(d['icounts'][lod] for d in self.descs)
        vstart = off
        istart = off + nverts * STRIDE
        if nverts * STRIDE + nidx * 2 != size:
            # Padding between blocks: find it as Crimson Browser Sharp does.
            vstart, istart = self.find_layout(off, size, nverts, nidx, lod)
        out = []
        v = vstart
        i = istart
        for d in self.descs:
            nv, ni = d['vcounts'][lod], d['icounts'][lod]
            raw = np.frombuffer(bytes(self.data[v:v + nv * STRIDE]), dtype=np.uint8).reshape(nv, STRIDE)
            q = raw[:, 0:6].copy().view('<u2').astype(np.float64)
            pos = np.array(d['bbox_min']) + q / 32767.0 * np.array(d['bbox_ext'])
            nb = raw[:, 16:20].copy().view('<u4')[:, 0]
            nrm = np.stack([((nb >> 10) & 0x3FF), ((nb >> 20) & 0x3FF), (nb & 0x3FF)], 1) / 511.5 - 1.0
            idx = np.frombuffer(bytes(self.data[i:i + ni * 2]), dtype='<u2').astype(np.int64)
            packed = raw[:, 20:28].copy().view('<u4')
            bones = np.stack([(packed[:, k // 3] >> (10 * (k % 3))) & 0x3FF for k in range(6)], 1)
            weights = raw[:, 28:34].astype(np.float64)
            weights /= np.maximum(weights.sum(1, keepdims=True), 1)
            out.append({'desc': d, 'voffset': v, 'pos': pos, 'normal': nrm, 'normal_bits': nb,
                        'bones': bones, 'weights': weights, 'tris': idx.reshape(-1, 3)})
            v += nv * STRIDE
            i += ni * 2
        return out

    def find_layout(self, off, size, nverts, nidx, lod):
        extra = size - nverts * STRIDE - nidx * 2
        best = None
        for skip in range(0, max(extra, 0) + 1, 2):
            for vpad in range(0, max(extra - skip, 0) + 1, STRIDE):
                vstart = off + vpad
                istart = vstart + nverts * STRIDE + skip
                if istart + nidx * 2 > off + size:
                    continue
                idx = np.frombuffer(bytes(self.data[istart:istart + min(nidx, 3000) * 2]), dtype='<u2')
                if idx.size and idx.max() < max(d['vcounts'][lod] for d in self.descs):
                    return vstart, istart
        return off, off + nverts * STRIDE

    def set_positions(self, submesh, pos):
        """Writes new positions for a submesh (its box must hold them)."""
        d = submesh['desc']
        mn, ext = np.array(d['bbox_min']), np.array(d['bbox_ext'])
        q = np.clip(np.round((pos - mn) / np.where(ext == 0, 1, ext) * 32767.0), 0, 65535).astype('<u2')
        for k in range(len(pos)):
            struct.pack_into('<3H', self.data, submesh['voffset'] + k * STRIDE, *q[k])

    def raw_skin(self, submesh):
        """The stored bone slots (6 x 10 bits in two u32s, top bits apart)
        and weight bytes of a submesh's vertices."""
        n = len(submesh['pos'])
        raw = np.frombuffer(bytes(self.data[submesh['voffset']:submesh['voffset'] + n * STRIDE]),
                            dtype=np.uint8).reshape(n, STRIDE)
        return raw[:, 20:28].copy().view('<u4'), raw[:, 28:34].astype(np.int64)

    def set_skin(self, submesh, bones, weights):
        """Writes bone slots (n x 6 palette indices) and weight bytes (n x 6),
        keeping the top two bits of both packed words."""
        packed, _ = self.raw_skin(submesh)
        for w in range(2):
            word = packed[:, w] & 0xC0000000
            for k in range(3):
                word = word | (bones[:, 3 * w + k].astype(np.uint32) & 0x3FF) << (10 * k)
            packed[:, w] = word
        for k in range(len(bones)):
            at = submesh['voffset'] + k * STRIDE
            struct.pack_into('<2I', self.data, at + 20, int(packed[k, 0]), int(packed[k, 1]))
            struct.pack_into('<6B', self.data, at + 28, *(int(x) for x in weights[k]))

    def set_normals(self, submesh, normals):
        """Writes new normals (10:10:10, the top two bits kept)."""
        q = np.clip(np.round((normals + 1.0) * 511.5), 0, 1023).astype(np.uint32)
        bits = (submesh['normal_bits'] & 0xC0000000) | q[:, 2] | (q[:, 0] << 10) | (q[:, 1] << 20)
        for k in range(len(normals)):
            struct.pack_into('<I', self.data, submesh['voffset'] + k * STRIDE + 16, int(bits[k]))
