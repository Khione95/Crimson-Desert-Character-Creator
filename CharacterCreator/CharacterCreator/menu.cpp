#include "pch.h"
#include "menu.h"
#include "eyes.h"
#include "game.h"
#include "identity.h"
#include "log.h"
#include "menu_data.h"
#include "profile.h"

#include <stdio.h>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

enum PageKind
{
    PAGE_GENDER,
    PAGE_RACE,
    PAGE_MESH,      // pick a mesh option (target = mesh slot)
    PAGE_COLOR,     // pick a palette colour (target = decoration index)
    PAGE_TYPE,      // pick a texture type such as a tattoo (target = decoration index)
    PAGE_SLIDERS,   // adjust one or more values (sliders[])
    PAGE_EYES,      // pick an eye colour (see eyes.h)
};

struct Page
{
    const wchar_t* label;
    PageKind kind;
    int target;
    const wchar_t* itemWord;        // "Hair" -> items are called "Hair 1", "Hair 2", ...
    int sliders[6];
    const wchar_t* sliderNames[6];
    int sliderCount;
};

struct Tab
{
    const wchar_t* label;
    Page pages[10];
    int pageCount;
};

// A slider that is not an appearance value: the height (see identity.h).
static const int SLIDER_HEIGHT = -1;

// Decoration indices come from the game's decorationparam_player.xml
// (6 hairBaseColor, 22 skinColor, ...).
static const Tab TABS[] =
{
    { L"Gender", { { L"Gender", PAGE_GENDER } }, 1 },
    { L"Race", { { L"Race", PAGE_RACE } }, 1 },
    { L"Body", {
        { L"Body", PAGE_MESH, MESH_BODY, L"Body" },
        { L"Skin colour", PAGE_COLOR, 22 },
        { L"Skin shine", PAGE_SLIDERS, 0, NULL, { 23 }, { L"Shine" }, 1 } }, 3 },
    { L"Height", { { L"Height", PAGE_SLIDERS, 0, NULL, { SLIDER_HEIGHT }, { L"Height" }, 1 } }, 1 },
    { L"Head", { { L"Head", PAGE_MESH, MESH_HEAD, L"Head" } }, 1 },
    { L"Hair", {
        { L"Hair", PAGE_MESH, MESH_HAIR, L"Hair" },
        { L"Hair colour", PAGE_COLOR, 6 },
        { L"Hair length", PAGE_SLIDERS, 0, NULL, { 0, 1, 2 }, { L"Length 1", L"Length 2", L"Length 3" }, 3 },
        { L"Hair shine", PAGE_SLIDERS, 0, NULL, { 5 }, { L"Shine" }, 1 } }, 4 },
    { L"Beard", {
        { L"Beard", PAGE_MESH, MESH_BEARD, L"Beard" },
        { L"Beard colour", PAGE_COLOR, 32 },
        { L"Beard length", PAGE_SLIDERS, 0, NULL, { 29 }, { L"Length" }, 1 } }, 3 },
    { L"Eyebrow", {
        { L"Eyebrow", PAGE_TYPE, 41, L"Eyebrow" },
        { L"Eyebrow colour", PAGE_COLOR, 50 },
        { L"Eyebrow length", PAGE_SLIDERS, 0, NULL, { 47 }, { L"Length" }, 1 } }, 3 },
    { L"Face Tattoo", {
        { L"Face tattoo", PAGE_TYPE, 63, L"Tattoo" },
        { L"Tattoo colour", PAGE_COLOR, 71 },
        { L"Tattoo opacity", PAGE_SLIDERS, 0, NULL, { 69 }, { L"Opacity" }, 1 },
        { L"Tattoo placement", PAGE_SLIDERS, 0, NULL, { 64, 65, 66, 67, 68 },
            { L"Left / right", L"Up / down", L"Rotation", L"Width", L"Height" }, 5 } }, 4 },
    { L"Body Tattoo", {
        { L"Body tattoo", PAGE_TYPE, 114, L"Tattoo" },
        { L"Tattoo colour", PAGE_COLOR, 122 },
        { L"Tattoo opacity", PAGE_SLIDERS, 0, NULL, { 120 }, { L"Opacity" }, 1 },
        { L"Tattoo placement", PAGE_SLIDERS, 0, NULL, { 115, 116, 117, 118, 119 },
            { L"Left / right", L"Up / down", L"Rotation", L"Width", L"Height" }, 5 } }, 4 },
    { L"Eye Colour", { { L"Eye colour", PAGE_EYES } }, 1 },
    // Eyelash length (85) does nothing on the player's eyelashes. The dye
    // shows mostly on the lighter tips; dark colours barely change them.
    { L"Eyelashes", { { L"Eyelash colour", PAGE_COLOR, 86 } }, 1 },
    { L"Scars", {
        { L"Face scar", PAGE_TYPE, 13, L"Scar" },
        { L"Face scar colour", PAGE_COLOR, 21 },
        { L"Face scar placement", PAGE_SLIDERS, 0, NULL, { 19, 14, 15, 16, 17, 18 },
            { L"Opacity", L"Left / right", L"Up / down", L"Rotation", L"Width", L"Height" }, 6 },
        { L"Body scar", PAGE_TYPE, 132, L"Scar" },
        { L"Body scar colour", PAGE_COLOR, 140 },
        { L"Body scar placement", PAGE_SLIDERS, 0, NULL, { 138, 133, 134, 135, 136, 137 },
            { L"Opacity", L"Left / right", L"Up / down", L"Rotation", L"Width", L"Height" }, 6 } }, 6 },
    { L"Paint & Dirt", {
        { L"Face paint", PAGE_TYPE, 72, L"Paint" },
        { L"Face paint colour", PAGE_COLOR, 80 },
        { L"Face paint placement", PAGE_SLIDERS, 0, NULL, { 78, 73, 74, 75, 76, 77 },
            { L"Opacity", L"Left / right", L"Up / down", L"Rotation", L"Width", L"Height" }, 6 },
        { L"Body paint", PAGE_TYPE, 123, L"Paint" },
        { L"Body paint colour", PAGE_COLOR, 131 },
        { L"Body paint placement", PAGE_SLIDERS, 0, NULL, { 129, 124, 125, 126, 127, 128 },
            { L"Opacity", L"Left / right", L"Up / down", L"Rotation", L"Width", L"Height" }, 6 } }, 6 },
};

// Face makeup (not in the menu yet: player heads lack the makeup mask) is only
// taken in when the head is built, so changing it rebuilds the head.
static const int MAKEUP_FIRST = 141, MAKEUP_LAST = 183;

static const int TAB_COUNT = sizeof(TABS) / sizeof(TABS[0]);
static const int GRID_COLUMNS = 4;

// The third is a woman who keeps the male animations (see identity.h).
static const wchar_t* GENDERS[] = { L"Male", L"Female", L"Female (male animations)" };
static const int GENDER_CHOICE_FEMALE_MALE_MOVES = 2;
static const wchar_t* RACES[] = { L"Human", L"Orc", L"Dwarf", L"Goblin" };

// Body/head file prefixes for [race][gender].
static const char* RACE_CODES[4][2] = {
    { "phm", "phw" }, { "pom", "pow" }, { "pdm", "pdw" }, { "pgm", "pgw" } };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct Item
{
    std::wstring label;
    std::wstring icon;
    int value;
    bool hasColor;
    uint8_t r, g, b;
};

static MenuData g_data;
static bool g_dataLoaded = false;
static char g_folder[MAX_PATH];

static SRWLOCK g_lock = SRWLOCK_INIT;
static int g_char = CHAR_KLIFF;    // whose look the menu edits (F6 / F7 / F8)
static int g_tab = 5;              // open on Hair
static int g_page[TAB_COUNT] = {};
static int g_sliderRow = 0;
static int g_scrollRow = 0;
static int g_gender = 0;
static int g_race = 0;
static Appearance g_view = {};     // what the menu shows as current

// For Esc: the look when the menu was opened.
static Appearance g_openDesired;
static AppearanceMask g_openMask;
static Appearance g_openActual;
static AppearanceMask g_touched;
static int g_openGender, g_openRace, g_openEyes, g_openHeight;
static bool g_openFemaleMoves;

// A notice shown for a while after the editor closes.
static const DWORD RESTART_NOTICE_MS = 6000;
static std::wstring g_restartNotice;
static DWORD g_restartNoticeAt = 0;

// ---------------------------------------------------------------------------
// Gender and race (see identity.h)
// ---------------------------------------------------------------------------

// Meshes must fit the skeleton the character was built with, otherwise the
// body is pulled out of shape. A newly chosen gender or race only counts
// after a restart, so filtering uses what the character loaded as (or, until
// the game has built them, what they are without the mod).
static void BuiltAs(int ch, int* gender, int* race)
{
    if (!IdentityLoaded(ch, gender, race))
        IdentityNative(ch, gender, race);
}

static int FilterGender()
{
    int gender, race;
    BuiltAs(g_char, &gender, &race);
    return gender;
}

static int FilterRace()
{
    int gender, race;
    BuiltAs(g_char, &gender, &race);
    return race;
}

static bool OptionFitsFor(const MeshOption& m, int gender, int race)
{
    const char* code = RACE_CODES[race][gender];

    switch (m.slot)
    {
    // The body must match the skeleton.
    case MESH_BODY:
        return m.race == code;

    // Every head of the gender, of any race ("phm", "pom", ... end in m for
    // men, w for women). Slot 5 holds heads too and follows the head.
    case MESH_HEAD:
    case MESH_WHISKERS:
        return m.race.size() == 3 && m.race[2] == code[2];

    // Every hair style and beard, for every race and gender. (The beard
    // slot's women's eyebrow entries are only the "None" option.)
    case MESH_HAIR:
        return true;

    case MESH_BEARD:
        return m.race != "phw";     // women start without one (see FitCharacter)
    }

    return true;
}

static bool OptionFits(const MeshOption& m)
{
    return OptionFitsFor(m, FilterGender(), FilterRace());
}

// The eyes and face makeup are taken in when the head is built, so they are
// applied by rebuilding it. The eyes are a part of the head (head_sub or
// eyeleft/right) that many heads share; the game only reads them again if the
// head shown for that moment has other eyes. Preferred: the nearest such head
// of the same race and gender, then of the same gender, then any.
// The game's shared eye files: other characters in the world (Oongka, NPCs)
// keep them loaded, so a rebuild cannot make the game read them again.
static bool SharedEyes(const std::string& eyes)
{
    return eyes == "cd_phm_00_eyeleft_00_0001" || eyes == "cd_phw_00_eyeleft_00_0001";
}

static std::string EyesOf(int ch, int head)
{
    for (const MeshOption& m : g_data.meshes)
        if (m.slot == MESH_HEAD && m.index[ch] == head)
            return m.eyes;

    return std::string();
}

// Returns HEAD_NO_REBUILD for heads with shared eyes (their colour applies
// after a restart).
static int ChooseAwayHead(int ch, int current)
{
    uint32_t loaded = GameMeshOptionCount(ch, MESH_HEAD);
    std::string currentEyes = EyesOf(ch, current);

    if (SharedEyes(currentEyes))
        return HEAD_NO_REBUILD;

    int gender, race;
    BuiltAs(ch, &gender, &race);
    int away = -1, awayRank = 99;

    for (const MeshOption& m : g_data.meshes)
    {
        int index = m.index[ch];

        if (m.slot != MESH_HEAD || index == current || (loaded && (uint32_t)index >= loaded))
            continue;

        bool otherEyes = currentEyes.empty() || m.eyes.empty() || m.eyes != currentEyes;
        bool fits = OptionFitsFor(m, gender, race);
        bool sameGender = false;

        for (int r = 0; r < 4 && !sameGender; ++r)
            sameGender = OptionFitsFor(m, gender, r);

        int rank = otherEyes ? (fits ? 0 : sameGender ? 1 : 2) : (fits ? 3 : 4);

        if (rank < awayRank || (rank == awayRank && abs(index - current) < abs(away - current)))
        {
            away = index;
            awayRank = rank;
        }
    }

    return away;
}

static void ReloadHead()
{
    GameReloadHead(g_char, HEAD_AWAY_MS, ChooseAwayHead(g_char, g_view.mesh[MESH_HEAD]));
}

// ---------------------------------------------------------------------------
// Items of a page
// ---------------------------------------------------------------------------

// The game does not treat 255 as "none" in a slot that has options: it falls
// back to option 0 (a beard). "No beard" is the women's eyebrow entry the
// mod's lists have in the beard slot (a model without a prefab, which shows
// nothing there); the game crashed on options without meshes.
static const char* const NO_BEARD_MESH = "cd_phw_00_eyebrow_00_0001";

static int NoneOption(int ch, int slot)
{
    for (const MeshOption& m : g_data.meshes)
        if (m.slot == slot && (m.mesh == "-" || (slot == MESH_BEARD && m.mesh == NO_BEARD_MESH)))
            return m.index[ch];

    return MESH_NONE;
}

static bool IsNone(int ch, int slot, int value)
{
    return value == MESH_NONE || value == NoneOption(ch, slot);
}

static const wchar_t* TabLabel(const Tab& tab)
{
    return tab.label;
}

static const wchar_t* PageLabel(const Tab& tab, int page)
{
    return tab.pages[page].label;
}

static const wchar_t* ItemWord(const Tab& tab, const Page& page)
{
    (void)tab;
    return page.itemWord;
}

static const Page& CurrentPage()
{
    return TABS[g_tab].pages[g_page[g_tab]];
}

static void BuildItems(const Page& page, std::vector<Item>* items, int* selected)
{
    items->clear();
    *selected = -1;
    wchar_t label[128];

    switch (page.kind)
    {
    case PAGE_GENDER:
        // Only Kliff has the choice: the female animations are what break his
        // crow wings and two-handed swords.
        for (int i = 0; i < (g_char == CHAR_KLIFF ? 3 : 2); ++i)
            items->push_back({ GENDERS[i], L"", i });
        *selected = g_gender == GENDER_FEMALE && !IdentityFemaleMoves(g_char) ? GENDER_CHOICE_FEMALE_MALE_MOVES : g_gender;
        break;

    case PAGE_RACE:
        for (int i = 0; i < 4; ++i)
            items->push_back({ RACES[i], L"", i });
        *selected = g_race;
        break;

    case PAGE_MESH:
    {
        uint32_t loaded = GameMeshOptionCount(g_char, page.target);

        if (page.target == MESH_BEARD)
        {
            items->push_back({ L"None", L"", NoneOption(g_char, MESH_BEARD) });

            if (IsNone(g_char, MESH_BEARD, g_view.mesh[page.target]))
                *selected = 0;
        }

        size_t fixedItems = items->size();

        for (int pass = 0; pass < 2 && items->size() == fixedItems; ++pass)
        {
            for (const MeshOption& m : g_data.meshes)
            {
                if (m.slot != page.target || !m.shown || (loaded && (uint32_t)m.index[g_char] >= loaded))
                    continue;

                if (pass == 0 && !OptionFits(m))
                    continue;   // second pass shows everything if nothing fits

                swprintf_s(label, L"%s %d", ItemWord(TABS[g_tab], page), (int)(items->size() - fixedItems) + 1);
                items->push_back({ label, m.icon, m.index[g_char] });

                if (m.index[g_char] == g_view.mesh[page.target])
                    *selected = (int)items->size() - 1;
            }
        }
        break;
    }

    case PAGE_COLOR:
    {
        const DecorationParam& p = g_data.params[page.target];
        int palette = p.known ? p.palette : -1;

        if (palette < 0 || palette > 255)
            break;

        const std::vector<PaletteColor>& colors = g_data.palettes[palette];
        int count = (int)colors.size();

        if (p.known && p.max + 1 < count)
            count = p.max + 1;

        for (int i = 0; i < count; ++i)
        {
            Item it = { colors[i].name, L"", i, true, colors[i].r, colors[i].g, colors[i].b };
            items->push_back(it);
        }

        *selected = g_view.decoration[page.target];
        break;
    }

    case PAGE_TYPE:
    {
        const DecorationParam& p = g_data.params[page.target];
        int count = p.known && p.palette >= 0 && p.palette < 256 ? g_data.textureCounts[p.palette] : 0;

        if (count <= 0)
            count = p.known ? p.max + 1 : 1;

        for (int i = 0; i < count; ++i)
        {
            if (i == 0)
                wcscpy_s(label, L"None");
            else
                swprintf_s(label, L"%s %d", page.itemWord, i);

            items->push_back({ label, L"", i });
        }

        *selected = g_view.decoration[page.target];
        break;
    }

    case PAGE_EYES:
        for (int i = 0; i < EYE_COLOUR_COUNT; ++i)
        {
            const EyeColour& c = EYE_COLOURS[i];
            items->push_back({ c.name, L"", i, true, c.r, c.g, c.b });
        }

        *selected = EyesChosen(g_char);
        break;

    case PAGE_SLIDERS:
        break;
    }

    if (*selected >= (int)items->size())
        *selected = -1;
}

// ---------------------------------------------------------------------------
// Changing values (live preview)
// ---------------------------------------------------------------------------

static void SetMesh(int slot, int option)
{
    g_view.mesh[slot] = (uint8_t)option;
    g_touched.mesh[slot] = true;
    GameSetMesh(g_char, slot, (uint8_t)option);
}

static void SetValue(int index, int value)
{
    const DecorationParam& p = g_data.params[index];

    if (p.known)
    {
        if (value < p.min) value = p.min;
        if (value > p.max) value = p.max;
    }

    bool changed = g_view.decoration[index] != (uint8_t)value;
    g_view.decoration[index] = (uint8_t)value;
    g_touched.decoration[index] = true;
    GameSetDecoration(g_char, index, (uint8_t)value);

    if (changed && index >= MAKEUP_FIRST && index <= MAKEUP_LAST)
        ReloadHead();
}

static void Choose(const Page& page, const Item& item)
{
    switch (page.kind)
    {
    case PAGE_GENDER:
    case PAGE_RACE:
        // A new skeleton, animations and base body: applied on the next load.
        if (page.kind == PAGE_GENDER)
        {
            g_gender = item.value == GENDER_CHOICE_FEMALE_MALE_MOVES ? GENDER_FEMALE : item.value;
            IdentityChooseFemaleMoves(g_char, item.value != GENDER_CHOICE_FEMALE_MALE_MOVES);
        }
        else
            g_race = item.value;

        IdentityChoose(g_char, g_gender, g_race);
        break;

    case PAGE_MESH:
        SetMesh(page.target, item.value);
        break;

    case PAGE_COLOR:
    case PAGE_TYPE:
        SetValue(page.target, item.value);
        break;

    case PAGE_EYES:
        // The game keeps eye files loaded for the session: a new colour is
        // read when the game next starts.
        EyesChoose(g_char, item.value);
        break;

    case PAGE_SLIDERS:
        break;
    }
}

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------

void MenuInit(const char* folder)
{
    strcpy_s(g_folder, folder);
    g_dataLoaded = MenuDataLoad(folder, &g_data);
    IdentityInit(folder, &g_data);
    EyesInit(folder);
    GameSetMarker(g_data.markerSlot, (uint32_t)g_data.markerCount);
    GameSetHeadChooser(ChooseAwayHead);

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        if (g_data.startKnown[ch])
            GameSetStartValues(ch, g_data.start[ch]);

    if (g_data.markerSlot < 0)
        Log("menu data has no character marker - rebuild it with tools/build_data.py");
}

static void Open(int ch)
{
    g_char = ch;
    GameRequestValues(ch);
    g_restartNotice.clear();
    OverlaySetDrawing(false);
    GameGetDesired(g_char, &g_openDesired, &g_openMask);

    // A character the game does not have right now shows their saved look.
    if (!GameReadAppearance(g_char, &g_openActual))
        g_openActual = g_openDesired;

    // Show the game's current look, with chosen values that may still be on
    // their way in on top.
    g_view = g_openActual;

    for (int i = 0; i < DECORATION_COUNT; ++i)
        if (g_openMask.decoration[i])
            g_view.decoration[i] = g_openDesired.decoration[i];

    for (int i = 0; i < MESH_SLOT_COUNT; ++i)
        if (g_openMask.mesh[i])
            g_view.mesh[i] = g_openDesired.mesh[i];

    g_touched = {};
    IdentityChosen(g_char, &g_gender, &g_race);
    g_openGender = g_gender;
    g_openRace = g_race;
    g_openEyes = EyesChosen(g_char);
    g_openHeight = IdentityHeight(g_char);
    g_openFemaleMoves = IdentityFemaleMoves(g_char);
    g_scrollRow = 0;
    g_sliderRow = 0;
}

// Gender, race and height are built in when the game starts: tells which of
// them differ from what the character was built with.
static std::wstring RestartNeeded(int ch)
{
    int gender, race, builtGender, builtRace;
    IdentityChosen(ch, &gender, &race);

    if (!IdentityLoaded(ch, &builtGender, &builtRace))
        IdentityNative(ch, &builtGender, &builtRace);

    std::wstring what;

    if (gender != builtGender)
        what += L"gender";

    if (race != builtRace)
        what += std::wstring(what.empty() ? L"" : L", ") + L"race";

    if (gender == GENDER_FEMALE && IdentityFemaleMoves(ch) != IdentityStartFemaleMoves(ch))
        what += std::wstring(what.empty() ? L"" : L", ") + L"animations";

    if (IdentityHeight(ch) != IdentityLoadedHeight(ch))
        what += std::wstring(what.empty() ? L"" : L", ") + L"height";

    // Heads using the game's shared eye files only take a new colour at start.
    if (EyesChosen(ch) != g_openEyes)
        what += std::wstring(what.empty() ? L"" : L", ") + L"eye colour";

    return what;
}

static void Keep()
{
    OverlaySetVisible(false);
    ProfileSaveIfChanged();
    Log("menu: %S changes kept", CHARACTER_NAMES[g_char]);

    std::wstring what = RestartNeeded(g_char);

    if (!what.empty())
    {
        g_restartNotice = std::wstring(CHARACTER_NAMES[g_char]) + L"'s " + what +
            L" will be fully applied after restarting the game.";
        g_restartNoticeAt = GetTickCount();
        OverlaySetDrawing(true);
        Log("menu: restart notice shown (%S)", what.c_str());
    }
}

static void Cancel()
{
    Appearance values = g_openDesired;
    AppearanceMask mask = g_openMask;

    // Values first changed in this session go back to what the game had.
    for (int i = 0; i < DECORATION_COUNT; ++i)
    {
        if (g_touched.decoration[i] && !mask.decoration[i])
        {
            values.decoration[i] = g_openActual.decoration[i];
            mask.decoration[i] = true;
        }
    }

    for (int i = 0; i < MESH_SLOT_COUNT; ++i)
    {
        if (g_touched.mesh[i] && !mask.mesh[i])
        {
            values.mesh[i] = g_openActual.mesh[i];
            mask.mesh[i] = true;
        }
    }

    GameSetDesired(g_char, values, mask);
    g_gender = g_openGender;
    g_race = g_openRace;
    IdentityChoose(g_char, g_gender, g_race);
    IdentityChooseHeight(g_char, g_openHeight);
    IdentityChooseFemaleMoves(g_char, g_openFemaleMoves);

    bool makeupTouched = false;

    for (int i = MAKEUP_FIRST; i <= MAKEUP_LAST; ++i)
        makeupTouched = makeupTouched || g_touched.decoration[i];

    if (EyesChosen(g_char) != g_openEyes)
        EyesChoose(g_char, g_openEyes);

    if (makeupTouched)
        ReloadHead();

    OverlaySetVisible(false);
    Log("menu: %S changes cancelled", CHARACTER_NAMES[g_char]);
}

void MenuToggle(int ch)
{
    if (ch < 0 || ch >= CHARACTER_COUNT)
        return;

    AcquireSRWLockExclusive(&g_lock);

    bool open = OverlayVisible();

    if (open)
        Keep();

    // The same key closes the editor; another character's key switches to them.
    if ((!open || ch != g_char) && g_dataLoaded)
    {
        Open(ch);
        OverlaySetVisible(true);
        Log("menu opened for %S", CHARACTER_NAMES[ch]);
        GameLogCharacter(ch);
    }

    ReleaseSRWLockExclusive(&g_lock);
}

// ---------------------------------------------------------------------------
// Keeping body, head and hair matched to the loaded race and gender
// ---------------------------------------------------------------------------

// The save (or the profile) can hold a body, head or hair made for another
// skeleton, which then looks stretched. Such a choice is replaced by the race
// and gender's base one (the values of its base character file).

static int FindOption(int ch, int slot, const std::string& mesh)
{
    for (const MeshOption& m : g_data.meshes)
        if (m.slot == slot && m.mesh == mesh)
            return m.index[ch];

    return -1;
}

static int FirstFitting(int ch, int slot, int gender, int race)
{
    uint32_t loaded = GameMeshOptionCount(ch, slot);

    for (const MeshOption& m : g_data.meshes)
        if (m.slot == slot && m.shown && OptionFitsFor(m, gender, race) && (!loaded || (uint32_t)m.index[ch] < loaded))
            return m.index[ch];

    return -1;
}

static void FitCharacter(int ch)
{
    int gender, race;
    Appearance look;

    if (!IdentityLoaded(ch, &gender, &race) || GameHeadRebuilding(ch) || !GameReadAppearance(ch, &look))
        return;

    Appearance desired;
    AppearanceMask desiredMask;
    GameGetDesired(ch, &desired, &desiredMask);

    // The player's choices are checked, not what the save loads before they
    // are applied: a loaded look replaced here would overwrite the profile.
    for (int s = 0; s < MESH_SLOT_COUNT; ++s)
        if (desiredMask.mesh[s])
            look.mesh[s] = desired.mesh[s];

    // As themselves, characters go back to their own body, head and hair;
    // as another race, to that race's base ones.
    int nativeGender, nativeRace;
    IdentityNative(ch, &nativeGender, &nativeRace);
    bool themselves = gender == nativeGender && race == nativeRace && g_data.own[ch].known;
    const BaseCharacter& base = themselves ? g_data.own[ch] : g_data.bases[gender][race];

    // Slot 5 holds heads too in the mod's lists; it follows the head.
    static const std::string none;
    const std::string* baseMesh[5] = { &base.body, &base.head, &base.hair, &none, &base.head };
    const int slots[5] = { MESH_BODY, MESH_HEAD, MESH_HAIR, MESH_BEARD, MESH_WHISKERS };

    for (int i = 0; i < 5; ++i)
    {
        int slot = slots[i];
        const MeshOption* current = NULL;

        for (const MeshOption& m : g_data.meshes)
            if (m.slot == slot && m.index[ch] == look.mesh[slot])
                current = &m;

        // Slot 5 follows the head: "none" there shows option 0 (Kliff's head).
        if (slot == MESH_WHISKERS)
        {
            int wanted = -1;

            for (const MeshOption& h : g_data.meshes)
                if (h.slot == MESH_HEAD && h.index[ch] == look.mesh[MESH_HEAD])
                    wanted = FindOption(ch, MESH_WHISKERS, h.mesh);

            // Set once: until the character is built it stays pending.
            bool pending = desiredMask.mesh[MESH_WHISKERS] && desired.mesh[MESH_WHISKERS] == wanted;

            if (wanted >= 0 && wanted != look.mesh[MESH_WHISKERS] && !pending)
            {
                Log("menu: %S's slot 5 set to match the head (%d)", CHARACTER_NAMES[ch], wanted);
                GameSetMesh(ch, MESH_WHISKERS, (uint8_t)wanted);
            }

            continue;
        }

        // A woman's beard slot at "none" shows option 0, a beard.
        if (slot == MESH_BEARD && gender == GENDER_FEMALE && look.mesh[MESH_BEARD] == MESH_NONE &&
            !(desiredMask.mesh[MESH_BEARD] && desired.mesh[MESH_BEARD] == MESH_NONE && NoneOption(ch, MESH_BEARD) == MESH_NONE))
        {
            int none = NoneOption(ch, MESH_BEARD);
            bool pending = desiredMask.mesh[MESH_BEARD] && desired.mesh[MESH_BEARD] == none;

            if (none != MESH_NONE && !pending)
            {
                Log("menu: %S's beard set to none (%d)", CHARACTER_NAMES[ch], none);
                GameSetMesh(ch, MESH_BEARD, (uint8_t)none);
            }

            continue;
        }

        if (!current || current->mesh == "-" || current->mesh == NO_BEARD_MESH)
            continue;

        // Women start without a beard: one from the save is removed, one
        // chosen in the Beard tab stays.
        bool unwantedBeard = slot == MESH_BEARD && gender == GENDER_FEMALE &&
            !(desiredMask.mesh[MESH_BEARD] && desired.mesh[MESH_BEARD] == look.mesh[MESH_BEARD]);

        if (OptionFitsFor(*current, gender, race) && !unwantedBeard)
            continue;

        // A beard that does not fit is removed.
        int replacement = slot == MESH_BEARD ? NoneOption(ch, MESH_BEARD) : FindOption(ch, slot, *baseMesh[i]);

        if (replacement < 0)
            replacement = FirstFitting(ch, slot, gender, race);

        if (replacement >= 0 && !(desiredMask.mesh[slot] && desired.mesh[slot] == replacement))
        {
            static const char* const NAMES[] = { "body", "head", "hair", "beard", "slot 4", "slot 5" };
            Log("menu: %S's %s option %d does not fit, using %d", CHARACTER_NAMES[ch], NAMES[slot], look.mesh[slot], replacement);
            GameSetMesh(ch, slot, (uint8_t)replacement);

            if (ch == g_char)
                g_view.mesh[slot] = (uint8_t)replacement;
        }
    }
}

void MenuPoll()
{
    static DWORD last = 0;
    DWORD now = GetTickCount();

    if (!g_dataLoaded || now - last < 500)
        return;

    last = now;
    AcquireSRWLockExclusive(&g_lock);

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        FitCharacter(ch);

    ReleaseSRWLockExclusive(&g_lock);
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

static void MoveSelection(int delta)
{
    const Page& page = CurrentPage();
    std::vector<Item> items;
    int selected;
    BuildItems(page, &items, &selected);

    if (items.empty())
        return;

    int next = selected < 0 ? 0 : selected + delta;

    if (next < 0) next = 0;
    if (next >= (int)items.size()) next = (int)items.size() - 1;

    if (next != selected)
        Choose(page, items[next]);
}

static void AdjustSlider(int delta)
{
    const Page& page = CurrentPage();

    if (g_sliderRow >= page.sliderCount)
        return;

    int index = page.sliders[g_sliderRow];

    if (index == SLIDER_HEIGHT)
    {
        int height = IdentityHeight(g_char) + delta;
        IdentityChooseHeight(g_char, height < HEIGHT_MIN ? HEIGHT_MIN : height > HEIGHT_MAX ? HEIGHT_MAX : height);
        return;
    }

    SetValue(index, g_view.decoration[index] + delta);
}

void MenuKey(int vk)
{
    AcquireSRWLockExclusive(&g_lock);

    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const Page& page = CurrentPage();
    bool sliders = page.kind == PAGE_SLIDERS;

    switch (vk)
    {
    case VK_ESCAPE:
        Cancel();
        break;

    case VK_RETURN:
    case VK_SPACE:
        Keep();
        break;

    case VK_TAB:
        g_tab = (g_tab + (shift ? TAB_COUNT - 1 : 1)) % TAB_COUNT;
        g_scrollRow = g_sliderRow = 0;
        break;

    case 'Q':
    case VK_OEM_4:      // [ (for keyboards where Q is elsewhere)
        g_page[g_tab] = (g_page[g_tab] + TABS[g_tab].pageCount - 1) % TABS[g_tab].pageCount;
        g_scrollRow = g_sliderRow = 0;
        break;

    case 'E':
    case VK_OEM_6:      // ]
        g_page[g_tab] = (g_page[g_tab] + 1) % TABS[g_tab].pageCount;
        g_scrollRow = g_sliderRow = 0;
        break;

    case VK_LEFT:
    case 'A':
        if (sliders) AdjustSlider(shift ? -10 : -1);
        else MoveSelection(-1);
        break;

    case VK_RIGHT:
    case 'D':
        if (sliders) AdjustSlider(shift ? 10 : 1);
        else MoveSelection(1);
        break;

    case VK_UP:
    case 'W':
        if (sliders) g_sliderRow = g_sliderRow > 0 ? g_sliderRow - 1 : 0;
        else MoveSelection(-GRID_COLUMNS);
        break;

    case VK_DOWN:
    case 'S':
        if (sliders) g_sliderRow = g_sliderRow + 1 < page.sliderCount ? g_sliderRow + 1 : g_sliderRow;
        else MoveSelection(GRID_COLUMNS);
        break;

    case VK_PRIOR:
        if (!sliders) MoveSelection(-GRID_COLUMNS * 3);
        break;

    case VK_NEXT:
        if (!sliders) MoveSelection(GRID_COLUMNS * 3);
        break;

    default:
        // 1..9 and 0 jump to the first ten tabs.
        if (vk >= '1' && vk <= '9' && vk - '1' < TAB_COUNT)
            g_tab = vk - '1';
        else if (vk == '0' && TAB_COUNT >= 10)
            g_tab = 9;
        break;
    }

    ReleaseSRWLockExclusive(&g_lock);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

struct Style
{
    bool ready;
    IDWriteTextFormat* title;
    IDWriteTextFormat* body;
    IDWriteTextFormat* caption;
    IDWriteTextFormat* tab;
    IDWriteTextFormat* big;
    ID2D1SolidColorBrush* gold;
    ID2D1SolidColorBrush* text;
    ID2D1SolidColorBrush* dim;
    ID2D1SolidColorBrush* line;
    ID2D1SolidColorBrush* tabOff;
    ID2D1SolidColorBrush* tabOn;
    ID2D1SolidColorBrush* cell;
    ID2D1SolidColorBrush* swatch;
    ID2D1SolidColorBrush* barBack;
    ID2D1LinearGradientBrush* panel;
    float scale;
};

static Style g_style = {};
static std::unordered_map<std::wstring, ID2D1Bitmap*> g_icons;

static IDWriteTextFormat* Font(IDWriteFactory* write, float size, DWRITE_TEXT_ALIGNMENT align)
{
    IDWriteTextFormat* f = NULL;
    write->CreateTextFormat(L"Georgia", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);

    if (f)
    {
        f->SetTextAlignment(align);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    return f;
}

static void MakeStyle(const OverlayDrawContext& ctx)
{
    float s = ctx.height / 1080.0f;
    ID2D1DeviceContext* dc = ctx.dc;
    Style& st = g_style;

    st.scale = s;
    st.title = Font(ctx.write, 30 * s, DWRITE_TEXT_ALIGNMENT_CENTER);
    st.body = Font(ctx.write, 20 * s, DWRITE_TEXT_ALIGNMENT_LEADING);
    st.caption = Font(ctx.write, 17 * s, DWRITE_TEXT_ALIGNMENT_CENTER);
    st.tab = Font(ctx.write, 17 * s, DWRITE_TEXT_ALIGNMENT_CENTER);
    st.big = Font(ctx.write, 24 * s, DWRITE_TEXT_ALIGNMENT_CENTER);

    dc->CreateSolidColorBrush(D2D1::ColorF(0.93f, 0.76f, 0.45f), &st.gold);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.93f, 0.91f, 0.87f), &st.text);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.62f, 0.60f, 0.56f), &st.dim);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.47f, 0.32f, 0.8f), &st.line);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.13f, 0.09f, 0.85f), &st.tabOff);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.39f, 0.15f, 0.95f), &st.tabOn);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.11f, 0.10f, 0.09f, 0.75f), &st.cell);
    dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &st.swatch);
    dc->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.22f, 0.18f, 0.9f), &st.barBack);

    // Panel fades in from the left like the game's own side menus.
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2D1::ColorF(0.06f, 0.055f, 0.05f, 0.0f) },
        { 0.12f, D2D1::ColorF(0.06f, 0.055f, 0.05f, 0.82f) },
        { 1.0f, D2D1::ColorF(0.06f, 0.055f, 0.05f, 0.9f) } };
    ID2D1GradientStopCollection* collection = NULL;
    dc->CreateGradientStopCollection(stops, 3, &collection);

    if (collection)
    {
        dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(1, 0)),
            collection, &st.panel);
        collection->Release();
    }

    st.ready = st.title && st.body && st.caption && st.tab && st.big && st.gold && st.text && st.dim &&
        st.line && st.tabOff && st.tabOn && st.cell && st.swatch && st.barBack && st.panel;
}

static void Text(ID2D1DeviceContext* dc, const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, ID2D1Brush* b)
{
    dc->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// Loads at most a few icons per frame so opening a big list does not stutter.
static ID2D1Bitmap* Icon(const std::wstring& path, int* budget)
{
    if (path.empty())
        return NULL;

    auto it = g_icons.find(path);

    if (it != g_icons.end())
        return it->second;

    if (*budget <= 0)
        return NULL;

    --*budget;
    ID2D1Bitmap* bitmap = OverlayLoadImage(path.c_str());
    g_icons[path] = bitmap;
    return bitmap;
}

static bool g_unsupported = false;

void MenuSetUnsupported()
{
    g_unsupported = true;
}

static void DrawNotice(ID2D1DeviceContext* dc, const OverlayDrawContext& ctx)
{
    Style& st = g_style;
    float s = st.scale;
    float x0 = ctx.width - 900 * s, x1 = ctx.width - 24 * s, y0 = 50 * s, y1 = y0 + 260 * s;
    dc->FillRectangle(D2D1::RectF(x0, y0, x1, y1), st.panel);
    Text(dc, L"Character Creator", st.title, D2D1::RectF(x0 + 36 * s, y0 + 24 * s, x1, y0 + 80 * s), st.gold);
    Text(dc, L"This version of the game is not supported by this version of Character Creator. "
        L"Nothing has been changed. Please check the mod page for an update.",
        st.big, D2D1::RectF(x0 + 36 * s, y0 + 96 * s, x1 - 36 * s, y1 - 60 * s), st.text);
    Text(dc, L"[F6] Close", st.big, D2D1::RectF(x0 + 36 * s, y1 - 56 * s, x1, y1 - 16 * s), st.dim);
}

// The restart notice, top right, while the editor is closed.
static void DrawRestartNotice(ID2D1DeviceContext* dc, const OverlayDrawContext& ctx)
{
    if (g_restartNotice.empty() || GetTickCount() - g_restartNoticeAt > RESTART_NOTICE_MS)
    {
        OverlaySetDrawing(false);
        return;
    }

    Style& st = g_style;
    float s = st.scale;
    float x0 = ctx.width - 900 * s, x1 = ctx.width - 24 * s, y0 = 50 * s, y1 = y0 + 120 * s;
    st.panel->SetStartPoint(D2D1::Point2F(x0 - 160 * s, 0));
    st.panel->SetEndPoint(D2D1::Point2F(x1, 0));
    dc->FillRectangle(D2D1::RectF(x0 - 160 * s, y0, x1, y1), st.panel);
    dc->DrawLine(D2D1::Point2F(x0 - 40 * s, y0), D2D1::Point2F(x1, y0), st.line, 1.2f * s);
    dc->DrawLine(D2D1::Point2F(x0 - 40 * s, y1), D2D1::Point2F(x1, y1), st.line, 1.2f * s);
    Text(dc, L"Restart required", st.title, D2D1::RectF(x0, y0 + 10 * s, x1, y0 + 58 * s), st.gold);
    Text(dc, g_restartNotice, st.caption, D2D1::RectF(x0, y0 + 62 * s, x1, y1 - 12 * s), st.text);
}

template <typename T> static void ReleaseDrawing(T*& p)
{
    if (p)
    {
        p->Release();
        p = NULL;
    }
}

// The overlay made its drawing objects anew (the game changed its swap
// chain): everything made with the old ones goes.
static void ForgetDrawing()
{
    Style& st = g_style;
    ReleaseDrawing(st.title);
    ReleaseDrawing(st.body);
    ReleaseDrawing(st.caption);
    ReleaseDrawing(st.tab);
    ReleaseDrawing(st.big);
    ReleaseDrawing(st.gold);
    ReleaseDrawing(st.text);
    ReleaseDrawing(st.dim);
    ReleaseDrawing(st.line);
    ReleaseDrawing(st.tabOff);
    ReleaseDrawing(st.tabOn);
    ReleaseDrawing(st.cell);
    ReleaseDrawing(st.swatch);
    ReleaseDrawing(st.barBack);
    ReleaseDrawing(st.panel);
    st = {};

    for (auto& icon : g_icons)
        ReleaseDrawing(icon.second);

    g_icons.clear();
}

void MenuDraw(const OverlayDrawContext& ctx)
{
    static unsigned generation = 0;

    if (ctx.generation != generation)
    {
        if (generation)
            ForgetDrawing();

        generation = ctx.generation;
    }

    if (!g_style.ready)
    {
        MakeStyle(ctx);

        if (!g_style.ready)
            return;
    }

    if (g_unsupported)
    {
        DrawNotice(ctx.dc, ctx);
        return;
    }

    if (!OverlayVisible())
    {
        AcquireSRWLockExclusive(&g_lock);
        DrawRestartNotice(ctx.dc, ctx);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    AcquireSRWLockExclusive(&g_lock);   // drawing also scrolls the grid

    ID2D1DeviceContext* dc = ctx.dc;
    Style& st = g_style;
    float s = st.scale;

    const Tab& tab = TABS[g_tab];
    const Page& page = CurrentPage();
    std::vector<Item> items;
    int selected;
    BuildItems(page, &items, &selected);

    // Panel on the right
    float x0 = ctx.width - 900 * s, x1 = ctx.width - 24 * s;
    float y0 = 50 * s, y1 = ctx.height - 50 * s;
    float fadeX = x0 - 160 * s;
    st.panel->SetStartPoint(D2D1::Point2F(fadeX, 0));
    st.panel->SetEndPoint(D2D1::Point2F(x1, 0));
    dc->FillRectangle(D2D1::RectF(fadeX, y0, x1, y1), st.panel);
    dc->DrawLine(D2D1::Point2F(fadeX + 120 * s, y0), D2D1::Point2F(x1, y0), st.line, 1.2f * s);
    dc->DrawLine(D2D1::Point2F(fadeX + 120 * s, y1), D2D1::Point2F(x1, y1), st.line, 1.2f * s);

    // Title: "Hair 1/3: Hair 12 / 61"
    wchar_t title[256];

    if (page.kind == PAGE_SLIDERS)
        swprintf_s(title, L"%s  |  %s %d/%d: %s", CHARACTER_NAMES[g_char], TabLabel(tab), g_page[g_tab] + 1,
            tab.pageCount, PageLabel(tab, g_page[g_tab]));
    else
        swprintf_s(title, L"%s  |  %s %d/%d: %s %d / %d", CHARACTER_NAMES[g_char], TabLabel(tab), g_page[g_tab] + 1,
            tab.pageCount, PageLabel(tab, g_page[g_tab]), selected + 1, (int)items.size());

    Text(dc, title, st.title, D2D1::RectF(x0, y0 + 18 * s, x1, y0 + 70 * s), st.gold);

    // Sub-tabs and key guide
    std::wstring subs;

    // Tabs with many pages show only the neighbours of the current one.
    int current = g_page[g_tab];
    int first = 0, last = tab.pageCount - 1;

    if (tab.pageCount > 4)
    {
        first = current > 0 ? current - 1 : 0;
        last = current + 1 < tab.pageCount ? current + 1 : current;

        if (first > 0)
            subs += L"<  ";
    }

    for (int i = first; i <= last; ++i)
    {
        if (i > first) subs += L"  |  ";
        subs += i == current ? std::wstring(L"[ ") + PageLabel(tab, i) + L" ]" : PageLabel(tab, i);
    }

    if (last < tab.pageCount - 1)
        subs += L"  >";

    float gx0 = x0 + 36 * s, gx1 = x1 - 36 * s;
    Text(dc, subs, st.body, D2D1::RectF(gx0, y0 + 84 * s, gx1, y0 + 112 * s), st.text);
    Text(dc, L"Tab: area     Q / E or [ / ]: parameter     Arrows: choose", st.body,
        D2D1::RectF(gx0, y0 + 114 * s, gx1, y0 + 142 * s), st.dim);

    // Tab bar, two rows
    const int perRow = (TAB_COUNT + 1) / 2;
    float tabTop = y0 + 158 * s, tabH = 42 * s, tabGap = 4 * s;
    float tabW = (gx1 - gx0 - tabGap * (perRow - 1)) / perRow;

    for (int i = 0; i < TAB_COUNT; ++i)
    {
        float tx = gx0 + (i % perRow) * (tabW + tabGap);
        float ty = tabTop + (i / perRow) * (tabH + tabGap);
        D2D1_RECT_F r = D2D1::RectF(tx, ty, tx + tabW, ty + tabH);
        dc->FillRectangle(r, i == g_tab ? st.tabOn : st.tabOff);
        Text(dc, i == g_tab ? std::wstring(L"[ ") + TabLabel(TABS[i]) + L" ]" : TabLabel(TABS[i]), st.tab, r,
            i == g_tab ? st.text : st.dim);
    }

    float contentTop = tabTop + 2 * (tabH + tabGap) + 20 * s;
    float footerTop = y1 - 64 * s;

    if (page.kind == PAGE_SLIDERS)
    {
        // One row per value: name, bar, number
        float rowH = 64 * s;

        for (int i = 0; i < page.sliderCount; ++i)
        {
            int index = page.sliders[i];
            bool height = index == SLIDER_HEIGHT;
            int lo = 0, hi = 100, value;

            if (height)
            {
                lo = HEIGHT_MIN;
                hi = HEIGHT_MAX;
                value = IdentityHeight(g_char);
            }
            else
            {
                const DecorationParam& p = g_data.params[index];
                lo = p.known ? p.min : 0;
                hi = p.known && p.max > lo ? p.max : 100;
                value = g_view.decoration[index];
            }

            float ry = contentTop + i * rowH;
            bool on = i == g_sliderRow;

            Text(dc, on ? std::wstring(L"[ ") + page.sliderNames[i] + L" ]" : page.sliderNames[i], st.body,
                D2D1::RectF(gx0, ry, gx0 + 200 * s, ry + 40 * s), on ? st.gold : st.text);

            float bx0 = gx0 + 210 * s, bx1 = gx1 - 90 * s, by = ry + 14 * s;
            float fill = (float)(value - lo) / (float)(hi - lo);
            dc->FillRectangle(D2D1::RectF(bx0, by, bx1, by + 12 * s), st.barBack);
            dc->FillRectangle(D2D1::RectF(bx0, by, bx0 + (bx1 - bx0) * fill, by + 12 * s), on ? st.gold : st.dim);

            wchar_t number[16];
            if (height)
                swprintf_s(number, value ? L"%+d%%" : L"0%%", value);
            else
                swprintf_s(number, L"%d", value);
            Text(dc, number, st.big, D2D1::RectF(bx1 + 10 * s, ry, gx1, ry + 40 * s), on ? st.gold : st.text);
        }

        Text(dc, L"Up / Down: value     Left / Right: adjust     Shift: x10", st.caption,
            D2D1::RectF(gx0, contentTop + page.sliderCount * rowH + 10 * s, gx1, contentTop + page.sliderCount * rowH + 40 * s), st.dim);
    }
    else
    {
        // Grid of options
        float cellW = (gx1 - gx0) / GRID_COLUMNS;
        float cellH = 200 * s;
        int visibleRows = (int)((footerTop - contentTop) / cellH);

        if (visibleRows < 1) visibleRows = 1;

        int selRow = selected < 0 ? 0 : selected / GRID_COLUMNS;

        if (selRow < g_scrollRow) g_scrollRow = selRow;
        if (selRow >= g_scrollRow + visibleRows) g_scrollRow = selRow - visibleRows + 1;

        int budget = 6;

        for (int row = 0; row < visibleRows; ++row)
        {
            for (int col = 0; col < GRID_COLUMNS; ++col)
            {
                int i = (g_scrollRow + row) * GRID_COLUMNS + col;

                if (i >= (int)items.size())
                    break;

                const Item& it = items[i];
                float cx = gx0 + col * cellW, cy = contentTop + row * cellH;
                D2D1_RECT_F box = D2D1::RectF(cx + 8 * s, cy + 4 * s, cx + cellW - 8 * s, cy + cellH - 40 * s);
                bool on = i == selected;

                if (it.hasColor)
                {
                    st.swatch->SetColor(D2D1::ColorF(it.r / 255.0f, it.g / 255.0f, it.b / 255.0f));
                    dc->FillRectangle(box, st.swatch);
                }
                else if (ID2D1Bitmap* icon = Icon(it.icon, &budget))
                {
                    dc->FillRectangle(box, st.cell);
                    D2D1_SIZE_F size = icon->GetSize();
                    float bw = box.right - box.left, bh = box.bottom - box.top;
                    float k = min(bw / size.width, bh / size.height);
                    float iw = size.width * k, ih = size.height * k;
                    float ix = box.left + (bw - iw) / 2, iy = box.top + (bh - ih) / 2;
                    dc->DrawBitmap(icon, D2D1::RectF(ix, iy, ix + iw, iy + ih));
                }
                else
                {
                    dc->FillRectangle(box, st.cell);
                    Text(dc, it.label, st.big, box, on ? st.gold : st.dim);
                }

                if (on)
                    dc->DrawRectangle(box, st.gold, 2.5f * s);

                Text(dc, on ? L"[ " + it.label + L" ]" : it.label, st.caption,
                    D2D1::RectF(cx, cy + cellH - 38 * s, cx + cellW, cy + cellH - 8 * s), on ? st.gold : st.text);
            }
        }

        if (items.empty())
            Text(dc, L"Nothing to choose here for this character", st.big,
                D2D1::RectF(gx0, contentTop, gx1, contentTop + 60 * s), st.dim);
    }

    // Footer
    const wchar_t* note = NULL;

    if (page.kind == PAGE_GENDER || page.kind == PAGE_RACE)
    {
        bool pending = FilterGender() != g_gender || FilterRace() != g_race ||
            (g_gender == GENDER_FEMALE && IdentityFemaleMoves(g_char) != IdentityStartFemaleMoves(g_char));

        note = pending
            ? L"Chosen for the next start. Restart the game to see this gender and race."
            : L"Gender and race change skeleton, animations and body: they apply after restarting the game.";
    }

    if (page.kind == PAGE_SLIDERS && page.sliders[0] == SLIDER_HEIGHT)
    {
        note = IdentityHeight(g_char) != IdentityLoadedHeight(g_char)
            ? L"Preview: restart the game so the body and animations fit this height."
            : L"Height shows at once; after a change, restart the game so the body and animations fit.";
    }

    if (page.kind == PAGE_EYES)
    {
        note = L"Eye colour applies after restarting the game (only your character's eyes change, not NPCs').";
    }

    wchar_t absent[160];

    if (!note && (page.kind == PAGE_COLOR || page.kind == PAGE_TYPE || page.kind == PAGE_SLIDERS) &&
        !GameCharacterHasValues(g_char))
    {
        swprintf_s(absent, L"%s has no colours yet: they are being set up - colours and tattoos work in a moment.",
            CHARACTER_NAMES[g_char]);
        note = absent;
    }

    if (!note && !GameCharacterPresent(g_char))
    {
        swprintf_s(absent, L"%s is not in the game right now: changes are saved and applied when they appear.",
            CHARACTER_NAMES[g_char]);
        note = absent;
    }

    if (note)
        Text(dc, note, st.caption, D2D1::RectF(gx0, footerTop - 34 * s, gx1, footerTop - 6 * s), st.dim);

    dc->DrawLine(D2D1::Point2F(gx0, footerTop), D2D1::Point2F(gx1, footerTop), st.line, 1.0f * s);
    Text(dc, L"[Space] Keep     [Esc] Cancel     [F6] Kliff  [F7] Damiane  [F8] Oongka", st.big, D2D1::RectF(x0, footerTop + 8 * s, x1, y1 - 8 * s), st.text);

    ReleaseSRWLockExclusive(&g_lock);
}
