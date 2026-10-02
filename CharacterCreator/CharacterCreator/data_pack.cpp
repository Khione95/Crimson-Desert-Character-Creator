#include "pch.h"
#include "data_pack.h"
#include "log.h"
#include "resource.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Format (tools/build_data.py, pack_runtime_data):
//   "CCDATA1\0", version "\0", uint32 file count, then per file:
//   uint16 name length, name ('/' separators), uint32 size, bytes.
static const char MAGIC[] = "CCDATA1";

static bool WriteWhole(const char* path, const void* data, size_t size)
{
    FILE* f = NULL;
    fopen_s(&f, path, "wb");

    if (!f)
        return false;

    bool ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

// "<version> <size> <checksum of the packed data>": unpacking is skipped
// while it matches. The checksum catches data changed within one version.
static void Stamp(char* out, size_t size, const char* version, const BYTE* data, DWORD packed)
{
    uint32_t hash = 2166136261u;    // FNV-1a

    for (DWORD i = 0; i < packed; ++i)
        hash = (hash ^ data[i]) * 16777619u;

    sprintf_s(out, size, "%s %lu %08X", version, packed, hash);
}

static bool StampMatches(const char* folder, const char* stamp)
{
    char path[MAX_PATH], menu[MAX_PATH], text[128] = { 0 };
    sprintf_s(path, "%s\\data.version", folder);
    sprintf_s(menu, "%s\\menu.txt", folder);

    FILE* f = NULL;
    fopen_s(&f, path, "r");

    if (!f)
        return false;

    fgets(text, sizeof(text), f);
    fclose(f);
    text[strcspn(text, "\r\n")] = 0;
    return strcmp(text, stamp) == 0 && GetFileAttributesA(menu) != INVALID_FILE_ATTRIBUTES;
}

bool DataPackUnpack(HMODULE module, const char* folder)
{
    HRSRC resource = FindResourceA(module, MAKEINTRESOURCEA(IDR_MENU_DATA), MAKEINTRESOURCEA(10));   // RT_RCDATA
    HGLOBAL handle = resource ? LoadResource(module, resource) : NULL;
    const BYTE* data = handle ? (const BYTE*)LockResource(handle) : NULL;
    DWORD size = resource ? SizeofResource(module, resource) : 0;

    if (!data || size < sizeof(MAGIC) + 8 || memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
    {
        Log("no menu data built into the plugin - using the data folder as it is");
        return false;
    }

    const BYTE* p = data + sizeof(MAGIC);
    const BYTE* end = data + size;
    const char* version = (const char*)p;
    size_t versionLength = strnlen(version, end - p);

    if (p + versionLength + 1 + 4 > end)
        return false;

    char stamp[128];
    Stamp(stamp, sizeof(stamp), version, data, size);

    if (StampMatches(folder, stamp))
        return true;

    p += versionLength + 1;
    uint32_t count;
    memcpy(&count, p, 4);
    p += 4;

    int written = 0;

    for (uint32_t i = 0; i < count; ++i)
    {
        uint16_t nameLength;
        uint32_t fileSize;

        if (p + 2 > end)
            break;

        memcpy(&nameLength, p, 2);
        p += 2;

        if (p + nameLength + 4 > end || nameLength >= MAX_PATH / 2)
            break;

        char name[MAX_PATH];
        memcpy(name, p, nameLength);
        name[nameLength] = 0;
        p += nameLength;
        memcpy(&fileSize, p, 4);
        p += 4;

        if (p + fileSize > end || strstr(name, ".."))
            break;

        // "icons/x.jpg" -> <folder>\icons\x.jpg, creating the folder first.
        char path[MAX_PATH];
        sprintf_s(path, "%s\\%s", folder, name);

        for (char* c = path + strlen(folder) + 1; *c; ++c)
        {
            if (*c == '/')
            {
                *c = 0;
                CreateDirectoryA(path, NULL);
                *c = '\\';
            }
        }

        if (WriteWhole(path, p, fileSize))
            ++written;

        p += fileSize;
    }

    char stampPath[MAX_PATH];
    sprintf_s(stampPath, "%s\\data.version", folder);

    if (written == (int)count)
        WriteWhole(stampPath, stamp, strlen(stamp));

    Log("menu data %s unpacked: %d of %u files", version, written, count);
    return written == (int)count;
}
