"""Minimal reader for the game's executable (PE32+): sections, RVA <-> file
offset, reads by RVA, and MSVC RTTI class names of vtables."""
import struct

GAME_EXE = r'E:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe'


class Image:
    def __init__(self, path=GAME_EXE):
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        self.timestamp = struct.unpack_from('<I', d, pe + 8)[0]
        count = struct.unpack_from('<H', d, pe + 6)[0]
        optional = struct.unpack_from('<H', d, pe + 20)[0]
        self.image_base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
        self.size_of_image = struct.unpack_from('<I', d, pe + 24 + 56)[0]
        self.sections = []
        for i in range(count):
            o = pe + 24 + optional + i * 40
            name = d[o:o + 8].rstrip(b'\0').decode('latin-1')
            vsize, rva, rawsize, raw = struct.unpack_from('<IIII', d, o + 8)
            chars = struct.unpack_from('<I', d, o + 36)[0]
            self.sections.append((name, rva, vsize, raw, rawsize, chars))

    def offset(self, rva):
        for name, start, vsize, raw, rawsize, chars in self.sections:
            if start <= rva < start + max(vsize, rawsize) and rva - start < rawsize:
                return raw + rva - start
        return None

    def read(self, rva, size):
        o = self.offset(rva)
        return None if o is None else self.data[o:o + size]

    def u32(self, rva):
        b = self.read(rva, 4)
        return None if b is None else struct.unpack('<I', b)[0]

    def u64(self, rva):
        b = self.read(rva, 8)
        return None if b is None else struct.unpack('<Q', b)[0]

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        return None

    def class_name(self, vtable_rva):
        """The RTTI name of the class whose vtable starts at vtable_rva."""
        locator = self.u64(vtable_rva - 8)
        if not locator:
            return None
        type_rva = self.u32(locator - self.image_base + 12)
        if type_rva is None:
            return None
        raw = self.read(type_rva + 16, 200)
        return raw.split(b'\0')[0].decode('latin-1') if raw else None
