# Helpers for analysing the in-memory dump of CrimsonDesert.exe
import mmap, struct, re
import numpy as np
import capstone

DUMP = r'C:\temp\cc_research\CrimsonDesert_memory.bin'
BASE = 0x140000000

_f = open(DUMP, 'rb')
m = mmap.mmap(_f.fileno(), 0, access=mmap.ACCESS_READ)
arr = np.frombuffer(m, dtype=np.uint8)

def u32(va): return struct.unpack_from('<I', m, va - BASE)[0]
def i32(va): return struct.unpack_from('<i', m, va - BASE)[0]
def u64(va): return struct.unpack_from('<Q', m, va - BASE)[0]
def rd(va, n): return m[va - BASE: va - BASE + n]
def cstr(va, n=256):
    b = rd(va, n); return b.split(b'\0')[0].decode('latin1')

def find_all(pattern: bytes, limit=100000):
    out = []
    i = m.find(pattern)
    while i != -1 and len(out) < limit:
        out.append(BASE + i); i = m.find(pattern, i + 1)
    return out

def find_u32(value):
    return find_all(struct.pack('<I', value))

def find_u64(value):
    return find_all(struct.pack('<Q', value))

# ---- RTTI ----
def type_descriptor(name: str):
    """name like '.?AVCharacterCustomizationController@pa@@' -> TypeDescriptor VA"""
    hits = find_all(name.encode() + b'\0')
    return [h - 0x10 for h in hits]

def vtables_for(name: str):
    res = []
    for td in type_descriptor(name):
        rva = td - BASE
        for p in find_u32(rva):
            col = p - 12                      # COL.pTypeDescriptor at +12
            if u32(col) != 1: continue        # signature 1 on x64
            off = u32(col + 4)
            for q in find_u64(col):
                res.append((q + 8, off))     # vtable starts after COL pointer
    return res

def vfuncs(vt, count=40):
    out = []
    for i in range(count):
        f = u64(vt + i * 8)
        if not (BASE <= f < BASE + len(m)): break
        out.append(f)
    return out

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = False
def dis(va, n=0x80, stop_ret=True):
    lines = []
    for ins in md.disasm(rd(va, n), va):
        lines.append(f'{ins.address:x}: {ins.mnemonic} {ins.op_str}')
        if stop_ret and ins.mnemonic in ('ret', 'int3'): break
    return '\n'.join(lines)

# ---- code references (RIP-relative) ----
CODE_START = 0x140001000
CODE_END = 0x1451eb000

def rip_refs(targets, start=CODE_START, end=CODE_END, chunk=1<<24):
    """Find positions whose RIP-relative disp32 points at any target.
    Returns {target: [disp32_address,...]}; instruction starts a few bytes earlier."""
    if isinstance(targets, int): targets = [targets]
    tg = np.array(sorted(targets), dtype=np.int64)
    res = {t: [] for t in targets}
    s0, e0 = start - BASE, end - BASE
    for s in range(s0, e0, chunk):
        e = min(e0, s + chunk + 3)
        a = arr[s:e].view(np.uint8)
        n = len(a) - 4
        if n <= 0: break
        d = (a[:n].astype(np.int64) | (a[1:n+1].astype(np.int64) << 8) |
             (a[2:n+2].astype(np.int64) << 16) | (a[3:n+3].astype(np.int64) << 24))
        d -= (d >= 2**31) * (2**32)
        base_t = BASE + s + np.arange(n, dtype=np.int64) + 4 + d
        for extra in (0, 1, 4):
            t = base_t + extra
            idx = np.searchsorted(tg, t)
            idx[idx >= len(tg)] = len(tg) - 1
            ok = np.nonzero(tg[idx] == t)[0]
            for h in ok:
                res[int(tg[idx[h]])].append(BASE + s + int(h))
    return res

def func_start(va, maxback=0x4000):
    """Walk back to the int3/cc padding before a function (heuristic)."""
    p = va
    while p > va - maxback:
        if rd(p-1,1) == b'\xcc' and rd(p-2,1) == b'\xcc':
            return p
        p -= 1
    return None
