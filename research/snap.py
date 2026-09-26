# Parse CC Research v2 snapshot files
import re
def load(path):
    objs = {}; cur = None; vec = None
    for line in open(path):
        m = re.match(r'\s+object #\d+ at ([0-9A-F]+)', line)
        if m:
            cur = {'addr': int(m.group(1), 16), 'bytes': bytearray(), 'vectors': {}}
            objs[cur['addr']] = cur; vec = None; continue
        if cur is None: continue
        m = re.match(r'\s+vector at \+([0-9A-F]+) : (\d+) bytes at ([0-9A-F]+)', line)
        if m:
            vec = bytearray(); cur['vectors'][int(m.group(1), 16)] = (int(m.group(3), 16), vec); continue
        m = re.match(r'(\s+)\+([0-9A-F]+) ((?: [0-9A-F]{2})+)\s*$', line)
        if m:
            data = bytes(int(x, 16) for x in m.group(3).split())
            if len(m.group(1)) >= 6 and vec is not None: vec.extend(data)
            else: cur['bytes'].extend(data)
        if line.startswith('===='): cur = None
    return objs
