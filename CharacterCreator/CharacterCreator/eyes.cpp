#include "pch.h"
#include "eyes.h"
#include "addresses.h"
#include "identity.h"
#include "log.h"
#include "parttable.h"
#include "switches.h"

#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "MinHook.h"

// Iris textures shipped with the game. The player eye files use the
// cd_phm_00_eye_iris_* ones; the animal ones fit the same eye. The game has no
// green iris and barely a blue one: cc_iris_* are the package's own
// (tools/iris_colours.py).
const EyeColour EYE_COLOURS[] =
{
    { L"Default",      NULL,                                                       110, 115, 120 },
    { L"Grey blue",    "character/texture/phm_00_eye_iris_0001.dds",               100, 110, 120 },
    { L"Grey hazel",   "character/texture/phm_00_eye_iris_0002.dds",                95,  90,  80 },
    { L"Silver",       "character/texture/phm_00_eye_iris_0003.dds",               160, 160, 165 },
    { L"Dark brown",   "character/texture/phm_00_eye_iris_0004.dds",                60,  45,  30 },
    { L"Amber",        "character/texture/phm_00_eye_iris_0005.dds",               140, 100,  60 },
    { L"Hazel",        "character/texture/phm_00_eye_iris_0009.dds",               125, 100,  80 },
    { L"Ringed brown", "character/texture/phm_00_eye_iris_0013.dds",                90,  75,  55 },
    { L"Light brown",  "character/texture/phm_00_eye_iris_0014.dds",               175, 130, 100 },
    { L"Blood red",    "character/texture/phm_00_eye_iris_0011.dds",               120,  10,  15 },
    { L"Blue",         "character/texture/cc_iris_blue.dds",                        82, 148, 240 },
    { L"Green",        "character/texture/cc_iris_green.dds",                       86, 190,  92 },
    { L"Olive",        "character/texture/cd_animal_00_eye_iris_0004.dds",         105, 105,  80 },
    { L"Moss",         "character/texture/cd_animal_00_eye_iris_0013.dds",          80,  85,  45 },
    { L"Gold",         "character/texture/cd_animal_00_eye_iris_0005.dds",         185, 135,  40 },
    { L"Red",          "character/texture/animal_00_eye_iris_0006.dds",            190,  80,  60 },
    { L"Dark red",     "character/texture/cd_animal_00_eye_iris_0007.dds",         100,  35,  25 },
    { L"Black",        "character/texture/cd_animal_00_eye_iris_0002.dds",          12,  12,  12 },
    { L"Ice",          "character/texture/cd_m0002_00_husky_eye_iris_0001.dds",    140, 165, 170 },
    { L"Cat green",    "character/texture/cd_m0002_00_cat_eye_iris_0002_04.dds",   120, 130, 100 },
    { L"Cat blue",     "character/texture/cd_m0002_00_cat_eye_iris_0002_05.dds",    70,  90, 130 },
};

const int EYE_COLOUR_COUNT = sizeof(EYE_COLOURS) / sizeof(EYE_COLOURS[0]);

// The iris paths found in the player eye files (normal maps end in _n.dds).

static char g_path[CHARACTER_COUNT][MAX_PATH];
static volatile LONG g_chosen[CHARACTER_COUNT] = {};
static volatile LONG g_reads[CHARACTER_COUNT] = {};

LONG EyesReadCount(int ch)
{
    return ch >= 0 && ch < CHARACTER_COUNT ? g_reads[ch] : 0;
}

// ---------------------------------------------------------------------------
// Chosen colour
// ---------------------------------------------------------------------------

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
        fprintf(f, "%ld\n", g_chosen[ch]);
        fclose(f);
    }
}

static void Load(int ch)
{
    // The Barber edition has no editor: the characters' own eye colour.
    if (CC_BARBER)
        return;

    FILE* f = NULL;
    fopen_s(&f, g_path[ch], "r");

    if (!f)
        return;

    int colour = 0;

    if (fscanf_s(f, "%d", &colour) == 1 && colour >= 0 && colour < EYE_COLOUR_COUNT)
        g_chosen[ch] = colour;

    fclose(f);
}

void EyesChoose(int ch, int colour)
{
    if (!ValidCharacter(ch) || colour < 0 || colour >= EYE_COLOUR_COUNT)
        return;

    InterlockedExchange(&g_chosen[ch], colour);
    Save(ch);
    Log("%S eye colour chosen: %d", CHARACTER_NAMES[ch], colour);
}

int EyesChosen(int ch)
{
    return ValidCharacter(ch) ? g_chosen[ch] : 0;
}

// ---------------------------------------------------------------------------
// XML file loader: iris texture swapped in the loaded tree
// ---------------------------------------------------------------------------

// Loads an XML file into a tree (the same tree the base character parser
// reads, see identity.cpp):
//   element:   +0x00 name, +0x38 first child, +0x48 first attribute, +0x60 next sibling
//   attribute: +0x00 name, +0x08 value, +0x38 next attribute
// The second argument receives the root element.
static const BYTE LOAD_XML_PROLOGUE[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18 };

typedef uintptr_t(__fastcall* LoadXmlFn)(void* a, void* rootOut, void* file, uintptr_t flags, void* e);
static LoadXmlFn g_originalLoad = NULL;
static bool g_kliffFemaleWithOwnMoves = false;  // see RightHandTwoHandSword

static uintptr_t Ptr(uintptr_t node, size_t offset)
{
    return *(uintptr_t*)(node + offset);
}

// The player characters' own copies of heads, eye models and their iris
// textures (tools/private_eyes.py, names.py) are marked in their names, at
// the same length: zk_... / zd_... / zo_... in place of "cd_" (Kliff,
// Damiane, Oongka), and for a name without that prefix (since the 2026-10-30
// update) the character's letter for its first digit: phm_k0_eye_iris_...,
// PHM_d0_Eye_iris_... Only those are changed: NPC eyes read the game's own
// files.
static const char TEXTURE_FOLDER[] = "character/texture/";

static int OwnerOfLetter(char c)
{
    switch (c)
    {
    case 'k': return CHAR_KLIFF;
    case 'd': return CHAR_DAMIANE;
    case 'o': return CHAR_OONGKA;
    default: return -1;
    }
}

// Whose copy a part or texture name marks (-1: none); *at: where the mark is
// (0: "z?" for "cd", 4: the letter for the digit 0).
static int MarkOf(const char* n, int* at)
{
    if (n[0] == 'z' && n[1] && n[2] == '_' && OwnerOfLetter(n[1]) >= 0)
    {
        *at = 0;
        return OwnerOfLetter(n[1]);
    }

    if ((n[0] == 'p' || n[0] == 'P') && n[1] && n[2] && n[3] == '_' && n[4] && n[5] >= '0' && n[5] <= '9' &&
        OwnerOfLetter(n[4]) >= 0)
    {
        *at = 4;
        return OwnerOfLetter(n[4]);
    }

    return -1;
}

// The game's own name back, in place.
static void Unmark(char* n, int at)
{
    if (at == 0)
        memcpy(n, "cd", 2);
    else
        n[4] = '0';
}

static bool HasIris(const char* value)
{
    for (const char* p = value; *p; ++p)
        if (_strnicmp(p, "_eye_iris_", 10) == 0)
            return true;

    return false;
}

static int MarkedOwner(const char* value, int* at)
{
    if (strncmp(value, TEXTURE_FOLDER, sizeof(TEXTURE_FOLDER) - 1) != 0 || !HasIris(value))
        return -1;

    return MarkOf(value + sizeof(TEXTURE_FOLDER) - 1, at);
}

// The iris's normal map (_n) and, in the women's eye files, its material map
// (_sp) stay the game's: only the colour map takes the chosen colour.
static bool IsColourMap(const char* value)
{
    const char* dot = strrchr(value, '.');

    if (!dot)
        return false;

    bool normal = dot - value >= 2 && dot[-2] == '_' && dot[-1] == 'n';
    bool material = dot - value >= 3 && dot[-3] == '_' && dot[-2] == 's' && dot[-1] == 'p';
    return !normal && !material;
}

// Points every marked iris path of the tree at its character's colour, or
// back at the game's texture (the mark undone, Unmark). The game
// reads attribute values up to their terminating zero, so a static string of
// any length can stand in.
static int SwapIris(uintptr_t root, int counts[CHARACTER_COUNT])
{
    static const int MAX_DEPTH = 64;
    static const int MAX_NODES = 200000;

    uintptr_t stack[MAX_DEPTH];
    int depth = 0, nodes = 0, swapped = 0;
    stack[depth++] = root;

    while (depth > 0 && nodes < MAX_NODES)
    {
        uintptr_t e = stack[--depth];

        for (; e && nodes < MAX_NODES; e = Ptr(e, 0x60), ++nodes)
        {
            for (uintptr_t a = Ptr(e, 0x48); a; a = Ptr(a, 0x38))
            {
                const char* name = (const char*)Ptr(a, 0x00);
                char* value = (char*)Ptr(a, 0x08);
                int at = 0;
                int owner = name && value && strcmp(name, "_path") == 0 ? MarkedOwner(value, &at) : -1;

                if (owner < 0)
                    continue;

                int colour = g_chosen[owner];

                if (colour > 0 && IsColourMap(value))
                    *(const char**)(a + 0x08) = EYE_COLOURS[colour].texture;
                else
                    Unmark(value + sizeof(TEXTURE_FOLDER) - 1, at);

                ++counts[owner];
                ++swapped;
            }

            uintptr_t child = Ptr(e, 0x38);

            if (child && depth < MAX_DEPTH)
                stack[depth++] = child;
        }
    }

    return swapped;
}

// Only model property files (the eye files are ones) are looked through.
// When the game loads another mod's part table, it does not know the
// characters' own head copies: the head lists (meshparam_example_*.xml) are
// set back to the game's heads while they load (the copies' marks undone,
// Unmark). Eye colour is then off.
static bool g_ownHeadsOff = false;

static int GameHeads(uintptr_t root)
{
    // Every element's children wait on the stack while its siblings are
    // walked: a head list has hundreds of entries side by side (16 took only
    // the first few dozen).
    static const int MAX_DEPTH = 4096;
    static const int MAX_NODES = 50000;

    uintptr_t stack[MAX_DEPTH];
    int depth = 0, nodes = 0, changed = 0;
    stack[depth++] = root;

    while (depth > 0 && nodes < MAX_NODES)
    {
        uintptr_t e = stack[--depth];

        for (; e && nodes < MAX_NODES; e = Ptr(e, 0x60), ++nodes)
        {
            for (uintptr_t a = Ptr(e, 0x48); a; a = Ptr(a, 0x38))
            {
                const char* name = (const char*)Ptr(a, 0x00);
                char* value = (char*)Ptr(a, 0x08);

                int at = 0;

                if (name && value && strcmp(name, "MeshFileName") == 0 && MarkOf(value, &at) >= 0)
                {
                    Unmark(value, at);
                    ++changed;
                }
            }

            uintptr_t child = Ptr(e, 0x38);

            if (child && depth < MAX_DEPTH)
                stack[depth++] = child;
        }
    }

    return changed;
}

static int TryGameHeads(void* rootOut)
{
    __try
    {
        uintptr_t root = rootOut ? *(uintptr_t*)rootOut : 0;
        return root ? GameHeads(root) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool EyesOwnHeadsOff()
{
    return g_ownHeadsOff;
}

static int TrySwap(void* rootOut, int counts[CHARACTER_COUNT])
{
    __try
    {
        uintptr_t root = rootOut ? *(uintptr_t*)rootOut : 0;
        bool property = false;

        // The file's top elements (the root, its children and their
        // siblings): a model property file starts with SkinnedMeshProperty...
        for (uintptr_t top : { root, root ? Ptr(root, 0x38) : 0 })
            for (uintptr_t e = top; e && !property; e = Ptr(e, 0x60))
            {
                const char* name = (const char*)Ptr(e, 0x00);
                property = name && strncmp(name, "SkinnedMeshProperty", 19) == 0;
            }

        return property ? SwapIris(root, counts) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The mod's female player description (phw_description_player_001.xml)
// holds the two-handed sword in the left hand, as Damiane's animations hold
// it. A woman Kliff with his own animations holds it in the right, as his
// description does: while that file is loaded, the socket is set back.
static const char* const TWO_HAND_SWORD_PARTS[] = { "TwoHandWeapon_Sword", "TwoHandWeapon_Sword_IN" };

static const char* AttributeValue(uintptr_t element, const char* name)
{
    for (uintptr_t a = Ptr(element, 0x48); a; a = Ptr(a, 0x38))
    {
        const char* n = (const char*)Ptr(a, 0x00);

        if (n && strcmp(n, name) == 0)
            return (const char*)Ptr(a, 0x08);
    }

    return NULL;
}

static int RightHandTwoHandSword(uintptr_t root)
{
    static const int MAX_DEPTH = 16;
    static const int MAX_NODES = 20000;

    uintptr_t stack[MAX_DEPTH];
    int depth = 0, nodes = 0, changed = 0;
    stack[depth++] = root;

    while (depth > 0 && nodes < MAX_NODES)
    {
        uintptr_t e = stack[--depth];

        for (; e && nodes < MAX_NODES; e = Ptr(e, 0x60), ++nodes)
        {
            const char* name = (const char*)Ptr(e, 0x00);

            if (name && strcmp(name, "PartInOutSocket") == 0)
            {
                const char* part = AttributeValue(e, "PartName");
                char* socket = (char*)AttributeValue(e, "OutSocketBone");

                for (const char* p : TWO_HAND_SWORD_PARTS)
                {
                    // "CD_" before the 2026-10-30 update.
                    const char* bare = part && strncmp(part, "CD_", 3) == 0 ? part + 3 : part;

                    if (bare && socket && strcmp(bare, p) == 0 && strcmp(socket, "LHand_Socket") == 0)
                    {
                        socket[0] = 'R';    // "RHand_Socket", same length
                        ++changed;
                    }
                }
            }

            uintptr_t child = Ptr(e, 0x38);

            if (child && depth < MAX_DEPTH)
                stack[depth++] = child;
        }
    }

    return changed;
}

static int TryRightHandTwoHandSword(void* rootOut)
{
    __try
    {
        uintptr_t root = rootOut ? *(uintptr_t*)rootOut : 0;
        return root ? RightHandTwoHandSword(root) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

#if CC_RESEARCH
// Research: the game's bone customization list (boneparam_example_new.xml:
// ParamDesc Index, BoneName, Component S/R/T). Its loader (rva 807D70 on build
// 6AE387F2, slot 3 of CustomizationBoneParamData's vtable) loads the file and
// adds each entry to a table (bone hash -> parameter index for S, R and T)
// without clearing it. The loader is hooked at the start: after the game's own
// pass it runs again on a list made from bonemap.txt ("<index> <S|R|T> <bone
// name>", bone names have spaces). The game's XML reader parses in place (it
// writes into the text), so the file's text can't be read a second time; the
// loader only passes the file on to the XML loader, which reads its first
// field, the text.
static char g_boneMapPath[MAX_PATH] = { 0 };
static const char* const COMPONENTS[] = { "S", "R", "T" };
static const int BONE_ENTRIES = 256;

struct BoneEntry
{
    int index;
    const char* component;
    char bone[64];
};

static BoneEntry g_boneEntries[BONE_ENTRIES];
static int g_boneEntryCount = 0;
static char g_boneListText[BONE_ENTRIES * 128 + 64];

static const char* AttributeText(uintptr_t element, const char* name)
{
    for (uintptr_t a = Ptr(element, 0x48); a; a = Ptr(a, 0x38))
        if (Ptr(a, 0x00) && _stricmp((const char*)Ptr(a, 0x00), name) == 0)
            return Ptr(a, 0x08) ? (const char*)Ptr(a, 0x08) : "";

    return NULL;
}

static void LoadBoneMap()
{
    g_boneEntryCount = 0;
    FILE* f = NULL;
    char line[160];

    if (fopen_s(&f, g_boneMapPath, "r") != 0 || !f)
        return;

    while (fgets(line, sizeof(line), f) && g_boneEntryCount < BONE_ENTRIES)
    {
        int index = -1, used = 0;
        char component[4] = "";

        if (sscanf_s(line, "%d %3s %n", &index, component, (unsigned)sizeof(component), &used) < 2 || index < 0 || !line[used])
            continue;

        BoneEntry& b = g_boneEntries[g_boneEntryCount++];
        b.index = index;
        strncpy_s(b.bone, line + used, _TRUNCATE);
        b.bone[strcspn(b.bone, "\r\n")] = 0;
        b.component = component[0] == 'S' || component[0] == 's' ? COMPONENTS[0] :
                      component[0] == 'R' || component[0] == 'r' ? COMPONENTS[1] : COMPONENTS[2];
    }

    fclose(f);
}

static void BoneList(uintptr_t root)
{
    // The loaded root is the BoneParam element itself (the loader, rva
    // 807D70, checks its name and walks its children).
    uintptr_t list = Ptr(root, 0x00) && _stricmp((const char*)Ptr(root, 0x00), "BoneParam") == 0 ? root : 0;

    for (uintptr_t c = Ptr(root, 0x38); c && !list; c = Ptr(c, 0x60))
        if (Ptr(c, 0x00) && _stricmp((const char*)Ptr(c, 0x00), "BoneParam") == 0)
            list = c;

    // CC's own list (from bonemap.txt) is not logged again.
    if (!list || !Ptr(list, 0x38) || !AttributeText(Ptr(list, 0x38), "BoneName") || Ptr(root, 0x00) - (uintptr_t)g_boneListText < sizeof(g_boneListText))
        return;

    for (uintptr_t p = Ptr(list, 0x38); p; p = Ptr(p, 0x60))
    {
        const char* index = AttributeText(p, "Index");
        const char* bone = AttributeText(p, "BoneName");
        const char* comp = AttributeText(p, "Component");
        Log("bone list: %s %s %s", index ? index : "?", bone ? bone : "?", comp ? comp : "?");
    }
}

static void TryBoneList(void* rootOut)
{
    __try
    {
        uintptr_t root = rootOut ? *(uintptr_t*)rootOut : 0;

        if (root)
            BoneList(root);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static const uintptr_t BONE_LIST_LOAD_RVA = 0x807DA0;
static const BYTE BONE_LIST_LOAD_START[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                             0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x60, 0xFF, 0xFF, 0xFF };
typedef uintptr_t(__fastcall* BoneListLoadFn)(void* self, void* file, void* c, void* d);
static BoneListLoadFn g_originalBoneListLoad = NULL;

// The table's bone count (the table is at +0x28: buckets, then the count).
static uint32_t BoneTableCount(void* self)
{
    __try
    {
        return *(uint32_t*)((uintptr_t)self + 0x2C);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

static uintptr_t __fastcall HookedBoneListLoad(void* self, void* file, void* c, void* d)
{
    uintptr_t result = g_originalBoneListLoad(self, file, c, d);
    Log("bone list loaded: %u bones in the table (object %p)", BoneTableCount(self), self);

    LoadBoneMap();

    if (g_boneEntryCount)
    {
        size_t used = sprintf_s(g_boneListText, "<BoneParam>\n");

        for (int i = 0; i < g_boneEntryCount; ++i)
            used += sprintf_s(g_boneListText + used, sizeof(g_boneListText) - used, "<ParamDesc Index=\"%d\" BoneName=\"%s\" Component=\"%s\"/>\n",
                              g_boneEntries[i].index, g_boneEntries[i].bone, g_boneEntries[i].component);
        sprintf_s(g_boneListText + used, sizeof(g_boneListText) - used, "</BoneParam>\n");

        // The loader is given a file whose text is CC's list (its other
        // arguments are not used).
        void* ownFile[4] = { g_boneListText, 0, 0, 0 };
        uintptr_t added = g_originalBoneListLoad(self, ownFile, c, d);
        Log("bone list: %d entries of bonemap.txt %s - %u bones in the table", g_boneEntryCount, added & 0xFF ? "added" : "NOT added (the list was refused)",
            BoneTableCount(self));
    }

    return result;
}

static bool g_xmlHookedEarly = false;
#endif // CC_RESEARCH

static uintptr_t __fastcall HookedLoad(void* a, void* rootOut, void* file, uintptr_t flags, void* e)
{
    uintptr_t result = g_originalLoad(a, rootOut, file, flags, e);

#if CC_RESEARCH
    TryBoneList(rootOut);
#endif

    // As the game was started (the description is not read again).
    if (g_kliffFemaleWithOwnMoves)
    {
        int changed = TryRightHandTwoHandSword(rootOut);

        if (changed > 0)
            Log("two-handed sword: right hand for a woman Kliff with his own animations (%d sockets)", changed);
    }
    if (g_ownHeadsOff)
    {
        int heads = TryGameHeads(rootOut);

        if (heads > 0)
            Log("part table: a head list set back to the game's heads (%d)", heads);
    }

    int counts[CHARACTER_COUNT] = {};
    int swapped = TrySwap(rootOut, counts);

    for (int ch = 0; swapped > 0 && ch < CHARACTER_COUNT; ++ch)
        if (counts[ch])
        {
            InterlockedIncrement(&g_reads[ch]);
            int colour = g_chosen[ch];
            Log("eyes: %S's eyes read with %s (%d places)", CHARACTER_NAMES[ch],
                colour > 0 ? EYE_COLOURS[colour].texture : "the game's iris", counts[ch]);
        }

    if (swapped < 0)
        Log("eyes: could not read a loaded file");

    return result;
}

void EyesInit(const char* folder)
{

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        CharacterFile(g_path[ch], MAX_PATH, folder, "eyes", ch);
        Load(ch);
    }

    // <game>\bin64\CharacterCreator: the game's folder is two up.
    char game[MAX_PATH], sizePath[MAX_PATH];
    strcpy_s(game, folder);

    for (int up = 0; up < 2; ++up)
        if (char* slash = strrchr(game, '\\'))
            *slash = 0;

    // The package's table size, then Female Armor Fit's (the package's table
    // with the armor copies added: the heads are in it too).
    uint32_t expected[8];
    int sizes = 0;
    sprintf_s(sizePath, "%s\\parttable.txt", folder);
    FILE* f = NULL;

    if (fopen_s(&f, sizePath, "r") == 0 && f)
    {
        while (sizes < 8 && fscanf_s(f, "%u", &expected[sizes]) == 1)
            ++sizes;

        fclose(f);
    }

    g_ownHeadsOff = !PartDisabled("parttable") && !PartTableIsOurs(game, folder, expected, sizes);

    int gender, race;
    IdentityChosen(CHAR_KLIFF, &gender, &race);
    g_kliffFemaleWithOwnMoves = gender == GENDER_FEMALE && !IdentityStartFemaleMoves(CHAR_KLIFF);

    if (PartDisabled("eyes"))
        return;

#if CC_RESEARCH
    if (g_xmlHookedEarly)
    {
        Log("XML loader hooked at the start (research build; eye colours %ld %ld %ld)", g_chosen[0], g_chosen[1], g_chosen[2]);
        return;
    }
#endif

    BYTE* target = (BYTE*)AddressOf(ADDR_LOADXML);

    // On other game builds the address comes from checked patterns (addresses.h).
    if (!FunctionHookable(target, LOAD_XML_PROLOGUE, sizeof(LOAD_XML_PROLOGUE), "XML loader"))
    {
        Log("ERROR: XML loader not found - eye colour cannot change");
        return;
    }

    MH_STATUS init = MH_Initialize();

    if ((init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) ||
        MH_CreateHook(target, (void*)&HookedLoad, (void**)&g_originalLoad) != MH_OK ||
        MH_EnableHook(target) != MH_OK)
    {
        Log("ERROR: could not hook the XML loader");
        return;
    }

    Log("XML loader hooked (eye colours %ld %ld %ld)", g_chosen[0], g_chosen[1], g_chosen[2]);
}

#if CC_RESEARCH
// Research: the bone list loader and the XML loader hooked before the game
// loads its customization files (on build 6AE387F2 only: the known places,
// their first bytes checked), so the bone list's batches can be added.
void EyesResearchEarly(const char* folder)
{
    sprintf_s(g_boneMapPath, "%s\\bonemap.txt", folder);
    uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
    BYTE* list = (BYTE*)(base + BONE_LIST_LOAD_RVA);
    BYTE* xml = (BYTE*)(base + 0x160B0B0);
    MH_STATUS init = MH_Initialize();

    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        return;

    if (memcmp(xml, LOAD_XML_PROLOGUE, sizeof(LOAD_XML_PROLOGUE)) == 0 &&
        MH_CreateHook(xml, (void*)&HookedLoad, (void**)&g_originalLoad) == MH_OK && MH_EnableHook(xml) == MH_OK)
        g_xmlHookedEarly = true;

    bool listHooked = memcmp(list, BONE_LIST_LOAD_START, sizeof(BONE_LIST_LOAD_START)) == 0 &&
        MH_CreateHook(list, (void*)&HookedBoneListLoad, (void**)&g_originalBoneListLoad) == MH_OK && MH_EnableHook(list) == MH_OK;

    Log("research: XML loader %s, bone list loader %s at the start", g_xmlHookedEarly ? "hooked" : "NOT hooked",
        listHooked ? "hooked" : "NOT hooked");
}
#endif
