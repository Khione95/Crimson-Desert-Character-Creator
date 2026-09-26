from dump import *
import numpy as np
LO, HI = BASE + 0x1000, BASE + 0x51eb000
_seg = arr[LO-BASE:HI-BASE].astype(np.int64)
_v = _seg[:-3] | (_seg[1:-2] << 8) | (_seg[2:-1] << 16) | (_seg[3:] << 24)
_v = np.where(_v >= 2**31, _v - 2**32, _v)
_pos = np.arange(len(_v), dtype=np.int64) + LO
def xrefs(target):
    res = set()
    for k in (0, 1, 2, 4):
        for h in np.nonzero(_v == (target - (_pos + 4 + k)))[0]:
            res.add(int(LO + h))
    return sorted(res)
def func_start(va, back=0x4000):
    # walk back to CC padding
    p = va
    while p > va - back:
        if rd(p-1,1) == b'\xcc' and rd(p-2,1) in (b'\xcc', b'\xc3'): return p
        p -= 1
    return None
def dis(va, n=40):
    for i in md.disasm(rd(va, n*8), va):
        print(f'{i.address:x}: {i.mnemonic} {i.op_str}')
        n -= 1
        if n == 0: break
def callers(t):
    res=[]
    for h in np.nonzero(_v == (t - (_pos + 4)))[0]:
        a=int(LO+h)-1
        if rd(a,1) in (b'\xe8',b'\xe9'): res.append(a)
    return res
def ann(va, n):
    for i in md.disasm(rd(va, n), va):
        s=''
        if 'rip +' in i.op_str:
            try:
                t=i.address+i.size+int(i.op_str.split('rip + ')[1].split(']')[0],16); s=repr(cstr(t,40))
            except: pass
        print(f'{i.address:x}: {i.mnemonic} {i.op_str} {s}')
