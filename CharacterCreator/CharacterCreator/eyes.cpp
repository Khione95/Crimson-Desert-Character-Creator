#include "pch.h"
#include "eyes.h"
#include "addresses.h"
#include "identity.h"
#include "log.h"
#include "switches.h"

#include <stdio.h>
#include <string.h>

#include "MinHook.h"

// Iris textures shipped with the game. The player eye files use the
// cd_phm_00_eye_iris_* ones; the animal ones fit the same eye.
const EyeColour EYE_COLOURS[] =
{
    { L"Default",      NULL,                                                       110, 115, 120 },
    { L"Grey blue",    "character/texture/cd_phm_00_eye_iris_0001.dds",            100, 110, 120 },
    { L"Grey hazel",   "character/texture/cd_phm_00_eye_iris_0002.dds",             95,  90,  80 },
    { L"Silver",       "character/texture/cd_phm_00_eye_iris_0003.dds",            160, 160, 165 },
    { L"Dark brown",   "character/texture/cd_phm_00_eye_iris_0004.dds",             60,  45,  30 },
    { L"Amber",        "character/texture/cd_phm_00_eye_iris_0005.dds",            140, 100,  60 },
    { L"Hazel",        "character/texture/cd_phm_00_eye_iris_0009.dds",            125, 100,  80 },
    { L"Ringed brown", "character/texture/cd_phm_00_eye_iris_0013.dds",             90,  75,  55 },
    { L"Light brown",  "character/texture/cd_phm_00_eye_iris_0014.dds",            175, 130, 100 },
    { L"Blood red",    "character/texture/cd_phm_00_eye_iris_0011.dds",            120,  10,  15 },
    { L"Blue",         "character/texture/cd_animal_00_eye_iris_0014.dds",          70,  95, 130 },
    { L"Green",        "character/texture/cd_animal_00_eye_iris_0010.dds",          70,  85,  60 },
    { L"Olive",        "character/texture/cd_animal_00_eye_iris_0004.dds",         105, 105,  80 },
    { L"Moss",         "character/texture/cd_animal_00_eye_iris_0013.dds",          80,  85,  45 },
    { L"Gold",         "character/texture/cd_animal_00_eye_iris_0005.dds",         185, 135,  40 },
    { L"Red",          "character/texture/cd_animal_00_eye_iris_0006.dds",         190,  80,  60 },
    { L"Dark red",     "character/texture/cd_animal_00_eye_iris_0007.dds",         100,  35,  25 },
    { L"Black",        "character/texture/cd_animal_00_eye_iris_0002.dds",          12,  12,  12 },
    { L"Ice",          "character/texture/cd_m0002_00_husky_eye_iris_0001.dds",    140, 165, 170 },
    { L"Cat green",    "character/texture/cd_m0002_00_cat_eye_iris_0002_04.dds",   120, 130, 100 },
    { L"Cat blue",     "character/texture/cd_m0002_00_cat_eye_iris_0002_05.dds",    70,  90, 130 },
};

const int EYE_COLOUR_COUNT = sizeof(EYE_COLOURS) / sizeof(EYE_COLOURS[0]);

// The iris paths found in the player eye files (normal maps end in _n.dds).
static const char IRIS_PREFIX[] = "character/texture/cd_phm_00_eye_iris_";

static char g_path[CHARACTER_COUNT][MAX_PATH];
static volatile LONG g_chosen[CHARACTER_COUNT] = {};
static volatile LONG g_target = CHAR_KLIFF;     // whose colour the eye files get

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

void EyesSetTarget(int ch)
{
    if (ValidCharacter(ch))
        InterlockedExchange(&g_target, ch);
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

static bool IsIrisPath(const char* value)
{
    if (strncmp(value, IRIS_PREFIX, sizeof(IRIS_PREFIX) - 1) != 0)
        return false;

    const char* dot = strrchr(value, '.');
    return !(dot && dot - value >= 2 && dot[-2] == '_' && dot[-1] == 'n');
}

// Points every iris texture path of the tree at the chosen texture. The
// game reads attribute values up to their terminating zero, so a static
// string of any length can stand in.
static int SwapIris(uintptr_t root, const char* texture)
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
                const char* value = (const char*)Ptr(a, 0x08);

                if (name && value && strcmp(name, "_path") == 0 && IsIrisPath(value))
                {
                    *(const char**)(a + 0x08) = texture;
                    ++swapped;
                }
            }

            uintptr_t child = Ptr(e, 0x38);

            if (child && depth < MAX_DEPTH)
                stack[depth++] = child;
        }
    }

    return swapped;
}

static int TrySwap(void* rootOut, int colour)
{
    __try
    {
        uintptr_t root = rootOut ? *(uintptr_t*)rootOut : 0;
        return root ? SwapIris(root, EYE_COLOURS[colour].texture) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The loader's file argument, for the log: a path, or a pointer to one.
static void DescribeFile(void* file, char* out, size_t size)
{
    out[0] = 0;

    __try
    {
        const char* candidates[2] = { (const char*)file, file ? *(const char**)file : NULL };

        for (const char* s : candidates)
        {
            if (!s)
                continue;

            size_t n = 0;

            while (n < size - 1 && s[n] >= 32 && s[n] < 127)
                ++n;

            if (n >= 8 && s[n] == 0)
            {
                memcpy(out, s, n);
                out[n] = 0;
                return;
            }
        }

        const BYTE* b = (const BYTE*)file;
        sprintf_s(out, size, "? %02X %02X %02X %02X %02X %02X %02X %02X", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        strcpy_s(out, size, "?");
    }
}

// The mod's female player description (phw_description_player_001.xml)
// holds the two-handed sword in the left hand, as Damiane's animations hold
// it. A woman Kliff with his own animations holds it in the right, as his
// description does: while that file is loaded, the socket is set back.
static const char* const TWO_HAND_SWORD_PARTS[] = { "CD_TwoHandWeapon_Sword", "CD_TwoHandWeapon_Sword_IN" };

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
                    if (part && socket && strcmp(part, p) == 0 && strcmp(socket, "LHand_Socket") == 0)
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

static uintptr_t __fastcall HookedLoad(void* a, void* rootOut, void* file, uintptr_t flags, void* e)
{
    uintptr_t result = g_originalLoad(a, rootOut, file, flags, e);

    // As the game was started (the description is not read again).
    if (g_kliffFemaleWithOwnMoves)
    {
        int changed = TryRightHandTwoHandSword(rootOut);

        if (changed > 0)
            Log("two-handed sword: right hand for a woman Kliff with his own animations (%d sockets)", changed);
    }
    int colour = g_chosen[g_target];

    if (colour > 0)
    {
        int swapped = TrySwap(rootOut, colour);

        if (swapped > 0)
        {
            char name[160];
            DescribeFile(file, name, sizeof(name));
            Log("eyes: iris texture set to %s (%d places, %S's colour) in %s",
                EYE_COLOURS[colour].texture, swapped, CHARACTER_NAMES[g_target], name);
        }
        else if (swapped < 0)
        {
            Log("eyes: could not read a loaded file");
        }
    }

    return result;
}

void EyesInit(const char* folder)
{
    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        CharacterFile(g_path[ch], MAX_PATH, folder, "eyes", ch);
        Load(ch);
    }

    int gender, race;
    IdentityChosen(CHAR_KLIFF, &gender, &race);
    g_kliffFemaleWithOwnMoves = gender == GENDER_FEMALE && !IdentityStartFemaleMoves(CHAR_KLIFF);

    if (PartDisabled("eyes"))
        return;

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
