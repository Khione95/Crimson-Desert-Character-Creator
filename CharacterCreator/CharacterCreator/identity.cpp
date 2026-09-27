#include "pch.h"
#include "identity.h"
#include "addresses.h"
#include "chartable.h"
#include "game.h"
#include "log.h"
#include "switches.h"

#include <stdio.h>
#include <string.h>

#include <set>
#include <string>

#include "MinHook.h"

// ---------------------------------------------------------------------------
// Chosen and loaded identity, per character
// ---------------------------------------------------------------------------

// What each character is without the mod.
static const int NATIVE_GENDER[CHARACTER_COUNT] = { GENDER_MALE, GENDER_FEMALE, GENDER_MALE };
static const int NATIVE_RACE[CHARACTER_COUNT] = { RACE_HUMAN, RACE_HUMAN, RACE_ORC };

// The mesh list each character's base character file names.
static const char* const MESH_PARAM_FILES[CHARACTER_COUNT] = {
    "meshparam_example_kliff.xml", "meshparam_example_damian.xml", "meshparam_example_oongka.xml" };

static const MenuData* g_data = NULL;
static char g_path[CHARACTER_COUNT][MAX_PATH];
static volatile LONG g_gender[CHARACTER_COUNT], g_race[CHARACTER_COUNT];
static volatile LONG g_loadedGender[CHARACTER_COUNT] = { -1, -1, -1 };
static volatile LONG g_loadedRace[CHARACTER_COUNT] = { -1, -1, -1 };
static volatile LONG g_height[CHARACTER_COUNT];                     // percent
static volatile LONG g_loadedHeight[CHARACTER_COUNT] = { 0, 0, 0 };
static float g_baseScale[CHARACTER_COUNT];                          // before the height
static volatile LONG g_femaleMoves[CHARACTER_COUNT] = { 1, 1, 1 };  // female animations for a woman
static LONG g_startFemaleMoves[CHARACTER_COUNT] = { 1, 1, 1 };      // as the game started
static volatile LONG g_tableVersion = 1;   // bumped when a gender changes

static bool ValidCharacter(int ch)
{
    return ch >= 0 && ch < CHARACTER_COUNT;
}

static void Save(int ch)
{
    FILE* f = NULL;
    fopen_s(&f, g_path[ch], "w");

    if (f)
    {
        fprintf(f, "%ld %ld %ld %ld\n", g_gender[ch], g_race[ch], g_height[ch], g_femaleMoves[ch]);
        fclose(f);
    }
}

static void Load(int ch)
{
    g_gender[ch] = NATIVE_GENDER[ch];
    g_race[ch] = NATIVE_RACE[ch];
    g_height[ch] = 0;
    g_femaleMoves[ch] = 1;

    FILE* f = NULL;
    fopen_s(&f, g_path[ch], "r");

    if (!f)
        return;

    int gender = 0, race = 0, height = 0, moves = 1;
    int n = fscanf_s(f, "%d %d %d %d", &gender, &race, &height, &moves);

    if (n >= 2 && gender >= 0 && gender < 2 && race >= 0 && race < 4)
    {
        g_gender[ch] = gender;
        g_race[ch] = race;
    }

    // Older files hold only gender and race.
    if (n >= 3 && height >= HEIGHT_MIN && height <= HEIGHT_MAX)
        g_height[ch] = height;

    if (n == 4)
        g_femaleMoves[ch] = moves ? 1 : 0;

    fclose(f);
}

void IdentityChoose(int ch, int gender, int race)
{
    if (!ValidCharacter(ch) || gender < 0 || gender > 1 || race < 0 || race > 3)
        return;

    bool genderChanged = gender != g_gender[ch];
    InterlockedExchange(&g_gender[ch], gender);
    InterlockedExchange(&g_race[ch], race);
    Save(ch);

    if (genderChanged)
        InterlockedIncrement(&g_tableVersion);

    Log("%S identity chosen: gender %d race %d (applies after a restart)", CHARACTER_NAMES[ch], gender, race);
}

void IdentityChosen(int ch, int* gender, int* race)
{
    int c = ValidCharacter(ch) ? ch : CHAR_KLIFF;
    *gender = g_gender[c];
    *race = g_race[c];
}

void IdentityNative(int ch, int* gender, int* race)
{
    int c = ValidCharacter(ch) ? ch : CHAR_KLIFF;
    *gender = NATIVE_GENDER[c];
    *race = NATIVE_RACE[c];
}

void IdentityChooseFemaleMoves(int ch, bool femaleMoves)
{
    if (!ValidCharacter(ch) || (g_femaleMoves[ch] != 0) == femaleMoves)
        return;

    InterlockedExchange(&g_femaleMoves[ch], femaleMoves ? 1 : 0);
    Save(ch);
    InterlockedIncrement(&g_tableVersion);
    Log("%S female animations %s (applies after a restart)", CHARACTER_NAMES[ch], femaleMoves ? "on" : "off");
}

bool IdentityFemaleMoves(int ch)
{
    return !ValidCharacter(ch) || g_femaleMoves[ch] != 0;
}

bool IdentityStartFemaleMoves(int ch)
{
    return !ValidCharacter(ch) || g_startFemaleMoves[ch] != 0;
}

void IdentityChooseHeight(int ch, int height)
{
    if (!ValidCharacter(ch) || height < HEIGHT_MIN || height > HEIGHT_MAX || height == g_height[ch])
        return;

    InterlockedExchange(&g_height[ch], height);
    Save(ch);
    Log("%S height chosen: %+d%% (applies after a restart)", CHARACTER_NAMES[ch], height);
}

int IdentityHeight(int ch)
{
    return ValidCharacter(ch) ? g_height[ch] : 0;
}

int IdentityLoadedHeight(int ch)
{
    return ValidCharacter(ch) ? g_loadedHeight[ch] : 0;
}

float IdentityBaseScale(int ch)
{
    return ValidCharacter(ch) ? g_baseScale[ch] : 0;
}

bool IdentityLoaded(int ch, int* gender, int* race)
{
    if (!ValidCharacter(ch) || g_loadedGender[ch] < 0)
        return false;

    *gender = g_loadedGender[ch];
    *race = g_loadedRace[ch];
    return true;
}

// ---------------------------------------------------------------------------
// Base character file: swapped while the game parses it
// ---------------------------------------------------------------------------

// The parser receives the file as a parsed XML tree:
//   element:   +0x00 name, +0x38 first child, +0x48 first attribute, +0x60 next sibling
//   attribute: +0x00 name, +0x08 value, +0x38 next attribute
static const BYTE PARSE_PROLOGUE[] = { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x4C, 0x24, 0x08 };

typedef void* (__fastcall* ParseAppearanceFn)(void* table, void* root, void* extra, void* unused);
static ParseAppearanceFn g_originalParse = NULL;

static uintptr_t Ptr(uintptr_t node, size_t offset)
{
    return *(uintptr_t*)(node + offset);
}

static const char* Str(uintptr_t node, size_t offset)
{
    const char* s = (const char*)Ptr(node, offset);
    return s ? s : "";
}

static uintptr_t Child(uintptr_t element, const char* name)
{
    for (uintptr_t e = Ptr(element, 0x38); e; e = Ptr(e, 0x60))
        if (_stricmp(Str(e, 0x00), name) == 0)
            return e;

    return 0;
}

static uintptr_t Attribute(uintptr_t element, const char* name)
{
    for (uintptr_t a = element ? Ptr(element, 0x48) : 0; a; a = Ptr(a, 0x38))
        if (_stricmp(Str(a, 0x00), name) == 0)
            return a;

    return 0;
}

// Longer values are kept here; the attribute is pointed at them. (The game
// reads values up to their terminating zero, see eyes.cpp.)
static const int STORE_FIELDS = 6;
static char g_store[CHARACTER_COUNT][STORE_FIELDS][160];

// Sets the attribute's text: in place if it fits, else by pointing the
// attribute at the plugin's own copy.
static bool SetValue(uintptr_t attribute, const std::string& value, int ch, int field, const char* what)
{
    if (!attribute)
        return false;

    char* text = (char*)Ptr(attribute, 0x08);

    if (!text)
        return false;

    if (value.size() <= strlen(text))
    {
        memcpy(text, value.c_str(), value.size() + 1);
        return true;
    }

    char* copy = g_store[ch][field];

    if (value.size() >= sizeof(g_store[ch][field]))
    {
        Log("  %s: \"%s\" is too long - kept", what, value.c_str());
        return false;
    }

    memcpy(copy, value.c_str(), value.size() + 1);
    *(char**)(attribute + 0x08) = copy;
    return true;
}

// Unlinks the beard prefabs from the Hair element (element +0x38 first child,
// +0x60 next sibling).
static void RemoveBeards(uintptr_t hair)
{
    if (!hair)
        return;

    uintptr_t* link = (uintptr_t*)(hair + 0x38);

    while (*link)
    {
        uintptr_t e = *link;
        uintptr_t name = Attribute(e, "Name");

        if (name && strstr(Str(name, 0x08), "_beard_"))
        {
            Log("  removed beard %s", Str(name, 0x08));
            *link = Ptr(e, 0x60);
        }
        else
        {
            link = (uintptr_t*)(e + 0x60);
        }
    }
}

// Whose base character file this is, or -1.
static int CharacterOfFile(uintptr_t root)
{
    uintptr_t customization = Child(root, "Customization");
    uintptr_t meshParam = Attribute(customization, "MeshParamFile");

    if (!meshParam)
        return -1;

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        if (_stricmp(Str(meshParam, 0x08), MESH_PARAM_FILES[ch]) == 0)
            return ch;

    return -1;
}

// Multiplies the body scale (CharacterScale) by the chosen height.
static void ApplyHeight(uintptr_t root, int ch)
{
    int height = g_height[ch];
    InterlockedExchange(&g_loadedHeight[ch], height);

    uintptr_t scale = Attribute(Child(Child(root, "Nude"), "Prefab"), "CharacterScale");
    double base = scale ? atof(Str(scale, 0x08)) : 0;

    if (base <= 0)
    {
        Log("base character: %S has no body scale - height kept", CHARACTER_NAMES[ch]);
        return;
    }

    // The live height (height.h) scales from this.
    g_baseScale[ch] = (float)base;

    if (!height)
        return;

    char value[32];
    sprintf_s(value, "%.6f", base * (100 + height) / 100.0);

    if (SetValue(scale, value, ch, 2, "scale"))
        Log("base character: %S height %+d%% (scale %.6f -> %s)", CHARACTER_NAMES[ch], height, base, value);
}

static void SwapBaseCharacter(uintptr_t root, int ch)
{
    int gender = g_gender[ch], race = g_race[ch];

    // As themselves, characters keep their own file (face, hair, scale).
    if (gender == NATIVE_GENDER[ch] && race == NATIVE_RACE[ch])
    {
        InterlockedExchange(&g_loadedGender[ch], gender);
        InterlockedExchange(&g_loadedRace[ch], race);
        Log("base character: %S built as themselves", CHARACTER_NAMES[ch]);
        ApplyHeight(root, ch);
        return;
    }

    const BaseCharacter& b = g_data->bases[gender][race];

    if (!b.known)
    {
        Log("base character: no data for gender %d race %d - %S kept as they are", gender, race, CHARACTER_NAMES[ch]);
        return;
    }

    SetValue(Attribute(Child(root, "Customization"), "CustomizationFile"), b.customization, ch, 0, "customization");

    uintptr_t nude = Child(Child(root, "Nude"), "Prefab");
    SetValue(Attribute(nude, "Name"), b.body, ch, 1, "body");
    SetValue(Attribute(nude, "CharacterScale"), b.scale, ch, 2, "scale");

    uintptr_t head = Child(Child(root, "Head"), "Prefab");
    SetValue(Attribute(head, "Name"), b.head, ch, 3, "head");
    SetValue(Attribute(head, "HeadScale"), b.headScale, ch, 4, "head scale");

    SetValue(Attribute(Child(Child(root, "Hair"), "Prefab"), "Name"), b.hair, ch, 5, "hair");

    // Kliff's and Oongka's files also list their beard under Hair; a woman
    // gets none (the Beard tab adds one if chosen).
    if (gender == GENDER_FEMALE)
        RemoveBeards(Child(root, "Hair"));

    InterlockedExchange(&g_loadedGender[ch], gender);
    InterlockedExchange(&g_loadedRace[ch], race);
    Log("base character: %S built as gender %d race %d (%s, %s)", CHARACTER_NAMES[ch], gender, race,
        b.body.c_str(), b.head.c_str());
    ApplyHeight(root, ch);
}

// Off in releases (every NPC's file is parsed).
static const bool LOG_PARSED_FILES = false;

// Research: what each parsed appearance file names (customization, mesh list,
// body and the hair element's prefabs), to find the files previews use.
static void LogParsed(uintptr_t root)
{
    static volatile LONG logged = 0;

    if (InterlockedIncrement(&logged) > 200)
        return;

    // Missing elements and attributes read as "".
    auto child = [](uintptr_t element, const char* name) { return element ? Child(element, name) : 0; };
    auto text = [](uintptr_t element, const char* name) { uintptr_t a = element ? Attribute(element, name) : 0; return a ? Str(a, 0x08) : ""; };

    uintptr_t customization = child(root, "Customization");
    uintptr_t hairElement = child(root, "Hair");
    char hair[256] = "";

    for (uintptr_t e = hairElement ? Ptr(hairElement, 0x38) : 0; e; e = Ptr(e, 0x60))
    {
        const char* name = text(e, "Name");

        if (name[0] && strlen(hair) + strlen(name) + 2 < sizeof(hair))
        {
            strcat_s(hair, " ");
            strcat_s(hair, name);
        }
    }

    Log("parsed appearance: customization %s, mesh list %s, body %s, hair:%s", text(customization, "CustomizationFile"),
        text(customization, "MeshParamFile"), text(child(child(root, "Nude"), "Prefab"), "Name"), hair);
}

// The heads the package has private copies of (tools/private_eyes.py): the
// head slot shows the character's own copy (zk_..., zd_..., zo_...), whose eye
// files only they read. Also the heads that have a prefab.
static std::set<std::string> g_privateHeads;

static void LoadPrivateHeads(const char* folder)
{
    char path[MAX_PATH];
    sprintf_s(path, "%s\\private_heads.txt", folder);
    FILE* f = NULL;

    if (fopen_s(&f, path, "r") != 0 || !f)
        return;

    char line[128];

    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;

        if (line[0])
            g_privateHeads.insert(line);
    }

    fclose(f);
    Log("private heads: %zu", g_privateHeads.size());
}

// The head chosen in the editor, or "" if none was chosen.
static std::string ChosenHead(int ch)
{
    Appearance desired;
    AppearanceMask mask;
    GameGetDesired(ch, &desired, &mask);

    if (!g_data || !mask.mesh[MESH_HEAD] || desired.mesh[MESH_HEAD] == MESH_NONE)
        return std::string();

    for (const MeshOption& m : g_data->meshes)
        if (m.slot == MESH_HEAD && m.index[ch] == desired.mesh[MESH_HEAD])
            return m.mesh;

    return std::string();
}

// The character's file names the head chosen in the editor, the one the head
// slot shows. With another head named here, the skin (scars, tattoos, paint,
// dirt) was laid out for that one and came out in the wrong places on the
// chosen head. A named head is there even when the plugin cannot set the head
// slot (a character without appearance values, a shop preview); an empty one
// left them without a face. It is the game's own head, not the character's
// private copy: only the game's own heads come with their face shape here,
// and they are in any part table (another mod's included). The slot then
// shows the private copy (eye colour). Its head scale was meant for the
// file's own head: 1 for any other (as the Face Fix mod had it).
static void NameChosenHead(uintptr_t root, int ch)
{
    // Only heads with a prefab (those the package has copies of): a few in
    // the game's lists have only a model, and named here they left no face.
    std::string head = ChosenHead(ch);

    if (head.empty() || !g_privateHeads.count(head))
        return;

    uintptr_t prefab = Child(Child(root, "Head"), "Prefab");
    uintptr_t name = Attribute(prefab, "Name");
    const char* text = name ? (const char*)Ptr(name, 0x08) : NULL;

    if (!text)
        return;

    bool own = head == text;

    if (head != text && SetValue(name, head, ch, 3, "head"))
        Log("base character: %S's file names their chosen head %s", CHARACTER_NAMES[ch], head.c_str());

    if (!own)
        SetValue(Attribute(prefab, "HeadScale"), "1", ch, 4, "head scale");
}

static void TrySwap(void* rootHolder)
{
    __try
    {
        uintptr_t root = rootHolder ? *(uintptr_t*)rootHolder : 0;

        int ch = root ? CharacterOfFile(root) : -1;

        if (ch >= 0)
        {
            SwapBaseCharacter(root, ch);
            NameChosenHead(root, ch);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("base character: could not read the file being parsed");
    }
}

static void TryLogParsed(void* rootHolder)
{
    __try
    {
        uintptr_t root = rootHolder ? *(uintptr_t*)rootHolder : 0;

        if (root)
            LogParsed(root);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void* __fastcall HookedParse(void* table, void* root, void* extra, void* unused)
{
    if (LOG_PARSED_FILES)
        TryLogParsed(root);
    TrySwap(root);
    return g_originalParse(table, root, extra, unused);
}

static void HookParser()
{
    BYTE* target = (BYTE*)AddressOf(ADDR_PARSEAPPEARANCE);

    // On other game builds the address comes from checked patterns (addresses.h).
    if (!FunctionHookable(target, PARSE_PROLOGUE, sizeof(PARSE_PROLOGUE), "base character parser"))
    {
        Log("ERROR: base character parser not found - race and gender cannot change");
        return;
    }

    MH_STATUS init = MH_Initialize();

    if ((init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) ||
        MH_CreateHook(target, (void*)&HookedParse, (void**)&g_originalParse) != MH_OK ||
        MH_EnableHook(target) != MH_OK)
    {
        Log("ERROR: could not hook the base character parser");
        return;
    }

    Log("base character parser hooked");
}

// ---------------------------------------------------------------------------
// Character table: gender fields
// ---------------------------------------------------------------------------

enum FieldMask
{
    F_APPEARANCE = 1, F_PREFAB = 2, F_SKELETON = 4, F_LOOKUP24 = 8, F_LOOKUP25 = 16, F_F36 = 32,
    F_ACTION = 64, F_WEIGHT = 128,
};

// The entries and fields Female Animations.field.json changes for Kliff, and
// the same for Damiane and Oongka. Each gender's values come from an entry
// that has them natively (NULL = the entry's own).
struct GenderEntry
{
    const char* name;
    int character;
    int mask;
    const char* maleFrom;
    const char* femaleFrom;
};

static const int KLIFF_FIELDS = F_APPEARANCE | F_PREFAB | F_SKELETON | F_ACTION | F_F36 | F_LOOKUP24 | F_LOOKUP25;
static const int CLONE_FIELDS = F_APPEARANCE | F_PREFAB | F_WEIGHT | F_F36 | F_LOOKUP24 | F_LOOKUP25;

static const GenderEntry GENDER_ENTRIES[] =
{
    { "Kliff",        CHAR_KLIFF,   KLIFF_FIELDS, NULL, "Damian" },
    { "Kliff_Clone",  CHAR_KLIFF,   CLONE_FIELDS, NULL, "Damian" },
    { "Kliff_AI",     CHAR_KLIFF,   F_APPEARANCE | F_PREFAB | F_ACTION | F_F36 | F_LOOKUP24 | F_LOOKUP25, NULL, "Damian" },
    { "Yann",         CHAR_KLIFF,   F_APPEARANCE | F_PREFAB | F_LOOKUP24, NULL, "Damian" },
    { "PlayerAll",    CHAR_KLIFF,   F_ACTION | F_F36 | F_LOOKUP25, NULL, "Damian" },
    { "Damian",       CHAR_DAMIANE, KLIFF_FIELDS, "Kliff", NULL },
    { "Damian_Clone", CHAR_DAMIANE, CLONE_FIELDS, "Kliff_Clone", NULL },
    { "Oongka",       CHAR_OONGKA,  KLIFF_FIELDS, NULL, "Damian" },
};

static const int ENTRY_COUNT = sizeof(GENDER_ENTRIES) / sizeof(GENDER_ENTRIES[0]);

// Field offsets in an entry (see chartable.h). "Weight" is where the clones
// keep the value the file calls character_weight.
struct FieldInfo { int flag; size_t offset; size_t size; };
static const FieldInfo FIELDS[] =
{
    { F_APPEARANCE, 0x8C, 2 }, { F_PREFAB, 0x8E, 2 }, { F_SKELETON, 0x90, 2 }, { F_LOOKUP24, 0x96, 2 },
    { F_LOOKUP25, 0x98, 2 }, { F_F36, 0xBA, 2 }, { F_ACTION, 0x1CC, 4 }, { F_WEIGHT, 0x1E0, 4 },
};

static const int FIELD_COUNT = sizeof(FIELDS) / sizeof(FIELDS[0]);
static const int ACTION_FIELD = 6, SKELETON_FIELD = 2;

struct EntryValues
{
    int index;
    uint32_t own[FIELD_COUNT];      // the game's values, before the mod wrote any
};

static EntryValues g_entries[ENTRY_COUNT];
static bool g_tableReady = false;
static LONG g_appliedVersion = 0;

static uint32_t ReadField(int index, const FieldInfo& f)
{
    uint32_t v = 0;
    CharTableReadRaw(index, f.offset, &v, f.size);
    return v;
}

static const EntryValues* FindEntry(const char* name)
{
    for (int e = 0; e < ENTRY_COUNT; ++e)
        if (strcmp(GENDER_ENTRIES[e].name, name) == 0)
            return g_entries[e].index >= 0 ? &g_entries[e] : NULL;

    return NULL;
}

// Remembers every entry's own values. The package no longer contains Female
// Animations.field.json, so the table starts out as the game has it.
static bool PrepareTable()
{
    for (int e = 0; e < ENTRY_COUNT; ++e)
    {
        g_entries[e].index = CharTableIndexOf(GENDER_ENTRIES[e].name);

        for (int f = 0; f < FIELD_COUNT; ++f)
            g_entries[e].own[f] = g_entries[e].index >= 0 ? ReadField(g_entries[e].index, FIELDS[f]) : 0;
    }

    const EntryValues* kliff = FindEntry("Kliff");
    const EntryValues* damian = FindEntry("Damian");

    if (!kliff || !damian)
    {
        Log("gender: Kliff or Damian not found in the character table");
        return false;
    }

    if (kliff->own[SKELETON_FIELD] == damian->own[SKELETON_FIELD])
    {
        // Still has the female skeleton: Female Animations.field.json is mounted.
        Log("gender: Kliff already has female values - remove Female Animations.field.json from the mod");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Testing which fields make what: genderfields.txt in the data folder lists
// the fields and/or table entries that get the other gender's values, e.g.
//   skeleton appearance prefab lookup24 lookup25 f36 weight   (all but action)
//   kliff kliff_clone                                          (only these entries)
// Fields or entries not listed keep their own values. Without the file (or
// without field / entry names in it) everything is copied, as always.
// ---------------------------------------------------------------------------

static const char* const FIELD_NAMES[] = { "appearance", "prefab", "skeleton", "lookup24", "lookup25", "f36", "action", "weight" };
static char g_filterPath[MAX_PATH];
static bool g_filterFields = false, g_filterEntries = false;
static bool g_fieldAllowed[8], g_entryAllowed[16];
static bool g_pairExcluded[16][8];      // "-entry.field": that field of that entry keeps its value

static void LoadGenderFilter()
{
    for (bool& b : g_fieldAllowed) b = true;
    for (bool& b : g_entryAllowed) b = true;
    memset(g_pairExcluded, 0, sizeof(g_pairExcluded));

    FILE* f = NULL;
    fopen_s(&f, g_filterPath, "r");

    if (!f)
        return;

    bool fields[8] = {}, entries[16] = {};
    char word[64];

    char excluded[256] = "";

    while (fscanf_s(f, "%63s", word, (unsigned)sizeof(word)) == 1)
    {
        const char* dot = word[0] == '-' ? strchr(word, '.') : NULL;

        if (dot)
        {
            for (int e = 0; e < ENTRY_COUNT; ++e)
                for (int i = 0; i < 8; ++i)
                    if (_strnicmp(word + 1, GENDER_ENTRIES[e].name, dot - word - 1) == 0 &&
                        strlen(GENDER_ENTRIES[e].name) == (size_t)(dot - word - 1) && _stricmp(dot + 1, FIELD_NAMES[i]) == 0)
                    {
                        g_pairExcluded[e][i] = true;
                        strcat_s(excluded, " ");
                        strcat_s(excluded, word + 1);
                    }

            continue;
        }

        for (int i = 0; i < 8; ++i)
            if (_stricmp(word, FIELD_NAMES[i]) == 0)
                fields[i] = g_filterFields = true;

        for (int e = 0; e < ENTRY_COUNT; ++e)
            if (_stricmp(word, GENDER_ENTRIES[e].name) == 0)
                entries[e] = g_filterEntries = true;
    }

    fclose(f);

    char text[256] = "";

    if (g_filterFields)
    {
        strcat_s(text, "fields:");

        for (int i = 0; i < 8; ++i)
        {
            g_fieldAllowed[i] = fields[i];

            if (fields[i])
            {
                strcat_s(text, " ");
                strcat_s(text, FIELD_NAMES[i]);
            }
        }
    }

    if (g_filterEntries)
    {
        strcat_s(text, g_filterFields ? "  entries:" : "entries:");

        for (int e = 0; e < ENTRY_COUNT; ++e)
        {
            g_entryAllowed[e] = entries[e];

            if (entries[e])
            {
                strcat_s(text, " ");
                strcat_s(text, GENDER_ENTRIES[e].name);
            }
        }
    }

    if (text[0])
        Log("gender: genderfields.txt - only %s get the other gender's values", text);

    if (excluded[0])
        Log("gender: genderfields.txt - kept as they are:%s", excluded);
}

static void ApplyGenders()
{
    for (int e = 0; e < ENTRY_COUNT; ++e)
    {
        const GenderEntry& g = GENDER_ENTRIES[e];
        int index = g_entries[e].index;

        if (index < 0)
            continue;

        bool female = g_gender[g.character] == GENDER_FEMALE;
        const char* fromName = female ? g.femaleFrom : g.maleFrom;
        const EntryValues* from = fromName ? FindEntry(fromName) : &g_entries[e];

        // A woman with male animations: the animation fields (action, and the
        // clones' weight, which holds an action) come from the male source.
        const char* movesName = female && !g_femaleMoves[g.character] ? g.maleFrom : fromName;
        const EntryValues* movesFrom = movesName ? FindEntry(movesName) : &g_entries[e];

        if (!from || !movesFrom)
            continue;

        for (int f = 0; f < FIELD_COUNT; ++f)
        {
            if (!(g.mask & FIELDS[f].flag))
                continue;

            bool moves = FIELDS[f].flag == F_ACTION || FIELDS[f].flag == F_WEIGHT;
            const char* sourceName = moves ? movesName : fromName;
            const EntryValues* source = moves ? movesFrom : from;

            // A clone's weight field holds an action value; taken from an entry
            // that is not a clone, it is that entry's action.
            bool fromClone = sourceName == NULL || strstr(sourceName, "_Clone") != NULL;
            uint32_t v = FIELDS[f].flag == F_WEIGHT && !fromClone ? source->own[ACTION_FIELD] : source->own[f];

            // Left out by genderfields.txt: the entry's own value.
            if (sourceName && (!g_fieldAllowed[f] || !g_entryAllowed[e] || g_pairExcluded[e][f]))
                v = g_entries[e].own[f];

            CharTableWriteRaw(index, FIELDS[f].offset, &v, FIELDS[f].size);
        }
    }

    Log("gender: character table set (Kliff %s, Damiane %s, Oongka %s)",
        g_gender[CHAR_KLIFF] ? "female" : "male", g_gender[CHAR_DAMIANE] ? "female" : "male",
        g_gender[CHAR_OONGKA] ? "female" : "male");
}

void IdentityPoll()
{
    static uintptr_t instance = 0;

    if (!CharTableAvailable())
    {
        g_tableReady = false;
        return;
    }

    if (CharTableInstance() != instance)
    {
        // A new table (first load, or rebuilt by the game): start over.
        instance = CharTableInstance();
        g_tableReady = false;
        g_appliedVersion = 0;
    }

    if (!g_tableReady)
    {
        g_tableReady = true;

        if (!PrepareTable())
        {
            g_appliedVersion = LONG_MAX;    // leave the table as the game has it
            return;
        }

        Log("gender: character table ready");
    }

    LONG version = g_tableVersion;

    if (g_appliedVersion == version || g_appliedVersion == LONG_MAX)
        return;

    ApplyGenders();
    g_appliedVersion = version;
}

// ---------------------------------------------------------------------------

void IdentityInit(const char* folder, const MenuData* data)
{
    g_data = data;
    LoadPrivateHeads(folder);

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        CharacterFile(g_path[ch], MAX_PATH, folder, "identity", ch);
        Load(ch);
        g_startFemaleMoves[ch] = g_femaleMoves[ch];
        Log("%S identity: gender %ld race %ld height %+ld%%%s", CHARACTER_NAMES[ch], g_gender[ch], g_race[ch], g_height[ch],
            g_gender[ch] == GENDER_FEMALE && !g_femaleMoves[ch] ? " (male animations)" : "");
    }

    sprintf_s(g_filterPath, "%s\\genderfields.txt", folder);
    LoadGenderFilter();

    if (!PartDisabled("parser"))
        HookParser();
}
