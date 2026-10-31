#include "pch.h"
#include "profile.h"
#include "faceshape.h"
#include "game.h"
#include "log.h"
#include "menu_data.h"

#include <stdio.h>
#include <string.h>

static char g_path[CHARACTER_COUNT][MAX_PATH] = {};
static Appearance g_savedValues[CHARACTER_COUNT] = {};
static AppearanceMask g_savedMask[CHARACTER_COUNT] = {};
static int g_details[CHARACTER_COUNT] = { -1, -1, -1 };
static int g_savedDetails[CHARACTER_COUNT] = { -1, -1, -1 };

// Profiles written since the 2026-10-30 update say so ("layout 2 0"); older
// ones hold the option and colour numbers of the game before it and are
// converted as they load (and written anew at the next save).
static const int LAYOUT = 2;
static const MenuData* g_conversion = NULL;

void ProfileSetConversion(const MenuData* data)
{
    g_conversion = data;
}

int ProfileDetails(int ch)
{
    return ch >= 0 && ch < CHARACTER_COUNT ? g_details[ch] : -1;
}

void ProfileSetDetails(int ch, int option)
{
    if (ch >= 0 && ch < CHARACTER_COUNT)
        g_details[ch] = option >= 0 && option <= 255 ? option : -1;
}

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
    int index = 0, value = 0, loaded = 0, layout = 1;

    while (fscanf_s(f, "%31s %d %d", kind, (unsigned)sizeof(kind), &index, &value) == 3)
    {
        if (strcmp(kind, "layout") == 0)
        {
            layout = index;
            continue;
        }

        // Face Shape sliders: -100 .. 100.
        if (strcmp(kind, "face") == 0)
        {
            FaceSetLoaded(ch, index, value);
            continue;
        }

        if (strcmp(kind, "body") == 0)
        {
            BodySetLoaded(ch, index, value);
            continue;
        }

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
        else if (strcmp(kind, "details") == 0 && index == 5)
        {
            g_details[ch] = value;
            g_savedDetails[ch] = value;
        }
    }

    fclose(f);

    int converted = 0;

    if (layout < LAYOUT && g_conversion)
    {
        for (int slot = 0; slot < MESH_SLOT_COUNT; ++slot)
            if (mask.mesh[slot])
            {
                int now = g_conversion->Renumbered(ch, slot, values.mesh[slot]);
                converted += now != values.mesh[slot];
                values.mesh[slot] = (uint8_t)now;
            }

        for (int i = 0; i < DECORATION_COUNT; ++i)
            if (mask.decoration[i])
            {
                int now = g_conversion->Recoloured(i, values.decoration[i]);
                converted += now != values.decoration[i];
                values.decoration[i] = (uint8_t)now;
            }

        if (g_details[ch] >= 0)
        {
            g_details[ch] = g_conversion->Renumbered(ch, 5, g_details[ch]);
            g_savedDetails[ch] = -2;    // written anew
        }

        Log("%S profile from before the 2026-10-30 game update: %d numbers converted", CHARACTER_NAMES[ch], converted);
    }

    GameSetDesired(ch, values, mask);
    g_savedValues[ch] = values;
    g_savedMask[ch] = mask;

    // A converted profile is written anew (in the new layout) at the next save.
    if (layout < LAYOUT)
        g_savedMask[ch].mesh[0] = !g_savedMask[ch].mesh[0];

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
        memcmp(&mask, &g_savedMask[ch], sizeof(mask)) == 0 && g_details[ch] == g_savedDetails[ch] &&
        !FaceChangedSinceSave(ch) && !BodyChangedSinceSave(ch))
        return;

    FILE* f = NULL;
    fopen_s(&f, g_path[ch], "w");

    if (!f)
    {
        Log("ERROR: could not save profile to %s", g_path[ch]);
        return;
    }

    int saved = 0;
    fprintf(f, "layout %d 0\n", LAYOUT);

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

    if (g_details[ch] >= 0)
        fprintf(f, "details 5 %d\n", g_details[ch]);

    int face = 0;

    for (int i = 0; i < FaceSliderCount(); ++i)
    {
        if (FaceValue(ch, i))
        {
            fprintf(f, "face %d %d\n", i, FaceValue(ch, i));
            ++face;
        }
    }

    for (int i = 0; i < BodySliderCount(); ++i)
    {
        if (BodyValue(ch, i))
        {
            fprintf(f, "body %d %d\n", i, BodyValue(ch, i));
            ++face;
        }
    }

    fclose(f);

    g_savedValues[ch] = values;
    g_savedMask[ch] = mask;
    g_savedDetails[ch] = g_details[ch];
    FaceMarkSaved(ch);
    BodyMarkSaved(ch);
    Log("%S profile saved: %d values%s, %d face and body shape sliders", CHARACTER_NAMES[ch], saved,
        g_details[ch] >= 0 ? " and face details" : "", face);
}

void ProfileSaveIfChanged()
{
    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        SaveIfChanged(ch);
}
