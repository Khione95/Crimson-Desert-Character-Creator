#include "pch.h"
#include "commands.h"
#include "chartable.h"
#include "eyes.h"
#include "game.h"
#include "log.h"
#include "menu.h"
#include "research.h"

#include <stdio.h>
#include <string.h>

static char g_path[MAX_PATH] = { 0 };
static char g_folder[MAX_PATH] = { 0 };

void CommandsInit(const char* folder)
{
    sprintf_s(g_path, "%s\\command.txt", folder);
    strcpy_s(g_folder, folder);
    ResearchInit(folder);
}

// Character the commands act on ("character <0 Kliff / 1 Damiane / 2 Oongka>").
static int g_character = CHAR_KLIFF;

static void Dump()
{
    Appearance look;

    if (!GameReadAppearance(g_character, &look))
    {
        Log("dump: %S not found yet", CHARACTER_NAMES[g_character]);
        return;
    }

    char line[1024];
    int len = sprintf_s(line, "dump: mesh");

    for (int slot = 0; slot < MESH_SLOT_COUNT; ++slot)
        len += sprintf_s(line + len, sizeof(line) - len, " %d", look.mesh[slot]);

    Log("%s", line);

    for (int row = 0; row < DECORATION_COUNT; row += 25)
    {
        len = sprintf_s(line, "dump: values %3d:", row);

        for (int i = row; i < row + 25 && i < DECORATION_COUNT; ++i)
            len += sprintf_s(line + len, sizeof(line) - len, " %d", look.decoration[i]);

        Log("%s", line);
    }

    char counts[256];
    len = sprintf_s(counts, "dump: mesh options");

    for (int slot = 0; slot < 7; ++slot)
        len += sprintf_s(counts + len, sizeof(counts) - len, " %u", GameMeshOptionCount(g_character, slot));

    Log("%s", counts);
}

static void Run(const char* line)
{
    char kind[32] = { 0 };
    int a = 0, b = 0;
    int n = sscanf_s(line, "%31s %d %d", kind, (unsigned)sizeof(kind), &a, &b);

    if (n <= 0)
        return;

    Log("command: %s", line);

    if (strcmp(kind, "decoration") == 0 && n == 3)
        GameSetDecoration(g_character, a, (uint8_t)b);
    else if (strcmp(kind, "mesh") == 0 && n == 3)
        GameSetMesh(g_character, a, (uint8_t)b);
    else if (strcmp(kind, "dump") == 0)
        Dump();
    else if (strcmp(kind, "dumpclass") == 0)
    {
        unsigned long long rva = 0, bytes = 0x200;
        int count = 20;

        if (sscanf_s(line, "%*s %llx %llx %d", &rva, &bytes, &count) >= 1)
            ResearchDumpClass((uintptr_t)rva, (size_t)bytes, count);
    }
    else if (strcmp(kind, "dumpmem") == 0)
    {
        unsigned long long address = 0, bytes = 0x100;

        if (sscanf_s(line, "%*s %llx %llx", &address, &bytes) >= 1)
            ResearchDumpMemory((uintptr_t)address, (size_t)bytes);
    }
    else if (strcmp(kind, "chartable") == 0)
    {
        unsigned long long array = 0, bytes = 0x700;
        int count = 0;
        char filter[64] = "";

        if (sscanf_s(line, "%*s %llx %d %llx %63s", &array, &count, &bytes, filter, (unsigned)sizeof(filter)) >= 2)
            ResearchCharacterTable((uintptr_t)array, count, (size_t)bytes, filter[0] ? filter : NULL);
    }
    else if (strcmp(kind, "charfields") == 0 && n >= 2)
    {
        CharacterFields f;
        char name[64] = "?";

        if (CharTableFind() && CharTableRead(a, &f))
        {
            CharTableName(a, name, sizeof(name));
            Log("character %d %s: appearance %u prefab %u skeleton %u lookup24 %u lookup25 %u f36 %u action %08X",
                a, name, f.appearance, f.prefab, f.skeleton, f.lookup24, f.lookup25, f.f36, f.action);
        }
    }
    else if (strcmp(kind, "copychar") == 0 && n == 3)
    {
        CharacterFields f;

        if (CharTableFind() && CharTableRead(a, &f) && CharTableWrite(b, f))
            Log("copied character fields %d -> %d", a, b);
    }
    else if (strcmp(kind, "eyes") == 0 && n == 2)
        EyesChoose(g_character, a);
    else if (strcmp(kind, "reloadhead") == 0)
    {
        int ms = HEAD_AWAY_MS, away = -1;
        sscanf_s(line, "%*s %d %d", &ms, &away);
        GameReloadHead(g_character, ms, away);
    }
    else if (strcmp(kind, "charcopy") == 0)
    {
        // charcopy <from name> <to name> <offset hex> <bytes 1-8>
        char from[64], to[64];
        unsigned offset = 0;
        int bytes = 0;

        if (sscanf_s(line, "%*s %63s %63s %x %d", from, (unsigned)sizeof(from), to, (unsigned)sizeof(to), &offset, &bytes) == 4 &&
            bytes >= 1 && bytes <= 8 && CharTableFind())
        {
            int a1 = CharTableIndexOf(from), b1 = CharTableIndexOf(to);
            uint64_t value = 0, before = 0, after = 0;

            if (a1 >= 0 && b1 >= 0 && CharTableReadRaw(a1, offset, &value, bytes) && CharTableReadRaw(b1, offset, &before, bytes) &&
                CharTableWriteRaw(b1, offset, &value, bytes) && CharTableReadRaw(b1, offset, &after, bytes))
                Log("charcopy %s -> %s +0x%X (%d bytes): %llX -> %llX", from, to, offset, bytes,
                    (unsigned long long)before, (unsigned long long)after);
            else
                Log("charcopy failed (%s %d, %s %d)", from, a1, to, b1);
        }
    }
    else if (strcmp(kind, "findfloat") == 0)
    {
        float value = 0;
        int run = 1;

        if (sscanf_s(line, "%*s %f %d", &value, &run) >= 1 && run >= 1 && run <= 16)
            ResearchFindFloats(value, run);
    }
    else if (strcmp(kind, "setfloat") == 0)
    {
        unsigned long long address = 0;
        float value = 0;
        int count = 1;

        if (sscanf_s(line, "%*s %llx %f %d", &address, &value, &count) >= 2 && count >= 1 && count <= 16)
            ResearchSetFloats((uintptr_t)address, value, count);
    }
    else if (strcmp(kind, "findptr") == 0)
    {
        unsigned long long address = 0, span = 8;

        if (sscanf_s(line, "%*s %llx %llx", &address, &span) >= 1)
            ResearchFindPointers((uintptr_t)address, (size_t)span);
    }
    else if (strcmp(kind, "findowner") == 0)
        ResearchFindOwner(GameMainController());
    else if (strcmp(kind, "menu") == 0)
        MenuToggle(g_character);
    else if (strcmp(kind, "clear") == 0)
        GameSetDesired(g_character, Appearance{}, AppearanceMask{});
    else if (strcmp(kind, "character") == 0 && n == 2 && a >= 0 && a < CHARACTER_COUNT)
        g_character = a;
    else
        Log("unknown command");
}

void CommandsPoll()
{
    FILE* f = NULL;
    fopen_s(&f, g_path, "r");

    if (!f)
        return;

    char line[256];

    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        Run(line);
    }

    fclose(f);
    DeleteFileA(g_path);
}
