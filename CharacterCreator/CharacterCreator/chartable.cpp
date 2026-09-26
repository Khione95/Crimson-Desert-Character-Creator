#include "pch.h"
#include "chartable.h"
#include "addresses.h"
#include "log.h"

#include <string.h>

// The CharacterInfoManager (its vtable and the game's pointer to it come from
// addresses.h) and its layout:
//   +0x08 u32  number of characters
//   +0x58      pointer to an array of pointers to the entries

static uintptr_t g_manager = 0;
static uintptr_t g_entries = 0;
static int g_count = 0;

static bool SafeRead(uintptr_t address, void* out, size_t size)
{
    __try
    {
        memcpy(out, (const void*)address, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SafeWrite(uintptr_t address, const void* data, size_t size)
{
    __try
    {
        memcpy((void*)address, data, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static uintptr_t Entry(int index)
{
    uint64_t p = 0;

    if (index < 0 || index >= g_count || !SafeRead(g_entries + index * 8, &p, 8))
        return 0;

    return (uintptr_t)p;
}

uintptr_t CharTableEntry(int index)
{
    return Entry(index);
}

// The name is reached through two pointers: entry+8 -> string object -> text.
bool CharTableName(int index, char* out, size_t size)
{
    uintptr_t e = Entry(index);
    uint64_t object = 0, text = 0;

    if (!e || !SafeRead(e + 8, &object, 8) || !object || !SafeRead((uintptr_t)object, &text, 8) ||
        !text || !SafeRead((uintptr_t)text, out, size - 1))
        return false;

    out[size - 1] = 0;

    for (size_t i = 0; i < size - 1; ++i)
    {
        if (out[i] == 0)
            break;

        if (out[i] < 0x20 || out[i] >= 0x7F)
        {
            out[i] = 0;
            break;
        }
    }

    return true;
}

// A manager candidate is accepted when its first entry is named "Kliff".
static bool Accept(uintptr_t manager)
{
    uint32_t count = 0;
    uint64_t entries = 0;

    if (!SafeRead(manager + 0x08, &count, 4) || count < 100 || count > 100000 ||
        !SafeRead(manager + 0x58, &entries, 8) || !entries)
        return false;

    g_manager = manager;
    g_entries = (uintptr_t)entries;
    g_count = (int)count;

    char name[16];

    if (CharTableName(0, name, sizeof(name)) && strcmp(name, "Kliff") == 0)
        return true;

    g_manager = g_entries = 0;
    g_count = 0;
    return false;
}

// Logs where the game keeps a pointer to the manager inside its own module,
// so a later version can read it directly instead of searching.
static void LogGlobalReferences()
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
    int found = 0;

    for (uintptr_t a = base; a < end && found < 8; )
    {
        MEMORY_BASIC_INFORMATION mbi;

        if (VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        uintptr_t re = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        DWORD p = mbi.Protect & 0xFF;

        if (mbi.State == MEM_COMMIT && (p == PAGE_READWRITE || p == PAGE_WRITECOPY) && !(mbi.Protect & PAGE_GUARD))
        {
            __try
            {
                for (const uint64_t* q = (const uint64_t*)a; (uintptr_t)(q + 1) <= re && found < 8; ++q)
                {
                    if (*q == g_manager)
                    {
                        Log("  manager pointer stored at module+%llX", (unsigned long long)((uintptr_t)q - base));
                        ++found;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        a = re;
    }

    if (!found)
        Log("  no manager pointer in the module's data");
}

// The game's own pointer to the manager: instant, but only set once the
// world has been loaded.
static bool FindFast()
{
    uint64_t manager = 0, vtable = 0;

    if (!AddressOf(ADDR_MANAGERPOINTER) || !AddressOf(ADDR_MANAGERVTABLE) ||
        !SafeRead(AddressOf(ADDR_MANAGERPOINTER), &manager, 8) || !manager ||
        !SafeRead((uintptr_t)manager, &vtable, 8) || vtable != AddressOf(ADDR_MANAGERVTABLE))
        return false;

    return Accept((uintptr_t)manager);
}

// Follows the game's pointer, so a table rebuilt by the game is picked up.
bool CharTableAvailable()
{
    uint64_t current = 0;

    if (AddressOf(ADDR_MANAGERPOINTER) && SafeRead(AddressOf(ADDR_MANAGERPOINTER), &current, 8) && current &&
        current == g_manager)
        return true;

    g_manager = g_entries = 0;
    g_count = 0;
    return FindFast();
}

uintptr_t CharTableInstance()
{
    return g_manager;
}

bool CharTableFind()
{
    if (g_manager || FindFast())
        return true;

    uint64_t vtable = AddressOf(ADDR_MANAGERVTABLE);

    if (!vtable)
        return false;

    DWORD t0 = GetTickCount();

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t address = (uintptr_t)si.lpMinimumApplicationAddress;

    while (address < (uintptr_t)si.lpMaximumApplicationAddress)
    {
        MEMORY_BASIC_INFORMATION mbi;

        if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        uintptr_t rs = (uintptr_t)mbi.BaseAddress, re = rs + mbi.RegionSize;

        if (re <= rs)
            break;

        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & 0xFF) == PAGE_READWRITE &&
            !(mbi.Protect & PAGE_GUARD))
        {
            // Read the region directly; memory freed meanwhile only ends
            // this region's search.
            __try
            {
                for (const uint64_t* q = (const uint64_t*)rs; (uintptr_t)(q + 1) <= re; ++q)
                {
                    if (*q == vtable && Accept((uintptr_t)q))
                    {
                        Log("character table found: manager %016llX, %d characters (%lu ms)",
                            (unsigned long long)g_manager, g_count, GetTickCount() - t0);
                        LogGlobalReferences();
                        return true;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        address = re;
    }

    Log("character table not found (%lu ms)", GetTickCount() - t0);
    return false;
}

int CharTableCount()
{
    return g_count;
}

bool CharTableRead(int index, CharacterFields* out)
{
    uintptr_t e = Entry(index);

    return e &&
        SafeRead(e + 0x8C, &out->appearance, 2) &&
        SafeRead(e + 0x8E, &out->prefab, 2) &&
        SafeRead(e + 0x90, &out->skeleton, 2) &&
        SafeRead(e + 0x96, &out->lookup24, 2) &&
        SafeRead(e + 0x98, &out->lookup25, 2) &&
        SafeRead(e + 0xBA, &out->f36, 2) &&
        SafeRead(e + 0x1CC, &out->action, 4);
}

bool CharTableWrite(int index, const CharacterFields& f)
{
    uintptr_t e = Entry(index);

    return e &&
        SafeWrite(e + 0x8C, &f.appearance, 2) &&
        SafeWrite(e + 0x8E, &f.prefab, 2) &&
        SafeWrite(e + 0x90, &f.skeleton, 2) &&
        SafeWrite(e + 0x96, &f.lookup24, 2) &&
        SafeWrite(e + 0x98, &f.lookup25, 2) &&
        SafeWrite(e + 0xBA, &f.f36, 2) &&
        SafeWrite(e + 0x1CC, &f.action, 4);
}

int CharTableIndexOf(const char* name)
{
    char buffer[64];

    for (int i = 0; i < g_count; ++i)
    {
        if (CharTableName(i, buffer, sizeof(buffer)) && strcmp(buffer, name) == 0)
            return i;
    }

    return -1;
}

bool CharTableReadRaw(int index, size_t offset, void* out, size_t size)
{
    uintptr_t e = Entry(index);
    return e && SafeRead(e + offset, out, size);
}

bool CharTableWriteRaw(int index, size_t offset, const void* data, size_t size)
{
    uintptr_t e = Entry(index);
    return e && SafeWrite(e + offset, data, size);
}
