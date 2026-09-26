# Rebuilds raw bytes from a research.txt dumpmem block
import re, struct
def blocks(path):
    out = {}; cur = None
    for line in open(path):
        m = re.match(r'==== dumpmem ([0-9A-F]+) ([0-9A-F]+)', line)
        if m:
            cur = int(m.group(1), 16); out[cur] = bytearray(); continue
        m = re.match(r'\s+\+([0-9A-F]+)\s+([0-9A-F]{16})', line)
        if m and cur is not None:
            out[cur] += struct.pack('<Q', int(m.group(2), 16))
    return out
