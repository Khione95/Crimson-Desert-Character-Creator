#include "pch.h"
#include "research.h"
#include "chartable.h"
#include "log.h"

#include <share.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#if CC_RESEARCH

static char g_path[MAX_PATH] = { 0 };

void ResearchInit(const char* folder)
{
    sprintf_s(g_path, "%s\\research.txt", folder);
}

static bool Read(uintptr_t address, void* out, size_t size)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, out, size, &got) && got == size;
}

// Text at address (or a little after it), for values that point at strings.
static bool TextAt(uintptr_t address, char* out, size_t size)
{
    char buffer[96];

    if (address < 0x10000 || !Read(address, buffer, sizeof(buffer)))
        return false;

    for (int start = 0; start <= 16; start += 8)
    {
        int n = 0;

        while (start + n < (int)sizeof(buffer) - 1 && buffer[start + n] >= 0x20 && buffer[start + n] < 0x7F)
            ++n;

        if (n >= 4 && (start + n == (int)sizeof(buffer) - 1 || buffer[start + n] == 0))
        {
            if (n > (int)size - 1)
                n = (int)size - 1;

            memcpy(out, buffer + start, n);
            out[n] = 0;
            return true;
        }
    }

    return false;
}

static void DumpBlock(FILE* f, uintptr_t address, size_t bytes)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);

    for (size_t off = 0; off < bytes; off += 8)
    {
        uint64_t v = 0;

        if (!Read(address + off, &v, sizeof(v)))
        {
            fprintf(f, "  +%03zX  [unreadable]\n", off);
            return;
        }

        char text[96];
        uint64_t inner = 0;
        fprintf(f, "  +%03zX  %016llX  %10u %10u", off, (unsigned long long)v, (uint32_t)v, (uint32_t)(v >> 32));

        if (v >= base && v < base + 0x20000000)
            fprintf(f, "  module+%llX", (unsigned long long)(v - base));
        else if (TextAt((uintptr_t)v, text, sizeof(text)))
            fprintf(f, "  \"%s\"", text);
        else if (Read((uintptr_t)v, &inner, sizeof(inner)) && TextAt((uintptr_t)inner, text, sizeof(text)))
            fprintf(f, "  -> \"%s\"", text);

        fprintf(f, "\n");
    }
}

static FILE* OpenOutput()
{
    return _fsopen(g_path, "a", _SH_DENYNO);
}

void ResearchDumpMemory(uintptr_t address, size_t bytes)
{
    FILE* f = OpenOutput();

    if (!f)
        return;

    fprintf(f, "\n==== dumpmem %016llX %zX ====\n", (unsigned long long)address, bytes);
    DumpBlock(f, address, bytes);
    fclose(f);
    Log("research: dumped %zX bytes at %016llX", bytes, (unsigned long long)address);
}

void ResearchCharacterTable(uintptr_t pointerArray, int count, size_t entryBytes, const char* nameFilter)
{
    FILE* f = OpenOutput();

    if (!f)
        return;

    // Values Female Animations.field.json writes into the Kliff entries.
    static const uint32_t targets[] = { 1767116530, 3755051597, 3000129643, 1287066785, 2831867940, 3511542393 };
    static const char* targetNames[] = { "appearance_name", "character_prefab_path", "skeleton_name",
        "default_action", "lookup_24", "lookup_25" };

    fprintf(f, "\n==== chartable %016llX x %d ====\n", (unsigned long long)pointerArray, count);
    uint8_t* entry = (uint8_t*)VirtualAlloc(NULL, entryBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    for (int i = 0; entry && i < count; ++i)
    {
        uint64_t p = 0, namePtr = 0;
        char name[96] = "?";

        if (!Read(pointerArray + i * 8, &p, 8) || !Read((uintptr_t)p, entry, entryBytes))
            continue;

        if (Read((uintptr_t)p + 8, &namePtr, 8))
            TextAt((uintptr_t)namePtr, name, sizeof(name));

        char found[512] = "";
        int len = 0;

        for (int t = 0; t < 6; ++t)
            for (size_t off = 0; off + 4 <= entryBytes; ++off)
                if (*(uint32_t*)(entry + off) == targets[t])
                    len += sprintf_s(found + len, sizeof(found) - len, " %s@+%zX", targetNames[t], off);

        if (len || (nameFilter && strstr(name, nameFilter)))
            fprintf(f, "  #%d %016llX %s%s\n", i, (unsigned long long)p, name, found);
    }

    if (entry)
        VirtualFree(entry, 0, MEM_RELEASE);

    fclose(f);
    Log("research: character table scanned");
}

void ResearchDumpClass(uintptr_t vtableRva, size_t bytes, int maxObjects)
{
    FILE* f = OpenOutput();

    if (!f)
        return;

    uintptr_t vtable = (uintptr_t)GetModuleHandleA(NULL) + vtableRva;
    fprintf(f, "\n==== dumpclass vtable %016llX (rva %llX) ====\n", (unsigned long long)vtable, (unsigned long long)vtableRva);

    const size_t CHUNK = 0x100000;
    uint8_t* buffer = (uint8_t*)VirtualAlloc(NULL, CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t address = (uintptr_t)si.lpMinimumApplicationAddress;
    int found = 0;
    DWORD t0 = GetTickCount();

    while (buffer && address < (uintptr_t)si.lpMaximumApplicationAddress && found < maxObjects)
    {
        MEMORY_BASIC_INFORMATION mbi;

        if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        uintptr_t rs = (uintptr_t)mbi.BaseAddress, re = rs + mbi.RegionSize;

        if (re <= rs)
            break;

        DWORD p = mbi.Protect & 0xFF;
        bool writable = p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE;

        if (mbi.State == MEM_COMMIT && writable && !(mbi.Protect & PAGE_GUARD) && rs != (uintptr_t)buffer)
        {
            for (uintptr_t q = rs; q < re && found < maxObjects; q += CHUNK)
            {
                size_t want = (size_t)(re - q) < CHUNK ? (size_t)(re - q) : CHUNK;

                if (!Read(q, buffer, want))
                    continue;

                for (size_t i = 0; i + 8 <= want && found < maxObjects; i += 8)
                {
                    if (*(uint64_t*)(buffer + i) != vtable)
                        continue;

                    fprintf(f, "\n-- object #%d at %016llX (%s)\n", found, (unsigned long long)(q + i),
                        mbi.Type == MEM_IMAGE ? "module data" : "heap");
                    DumpBlock(f, q + i, bytes);
                    ++found;
                }
            }
        }

        address = re;
    }

    if (buffer)
        VirtualFree(buffer, 0, MEM_RELEASE);

    fprintf(f, "\n%d objects (%lu ms)\n", found, GetTickCount() - t0);
    fclose(f);
    Log("research: %d objects of vtable rva %llX", found, (unsigned long long)vtableRva);
}

// ---------------------------------------------------------------------------
// Owner search: which character table entry an object leads to
// ---------------------------------------------------------------------------

static const int OWNER_DEPTH = 3;
static const size_t OWNER_SCAN_BYTES[OWNER_DEPTH] = { 0x800, 0x200, 0x100 };  // per depth
static const int OWNER_ENTRIES = 12;

static bool LooksLikePointer(uintptr_t p)
{
    return p >= 0x10000 && p < 0x00007FFFFFFFFFFF && (p & 7) == 0;
}

static int OwnerMatch(uintptr_t p, const uintptr_t* entries, size_t* inside)
{
    for (int i = 0; i < OWNER_ENTRIES; ++i)
    {
        if (entries[i] && p >= entries[i] && p < entries[i] + 0x700)
        {
            *inside = p - entries[i];
            return i;
        }
    }

    return -1;
}

static int OwnerSearch(FILE* f, uintptr_t object, int depth, size_t* path, const uintptr_t* entries, int* found)
{
    uintptr_t block[0x800 / sizeof(uintptr_t)];
    size_t count = OWNER_SCAN_BYTES[depth] / sizeof(uintptr_t);

    if (!Read(object, block, count * sizeof(uintptr_t)))
        return 0;

    for (size_t i = 0; i < count && *found < 60; ++i)
    {
        uintptr_t p = block[i];
        path[depth] = i * sizeof(uintptr_t);
        size_t inside = 0;
        int match = OwnerMatch(p, entries, &inside);

        if (match >= 0)
        {
            char name[64] = "?";
            CharTableName(match, name, sizeof(name));
            fprintf(f, "  entry %d %s +0x%zX via", match, name, inside);

            for (int d = 0; d <= depth; ++d)
                fprintf(f, " +0x%zX", path[d]);

            fprintf(f, "\n");
            ++*found;
        }
        else if (depth + 1 < OWNER_DEPTH && LooksLikePointer(p))
        {
            OwnerSearch(f, p, depth + 1, path, entries, found);
        }
    }

    return *found;
}

void ResearchFindOwner(uintptr_t object)
{
    FILE* f = OpenOutput();

    if (!f)
        return;

    uintptr_t entries[OWNER_ENTRIES];

    for (int i = 0; i < OWNER_ENTRIES; ++i)
        entries[i] = CharTableEntry(i);

    fprintf(f, "== owner search from %016llX (entries: Kliff %016llX)\n", (unsigned long long)object, (unsigned long long)entries[0]);
    size_t path[OWNER_DEPTH] = {};
    int found = 0;
    OwnerSearch(f, object, 0, path, entries, &found);
    fprintf(f, "  %d found\n", found);
    fclose(f);
    Log("research: owner search done (%d found)", found);
}

// ---------------------------------------------------------------------------
// Float search (character scale)
// ---------------------------------------------------------------------------

static const int FLOAT_HITS_MAX = 300;

static bool Near(float a, float b)
{
    float d = a - b;
    return d < 0.00001f && d > -0.00001f;
}

// Scans the writable memory of one region for `run` floats equal to value in a row.
static int ScanRegion(FILE* f, uintptr_t start, size_t size, float value, int run, int found)
{
    __try
    {
        const float* p = (const float*)start;
        size_t count = size / sizeof(float);

        for (size_t i = 0; i + run <= count && found < FLOAT_HITS_MAX; ++i)
        {
            if (!Near(p[i], value))
                continue;

            int n = 1;

            while (n < run && Near(p[i + n], value))
                ++n;

            if (n < run)
                continue;

            uintptr_t at = (uintptr_t)(p + i);
            fprintf(f, "\n---- hit %d at %016llX (next floats %g %g %g) ----\n", found, (unsigned long long)at,
                i + 1 < count ? p[i + 1] : 0.0f, i + 2 < count ? p[i + 2] : 0.0f, i + 3 < count ? p[i + 3] : 0.0f);
            DumpBlock(f, (at - 0x40) & ~(uintptr_t)7, 0x80);
            ++found;
            i += run - 1;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return found;
}

void ResearchFindFloats(float value, int run)
{
    FILE* f = OpenOutput();

    if (!f)
        return;

    fprintf(f, "\n==== findfloat %f x%d ====\n", value, run);

    int found = 0;
    size_t scanned = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0x10000;

    while (found < FLOAT_HITS_MAX && VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)))
    {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) &&
            !(mbi.Protect & PAGE_GUARD))
        {
            found = ScanRegion(f, (uintptr_t)mbi.BaseAddress, mbi.RegionSize, value, run, found);
            scanned += mbi.RegionSize;
        }

        if (next <= address)
            break;

        address = next;
    }

    fprintf(f, "\n==== %d hits in %zu MB ====\n", found, scanned >> 20);
    fclose(f);
    Log("research: findfloat %f x%d: %d hits in %zu MB (research.txt)", value, run, found, scanned >> 20);
}

// One value of the given kind at p (f float, i int32, b byte) equals v.
static bool ValueAt(const BYTE* p, char type, double v)
{
    if (type == 'f')
        return Near(*(const float*)p, (float)v);

    if (type == 'i')
        return *(const int32_t*)p == (int32_t)v;

    return *p == (BYTE)v;
}

static bool InWindow(const BYTE* base, size_t size, size_t at, size_t span, size_t step, char type, double v)
{
    size_t from = at > span ? at - span : 0;
    size_t to = at + span + step <= size ? at + span : size - step;

    for (size_t i = from; i <= to; i += step)
        if (i != at && ValueAt(base + i, type, v))
            return true;

    return false;
}

static int ScanNear(FILE* f, uintptr_t start, size_t size, const double v[3], size_t span, char type, int found)
{
    size_t step = type == 'b' ? 1 : 4;

    __try
    {
        const BYTE* base = (const BYTE*)start;

        for (size_t i = 0; i + step <= size && found < FLOAT_HITS_MAX; i += step)
        {
            if (!ValueAt(base + i, type, v[0]) || !InWindow(base, size, i, span, step, type, v[1]) ||
                !InWindow(base, size, i, span, step, type, v[2]))
                continue;

            uintptr_t at = start + i;
            fprintf(f, "\n---- hit %d at %016llX ----\n", found, (unsigned long long)at);
            DumpBlock(f, (at - span) & ~(uintptr_t)7, span * 2 + 8);
            ++found;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return found;
}

void ResearchFindNear(double a, double b, double c, size_t span, char type)
{
    FILE* f = OpenOutput();

    if (!f)
        return;

    const double v[3] = { a, b, c };
    fprintf(f, "\n==== findnear %g %g %g within 0x%zX (%c) ====\n", a, b, c, span, type);

    int found = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0x10000;

    while (found < FLOAT_HITS_MAX && VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)))
    {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) &&
            !(mbi.Protect & PAGE_GUARD))
            found = ScanNear(f, (uintptr_t)mbi.BaseAddress, mbi.RegionSize, v, span, type, found);

        if (next <= address)
            break;

        address = next;
    }

    fprintf(f, "\n==== %d hits ====\n", found);
    fclose(f);
    Log("research: findnear %g %g %g (%c): %d hits (research.txt)", a, b, c, type, found);
}

// ---------------------------------------------------------------------------
// Track / narrow (a value that changes when a setting changes)
// ---------------------------------------------------------------------------

struct TrackedPlace { uintptr_t address; char kind; };    // i int, f float, p float / 100
static std::vector<TrackedPlace> g_tracked;

// How a setting's value may be kept: as a 32-bit or 16-bit integer, or as a
// float or double of the value times a scale.
struct Encoding { char kind; int bytes; bool real; double scale; };
static const Encoding ENCODINGS[] = {
    { 'i', 4, false, 1 }, { 's', 2, false, 1 },
    { 'f', 4, true, 1 }, { 'p', 4, true, 0.01 }, { 'h', 4, true, 0.02 }, { 't', 4, true, 0.1 },
    { 'd', 8, true, 1 }, { 'e', 8, true, 0.01 },
};

static const Encoding& EncodingOf(char kind)
{
    for (const Encoding& e : ENCODINGS)
        if (e.kind == kind)
            return e;

    return ENCODINGS[0];
}

static bool Matches(const BYTE* p, const Encoding& e, double value)
{
    if (!e.real)
        return e.bytes == 4 ? *(const int32_t*)p == (int32_t)value : *(const int16_t*)p == (int16_t)value;

    double target = value * e.scale;
    double v = e.bytes == 4 ? (double)*(const float*)p : *(const double*)p;
    double d = v - target;
    return d < 0.0001 && d > -0.0001;
}

static bool Holds(uintptr_t address, char kind, double value)
{
    __try
    {
        return Matches((const BYTE*)address, EncodingOf(kind), value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void TrackRegion(uintptr_t start, size_t size, double value)
{
    __try
    {
        const BYTE* base = (const BYTE*)start;

        for (size_t i = 0; i + 8 <= size; i += 2)
            for (const Encoding& e : ENCODINGS)
                if (i % e.bytes == 0 && Matches(base + i, e, value))
                {
                    g_tracked.push_back({ start + i, e.kind });
                    break;
                }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void ResearchTrack(double value)
{
    g_tracked.clear();
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0x10000;

    while (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)))
    {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        // This thread's own stack holds the value just read from command.txt.
        bool ownStack = (uintptr_t)mbi.BaseAddress <= (uintptr_t)&mbi && (uintptr_t)&mbi < next;

        if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) &&
            !(mbi.Protect & PAGE_GUARD) && !ownStack)
            TrackRegion((uintptr_t)mbi.BaseAddress, mbi.RegionSize, value);

        if (next <= address)
            break;

        address = next;
    }

    Log("research: track %g: %zu places", value, g_tracked.size());
}

void ResearchNarrow(double value)
{
    std::vector<TrackedPlace> kept;

    for (const TrackedPlace& t : g_tracked)
        if (Holds(t.address, t.kind, value))
            kept.push_back(t);

    g_tracked.swap(kept);
    Log("research: narrow %g: %zu places left", value, g_tracked.size());

    if (g_tracked.size() > 40)
        return;

    FILE* f = OpenOutput();

    if (!f)
        return;

    fprintf(f, "\n==== narrow %g: %zu places ====\n", value, g_tracked.size());

    for (const TrackedPlace& t : g_tracked)
    {
        fprintf(f, "\n---- %016llX (%c) ----\n", (unsigned long long)t.address, t.kind);
        DumpBlock(f, (t.address - 0x80) & ~(uintptr_t)7, 0x100);
    }

    fclose(f);
}

// Floats between two values, for a setting kept as something else (a real
// distance): followed by whether they grow or shrink.
struct FloatPlace { uintptr_t address; float value; };
static std::vector<FloatPlace> g_floats;
static const size_t FLOATS_MAX = 40000000;

static void RangeRegion(uintptr_t start, size_t size, float lo, float hi)
{
    __try
    {
        const float* p = (const float*)start;

        for (size_t i = 0; i < size / 4 && g_floats.size() < FLOATS_MAX; ++i)
            if (p[i] >= lo && p[i] <= hi)
                g_floats.push_back({ (uintptr_t)(p + i), p[i] });
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void ResearchTrackRange(float lo, float hi)
{
    g_floats.clear();
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0x10000;

    while (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) && g_floats.size() < FLOATS_MAX)
    {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) &&
            !(mbi.Protect & PAGE_GUARD))
            RangeRegion((uintptr_t)mbi.BaseAddress, mbi.RegionSize, lo, hi);

        if (next <= address)
            break;

        address = next;
    }

    Log("research: trackrange %g-%g: %zu floats%s", lo, hi, g_floats.size(),
        g_floats.size() >= FLOATS_MAX ? " (limit reached)" : "");
}

void ResearchMoved(bool up)
{
    std::vector<FloatPlace> kept;

    for (const FloatPlace& f : g_floats)
    {
        float now;

        if (!Read(f.address, &now, sizeof(now)))
            continue;

        if (up ? now > f.value + 0.0001f : now < f.value - 0.0001f)
            kept.push_back({ f.address, now });
    }

    g_floats.swap(kept);
    Log("research: %s: %zu floats left", up ? "up" : "down", g_floats.size());

    if (g_floats.size() > 40)
        return;

    FILE* f = OpenOutput();

    if (!f)
        return;

    fprintf(f, "\n==== %s: %zu floats ====\n", up ? "up" : "down", g_floats.size());

    for (const FloatPlace& p : g_floats)
    {
        fprintf(f, "\n---- %016llX = %g ----\n", (unsigned long long)p.address, p.value);
        DumpBlock(f, (p.address - 0x80) & ~(uintptr_t)7, 0x100);
    }

    fclose(f);
}

void ResearchSetFloats(uintptr_t address, float value, int count)
{
    __try
    {
        float* p = (float*)address;
        float before = p[0];

        for (int i = 0; i < count; ++i)
            p[i] = value;

        Log("research: setfloat %016llX x%d: %g -> %g", (unsigned long long)address, count, before, value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("research: setfloat %016llX failed", (unsigned long long)address);
    }
}

// ---------------------------------------------------------------------------
// Pointer search (who points at an object)
// ---------------------------------------------------------------------------

// The class name of an object whose first 8 bytes are a vtable (MSVC RTTI).
static bool ClassName(uintptr_t object, char* out, size_t size)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t vtable = 0, locator = 0;
    uint32_t typeRva = 0;

    if (!Read(object, &vtable, 8) || vtable < base || vtable >= base + 0x20000000 ||
        !Read(vtable - 8, &locator, 8) || !Read(locator + 12, &typeRva, 4))
        return false;

    return TextAt(base + typeRva + 16, out, size);
}

static int ScanPointers(FILE* f, uintptr_t start, size_t size, uintptr_t target, size_t span, int found)
{
    __try
    {
        const uintptr_t* p = (const uintptr_t*)start;
        size_t count = size / sizeof(uintptr_t);

        for (size_t i = 0; i < count && found < FLOAT_HITS_MAX; ++i)
        {
            if (p[i] < target || p[i] >= target + span)
                continue;

            uintptr_t at = (uintptr_t)(p + i);
            fprintf(f, "\n---- %d: %016llX holds %016llX (+0x%llX) ----\n", found, (unsigned long long)at,
                (unsigned long long)p[i], (unsigned long long)(p[i] - target));

            // The object holding the pointer: walk back to a vtable.
            for (uintptr_t back = 0; back <= 0x400; back += 8)
            {
                char name[96];

                if (ClassName(at - back, name, sizeof(name)))
                {
                    fprintf(f, "  in %s at %016llX, offset +0x%llX\n", name, (unsigned long long)(at - back),
                        (unsigned long long)back);
                    break;
                }
            }

            DumpBlock(f, (at - 0x20) & ~(uintptr_t)7, 0x40);
            ++found;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return found;
}

void ResearchFindPointers(uintptr_t target, size_t span)
{
    FILE* f = OpenOutput();

    if (!f)
        return;

    char name[96] = "?";
    ClassName(target, name, sizeof(name));
    fprintf(f, "\n==== findptr %016llX +0x%zX (%s) ====\n", (unsigned long long)target, span, name);

    int found = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0x10000;

    while (found < FLOAT_HITS_MAX && VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)))
    {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE))
            found = ScanPointers(f, (uintptr_t)mbi.BaseAddress, mbi.RegionSize, target, span, found);

        if (next <= address)
            break;

        address = next;
    }

    fprintf(f, "\n==== %d pointers ====\n", found);
    fclose(f);
    Log("research: findptr %016llX (%s): %d pointers (research.txt)", (unsigned long long)target, name, found);
}

#endif // CC_RESEARCH
