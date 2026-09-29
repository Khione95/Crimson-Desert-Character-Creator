#include "pch.h"
#include "addresses.h"
#include "log.h"
#include "switches.h"

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <string.h>
#include <vector>

// ---------------------------------------------------------------------------
// The build the addresses were taken from (game 2.03.01 / 2.03.02)
// ---------------------------------------------------------------------------

static const DWORD KNOWN_TIMESTAMP = 0x6AB28F00, KNOWN_IMAGE_SIZE = 0x173AB000;

static const uintptr_t KNOWN_RVAS[ADDR_COUNT] = {
    0x72BF80,       // SetDecoration(controller, index, value)
    0x72CFB0,       // QueueMeshChange(controller + 0xF8, {slot, old, new})
    0x726C50,       // Rebuild(controller)
    0x3D2BE0,       // GrowBytes(vector, count)
    0x142BDD0,      // LoadXml(path, root out, ...)
    0x2438A40,      // ParseAppearance(table, root, ...)
    0x6D69A48,      // CharacterInfoManager*
    0x5B4C6C0,      // scale component vtable
    0x5B4D168,      // scale object vtable
    0x559DD98,      // CharacterCustomizationController vtable
    0x58FE780,      // CharacterInfoManager vtable
    0x2E8C460,      // BuildLipSyncPath(out, model folder, line)
    0x6D20E78,      // camera settings: distance (+0x00), vertical (+0xA0), horizontal (+0xF0)
};

static const char* const NAMES[ADDR_COUNT] = {
    "SetDecoration", "QueueMeshChange", "Rebuild", "GrowBytes", "LoadXml", "ParseAppearance",
    "ManagerPointer", "ScaleComponentVtable", "ScaleObjectVtable", "ControllerVtable", "ManagerVtable", "LipSyncPath",
    "CameraSettings",
};

// Classes found by their RTTI name on other builds.
static const struct { AddressId id; const char* name; } RTTI_CLASSES[] = {
    { ADDR_CONTROLLERVTABLE, ".?AVCharacterCustomizationController@pa@@" },
    { ADDR_MANAGERVTABLE, ".?AVCharacterInfoManager@pa@@" },
};

struct Signature
{
    AddressId id;
    const char* pattern;    // hex bytes, ?? = any
    int relOffset;          // offset of the rel32 in the pattern; -1 = the match itself
    int tail;               // bytes after the rel32 to the end of its instruction
};

static const Signature SIGNATURES[] = {
#include "signatures.inc"
};

static uintptr_t g_addresses[ADDR_COUNT];
static bool g_knownBuild = false;

uintptr_t AddressOf(AddressId id)
{
    return id >= 0 && id < ADDR_COUNT ? g_addresses[id] : 0;
}

bool AddressesKnownBuild()
{
    return g_knownBuild;
}

bool FunctionHookable(const void* target, const unsigned char* prologue, size_t size, const char* what)
{
    if (!target)
        return false;

    if (!g_knownBuild || memcmp(target, prologue, size) == 0)
        return true;

    // Another mod's hook: a jump (E9 rel32, FF 25 [rip], or mov rax / jmp rax).
    const BYTE* b = (const BYTE*)target;
    bool jump = b[0] == 0xE9 || (b[0] == 0xFF && b[1] == 0x25) || (b[0] == 0x48 && b[1] == 0xB8 && b[10] == 0xFF && b[11] == 0xE0);

    if (jump)
    {
        Log("%s: another mod hooks it already - hooking on top of that", what);
        return true;
    }

    Log("%s: unexpected code (%02X %02X %02X %02X %02X %02X)", what, b[0], b[1], b[2], b[3], b[4], b[5]);
    return false;
}

// ---------------------------------------------------------------------------
// The game's memory: its sections, and the readable parts of them
// ---------------------------------------------------------------------------

struct Range
{
    const uint8_t* start;
    size_t size;
};

static uintptr_t g_base = 0;
static std::vector<Range> g_code, g_data;   // readable parts of code / other sections

static void AddReadable(uintptr_t start, size_t size, std::vector<Range>* out)
{
    uintptr_t p = start, end = start + size;

    while (p < end)
    {
        MEMORY_BASIC_INFORMATION mbi;

        if (!VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi)))
            break;

        uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        DWORD protect = mbi.Protect & 0xFF;
        bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
            (protect == PAGE_READONLY || protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
             protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY);

        uintptr_t chunkEnd = regionEnd < end ? regionEnd : end;

        if (readable && chunkEnd > p)
        {
            if (!out->empty() && (uintptr_t)(out->back().start + out->back().size) == p)
                out->back().size += chunkEnd - p;
            else
                out->push_back({ (const uint8_t*)p, chunkEnd - p });
        }

        p = regionEnd;
    }
}

static void ReadSections()
{
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)g_base;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);

    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
    {
        DWORD c = s->Characteristics;
        bool code = (c & IMAGE_SCN_CNT_CODE) != 0;

        // The game's code sections; the protection's writable code section
        // (hundreds of MB) holds none of the plugin's addresses.
        if (code && !(c & IMAGE_SCN_MEM_WRITE))
            AddReadable(g_base + s->VirtualAddress, s->Misc.VirtualSize, &g_code);
        else if (!code)
            AddReadable(g_base + s->VirtualAddress, s->Misc.VirtualSize, &g_data);
    }
}

static bool InRanges(const std::vector<Range>& ranges, uintptr_t address)
{
    for (const Range& r : ranges)
        if (address >= (uintptr_t)r.start && address < (uintptr_t)r.start + r.size)
            return true;

    return false;
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

// Parses "48 8B ?? ..." (-1 = any byte).
static std::vector<int> ParsePattern(const char* text)
{
    std::vector<int> out;

    for (const char* p = text; *p; )
    {
        while (*p == ' ')
            ++p;

        if (!*p)
            break;

        if (p[0] == '?')
            out.push_back(-1);
        else
            out.push_back((int)strtoul(std::string(p, 2).c_str(), NULL, 16));

        p += 2;
    }

    return out;
}

// Where the pattern is in the ranges: fills up to 2 matches, returns how many.
static int FindPattern(const std::vector<Range>& ranges, const std::vector<int>& pattern, const uint8_t** first)
{
    // The longest run of fixed bytes is searched for, the rest compared.
    size_t bestStart = 0, bestLength = 0;

    for (size_t i = 0; i < pattern.size(); )
    {
        size_t j = i;

        while (j < pattern.size() && pattern[j] >= 0)
            ++j;

        if (j - i > bestLength)
        {
            bestStart = i;
            bestLength = j - i;
        }

        i = j + 1;
    }

    if (!bestLength)
        return 0;

    std::vector<uint8_t> anchor;

    for (size_t i = bestStart; i < bestStart + bestLength; ++i)
        anchor.push_back((uint8_t)pattern[i]);

    std::boyer_moore_horspool_searcher<std::vector<uint8_t>::iterator> searcher(anchor.begin(), anchor.end());
    int found = 0;

    for (const Range& r : ranges)
    {
        const uint8_t* p = r.start;
        const uint8_t* end = r.start + r.size;

        while (p < end)
        {
            const uint8_t* hit = std::search(p, end, searcher);

            if (hit == end)
                break;

            const uint8_t* start = hit - bestStart;
            bool match = start >= r.start && start + pattern.size() <= end;

            for (size_t i = 0; match && i < pattern.size(); ++i)
                if (pattern[i] >= 0 && start[i] != (uint8_t)pattern[i])
                    match = false;

            if (match)
            {
                if (!found)
                    *first = start;

                if (++found >= 2)
                    return found;
            }

            p = hit + 1;
        }
    }

    return found;
}

// ---------------------------------------------------------------------------
// RTTI: the vtable of a class by its name
// ---------------------------------------------------------------------------

static uintptr_t FindAligned(const std::vector<Range>& ranges, const void* value, size_t size, size_t alignment,
    const std::function<bool(uintptr_t)>& accept)
{
    for (const Range& r : ranges)
    {
        uintptr_t start = ((uintptr_t)r.start + alignment - 1) & ~(uintptr_t)(alignment - 1);

        for (uintptr_t p = start; p + size <= (uintptr_t)r.start + r.size; p += alignment)
            if (memcmp((const void*)p, value, size) == 0 && accept(p))
                return p;
    }

    return 0;
}

static uintptr_t VtableOfClass(const char* name)
{
    // The type descriptor: vtable pointer, spare, then the name.
    std::vector<int> text;

    for (const char* c = name; *c; ++c)
        text.push_back((uint8_t)*c);

    text.push_back(0);

    const uint8_t* at = NULL;

    if (FindPattern(g_data, text, &at) != 1)
        return 0;

    uint32_t typeRva = (uint32_t)((uintptr_t)at - 16 - g_base);

    // The complete object locator of the class's own vtable: signature 1,
    // offset 0, type descriptor, ..., its own RVA.
    uintptr_t locatorField = FindAligned(g_data, &typeRva, 4, 4, [](uintptr_t p) {
        const uint32_t* locator = (const uint32_t*)(p - 12);
        return locator[0] == 1 && locator[1] == 0 && locator[5] == (uint32_t)(p - 12 - g_base);
    });

    if (!locatorField)
        return 0;

    uint64_t locator = locatorField - 12;
    uintptr_t slot = FindAligned(g_data, &locator, 8, 8, [](uintptr_t) { return true; });
    return slot ? slot + 8 : 0;
}

// ---------------------------------------------------------------------------

static bool Plausible(AddressId id, uintptr_t address)
{
    switch (id)
    {
    case ADDR_MANAGERPOINTER:
        return InRanges(g_data, address);

    case ADDR_SCALECOMPONENTVTABLE:
    case ADDR_SCALEOBJECTVTABLE:
    case ADDR_CONTROLLERVTABLE:
    case ADDR_MANAGERVTABLE:
        return InRanges(g_data, address) && InRanges(g_code, *(const uintptr_t*)address);

    default:
        return InRanges(g_code, address);
    }
}

void AddressesInit()
{
    g_base = (uintptr_t)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(g_base + ((const IMAGE_DOS_HEADER*)g_base)->e_lfanew);
    g_knownBuild = nt->FileHeader.TimeDateStamp == KNOWN_TIMESTAMP && nt->OptionalHeader.SizeOfImage == KNOWN_IMAGE_SIZE;

    // disable.txt "knownbuild": look the addresses up anyway, to test the
    // lookup (the results are compared with the known addresses).
    bool test = g_knownBuild && PartDisabled("knownbuild");

    if (g_knownBuild && !test)
    {
        for (int i = 0; i < ADDR_COUNT; ++i)
            g_addresses[i] = g_base + KNOWN_RVAS[i];

        Log("addresses: known game build");
        return;
    }

    Log("addresses: unknown game build - looking the addresses up");
    DWORD started = GetTickCount();
    ReadSections();

    for (const auto& c : RTTI_CLASSES)
        g_addresses[c.id] = VtableOfClass(c.name);

    // Each pattern that matches exactly once votes for an address.
    std::map<uintptr_t, int> votes[ADDR_COUNT];
    int tried[ADDR_COUNT] = {};

    for (const Signature& s : SIGNATURES)
    {
        ++tried[s.id];
        const uint8_t* at = NULL;

        if (FindPattern(g_code, ParsePattern(s.pattern), &at) != 1)
            continue;

        uintptr_t address = (uintptr_t)at;

        if (s.relOffset >= 0)
        {
            int32_t rel = *(const int32_t*)(at + s.relOffset);
            address = (uintptr_t)(at + s.relOffset + 4 + s.tail) + rel;
        }

        if (Plausible(s.id, address))
            ++votes[s.id][address];
    }

    for (int i = 0; i < ADDR_COUNT; ++i)
    {
        if (votes[i].empty())
            continue;

        auto best = std::max_element(votes[i].begin(), votes[i].end(),
            [](const std::pair<const uintptr_t, int>& a, const std::pair<const uintptr_t, int>& b) { return a.second < b.second; });
        int ties = (int)std::count_if(votes[i].begin(), votes[i].end(),
            [&](const std::pair<const uintptr_t, int>& v) { return v.second == best->second; });

        if (ties == 1)
            g_addresses[i] = best->first;
    }

    for (int i = 0; i < ADDR_COUNT; ++i)
    {
        int agreeing = 0;

        for (const auto& v : votes[i])
            if (v.first == g_addresses[i])
                agreeing = v.second;

        if (g_addresses[i] && !tried[i])
            Log("addresses: %s at +0x%llX (by class name)", NAMES[i], (unsigned long long)(g_addresses[i] - g_base));
        else if (g_addresses[i])
            Log("addresses: %s at +0x%llX (%d of %d patterns)", NAMES[i], (unsigned long long)(g_addresses[i] - g_base),
                agreeing, tried[i]);
        else
            Log("addresses: %s not found (%d patterns) - the parts that need it stay off", NAMES[i], tried[i]);
    }

    Log("addresses: looked up in %lu ms", GetTickCount() - started);

    if (test)
    {
        int right = 0;

        for (int i = 0; i < ADDR_COUNT; ++i)
        {
            if (g_addresses[i] == g_base + KNOWN_RVAS[i])
                ++right;
            else
                Log("addresses: test - %s should be +0x%llX", NAMES[i], (unsigned long long)KNOWN_RVAS[i]);
        }

        Log("addresses: test - %d of %d found correctly", right, ADDR_COUNT);
    }
}
