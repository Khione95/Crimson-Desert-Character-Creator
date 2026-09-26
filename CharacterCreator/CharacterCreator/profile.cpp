#include "pch.h"
#include "profile.h"
#include "game.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

static char g_path[CHARACTER_COUNT][MAX_PATH] = {};
static Appearance g_savedValues[CHARACTER_COUNT] = {};
static AppearanceMask g_savedMask[CHARACTER_COUNT] = {};

void ProfileInit(const char* folder)
{
    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        CharacterFile(g_path[ch], MAX_PATH, folder, "profile", ch);
}

static void Load(int ch)
{
    Appearance values = {};
    AppearanceMask mask = {};

    FILE* f = NULL;
    fopen_s(&f, g_path[ch], "r");

    if (!f)
        return;

    char kind[32];
    int index = 0, value = 0, loaded = 0;

    while (fscanf_s(f, "%31s %d %d", kind, (unsigned)sizeof(kind), &index, &value) == 3)
    {
        if (value < 0 || value > 255)
            continue;

        if (strcmp(kind, "decoration") == 0 && index >= 0 && index < DECORATION_COUNT)
        {
            values.decoration[index] = (uint8_t)value;
            mask.decoration[index] = true;
            ++loaded;
        }
        else if (strcmp(kind, "mesh") == 0 && index >= 0 && index < MESH_SLOT_COUNT)
        {
            values.mesh[index] = (uint8_t)value;
            mask.mesh[index] = true;
            ++loaded;
        }
    }

    fclose(f);

    GameSetDesired(ch, values, mask);
    g_savedValues[ch] = values;
    g_savedMask[ch] = mask;
    Log("%S profile loaded: %d values", CHARACTER_NAMES[ch], loaded);
}

void ProfileLoad()
{
    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        Load(ch);
}

static void SaveIfChanged(int ch)
{
    Appearance values;
    AppearanceMask mask;
    GameGetDesired(ch, &values, &mask);

    if (memcmp(&values, &g_savedValues[ch], sizeof(values)) == 0 &&
        memcmp(&mask, &g_savedMask[ch], sizeof(mask)) == 0)
        return;

    FILE* f = NULL;
    fopen_s(&f, g_path[ch], "w");

    if (!f)
    {
        Log("ERROR: could not save profile to %s", g_path[ch]);
        return;
    }

    int saved = 0;

    for (int slot = 0; slot < MESH_SLOT_COUNT; ++slot)
    {
        if (mask.mesh[slot])
        {
            fprintf(f, "mesh %d %d\n", slot, values.mesh[slot]);
            ++saved;
        }
    }

    for (int i = 0; i < DECORATION_COUNT; ++i)
    {
        if (mask.decoration[i])
        {
            fprintf(f, "decoration %d %d\n", i, values.decoration[i]);
            ++saved;
        }
    }

    fclose(f);

    g_savedValues[ch] = values;
    g_savedMask[ch] = mask;
    Log("%S profile saved: %d values", CHARACTER_NAMES[ch], saved);
}

void ProfileSaveIfChanged()
{
    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        SaveIfChanged(ch);
}
