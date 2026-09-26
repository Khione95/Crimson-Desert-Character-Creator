#include <windows.h>
#include <cstdint>
#include <cstring>

static uintptr_t g_moduleBase = 0;
static uintptr_t g_moduleEnd = 0;
static uintptr_t g_map = 0;
static int g_state = 0;

static bool IsReadable(DWORD protect)
{
    protect &= 0xFF;
    return protect == PAGE_READONLY ||
           protect == PAGE_READWRITE ||
           protect == PAGE_WRITECOPY ||
           protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}

static bool FindPatternInRange(
    uintptr_t start,
    uintptr_t end,
    const BYTE* pattern,
    SIZE_T patternSize,
    uintptr_t& result)
{
    if (start >= end || patternSize == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t current = start;

    while (current < end)
    {
        if (!VirtualQuery((LPCVOID)current, &mbi, sizeof(mbi)))
            return false;

        uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
        uintptr_t regionEnd = regionStart + mbi.RegionSize;

        if (regionStart < start)
            regionStart = start;
        if (regionEnd > end)
            regionEnd = end;

        if (mbi.State == MEM_COMMIT && IsReadable(mbi.Protect))
        {
            const BYTE* data = (const BYTE*)regionStart;
            SIZE_T size = (SIZE_T)(regionEnd - regionStart);

            if (size >= patternSize)
            {
                for (SIZE_T i = 0; i + patternSize <= size; ++i)
                {
                    if (memcmp(data + i, pattern, patternSize) == 0)
                    {
                        result = regionStart + i;
                        return true;
                    }
                }
            }
        }

        if (regionEnd <= current)
            return false;

        current = regionEnd;
    }

    return false;
}

static bool GetImageSections(
    IMAGE_SECTION_HEADER*& sections,
    WORD& count)
{
    BYTE* module = (BYTE*)g_moduleBase;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)module;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    IMAGE_NT_HEADERS64* nt =
        (IMAGE_NT_HEADERS64*)(module + dos->e_lfanew);

    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    sections = IMAGE_FIRST_SECTION(nt);
    count = nt->FileHeader.NumberOfSections;
    return true;
}

static uintptr_t FindPrimaryGroupMap()
{
    IMAGE_SECTION_HEADER* sections = nullptr;
    WORD sectionCount = 0;

    if (!GetImageSections(sections, sectionCount))
        return 0;

    const BYTE strongPattern[] =
    {
        0x02, 0x03, 0x06, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF
    };

    const BYTE fallbackPattern[] =
    {
        0x02, 0x03, 0x06, 0x00
    };

    // Search only non-executable PE sections.
    // This avoids scanning the game's code and removes the
    // executable-memory XREF scan used by the diagnostic build.
    for (int pass = 0; pass < 2; ++pass)
    {
        const BYTE* pattern =
            (pass == 0) ? strongPattern : fallbackPattern;
        SIZE_T patternSize =
            (pass == 0) ? sizeof(strongPattern) : sizeof(fallbackPattern);

        for (WORD i = 0; i < sectionCount; ++i)
        {
            const IMAGE_SECTION_HEADER& section = sections[i];

            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
                continue;

            uintptr_t start =
                g_moduleBase + section.VirtualAddress;

            uintptr_t end =
                start + section.Misc.VirtualSize;

            if (end > g_moduleEnd)
                end = g_moduleEnd;

            uintptr_t result = 0;

            if (FindPatternInRange(
                start,
                end,
                pattern,
                patternSize,
                result))
            {
                return result;
            }
        }
    }

    return 0;
}

static bool ApplyMap()
{
    if (!g_map)
        return false;

    BYTE values[4];

    if (g_state == 0)
    {
        values[0] = 1;
        values[1] = 2;
        values[2] = 3;
        values[3] = 6;
    }
    else if (g_state == 1)
    {
        values[0] = 0;
        values[1] = 2;
        values[2] = 3;
        values[3] = 6;
    }
    else
    {
        values[0] = 5;
        values[1] = 2;
        values[2] = 3;
        values[3] = 6;
    }

    DWORD oldProtect = 0;

    if (!VirtualProtect(
        (LPVOID)g_map,
        sizeof(values),
        PAGE_READWRITE,
        &oldProtect))
    {
        return false;
    }

    memcpy((void*)g_map, values, sizeof(values));

    FlushInstructionCache(
        GetCurrentProcess(),
        (LPCVOID)g_map,
        sizeof(values));

    VirtualProtect(
        (LPVOID)g_map,
        sizeof(values),
        oldProtect,
        &oldProtect);

    return true;
}

static bool InitializeModuleRange()
{
    g_moduleBase =
        (uintptr_t)GetModuleHandleA(nullptr);

    if (!g_moduleBase)
        return false;

    BYTE* module = (BYTE*)g_moduleBase;

    IMAGE_DOS_HEADER* dos =
        (IMAGE_DOS_HEADER*)module;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    IMAGE_NT_HEADERS64* nt =
        (IMAGE_NT_HEADERS64*)(module + dos->e_lfanew);

    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    g_moduleEnd =
        g_moduleBase + nt->OptionalHeader.SizeOfImage;

    return true;
}

DWORD WINAPI MainThread(LPVOID)
{
    Sleep(5000);

    if (!InitializeModuleRange())
        return 0;

    g_map = FindPrimaryGroupMap();

    if (!g_map)
        return 0;

    g_state = 0;
    ApplyMap();

    while (true)
    {
        if (GetAsyncKeyState(VK_F6) & 1)
        {
            ++g_state;

            if (g_state > 2)
                g_state = 0;

            ApplyMap();
        }

        Sleep(16);
    }

    return 0;
}

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        HANDLE thread = CreateThread(
            nullptr,
            0,
            MainThread,
            nullptr,
            0,
            nullptr);

        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
