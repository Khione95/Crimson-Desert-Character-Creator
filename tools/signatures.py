"""Finds byte patterns that locate the plugin's game addresses in future game
versions, from the current executable (tools/pe.py).

For each address it looks for code that uses it - a call (E8 rel32) of a
function, or a rip-relative mov/lea of a global or vtable - and grows a
pattern around that instruction (instruction bytes, with every rip-relative
displacement and branch target as a wildcard) until it is unique in the
game's code. The plugin finds the pattern and reads the address from the
instruction at the recorded offset.

Usage: python signatures.py    writes CharacterCreator/CharacterCreator/signatures.inc

After a game update: the plugin logs the addresses it looked up
("addresses: SetDecoration at +0x..."). Put those RVAs in TARGETS here and in
KNOWN_RVAS / KNOWN_TIMESTAMP / KNOWN_IMAGE_SIZE in addresses.cpp, run this
script against the new executable and rebuild - the patterns then come from
the new build and are ready for the one after it. (disable.txt "knownbuild"
tests the lookup on the known build.)
"""
import os
import re
import struct

from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

from pe import Image

TARGETS = [
    # name, rva, kind ('call' = called function, 'ref' = rip-relative data)
    ('SetDecoration', 0x72BF80, 'call'),
    ('QueueMeshChange', 0x72CFB0, 'call'),
    ('Rebuild', 0x726C50, 'call'),
    ('GrowBytes', 0x3D2BE0, 'call'),
    ('LoadXml', 0x142BDD0, 'call'),
    ('ParseAppearance', 0x2438A40, 'call'),
    ('LipSyncPath', 0x2E8C460, 'call'),
    ('ManagerPointer', 0x6D69A48, 'ref'),
    ('ScaleComponentVtable', 0x5B4C6C0, 'ref'),
    ('ScaleObjectVtable', 0x5B4D168, 'ref'),
]

PATTERNS_PER_TARGET = 3
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'CharacterCreator', 'CharacterCreator', 'signatures.inc')

MAX_PATTERN = 48
BEFORE = 24         # bytes of code before the instruction the pattern may start at

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True


def code_sections(im):
    """(rva, bytes) of the executable sections the plugin will scan."""
    out = []
    for name, rva, vsize, raw, rawsize, chars in im.sections:
        if chars & 0x20000000 and name in ('.rsrc', '.text'):
            out.append((rva, im.data[raw:raw + min(vsize, rawsize)]))
    return out


def references(im, sections, target, kind):
    """Code positions (rva of the instruction, length, offset of its rel32) using target."""
    found = []
    for base, code in sections:
        if kind == 'call':
            for m in re.finditer(rb'\xE8', code):
                p = m.start()
                if p + 5 > len(code):
                    continue
                rel = struct.unpack_from('<i', code, p + 1)[0]
                if base + p + 5 + rel == target:
                    found.append((base + p, 5, 1))
        else:
            # mov / lea / cmp with a rip-relative operand: [REX] op modrm(00 reg 101) disp32
            for m in re.finditer(rb'[\x48\x4C][\x8B\x8D\x89\x39\x3B](?=[\x05\x0D\x15\x1D\x25\x2D\x35\x3D])', code):
                p = m.start()
                if p + 7 > len(code):
                    continue
                rel = struct.unpack_from('<i', code, p + 3)[0]
                if base + p + 7 + rel == target:
                    found.append((base + p, 7, 3))
    return found


def instructions(im, start_rva, size):
    """Decoded instructions from start_rva, as (rva, bytes, wildcard offsets)."""
    raw = im.read(start_rva, size)
    out = []
    for insn in md.disasm(raw, start_rva):
        wild = set()
        if insn.disp_offset and any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in insn.operands):
            wild.update(range(insn.disp_offset, insn.disp_offset + insn.disp_size))
        if insn.imm_offset and insn.group(1) or insn.group(7):   # jump / relative branch
            if insn.imm_offset:
                wild.update(range(insn.imm_offset, insn.imm_offset + insn.imm_size))
        if insn.mnemonic == 'call' and insn.imm_offset:
            wild.update(range(insn.imm_offset, insn.imm_offset + insn.imm_size))
        out.append((insn.address, bytes(insn.bytes), wild))
    return out


def to_regex(pattern):
    return re.compile(b''.join(re.escape(bytes([b])) if b is not None else b'.' for b in pattern), re.S)


def count(sections, pattern, limit=2):
    rx = to_regex(pattern)
    n = 0
    for base, code in sections:
        for _ in rx.finditer(code):
            n += 1
            if n >= limit:
                return n
    return n


def pattern_for(im, sections, ref_rva, ref_len, ref_rel):
    """A unique pattern around the instruction at ref_rva, and the offset of
    its rel32 within the pattern; None if none fits in MAX_PATTERN."""
    # Instructions from a little before: decode from each candidate start and
    # keep the one that lands exactly on the reference.
    for back in range(0, BEFORE + 1):
        insns = instructions(im, ref_rva - back, back + 64)
        if not insns or not any(a == ref_rva for a, _, _ in insns):
            continue
        # Grow: first the instructions from the reference on, then earlier ones.
        idx = next(i for i, (a, _, _) in enumerate(insns) if a == ref_rva)
        for first in range(idx, -1, -1):
            for last in range(idx, len(insns)):
                pat = []
                rel_offset = None
                for a, b, wild in insns[first:last + 1]:
                    if a == ref_rva:
                        rel_offset = len(pat) + ref_rel
                    for i, x in enumerate(b):
                        pat.append(None if i in wild else x)
                if rel_offset is None or len(pat) > MAX_PATTERN:
                    break
                fixed = sum(1 for x in pat if x is not None)
                if fixed >= 8 and count(sections, pat) == 1:
                    return pat, rel_offset, ref_len - ref_rel - 4
        break
    return None


def start_pattern(im, sections, rva):
    """A unique pattern of a function's first instructions (None for a
    function that starts with a jump into the protection)."""
    insns = instructions(im, rva, 96)
    if not insns or insns[0][1][0] in (0xE9, 0xEB):
        return None
    pat = []
    for a, b, wild in insns:
        for i, x in enumerate(b):
            pat.append(None if i in wild else x)
        if len(pat) > MAX_PATTERN:
            return None
        if sum(1 for x in pat if x is not None) >= 12 and count(sections, pat) == 1:
            return pat
    return None


def main():
    im = Image()
    sections = code_sections(im)
    lines = [f'// Generated by tools/signatures.py from game build {im.timestamp:08X} / {im.size_of_image:08X} - do not edit.',
             '// { address, pattern, offset of the rel32 in the pattern, bytes after it to the end of the instruction }']
    for name, rva, kind in TARGETS:
        refs = references(im, sections, rva, kind)
        found = []
        for ref_rva, ref_len, ref_rel in refs[:60]:
            r = pattern_for(im, sections, ref_rva, ref_len, ref_rel)
            if r and r[0] not in [f[0] for f in found]:
                found.append(r)
        found.sort(key=lambda r: len(r[0]))
        print(f'{name}: {len(refs)} references, {len(found)} unique patterns')
        if not found:
            lines.append(f'// {name}: no unique pattern')
        chosen = found[:PATTERNS_PER_TARGET]
        if kind == 'call':
            start = start_pattern(im, sections, rva)
            print(f'  function start: {"unique pattern" if start else "none"}')
            if start:
                chosen.append((start, -1, 0))
        for pat, rel_offset, tail in chosen:
            text = ' '.join('??' if b is None else f'{b:02X}' for b in pat)
            lines.append(f'{{ ADDR_{name.upper()}, "{text}", {rel_offset}, {tail} }},')
    open(OUT, 'w', encoding='utf-8', newline='\r\n').write('\n'.join(lines) + '\n')
    print('written', OUT)


if __name__ == '__main__':
    main()
