# Parse CC Research v3 snapshot files
import re
def load(path):
    objs = {}; cur = None; tgt = None
    for line in open(path):
        m = re.match(r'  object #\d+ at ([0-9A-F]+)', line)
        if m:
            cur = {'addr': int(m.group(1), 16), 'bytes': bytearray(), 'ptrs': {}}
            objs[cur['addr']] = cur; tgt = None; continue
        if cur is None: continue
        m = re.match(r'    pointer at \+([0-9A-F]+) -> ([0-9A-F]+) \((\d+) bytes\)', line)
        if m:
            tgt = bytearray(); cur['ptrs'][int(m.group(1), 16)] = (int(m.group(2), 16), tgt); continue
        m = re.match(r'(\s+)\+([0-9A-F]+) ((?: [0-9A-F]{2})+)\s*$', line)
        if m:
            data = bytes(int(x, 16) for x in m.group(3).split())
            if len(m.group(1)) >= 6 and tgt is not None: tgt.extend(data)
            else: cur['bytes'].extend(data)
    return objs
