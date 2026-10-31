#include "pch.h"
#include "barbertabs.h"
#include "eyes.h"
#include "faceshape.h"
#include "game.h"
#include "identity.h"
#include "log.h"
#include "menu.h"

#if CC_BARBER

#include <stdint.h>
#include <string.h>
#include <windows.h>

#include "MinHook.h"

// ---------------------------------------------------------------------------
// The game's barber builds its parts in code (rva 1137A80, build 6AE58024), one
// record each on UIGamePlayControlRootBarberShop's list at +0x388:
//
//   part (0x38 bytes): +0x00 u16 title text, +0x08 icon name, +0x10 sub-tabs
//                      (pointer, u32 count, u32 room), +0x20 groups (the same
//                      record: the top tab row, each with its own sub-tabs; only
//                      read when the part has no sub-tabs), +0x30 camera
//   sub-tab (0x20):    +0x00 u16 title text, +0x02 the mesh list's slot (head 1,
//                      hair 2, ...; 7 a type by its setting), +0x08 its settings
//                      (pairs: setting key, label), +0x18 kind (0 type, 1 colour,
//                      2 sliders)
//
// A part or group shows the first sub-tab of each kind, so more than one type,
// colour or slider list needs more groups. The records go on the game's list
// with the game's own helpers. Words for the new tabs: titles from FIRST_TITLE
// on are ours (HookedTitle); slider labels are free text.
//
// Face and Body Shape and Eye Colour are settings of their own, rows 184+ added
// to decorationparam_*.xml (tools/barber_lists.py) that the game keeps in the
// save but does nothing with; BarberTabsPoll moves them onto faceshape.cpp's
// sliders and eyes.cpp. A sub-tab of sliders next to a mesh list is greyed by
// the game unless its list names them (rva 113FBF0), so skin has its own group.
// The top row takes five tabs at most: a sixth, far to the right, could not be clicked.
// ---------------------------------------------------------------------------

static const DWORD KNOWN_TIMESTAMP = 0x6AE58024, KNOWN_IMAGE_SIZE = 0x13EE0000;
static const uintptr_t SET_VALUE = 0x803AE0, BUILD_PARTS = 0x1137A80, GROW_RECORDS = 0x1149540, SET_MESH = 0x804870, TITLE_OBJECT = 0x3DE3F0, RECORD_CTOR = 0x113F880,
                       SUB_CTOR = 0x112EB50, SUB_PUSH = 0x1148130, SUB_DTOR = 0x449810, PAIR_PUSH = 0xDF5810, INTERN = 0x149DE70,
                       NAME_HASH = 0x1545560, MAKE_STRING = 0x3E5530, ASSIGN_STRING = 0x444D80, FREE_STRING = 0x3E56E0;
static const unsigned char SET_MESH_START[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x57 };
static const unsigned char BUILD_START[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
static const unsigned char SET_VALUE_START[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x44, 0x88, 0x4C, 0x24, 0x20 };
static const unsigned char TITLE_START[] = { 0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08 };
static const unsigned char RECORD_CTOR_START[] = { 0x48, 0x89, 0x4C, 0x24 };
static const unsigned char SUB_CTOR_START[] = { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x66, 0xC7, 0x01, 0xFF, 0xFF };

static const size_t PARTS = 0x388, RECORD_SIZE = 0x38, RECORD_ICON = 0x08, RECORD_SUBS = 0x10, RECORD_GROUPS = 0x20, RECORD_CAMERA = 0x30, SUB_SIZE = 0x20;
static const size_t SUB_SLOT = 0x02, SUB_SETTINGS = 0x08, SUB_KIND = 0x18;
static const uint8_t SLOT_BODY = 0, SLOT_HEAD = 1, SLOT_HAIR = 2, SLOT_DETAILS = 5, SLOT_BY_SETTING = 7;
static const uint8_t KIND_TYPE = 0, KIND_COLOUR = 1, KIND_SLIDERS = 2;

typedef uintptr_t(__fastcall* BuildPartsFn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef void(__fastcall* GrowFn)(void*, uint32_t);
typedef void*(__fastcall* CtorFn)(void*);
typedef void(__fastcall* PushFn)(void*, void*);
typedef void*(__fastcall* InternFn)(uint32_t*, const char*, int, int);
typedef uint32_t(__fastcall* NameHashFn)(const char*, uint32_t);
typedef void*(__fastcall* MakeStringFn)(void*, const char*);
typedef void(__fastcall* AssignStringFn)(void*, void*);
typedef void*(__fastcall* TitleFn)(void*, uint32_t);
typedef void(__fastcall* SetMeshFn)(uintptr_t, uint32_t, uint32_t);

static BuildPartsFn g_buildParts = NULL;
static GrowFn g_grow = NULL;
static CtorFn g_recordCtor = NULL, g_subCtor = NULL, g_subDtor = NULL, g_freeString = NULL;
static PushFn g_subPush = NULL, g_pairPush = NULL;
static InternFn g_intern = NULL;
static NameHashFn g_nameHash = NULL;
static MakeStringFn g_makeString = NULL;
static AssignStringFn g_assignString = NULL;
static TitleFn g_title = NULL;
static SetMeshFn g_setMesh = NULL;

struct RecordList
{
    uint8_t* items;
    uint32_t count;
    uint32_t room;
};

struct Pair
{
    uint32_t key;
    uint32_t label;
};

struct Slider
{
    const char* key;
    const char* label;
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

// ---------------------------------------------------------------------------
// Words of the new tabs: the object the game asks a title for (rva 3DE3F0) has
// at +0x10 an object whose second virtual function gives the text.
// ---------------------------------------------------------------------------

enum Title
{
    TITLE_HEAD, TITLE_DETAILS, TITLE_FACE_SHAPE, TITLE_SCARS, TITLE_PAINT, TITLE_BODY, TITLE_BODY_SHAPE, TITLE_EYES, TITLE_LASHES, TITLE_TIPS, TITLE_INNER,
    TITLE_BROW_MAKEUP, TITLE_GLITTER, TITLE_SKIN, TITLE_IRIS, TITLE_HAIR_SHINE, TITLE_FACE_SCARS, TITLE_FACE_PAINT, TITLE_BLANK, TITLE_IDENTITY, TITLE_GENDER, TITLE_RACE, TITLE_COUNT
};

static const uint32_t FIRST_TITLE = 0xFE00;
static const char* const TITLE_WORDS[TITLE_COUNT] = { "Head", "Face Details", "Face Shape", "Body Scars", "Body Paint", "Body", "Body Shape", "Eyes", "Eyelashes",
    "Hair Tips", "Inner Hair", "Brow Makeup", "Glitter", "Skin", "Eye Details", "Hair Details", "Face Scars", "Face Paint", " ", "Gender and Race", "Gender", "Race" };

struct TitleText
{
    void* const* vtable;
    const char* text;
};

struct TitleObject
{
    uint8_t unused[0x10];
    TitleText* text;
};

static const char* __fastcall TitleWords(TitleText* self, int)
{
    return self->text;
}

static void* const TITLE_VTABLE[4] = { (void*)&TitleWords, (void*)&TitleWords, (void*)&TitleWords, (void*)&TitleWords };
static TitleText g_titleTexts[TITLE_COUNT];
static TitleObject g_titleObjects[TITLE_COUNT];

static void* __fastcall HookedTitle(void* a, uint32_t id)
{
    if (id >= FIRST_TITLE && id < FIRST_TITLE + TITLE_COUNT)
        return &g_titleObjects[id - FIRST_TITLE];

    return g_title(a, id);
}

static uint16_t Word(Title title)
{
    return (uint16_t)(FIRST_TITLE + title);
}

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

static uint32_t Key(const char* name)
{
    uint32_t key = 0;
    g_intern(&key, name, 1, 0x2FFFF);
    return key;
}

static RecordList* Groups(uint8_t* record) { return (RecordList*)(record + RECORD_GROUPS); }
static RecordList* Subs(uint8_t* record) { return (RecordList*)(record + RECORD_SUBS); }

static uint8_t* NewRecord(RecordList* list, uint16_t title)
{
    if (list->count >= list->room)
        g_grow(list, list->count + 4);

    uint8_t* record = list->items + list->count * RECORD_SIZE;
    g_recordCtor(record);
    ++list->count;
    *(uint16_t*)record = title;
    return record;
}

static void AddSub(uint8_t* record, uint16_t title, uint8_t slot, uint8_t kind, const Slider* settings, size_t count)
{
    uint8_t sub[SUB_SIZE];
    g_subCtor(sub);
    *(uint16_t*)sub = title;
    sub[SUB_SLOT] = slot;
    sub[SUB_KIND] = kind;

    for (size_t i = 0; i < count; ++i)
    {
        Pair pair = { Key(settings[i].key), settings[i].label ? Key(settings[i].label) : 0 };
        g_pairPush(sub + SUB_SETTINGS, &pair);
    }

    g_subPush(Subs(record), sub);
    g_subDtor(sub);
}

// The words the game uses for its own sub-tabs, in the player's language.
static uint16_t g_wordType = 0xFFFF, g_wordColour = 0xFFFF, g_wordSliders = 0xFFFF;

static void AddMeshList(uint8_t* record, uint8_t slot)
{
    AddSub(record, g_wordType, slot, KIND_TYPE, NULL, 0);
}

static void AddTypeList(uint8_t* record, const char* key)
{
    Slider setting = { key, NULL };
    AddSub(record, g_wordType, SLOT_BY_SETTING, KIND_TYPE, &setting, 1);
}

static void AddColours(uint8_t* record, const char* key)
{
    Slider setting = { key, NULL };
    AddSub(record, g_wordColour, SLOT_BY_SETTING, KIND_COLOUR, &setting, 1);
}

static void AddSliders(uint8_t* record, const Slider* sliders, size_t count)
{
    AddSub(record, g_wordSliders, SLOT_BY_SETTING, KIND_SLIDERS, sliders, count);
}

// A choice in a list is only taken when the part or group has both a type and
// a colour sub-tab (rva 1133790). A group of colours alone gets a type sub-tab
// without words, that the game greys out as it does any of slot 7 without settings.
static void AddBlankType(uint8_t* record)
{
    AddSub(record, Word(TITLE_BLANK), SLOT_BY_SETTING, KIND_TYPE, NULL, 0);
}

static uint8_t* NewPart(RecordList* parts, uint16_t title, const char* icon, const char* camera)
{
    uint8_t* part = NewRecord(parts, title);
    uint64_t name[2] = {};
    void* made = g_makeString(name, icon);
    g_assignString(part + RECORD_ICON, made);
    g_freeString(name);
    *(uint32_t*)(part + RECORD_CAMERA) = g_nameHash(camera, (uint32_t)strlen(camera));
    return part;
}

// The sub-tab holding a setting, in a part or its groups; `owner` the record it is on.
static uint8_t* FindSub(uint8_t* record, uint32_t key, uint8_t** owner)
{
    RecordList* subs = Subs(record);

    for (uint32_t i = 0; i < subs->count; ++i)
    {
        uint8_t* sub = subs->items + i * SUB_SIZE;
        RecordList* settings = (RecordList*)(sub + SUB_SETTINGS);

        for (uint32_t s = 0; s < settings->count; ++s)
            if (((Pair*)settings->items)[s].key == key)
            {
                if (owner)
                    *owner = record;
                return sub;
            }
    }

    RecordList* groups = Groups(record);

    for (uint32_t i = 0; i < groups->count; ++i)
    {
        uint8_t* found = FindSub(groups->items + i * RECORD_SIZE, key, owner);

        if (found)
            return found;
    }

    return NULL;
}

static int FindPart(RecordList* parts, const char* key)
{
    uint32_t wanted = Key(key);

    for (uint32_t i = 0; i < parts->count; ++i)
        if (FindSub(parts->items + i * RECORD_SIZE, wanted, NULL))
            return (int)i;

    return -1;
}

// More sliders on the list of the game's that holds `anchor`, each once.
static void ExtendSliders(RecordList* parts, const char* anchor, const Slider* sliders, size_t count)
{
    int part = FindPart(parts, anchor);

    if (part < 0)
    {
        Log("barber tabs: no list holds %s", anchor);
        return;
    }

    uint8_t* sub = FindSub(parts->items + part * RECORD_SIZE, Key(anchor), NULL);

    for (size_t i = 0; i < count; ++i)
    {
        uint32_t key = Key(sliders[i].key);
        RecordList* settings = (RecordList*)(sub + SUB_SETTINGS);
        bool have = false;

        for (uint32_t s = 0; s < settings->count; ++s)
            have |= ((Pair*)settings->items)[s].key == key;

        if (!have)
        {
            Pair pair = { key, Key(sliders[i].label) };
            g_pairPush(sub + SUB_SETTINGS, &pair);
        }
    }
}

// A part with sub-tabs of its own, turned into one with groups: its sub-tabs
// become the first group, named as the part.
static void GroupsFromSubs(uint8_t* part)
{
    if (!Subs(part)->count)
        return;

    uint8_t* group = NewRecord(Groups(part), *(uint16_t*)part);
    memcpy(group + RECORD_SUBS, part + RECORD_SUBS, sizeof(RecordList));
    memset(part + RECORD_SUBS, 0, sizeof(RecordList));
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

static const Slider SKIN[] = { { "skinGlossiness", "Shine" }, { "skinBrightness", "Brightness" }, { "skinSubsurface", "Subsurface" } };

// Rows 184+: ccFace<n> and ccBody<n>, n the slider of faceshape.cpp.
static const Slider FACE_SHAPE[] = {
    { "ccFace0", "Face width" }, { "ccFace1", "Jaw width" }, { "ccFace2", "Jaw height" }, { "ccFace3", "Cheekbone width" }, { "ccFace4", "Cheekbone height" },
    { "ccFace5", "Cheek fullness" }, { "ccFace6", "Chin length" }, { "ccFace7", "Chin width" }, { "ccFace8", "Chin forward" }, { "ccFace9", "Nose length" },
    { "ccFace10", "Nose height" }, { "ccFace11", "Nose width" }, { "ccFace12", "Nose bridge" }, { "ccFace13", "Mouth width" }, { "ccFace14", "Mouth height" },
    { "ccFace15", "Upper lip" }, { "ccFace16", "Lower lip" }, { "ccFace17", "Eye spacing" }, { "ccFace18", "Eye height" }, { "ccFace19", "Eye size" },
    { "ccFace20", "Eye depth" }, { "ccFace21", "Brow height" }, { "ccFace22", "Brow depth" }, { "ccFace23", "Ears out" } };
static const Slider BODY_SHAPE[] = {
    { "ccHeight", "Height" }, { "ccBody0", "Chest / bust size" }, { "ccBody1", "Bust shape (women)" }, { "ccBody2", "Bust sides (women)" }, { "ccBody3", "Upper chest" }, { "ccBody4", "Shoulders" },
    { "ccBody5", "Upper arms" }, { "ccBody13", "Biceps" }, { "ccBody14", "Triceps" }, { "ccBody15", "Forearms" }, { "ccBody6", "Neck" }, { "ccBody7", "Waist" },
    { "ccBody8", "Belly" }, { "ccBody9", "Hips" }, { "ccBody10", "Glutes" }, { "ccBody11", "Thighs" }, { "ccBody16", "Quads" }, { "ccBody17", "Outer thighs" },
    { "ccBody18", "Inner thighs" }, { "ccBody12", "Calves" } };

static const Slider FACE_SCAR[] = { { "faceScarOpacity", "Opacity" }, { "faceScarLocationX", "Left / right" }, { "faceScarLocationY", "Up / down" }, { "faceScarRotation", "Rotation" },
    { "faceScarSizeX", "Width" }, { "faceScarSizeY", "Height" } };
static const Slider FACE_DIRT[] = { { "faceDirtyOpacity", "Opacity" }, { "faceDirtyLocationX", "Left / right" }, { "faceDirtyLocationY", "Up / down" }, { "faceDirtyRotation", "Rotation" },
    { "faceDirtySizeX", "Width" }, { "faceDirtySizeY", "Height" } };
static const Slider BODY_SCAR[] = { { "bodyScarOpacity", "Opacity" }, { "bodyScarLocationX", "Left / right" }, { "bodyScarLocationY", "Up / down" }, { "bodyScarRotation", "Rotation" },
    { "bodyScarSizeX", "Width" }, { "bodyScarSizeY", "Height" } };
static const Slider BODY_DIRT[] = { { "bodyDirtyOpacity", "Opacity" }, { "bodyDirtyLocationX", "Left / right" }, { "bodyDirtyLocationY", "Up / down" }, { "bodyDirtyRotation", "Rotation" },
    { "bodyDirtySizeX", "Width" }, { "bodyDirtySizeY", "Height" } };

static const Slider EYES[] = { { "irisBrightness", "Brightness" }, { "irisOpacity", "Tint strength" }, { "irisSize", "Size" }, { "pupilArea", "Pupil size" }, { "irisType", "Iris pattern" },
    { "eyelashesOpacity", "Eyelash opacity" } };

static const Slider HAIR_MORE[] = { { "hairBaseGlossiness", "Shine" }, { "hairBrightness", "Brightness" }, { "hairTailOpacity", "Tips strength" }, { "hairTailArea", "Tips length" },
    { "hairInnerOpacity", "Inner strength" }, { "hairInnerArea", "Inner length" } };
static const Slider BROW_MAKEUP[] = { { "eyebrowMakeupOpacity", "Opacity" }, { "eyebrowMakeupTranslationY", "Up / down" }, { "eyebrowMakeupGlossiness", "Shine" },
    { "eyebrowMakeupArea", "Size" } };
static const Slider FACE_TATTOO_MORE[] = { { "faceTattooGlossiness", "Shine" }, { "faceTattooLocationX", "Left / right" }, { "faceTattooLocationY", "Up / down" },
    { "faceTattooRotation", "Rotation" }, { "faceTattooSizeX", "Width" }, { "faceTattooSizeY", "Height" } };
static const Slider BODY_TATTOO_MORE[] = { { "BodyTattooGlossiness", "Shine" }, { "BodyTattooLocationX", "Left / right" }, { "BodyTattooLocationY", "Up / down" },
    { "BodyTattooRotation", "Rotation" }, { "BodyTattooSizeX", "Width" }, { "BodyTattooSizeY", "Height" } };
static const Slider SHADOW_DOWN_MORE[] = { { "faceEyeShadowDownGlossiness", "Shine" }, { "faceEyeShadowDownMetallic", "Shimmer" } };
static const Slider GLITTER[] = { { "faceEyelineGlitterOpacity", "Strength" }, { "faceEyelineGlitterGlossiness", "Shine" }, { "faceEyelineGlitterMetallic", "Shimmer" } };

static const char* const ICON_HEAD = "ItemIcon_ItemGroup_makeup_cc_Head";
static const char* const ICON_BODY = "ItemIcon_ItemGroup_makeup_cc_Body";
static const char* const ICON_EYES = "ItemIcon_ItemGroup_makeup_cc_Eyes";
static const char* const ICON_IDENTITY = "ItemIcon_ItemGroup_makeup_cc_Identity";
static const char* const CAMERA_FACE = "dyeingcamera_barber_facial";
static const char* const CAMERA_BODY = "dyeingcamera_barber_bodytattoo";
static const char* const CAMERA_WHOLE = "dyeingcamera_barber";

static void AddHead(RecordList* parts)
{
    uint8_t* part = NewPart(parts, Word(TITLE_HEAD), ICON_HEAD, CAMERA_FACE);
    RecordList* groups = Groups(part);

    uint8_t* group = NewRecord(groups, Word(TITLE_HEAD));
    AddMeshList(group, SLOT_HEAD);

    group = NewRecord(groups, Word(TITLE_DETAILS));
    AddMeshList(group, SLOT_DETAILS);

    group = NewRecord(groups, Word(TITLE_FACE_SHAPE));
    AddSliders(group, FACE_SHAPE, COUNT_OF(FACE_SHAPE));

    group = NewRecord(groups, Word(TITLE_FACE_SCARS));
    AddTypeList(group, "faceScarType");
    AddColours(group, "faceScarColor");
    AddSliders(group, FACE_SCAR, COUNT_OF(FACE_SCAR));

    group = NewRecord(groups, Word(TITLE_FACE_PAINT));
    AddTypeList(group, "faceDirtyType");
    AddColours(group, "faceDirtyColor");
    AddSliders(group, FACE_DIRT, COUNT_OF(FACE_DIRT));
}

static void AddBody(RecordList* parts)
{
    uint8_t* part = NewPart(parts, Word(TITLE_BODY), ICON_BODY, CAMERA_BODY);
    RecordList* groups = Groups(part);

    uint8_t* group = NewRecord(groups, Word(TITLE_BODY));
    AddMeshList(group, SLOT_BODY);
    AddColours(group, "skinColor");

    group = NewRecord(groups, Word(TITLE_SKIN));
    AddSliders(group, SKIN, COUNT_OF(SKIN));

    group = NewRecord(groups, Word(TITLE_BODY_SHAPE));
    AddSliders(group, BODY_SHAPE, COUNT_OF(BODY_SHAPE));

    group = NewRecord(groups, Word(TITLE_SCARS));
    AddTypeList(group, "bodyScarType");
    AddColours(group, "bodyScarColor");
    AddSliders(group, BODY_SCAR, COUNT_OF(BODY_SCAR));

    group = NewRecord(groups, Word(TITLE_PAINT));
    AddTypeList(group, "bodyDirtyType");
    AddColours(group, "bodyDirtyColor");
    AddSliders(group, BODY_DIRT, COUNT_OF(BODY_DIRT));
}

static void AddEyes(RecordList* parts)
{
    uint8_t* part = NewPart(parts, Word(TITLE_EYES), ICON_EYES, CAMERA_FACE);
    RecordList* groups = Groups(part);

    // The game's iris colour setting does not reach a player's eyes: the
    // colour is a row of ours, with pictures, that eyes.cpp acts on.
    uint8_t* group = NewRecord(groups, Word(TITLE_EYES));
    AddTypeList(group, "ccEyeColour");
    Slider none = { "ccNone", NULL };
    AddSub(group, Word(TITLE_BLANK), SLOT_BY_SETTING, KIND_COLOUR, &none, 1);

    group = NewRecord(groups, Word(TITLE_IRIS));
    AddSliders(group, EYES, COUNT_OF(EYES));

    // Its own: the game greys a colour sub-tab while its type list is on the first
    // option (rva 1140F68), which for the eyes is their own colour.
    group = NewRecord(groups, Word(TITLE_LASHES));
    AddColours(group, "eyelashesColor");
    AddBlankType(group);
}

// A type list with its colour sub-tab (a list's choice needs one): without
// words, on a setting without colours, so that it shows nothing.
static void AddOwnTypeList(uint8_t* group, const char* key)
{
    AddTypeList(group, key);
    Slider none = { "ccNone", NULL };
    AddSub(group, Word(TITLE_BLANK), SLOT_BY_SETTING, KIND_COLOUR, &none, 1);
}

// Taken at the next start (BarberTabsPoll): the skeleton is chosen while the
// character is built.
static void AddIdentity(RecordList* parts)
{
    uint8_t* part = NewPart(parts, Word(TITLE_IDENTITY), ICON_IDENTITY, CAMERA_WHOLE);
    RecordList* groups = Groups(part);

    uint8_t* group = NewRecord(groups, Word(TITLE_GENDER));
    AddOwnTypeList(group, "ccGender");

    group = NewRecord(groups, Word(TITLE_RACE));
    AddOwnTypeList(group, "ccRace");
}

static void ExtendGameParts(RecordList* parts, int hair)
{
    uint8_t* part = parts->items + hair * RECORD_SIZE;
    GroupsFromSubs(part);

    // Sliders beside a mesh list need that list to name them (DecorationParamIndex,
    // tools/barber_lists.py): any other goes in a group without one.
    uint8_t* group = NewRecord(Groups(part), Word(TITLE_HAIR_SHINE));
    AddSliders(group, HAIR_MORE, COUNT_OF(HAIR_MORE));

    group = NewRecord(Groups(part), Word(TITLE_TIPS));
    AddColours(group, "hairTailColor");
    AddBlankType(group);

    group = NewRecord(Groups(part), Word(TITLE_INNER));
    AddColours(group, "hairInnerColor");
    AddBlankType(group);

    int brow = FindPart(parts, "eyeblowsColor");

    if (brow >= 0)
    {
        part = parts->items + brow * RECORD_SIZE;
        GroupsFromSubs(part);
        group = NewRecord(Groups(part), Word(TITLE_BROW_MAKEUP));
        AddTypeList(group, "eyebrowMakeupType");
        AddColours(group, "eyebrowMakeupColor");
        AddSliders(group, BROW_MAKEUP, COUNT_OF(BROW_MAKEUP));
    }

    ExtendSliders(parts, "faceTattooOpacity", FACE_TATTOO_MORE, COUNT_OF(FACE_TATTOO_MORE));
    ExtendSliders(parts, "BodyTattooOpacity", BODY_TATTOO_MORE, COUNT_OF(BODY_TATTOO_MORE));
    ExtendSliders(parts, "faceEyeShadowDownOpacity", SHADOW_DOWN_MORE, COUNT_OF(SHADOW_DOWN_MORE));

    int shadow = FindPart(parts, "faceEyeShadowMainColor");

    if (shadow >= 0)
    {
        group = NewRecord(Groups(parts->items + shadow * RECORD_SIZE), Word(TITLE_GLITTER));
        AddTypeList(group, "faceEyelineGlitterType");
        AddColours(group, "faceEyelineGlitterColor");
        AddSliders(group, GLITTER, COUNT_OF(GLITTER));
    }
}

// Gender and Race, Head, Body, then the game's parts, with Eyes after the
// eyebrows. A record's bytes can move: they hold no pointers to themselves.
static void Arrange(RecordList* parts, uint32_t gameParts, int brow)
{
    uint32_t count = parts->count;
    uint8_t* copy = (uint8_t*)malloc((size_t)count * RECORD_SIZE);

    if (!copy)
        return;

    memcpy(copy, parts->items, (size_t)count * RECORD_SIZE);
    uint32_t at = 0;
    uint32_t head = gameParts, body = gameParts + 1, eyes = gameParts + 2, identity = gameParts + 3;
    memcpy(parts->items + at++ * RECORD_SIZE, copy + identity * RECORD_SIZE, RECORD_SIZE);
    memcpy(parts->items + at++ * RECORD_SIZE, copy + head * RECORD_SIZE, RECORD_SIZE);
    memcpy(parts->items + at++ * RECORD_SIZE, copy + body * RECORD_SIZE, RECORD_SIZE);

    for (uint32_t i = 0; i < gameParts; ++i)
    {
        memcpy(parts->items + at++ * RECORD_SIZE, copy + i * RECORD_SIZE, RECORD_SIZE);

        if ((int)i == brow)
            memcpy(parts->items + at++ * RECORD_SIZE, copy + eyes * RECORD_SIZE, RECORD_SIZE);
    }

    if (brow < 0)
        memcpy(parts->items + at++ * RECORD_SIZE, copy + eyes * RECORD_SIZE, RECORD_SIZE);

    free(copy);
}

static void LogParts(RecordList* parts)
{
    for (uint32_t i = 0; i < parts->count; ++i)
    {
        uint8_t* part = parts->items + i * RECORD_SIZE;
        Log("barber tabs:   part %u: title %u, camera %08X, %u sub-tabs, %u groups", i, *(uint16_t*)part, *(uint32_t*)(part + RECORD_CAMERA), Subs(part)->count,
            Groups(part)->count);
    }
}

// ---------------------------------------------------------------------------
// The lists the barber shows: a slot of the controller's mesh table (+0x128)
// has at +0x48 slots of 0xC0 bytes, each with at +0x80 the shown options (their
// numbers, count at +0x88). The barber's choices are places in that list.
// ---------------------------------------------------------------------------

static const size_t TABLE = 0x128, TABLE_SLOTS = 0x48, TABLE_SLOT_COUNT = 0x50, SLOT_SIZE = 0xC0, SLOT_SHOWN = 0x80, SLOT_SHOWN_COUNT = 0x88;
static const uintptr_t CONTROLLER_OF = 0x1134510;
typedef uintptr_t(__fastcall* ControllerOfFn)(uintptr_t);
static ControllerOfFn g_controllerOf = NULL;

static uint8_t* MeshSlot(uintptr_t controller, int slot)
{
    uintptr_t table = *(uintptr_t*)(controller + TABLE);

    if (!table || (uint32_t)slot >= *(uint32_t*)(table + TABLE_SLOT_COUNT))
        return NULL;

    return *(uint8_t**)(table + TABLE_SLOTS) + (size_t)slot * SLOT_SIZE;
}

static int ShownToOption(uintptr_t controller, int slot, int shown)
{
    __try
    {
        uint8_t* s = MeshSlot(controller, slot);
        uint32_t count = s ? *(uint32_t*)(s + SLOT_SHOWN_COUNT) : 0;
        return count && (uint32_t)shown < count ? (*(uint8_t**)(s + SLOT_SHOWN))[shown] : shown;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return shown;
    }
}

static int OptionToShown(uintptr_t controller, int slot, int option)
{
    __try
    {
        uint8_t* s = MeshSlot(controller, slot);
        uint32_t count = s ? *(uint32_t*)(s + SLOT_SHOWN_COUNT) : 0;

        if (!count)
            return option;

        for (uint32_t i = 0; i < count; ++i)
            if ((*(uint8_t**)(s + SLOT_SHOWN))[i] == option)
                return (int)i;

        return -1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The package's lists show what fits the character as they are. Built as another
// gender or race, these lists replace them; they stay until the game ends.
static void ShowWhatFits(uintptr_t controller)
{
    static const int SLOTS[] = { MESH_BODY, MESH_HEAD, MESH_BEARD, MESH_WHISKERS };
    static uint8_t s_lists[CHARACTER_COUNT][4][255];
    static int s_counts[CHARACTER_COUNT][4];
    static bool s_made[CHARACTER_COUNT];

    int ch = GameBarberCharacter(controller);
    int gender, race, ownGender, ownRace;

    if (ch < 0 || !IdentityLoaded(ch, &gender, &race))
        return;

    IdentityNative(ch, &ownGender, &ownRace);

    if (gender == ownGender && race == ownRace)
        return;

    if (!s_made[ch])
    {
        s_made[ch] = true;

        for (int i = 0; i < 4; ++i)
            s_counts[ch][i] = MenuFittingOptions(ch, SLOTS[i], s_lists[ch][i], 255);

        Log("barber tabs: %S built as gender %d race %d: %d bodies, %d heads, %d beards, %d face details", CHARACTER_NAMES[ch], gender, race, s_counts[ch][0],
            s_counts[ch][1], s_counts[ch][2], s_counts[ch][3]);
    }

    __try
    {
        for (int i = 0; i < 4; ++i)
        {
            uint8_t* s = MeshSlot(controller, SLOTS[i]);

            if (s && *(uint8_t**)(s + SLOT_SHOWN) != s_lists[ch][i])
            {
                *(uint32_t*)(s + SLOT_SHOWN_COUNT) = (uint32_t)s_counts[ch][i];
                *(uint8_t**)(s + SLOT_SHOWN) = s_lists[ch][i];
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("barber tabs: fault while setting %S's lists", CHARACTER_NAMES[ch]);
    }
}

static uintptr_t __fastcall HookedBuildParts(uintptr_t self, uintptr_t b, uintptr_t c, uintptr_t d)
{
    uintptr_t result = g_buildParts(self, b, c, d);

    __try
    {
        uintptr_t controller = g_controllerOf(self);

        if (controller)
            ShowWhatFits(controller);

        RecordList* parts = (RecordList*)(self + PARTS);
        uint32_t gameParts = parts->count;
        Log("barber tabs: the game built %u parts", gameParts);
        int hair = -1;

        for (uint32_t i = 0; i < parts->count && hair < 0; ++i)
        {
            RecordList* subs = Subs(parts->items + i * RECORD_SIZE);

            if (subs->count && subs->items[SUB_SLOT] == SLOT_HAIR)
                hair = (int)i;
        }

        // The makeup-only screen has no hair and stays as it is.
        if (hair < 0 || Subs(parts->items + hair * RECORD_SIZE)->count < 3)
            return result;

        RecordList* subs = Subs(parts->items + hair * RECORD_SIZE);
        g_wordType = *(uint16_t*)(subs->items);
        g_wordColour = *(uint16_t*)(subs->items + SUB_SIZE);
        g_wordSliders = *(uint16_t*)(subs->items + 2 * SUB_SIZE);

        int brow = FindPart(parts, "eyeblowsColor");
        ExtendGameParts(parts, hair);
        AddHead(parts);
        AddBody(parts);
        AddEyes(parts);
        AddIdentity(parts);
        Arrange(parts, gameParts, brow);
        LogParts(parts);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("barber tabs: fault while adding the parts");
    }

    return result;
}

// ---------------------------------------------------------------------------
// Choosing a head or body
// ---------------------------------------------------------------------------

static void __fastcall HookedSetMesh(uintptr_t controller, uint32_t slot, uint32_t option)
{
    uint8_t s = (uint8_t)slot;

    // A blank type sub-tab: the game's own of slot 7 always have a setting, and
    // never come here.
    if (s == SLOT_BY_SETTING)
        return;

    int ch = GameBarberCharacter(controller);
    // What comes here is a place in the shown list, not the option's number.
    int head = s == SLOT_HEAD ? ShownToOption(controller, SLOT_HEAD, (uint8_t)option) : -1;
    int before = GameBarberMesh(controller, s);
    bool otherHead = s == SLOT_HEAD && ch >= 0 && before != head;

    g_setMesh(controller, slot, option);
    Log("barber tabs: slot %d -> place %d (option %d, controller %016llX)", s, (uint8_t)option, ShownToOption(controller, s, (uint8_t)option), (unsigned long long)controller);

    // Slot 5 holds heads too and must follow the head, or the old head's details
    // show on the new one (dark spots). Face Details is chosen after this.
    if (otherHead)
    {
        int details = MenuHeadDetails(ch, head);
        int place = details >= 0 ? OptionToShown(controller, SLOT_DETAILS, details) : -1;

        if (place >= 0)
            g_setMesh(controller, SLOT_DETAILS, (uint32_t)place);
    }

    // The game sets the same option again when a tab changes: no rebuild for that.
    if ((s == SLOT_HEAD || s == SLOT_BODY || s == SLOT_DETAILS) && GameBarberMesh(controller, s) != before)
        GameBarberMeshChanged(controller);
}

// ---------------------------------------------------------------------------
// Skin: its values only show after a rebuild, which the barber does not do for
// them (as the plugin always has). Dragging a slider sets one many times: the
// rebuild waits for a pause, in BarberTabsPoll.
// ---------------------------------------------------------------------------

typedef void(__fastcall* SetValueFn)(uintptr_t, uint32_t, uint32_t, uint32_t);
static SetValueFn g_setValue = NULL;
static const uint32_t SKIN_FIRST = 22, SKIN_LAST = 25;
static const DWORD SKIN_QUIET_MS = 150;
static volatile LONG64 g_skinController = 0;
static volatile LONG g_skinChangedAt = 0;

static void __fastcall HookedSetValue(uintptr_t controller, uint32_t index, uint32_t value, uint32_t flag)
{
    bool skin = index >= SKIN_FIRST && index <= SKIN_LAST && GameBarberIsPreview(controller) && GameBarberValue(controller, (int)index) != (int)(uint8_t)value;
    g_setValue(controller, index, value, flag);

    if (index == BARBER_EYE_ROW)
        Log("barber tabs: eye colour row -> %d (now %d)", (uint8_t)value, GameBarberValue(controller, (int)index));

    if (skin)
    {
        Log("barber tabs: skin value %u -> %d", index, (uint8_t)value);
        InterlockedExchange(&g_skinChangedAt, (LONG)GetTickCount());
        InterlockedExchange64(&g_skinController, (LONG64)controller);
    }
}

// ---------------------------------------------------------------------------
// Face and Body Shape, Eye Colour: the barber's values to the plugin's own
// ---------------------------------------------------------------------------

static const DWORD EYES_AWAY_MS = 1200;   // eye files are read again after about a second away

static int ShapeValue(uint8_t value, bool unset)
{
    if (unset && value <= 1)
        return 0;

    int shape = ((int)value - 50) * 2;
    return shape < -100 ? -100 : shape > 100 ? 100 : shape;
}

static void PollEyes(int ch, const Appearance& look, uintptr_t controller, bool preview)
{
    static DWORD s_backAt[CHARACTER_COUNT] = {};
    static uintptr_t s_copy[CHARACTER_COUNT] = {};
    static uint8_t s_head[CHARACTER_COUNT] = {};
    DWORD now = GetTickCount();

    if (s_backAt[ch])
    {
        if ((int)(now - s_backAt[ch]) >= 0)
        {
            s_backAt[ch] = 0;

            if (s_copy[ch] == controller)
                GameBarberSwapHead(controller, s_head[ch]);
        }

        return;
    }

    int colour = look.decoration[BARBER_EYE_ROW];

    if (colour >= EYE_COLOUR_COUNT || colour == EyesChosen(ch) || EyesOwnHeadsOff() || GameHeadRebuilding(ch))
        return;

    // The game keeps eye files while any head wears them: the character's own
    // head and the barber's copy both go away for a moment.
    EyesChoose(ch, colour);
    Appearance own;

    if (GameReadAppearance(ch, &own) && own.mesh[MESH_HEAD] != MESH_NONE)
        GameReloadHead(ch, EYES_AWAY_MS, MenuAwayHead(ch, own.mesh[MESH_HEAD]));

    int away = preview && look.mesh[MESH_HEAD] != MESH_NONE ? MenuAwayHead(ch, look.mesh[MESH_HEAD]) : -1;

    if (away >= 0)
    {
        s_copy[ch] = controller;
        s_head[ch] = look.mesh[MESH_HEAD];
        s_backAt[ch] = now + EYES_AWAY_MS;
        GameBarberSwapHead(controller, away);
    }
}

// Saved with the look, taken at the next start; a save without a choice keeps
// the last one, as the Anywhere edition does. A woman has the female animations,
// whoever she was.
static void PollIdentity(int ch, const Appearance& look, const Appearance* own)
{
    int h = look.decoration[BARBER_HEIGHT_ROW];

    if (h)
    {
        int height = (h - 50) * 2 / 5;
        height = height < HEIGHT_MIN ? HEIGHT_MIN : height > HEIGHT_MAX ? HEIGHT_MAX : height;

        if (height != IdentityHeight(ch))
        {
            IdentityChooseHeight(ch, height);
            Log("%S height chosen: %+d%%", CHARACTER_NAMES[ch], height);
        }
    }

    // Gender and race from the character's own values, which only change when the
    // barber's Save is pressed: with nothing shown live, and a save without a
    // choice keeping the last one, Esc would not take back a choice of the copy's.
    if (!own)
        return;

    int gender, race;
    int g = own->decoration[BARBER_GENDER_ROW], r = own->decoration[BARBER_RACE_ROW];
    IdentityChosen(ch, &gender, &race);
    int wantGender = g == 1 ? GENDER_MALE : g == 2 ? GENDER_FEMALE : gender;
    int wantRace = r >= 1 && r <= 4 ? r - 1 : race;

    if (wantGender == GENDER_FEMALE)
        IdentityChooseFemaleMoves(ch, true);

    if (wantGender != gender || wantRace != race)
        IdentityChoose(ch, wantGender, wantRace);
}

void BarberTabsPoll()
{
    static LONG s_writes[CHARACTER_COUNT] = {};

    uintptr_t skin = (uintptr_t)g_skinController;

    if (skin && GetTickCount() - (DWORD)g_skinChangedAt >= SKIN_QUIET_MS)
    {
        InterlockedExchange64(&g_skinController, 0);
        GameBarberMeshChanged(skin);
    }

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        Appearance look;
        uintptr_t controller = 0;
        bool preview = false;

        if (!GameBarberLook(ch, &look, &controller, &preview))
            continue;

        bool unset = GameBarberShapesUnset(look);

        for (int i = 0; i < FaceSliderCount() && i < BARBER_FACE_ROWS; ++i)
        {
            int shape = ShapeValue(look.decoration[BARBER_SHAPE_ROW + i], unset);

            if (FaceValue(ch, i) != shape)
                FaceSetValue(ch, i, shape);
        }

        for (int i = 0; i < BodySliderCount() && i < BARBER_BODY_ROWS; ++i)
        {
            int shape = ShapeValue(look.decoration[BARBER_SHAPE_ROW + BARBER_FACE_ROWS + i], unset);

            if (BodyValue(ch, i) != shape)
                BodySetValue(ch, i, shape);
        }

        PollEyes(ch, look, controller, preview);

        // Only the character's own: the barber's copy may yet be cancelled.
        Appearance own;
        bool haveOwn = GameReadAppearance(ch, &own);
        PollIdentity(ch, look, haveOwn ? &own : NULL);

        if (haveOwn && own.mesh[MESH_HEAD] != MESH_NONE)
            IdentityKeepHead(ch, own.mesh[MESH_HEAD]);

        ShowWhatFits(controller);

        // Shapes written: the barber's copy shows them after a rebuild (the
        // character's own is refreshed by faceshape.cpp).
        LONG writes = ShapeWrites(ch);

        if (writes != s_writes[ch])
        {
            s_writes[ch] = writes;

            if (preview)
                GameBarberMeshChanged(controller);
        }
    }
}

void BarberTabsInit()
{
    uint8_t* base = (uint8_t*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);

    if (nt->FileHeader.TimeDateStamp != KNOWN_TIMESTAMP || nt->OptionalHeader.SizeOfImage != KNOWN_IMAGE_SIZE)
    {
        Log("barber tabs: another game build - the barber keeps its own parts");
        return;
    }

    __try
    {
        if (memcmp(base + SET_VALUE, SET_VALUE_START, sizeof(SET_VALUE_START)) != 0 || memcmp(base + BUILD_PARTS, BUILD_START, sizeof(BUILD_START)) != 0 || memcmp(base + SET_MESH, SET_MESH_START, sizeof(SET_MESH_START)) != 0 ||
            memcmp(base + TITLE_OBJECT, TITLE_START, sizeof(TITLE_START)) != 0 || memcmp(base + SUB_CTOR, SUB_CTOR_START, sizeof(SUB_CTOR_START)) != 0 ||
            memcmp(base + RECORD_CTOR, RECORD_CTOR_START, sizeof(RECORD_CTOR_START)) != 0)
        {
            Log("barber tabs: the game's code is not as expected - the barber keeps its own parts");
            return;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }

    g_controllerOf = (ControllerOfFn)(base + CONTROLLER_OF);
    g_grow = (GrowFn)(base + GROW_RECORDS);
    g_recordCtor = (CtorFn)(base + RECORD_CTOR);
    g_subCtor = (CtorFn)(base + SUB_CTOR);
    g_subDtor = (CtorFn)(base + SUB_DTOR);
    g_subPush = (PushFn)(base + SUB_PUSH);
    g_pairPush = (PushFn)(base + PAIR_PUSH);
    g_intern = (InternFn)(base + INTERN);
    g_nameHash = (NameHashFn)(base + NAME_HASH);
    g_makeString = (MakeStringFn)(base + MAKE_STRING);
    g_assignString = (AssignStringFn)(base + ASSIGN_STRING);
    g_freeString = (CtorFn)(base + FREE_STRING);

    for (int i = 0; i < TITLE_COUNT; ++i)
    {
        g_titleTexts[i].vtable = TITLE_VTABLE;
        g_titleTexts[i].text = TITLE_WORDS[i];
        g_titleObjects[i].text = &g_titleTexts[i];
    }

    MH_STATUS init = MH_Initialize();

    if ((init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) ||
        MH_CreateHook(base + BUILD_PARTS, (void*)&HookedBuildParts, (void**)&g_buildParts) != MH_OK ||
        MH_EnableHook(base + BUILD_PARTS) != MH_OK ||
        MH_CreateHook(base + SET_MESH, (void*)&HookedSetMesh, (void**)&g_setMesh) != MH_OK ||
        MH_EnableHook(base + SET_MESH) != MH_OK ||
        MH_CreateHook(base + TITLE_OBJECT, (void*)&HookedTitle, (void**)&g_title) != MH_OK ||
        MH_EnableHook(base + TITLE_OBJECT) != MH_OK ||
        MH_CreateHook(base + SET_VALUE, (void*)&HookedSetValue, (void**)&g_setValue) != MH_OK ||
        MH_EnableHook(base + SET_VALUE) != MH_OK)
    {
        Log("barber tabs: could not hook the parts");
        return;
    }

    Log("barber tabs: on");
}

#endif
