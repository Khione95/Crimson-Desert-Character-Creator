#include "pch.h"
#include "game.h"
#include "eyes.h"
#include "addresses.h"
#include "log.h"
#include "switches.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Game functions (addresses.h finds them on every game build)
// ---------------------------------------------------------------------------

// CharacterCustomizationController vtable and the slot hooked to get onto a
// game thread. Slot 1 is called for every controller many times per second.
static const char* CONTROLLER_RTTI = ".?AVCharacterCustomizationController@pa@@";
static const int HOOK_SLOT = 1;

// SetDecoration(controller, index, value): writes one appearance value, marks
// the controller as changed and refreshes its materials.
// QueueMeshChange(controller + 0xF8, {slot, old, new}): registers a mesh swap;
// the caller then writes the new option into the mesh list, like the barber.
// Rebuild(controller): what the barber calls after copying a look in.

typedef bool(__fastcall* SetDecorationFn)(uintptr_t controller, uint32_t index, uint8_t value);
typedef void(__fastcall* QueueMeshChangeFn)(uintptr_t queue, const uint8_t* change);
typedef void(__fastcall* RebuildFn)(uintptr_t controller);
typedef uint64_t(__fastcall* SlotFn)(uint64_t, uint64_t, uint64_t, uint64_t);

static uintptr_t g_base = 0;
static uintptr_t g_controllerVtable = 0;
static SetDecorationFn g_setDecoration = NULL;
static QueueMeshChangeFn g_queueMeshChange = NULL;
static RebuildFn g_rebuild = NULL;
static SlotFn g_originalSlot = NULL;

// ---------------------------------------------------------------------------
// Desired look, one per character
// ---------------------------------------------------------------------------

static SRWLOCK g_desiredLock = SRWLOCK_INIT;
static Appearance g_desired[CHARACTER_COUNT] = {};
static AppearanceMask g_mask[CHARACTER_COUNT] = {};
static volatile LONG g_desiredVersion[CHARACTER_COUNT] = { 1, 1, 1 };

static bool ValidCharacter(int ch)
{
    return ch >= 0 && ch < CHARACTER_COUNT;
}

void GameSetDecoration(int ch, int index, uint8_t value)
{
    if (!ValidCharacter(ch) || index < 0 || index >= DECORATION_COUNT)
        return;

    AcquireSRWLockExclusive(&g_desiredLock);
    g_desired[ch].decoration[index] = value;
    g_mask[ch].decoration[index] = true;
    ReleaseSRWLockExclusive(&g_desiredLock);
    InterlockedIncrement(&g_desiredVersion[ch]);
}

void GameSetMesh(int ch, int slot, uint8_t option)
{
    if (!ValidCharacter(ch) || slot < 0 || slot >= MESH_SLOT_COUNT)
        return;

    AcquireSRWLockExclusive(&g_desiredLock);
    g_desired[ch].mesh[slot] = option;
    g_mask[ch].mesh[slot] = true;
    ReleaseSRWLockExclusive(&g_desiredLock);
    InterlockedIncrement(&g_desiredVersion[ch]);
}

void GameSetDesired(int ch, const Appearance& values, const AppearanceMask& mask)
{
    if (!ValidCharacter(ch))
        return;

    AcquireSRWLockExclusive(&g_desiredLock);
    g_desired[ch] = values;
    g_mask[ch] = mask;
    ReleaseSRWLockExclusive(&g_desiredLock);
    InterlockedIncrement(&g_desiredVersion[ch]);
}

void GameGetDesired(int ch, Appearance* values, AppearanceMask* mask)
{
    if (!ValidCharacter(ch))
    {
        *values = {};
        *mask = {};
        return;
    }

    AcquireSRWLockShared(&g_desiredLock);
    *values = g_desired[ch];
    *mask = g_mask[ch];
    ReleaseSRWLockShared(&g_desiredLock);
}

// ---------------------------------------------------------------------------
// Safe memory access (game objects can be freed at any time)
// ---------------------------------------------------------------------------

static bool SafeRead(uintptr_t address, void* out, size_t size)
{
    __try
    {
        memcpy(out, (const void*)address, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Reads a {pointer, uint32 count} array header at object + offset.
static bool ReadArrayHeader(uintptr_t object, size_t offset, uintptr_t* data, uint32_t* count)
{
    uint64_t header[2];

    if (!SafeRead(object + offset, header, sizeof(header)) || !header[0])
        return false;

    *data = (uintptr_t)header[0];
    *count = (uint32_t)(header[1] & 0xFFFFFFFF);
    return true;
}

// Oongka's controller has no appearance values (the game gives him none):
// only his meshes can change.
static bool HasDecorations(uintptr_t controller)
{
    uintptr_t decoration;
    uint32_t count;
    return ReadArrayHeader(controller, 0xB0, &decoration, &count) && count == DECORATION_COUNT;
}

// Reads the mesh choices and, if the controller has them, the appearance
// values (left at 0 otherwise).
static bool ReadLook(uintptr_t controller, Appearance* out)
{
    uintptr_t mesh, decoration;
    uint32_t meshCount, decorationCount;

    if (!ReadArrayHeader(controller, 0xA0, &mesh, &meshCount) || meshCount != MESH_SLOT_COUNT)
        return false;

    memset(out->decoration, 0, sizeof(out->decoration));

    if (ReadArrayHeader(controller, 0xB0, &decoration, &decorationCount) && decorationCount == DECORATION_COUNT &&
        !SafeRead(decoration, out->decoration, DECORATION_COUNT))
        return false;

    return SafeRead(mesh, out->mesh, MESH_SLOT_COUNT);
}

static uint32_t ReadMeshOptionCount(uintptr_t controller, int slot)
{
    // Same bounds the barber checks before a mesh swap.
    uint64_t table = 0, slots = 0;
    uint32_t slotCount = 0, options = 0;

    if (!SafeRead(controller + 0x128, &table, sizeof(table)) || !table)
        return 0;

    if (!SafeRead((uintptr_t)table + 0x30, &slotCount, sizeof(slotCount)) || (uint32_t)slot >= slotCount)
        return 0;

    if (!SafeRead((uintptr_t)table + 0x28, &slots, sizeof(slots)) || !slots)
        return 0;

    SafeRead((uintptr_t)slots + slot * 0x58 + 8, &options, sizeof(options));
    return options;
}

static int CharacterOf(uintptr_t controller, uint32_t* count);

// The player characters' controllers (and preview copies of them, such as the
// barber's) are the ones with a real mesh list: 16 entries, body slot in use -
// or, for Damiane and Oongka, whose body comes from their base character file
// (body "none"), a mesh list the package marked as theirs.
static bool IsPlayerLike(uintptr_t controller)
{
    uint64_t vtable = 0;

    if (!SafeRead(controller, &vtable, sizeof(vtable)) || vtable != g_controllerVtable)
        return false;

    uintptr_t mesh;
    uint32_t count;
    uint8_t body = 0xFF;

    if (!ReadArrayHeader(controller, 0xA0, &mesh, &count) || count != MESH_SLOT_COUNT)
        return false;

    if (!SafeRead(mesh, &body, 1))
        return false;

    uint32_t marker = 0;
    return body != 0xFF || CharacterOf(controller, &marker) >= 0;
}

// ---------------------------------------------------------------------------
// Whose controller: the package gives each character's mesh list a different
// number of hidden options in the marker slot (Kliff +0, Damiane +1, Oongka +2).
// ---------------------------------------------------------------------------

static int g_markerSlot = -1;
static uint32_t g_markerBase = 0;

void GameSetMarker(int slot, uint32_t kliffCount)
{
    g_markerSlot = slot;
    g_markerBase = kliffCount;
}

static int CharacterOf(uintptr_t controller, uint32_t* count)
{
    *count = g_markerSlot >= 0 ? ReadMeshOptionCount(controller, g_markerSlot) : 0;

    if (g_markerSlot < 0 || *count < g_markerBase || *count >= g_markerBase + CHARACTER_COUNT)
        return -1;

    return (int)(*count - g_markerBase);
}

// ---------------------------------------------------------------------------
// Player tracking
// ---------------------------------------------------------------------------

// Controllers seen by the hook are classified once and remembered; the table
// is cleared every few seconds so reused addresses are classified again.
static const int SEEN_SIZE = 2048;
static uintptr_t g_seen[SEEN_SIZE];
static uint8_t g_seenPlayer[SEEN_SIZE];
static DWORD g_seenCleared = 0;

struct Tracked
{
    uintptr_t controller;
    int character;
    DWORD firstSeen;
    DWORD lastSeen;         // last game update of this controller
    DWORD lastCheck;
    LONG appliedVersion;
    DWORD meshChangedAt;
    int recolorStep;        // next entry of RECOLOR_DELAYS_MS, or -1 when done
    bool valuesTried;       // CreateValues attempted
    bool noValuesNoted;     // "waiting for appearance values" logged
    DWORD valuesCreatedAt;  // nothing else is changed for a while after
    LONG updates;           // game updates seen (diagnostics)
    LONG busy;              // updates skipped because another thread was applying
    LONG applied;           // times the look was checked and applied
    bool preview;           // a copy made while the character was updated (shop, barber)
    uint8_t firstBeard;     // preview copies: the beard they started with
    bool beardNoted;
    bool shapePending;      // the first head waits for its face shape rebuild
    bool shapeDone;
    uint8_t snap[0x300];    // research: the controller as last seen while it is built
    DWORD snapAt;
    bool snapped;
};

// Research: while a controller is new, logs which of its 8-byte values change
// and when, to find the moment the game has finished building the character.
// Off in releases (a new controller's changes fill the log).
static const bool RESEARCH_LOG = false;
static const DWORD BUILD_WATCH_MS = 6000, BUILD_WATCH_STEP_MS = 100;

static void WatchBuild(uintptr_t controller, Tracked* t, DWORD now)
{
    if (now - t->firstSeen > BUILD_WATCH_MS || (t->snapped && now - t->snapAt < BUILD_WATCH_STEP_MS))
        return;

    uint8_t current[sizeof(t->snap)];

    if (!SafeRead(controller, current, sizeof(current)))
        return;

    if (!t->snapped)
    {
        Log("build watch %S %016llX: first seen", CHARACTER_NAMES[t->character], (unsigned long long)controller);
    }
    else
    {
        int logged = 0;

        for (size_t off = 0; off < sizeof(current) && logged < 12; off += 8)
        {
            uint64_t before = *(uint64_t*)(t->snap + off), after = *(uint64_t*)(current + off);

            if (before != after)
            {
                Log("build watch %S +%lu ms: +0x%03zX %016llX -> %016llX", CHARACTER_NAMES[t->character], now - t->firstSeen, off,
                    (unsigned long long)before, (unsigned long long)after);
                ++logged;
            }
        }
    }

    memcpy(t->snap, current, sizeof(current));
    t->snapAt = now;
    t->snapped = true;
}

static const int MAX_TRACKED = 8;
static const DWORD PREVIEW_OWNER_ACTIVE_MS = 500;
static Tracked g_tracked[MAX_TRACKED];
static SRWLOCK g_trackedLock = SRWLOCK_INIT;
static bool g_warnedUnknown = false;

// Returns false if the controller belongs to no known character.
static bool Track(uintptr_t controller, DWORD now)
{
    uint32_t count = 0;
    int character = CharacterOf(controller, &count);

    if (character < 0)
    {
        if (!g_warnedUnknown)
        {
            g_warnedUnknown = true;
            Log("a character with %u marker options is not Kliff, Damiane or Oongka (expected %u-%u) - is the mod package mounted?",
                count, g_markerBase, g_markerBase + CHARACTER_COUNT - 1);
        }
        return false;
    }

    AcquireSRWLockExclusive(&g_trackedLock);

    for (int i = 0; i < MAX_TRACKED; ++i)
    {
        if (g_tracked[i].controller == controller)
        {
            ReleaseSRWLockExclusive(&g_trackedLock);
            return true;
        }
    }

    // A preview copy (shop fitting, barber) is made while the character's own
    // controller is still updated; a new character after loading a save or
    // an area is made once the old one has stopped. Decided once, here: while
    // a shop or the barber is open the character's own controller may pause.
    bool preview = false;

    for (int i = 0; i < MAX_TRACKED; ++i)
        if (g_tracked[i].controller && g_tracked[i].character == character && now - g_tracked[i].lastSeen < PREVIEW_OWNER_ACTIVE_MS)
            preview = true;

    // A free slot, or else the one the game has not updated for longest:
    // characters from earlier loads are gone and never updated again.
    int slot = 0;

    for (int i = 0; i < MAX_TRACKED; ++i)
    {
        if (!g_tracked[i].controller)
        {
            slot = i;
            break;
        }

        if (g_tracked[i].lastSeen < g_tracked[slot].lastSeen)
            slot = i;
    }

    g_tracked[slot] = {};
    g_tracked[slot].controller = controller;
    g_tracked[slot].character = character;
    g_tracked[slot].firstSeen = now;
    g_tracked[slot].lastSeen = now;
    g_tracked[slot].recolorStep = -1;
    g_tracked[slot].preview = preview;
    Log("%S found: controller %016llX%s", CHARACTER_NAMES[character], (unsigned long long)controller,
        preview ? " (preview copy)" : "");

    ReleaseSRWLockExclusive(&g_trackedLock);
    return true;
}

static void Untrack(uintptr_t controller)
{
    AcquireSRWLockExclusive(&g_trackedLock);

    for (int i = 0; i < MAX_TRACKED; ++i)
    {
        if (g_tracked[i].controller == controller)
        {
            Log("%S gone: controller %016llX", CHARACTER_NAMES[g_tracked[i].character], (unsigned long long)controller);
            g_tracked[i] = {};
        }
    }

    ReleaseSRWLockExclusive(&g_trackedLock);
}

// A character's own controller: of the ones the game is still updating, the
// one alive the longest (preview copies such as the barber's come and go).
static uintptr_t MainFor(int ch)
{
    uintptr_t best = 0;
    DWORD bestSeen = 0;
    DWORD now = GetTickCount();

    AcquireSRWLockShared(&g_trackedLock);

    for (int i = 0; i < MAX_TRACKED; ++i)
    {
        if (!g_tracked[i].controller || g_tracked[i].character != ch || now - g_tracked[i].lastSeen > 3000)
            continue;

        if (!best || g_tracked[i].firstSeen < bestSeen)
        {
            best = g_tracked[i].controller;
            bestSeen = g_tracked[i].firstSeen;
        }
    }

    ReleaseSRWLockShared(&g_trackedLock);
    return best;
}

uintptr_t GameController(int ch)
{
    return ValidCharacter(ch) ? MainFor(ch) : 0;
}

uintptr_t GameMainController()
{
    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        uintptr_t c = MainFor(ch);

        if (c)
            return c;
    }

    return 0;
}

bool GameCharacterHasValues(int ch)
{
    uintptr_t controller = ValidCharacter(ch) ? MainFor(ch) : 0;
    return !controller || HasDecorations(controller);
}

bool GameCharacterPresent(int ch)
{
    return ValidCharacter(ch) && MainFor(ch) != 0;
}

bool GameReadAppearance(int ch, Appearance* out)
{
    uintptr_t controller = ValidCharacter(ch) ? MainFor(ch) : 0;
    return controller && ReadLook(controller, out);
}

uint32_t GameMeshOptionCount(int ch, int slot)
{
    uintptr_t controller = ValidCharacter(ch) ? MainFor(ch) : 0;
    return controller ? ReadMeshOptionCount(controller, slot) : 0;
}

// ---------------------------------------------------------------------------
// Applying the desired look (runs on a game thread)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Appearance values for characters that have none yet
// ---------------------------------------------------------------------------
//
// The game creates a character's appearance values on their first barber
// visit (Oongka has none until then). The plugin does the same: it grows the
// value array with the game's own byte-array function (so the game can free
// it normally), fills in the character's starting values and sets the count.

// GrowBytes(vector, capacity): {BYTE* data, uint32 count, uint32 capacity}.
typedef bool(__fastcall* GrowBytesFn)(uintptr_t vector, uint32_t capacity);

static uint8_t g_startValues[CHARACTER_COUNT][DECORATION_COUNT];
static bool g_startKnown[CHARACTER_COUNT] = {};

// Values are created once the editor has been opened for the character, or
// on their own well after the character appeared when a look was saved for
// them: created at once, they were made while some players' games were still
// on the loading screen, and the rebuild crashed the game. Without values the
// game shows the saved head without its face shape and nothing of the look.
static volatile LONG g_valuesWanted[CHARACTER_COUNT];
static const DWORD AUTO_VALUES_MS = 45000;

static bool HasSavedLook(int ch)
{
    Appearance desired;
    AppearanceMask mask;
    GameGetDesired(ch, &desired, &mask);

    for (int i = 0; i < MESH_SLOT_COUNT; ++i)
        if (mask.mesh[i])
            return true;

    for (int i = 0; i < DECORATION_COUNT; ++i)
        if (mask.decoration[i])
            return true;

    return false;
}

void GameRequestValues(int ch)
{
    if (ValidCharacter(ch))
        InterlockedExchange(&g_valuesWanted[ch], 1);
}

void GameSetStartValues(int ch, const uint8_t* values)
{
    if (!ValidCharacter(ch))
        return;

    memcpy(g_startValues[ch], values, DECORATION_COUNT);
    g_startKnown[ch] = true;
}

static bool CreateValues(uintptr_t controller, int ch)
{
    uint64_t table = 0;
    uint32_t params = 0;

    if (!g_startKnown[ch] || !SafeRead(controller + 0x130, &table, sizeof(table)) || !table ||
        !SafeRead((uintptr_t)table + 0x50, &params, sizeof(params)) || params < DECORATION_COUNT)
    {
        Log("%S: cannot create appearance values (no parameter table)", CHARACTER_NAMES[ch]);
        return false;
    }

    uintptr_t vector = controller + 0xB0;
    uint64_t header[2];

    if (!SafeRead(vector, header, sizeof(header)) || header[0])
        return false;

    if (!AddressOf(ADDR_GROWBYTES))
        return false;

    ((GrowBytesFn)AddressOf(ADDR_GROWBYTES))(vector, DECORATION_COUNT);

    uintptr_t data = 0;
    uint32_t count = 0;

    if (!SafeRead(vector, &data, sizeof(data)) || !data)
    {
        Log("%S: could not create appearance values", CHARACTER_NAMES[ch]);
        return false;
    }

    memcpy((void*)data, g_startValues[ch], DECORATION_COUNT);
    *(uint32_t*)(vector + 8) = DECORATION_COUNT;
    SafeRead(vector + 8, &count, sizeof(count));
    Log("%S: created appearance values (%u), as a barber visit does", CHARACTER_NAMES[ch], count);
    return true;
}

static const DWORD CHECK_INTERVAL_MS = 1000;
static const DWORD SETTLE_MIN_MS = 5000;    // everyone, after appearing
static const DWORD SETTLE_MS = 15000;       // characters without appearance values
static const DWORD VALUES_SETTLE_MS = 1000;    // after creating a character's values
static const DWORD PREVIEW_SETTLE_MS = 150; // preview copies (shop fitting, barber); 4 ms crashed the barber
static const LONG PREVIEW_SETTLE_UPDATES = 20;  // and as many game updates of the copy
static volatile LONG g_applying = 0;

// A new mesh (hair, body, head...) loads a moment after the swap and comes
// up with default colours. Chosen values are pushed again at these delays.
// Values alone do not reach a body swapped in while the game loads: its skin
// tone and tattoos come with a rebuild (as after any change), and the one
// after the swap ran before the body was there. From REBUILD_STEP on, the
// character is rebuilt again.
static const DWORD RECOLOR_DELAYS_MS[] = { 800, 2000, 4000, 8000, 15000 };
static const int REBUILD_STEP = 3;
static const int RECOLOR_STEPS = sizeof(RECOLOR_DELAYS_MS) / sizeof(RECOLOR_DELAYS_MS[0]);

// Calls the setter for every chosen value, even ones that already match, so
// freshly loaded meshes pick up their colours. Values set in the game's own
// screens (skin tone, tattoos) are pushed again too: they are not among the
// chosen ones, and a swapped body came up without them until the tattoo
// screen was visited.
static void ReapplyChosenValues(uintptr_t controller, int ch, bool rebuild)
{
    Appearance current;

    if (!HasDecorations(controller) || !ReadLook(controller, &current))
        return;

    Appearance desired;
    AppearanceMask mask;
    GameGetDesired(ch, &desired, &mask);

    int count = 0;

    for (int i = 0; i < DECORATION_COUNT; ++i)
    {
        uint8_t value;

        if (mask.decoration[i])
            value = desired.decoration[i];
        else if (current.decoration[i])
            value = current.decoration[i];
        else
            continue;

        g_setDecoration(controller, (uint32_t)i, value);
        ++count;
    }

    if (rebuild)
        g_rebuild(controller);

    if (count)
        Log("re-applied %d values to %S after a mesh change%s", count, CHARACTER_NAMES[ch], rebuild ? " (rebuilt)" : "");
}

// A preview copy builds itself from a default look for a moment after it
// appears and can add that look's beard after the chosen one was set; a
// woman then shows a moustache. The beard it started with (and the base
// look's, option 0) is swapped for the chosen one again at the follow-up steps.
static void RequeueBeard(uintptr_t controller, Tracked* t)
{
    if (!t->preview || !t->beardNoted)
        return;

    Appearance desired;
    AppearanceMask mask;
    GameGetDesired(t->character, &desired, &mask);

    if (!mask.mesh[MESH_BEARD])
        return;

    uint8_t wanted = desired.mesh[MESH_BEARD];
    uint8_t first[3] = { (uint8_t)MESH_BEARD, t->firstBeard, wanted };
    g_queueMeshChange(controller + 0xF8, first);

    if (t->firstBeard != 0)
    {
        uint8_t base[3] = { (uint8_t)MESH_BEARD, 0, wanted };
        g_queueMeshChange(controller + 0xF8, base);
    }

    uintptr_t mesh;
    uint32_t count;

    if (ReadArrayHeader(controller, 0xA0, &mesh, &count) && (uint32_t)MESH_BEARD < count)
        *(uint8_t*)(mesh + MESH_BEARD) = wanted;

    g_rebuild(controller);
    Log("preview: %S's beard swapped again (%d -> %d)", CHARACTER_NAMES[t->character], t->firstBeard, wanted);
}

static const DWORD SHAPE_AFTER_MS = 8000;     // after the last mesh change
static const DWORD SHAPE_SETTLE_MS = 15000;   // after the character appeared

static void ApplyDesired(uintptr_t controller, Tracked* t, DWORD now)
{
    int ch = t->character;
    Appearance current;

    if (!ReadLook(controller, &current))
        return;

    Appearance desired;
    AppearanceMask mask;
    GameGetDesired(ch, &desired, &mask);

    bool preview = t->preview;

    // The head the game puts on while loading (the save's head slot, or the
    // list's first head when the slot is empty) comes without its face shape
    // (skeleton variation): that comes with a real head swap during play.
    // When this pass swaps the head (another head than shown is chosen), it
    // brings the shape; otherwise the head is rebuilt through another one
    // once per load.
    uint8_t shownHead = current.mesh[MESH_HEAD] == MESH_NONE ? 0 : current.mesh[MESH_HEAD];
    bool otherHead = mask.mesh[MESH_HEAD] && desired.mesh[MESH_HEAD] != MESH_NONE && desired.mesh[MESH_HEAD] != shownHead;

    if (!preview && otherHead)
        t->shapeDone = true;

    // Not while the game is still loading and the look is being applied: a
    // head swap right after the other mesh changes froze the loading screen.
    if (!preview && !otherHead && !t->shapeDone)
        t->shapePending = true;

    if (t->shapePending && t->recolorStep < 0 && now - t->meshChangedAt >= SHAPE_AFTER_MS &&
        now - t->firstSeen >= SHAPE_SETTLE_MS && !GameHeadRebuilding(ch))
    {
        t->shapePending = false;
        t->shapeDone = true;
        Log("%S: head from the load, without its face shape - rebuilding it", CHARACTER_NAMES[ch]);
        // No second round: the head is mid-swap for as short a time as
        // possible (opening the inventory during a second round froze the game).
        GameReloadHead(ch, HEAD_AWAY_MS, -1, false);
    }

    // A preview copy (barber, shop) has no head rebuild: both swaps are
    // queued at once, to the next head and back.
    uint32_t headOptions = preview ? ReadMeshOptionCount(controller, MESH_HEAD) : 0;

    // Only for an empty slot: queued the same way from a head the copy already
    // showed, the swap back did not take and the copy kept the next head.
    if (preview && current.mesh[MESH_HEAD] == MESH_NONE && !otherHead && !t->shapeDone && headOptions > 1)
    {
        t->shapeDone = true;
        uint8_t other = (uint8_t)((uint32_t)shownHead + 1 < headOptions ? shownHead + 1 : shownHead - 1);
        uint8_t away[3] = { (uint8_t)MESH_HEAD, shownHead, other };
        uint8_t back[3] = { (uint8_t)MESH_HEAD, other, shownHead };
        g_queueMeshChange(controller + 0xF8, away);
        g_queueMeshChange(controller + 0xF8, back);

        uintptr_t mesh;
        uint32_t count;

        if (ReadArrayHeader(controller, 0xA0, &mesh, &count) && (uint32_t)MESH_HEAD < count)
            *(uint8_t*)(mesh + MESH_HEAD) = shownHead;

        g_rebuild(controller);

        // The head comes back without its skin colour: pushed again as after
        // any mesh change.
        t->meshChangedAt = now;
        t->recolorStep = 0;
        Log("preview: %S's head swapped away and back for its face shape", CHARACTER_NAMES[ch]);
        return;
    }

    int decorationChanges = 0, meshChanges = 0;
    bool decorations = HasDecorations(controller);

    for (int i = 0; decorations && i < DECORATION_COUNT; ++i)
    {
        if (!mask.decoration[i] || current.decoration[i] == desired.decoration[i])
            continue;

        g_setDecoration(controller, (uint32_t)i, desired.decoration[i]);
        ++decorationChanges;
    }

    // The body last: swapped before the head in the same pass, its shape did
    // not fit the new head's neck (a gap until the body was changed again).
    for (int step = 0; step < MESH_SLOT_COUNT; ++step)
    {
        int slot = (step + 1) % MESH_SLOT_COUNT;

        if (!mask.mesh[slot] || current.mesh[slot] == desired.mesh[slot])
            continue;

        // MESH_NONE removes the part (the barber accepts it without a range check).
        uint32_t options = ReadMeshOptionCount(controller, slot);

        if (desired.mesh[slot] != MESH_NONE && desired.mesh[slot] >= options)
            continue;

        // Logged before the change, so a crash in it names the slot.
        Log("  %S mesh %d: %d -> %d", CHARACTER_NAMES[ch], slot, current.mesh[slot], desired.mesh[slot]);
        // An empty slot that has options shows its option 0 (a woman's empty
        // beard slot shows a beard); the swap has to name that part to remove it.
        uint8_t shown = current.mesh[slot] == MESH_NONE && options ? 0 : current.mesh[slot];
        uint8_t change[3] = { (uint8_t)slot, shown, desired.mesh[slot] };
        g_queueMeshChange(controller + 0xF8, change);

        // A preview copy (shop, barber) starts from a default look and may add
        // that look's beard after this swap, while it builds itself; the beard
        // it started with is swapped out again a little later (see
        // RequeueBeard). Its base look's beard (option 0) is named too.
        if (slot == MESH_BEARD && preview)
        {
            if (!t->beardNoted)
            {
                t->firstBeard = shown;
                t->beardNoted = true;
            }

            if (shown != 0 && options)
            {
                uint8_t baseBeard[3] = { (uint8_t)slot, 0, desired.mesh[slot] };
                g_queueMeshChange(controller + 0xF8, baseBeard);
            }
        }

        uintptr_t mesh;
        uint32_t count;

        if (ReadArrayHeader(controller, 0xA0, &mesh, &count) && (uint32_t)slot < count)
            *(uint8_t*)(mesh + slot) = desired.mesh[slot];

        ++meshChanges;
    }

    if (!decorationChanges && !meshChanges)
        return;

    g_rebuild(controller);

    if (meshChanges)
    {
        t->meshChangedAt = now;
        t->recolorStep = 0;
    }

    Log("applied %d appearance values and %d meshes to %S", decorationChanges, meshChanges, CHARACTER_NAMES[ch]);

    // Research: every mesh slot and how many options it has.
    if (meshChanges && RESEARCH_LOG)
    {
        char line[256];
        int len = sprintf_s(line, "  meshes now:");

        for (int s = 0; s < MESH_SLOT_COUNT; ++s)
            len += sprintf_s(line + len, sizeof(line) - len, " %d/%u", current.mesh[s] == desired.mesh[s] || !mask.mesh[s]
                ? current.mesh[s] : desired.mesh[s], ReadMeshOptionCount(controller, s));

        Log("%s", line);
    }

    // The game may clamp a value (for example a colour beyond the palette).
    // Accept what it settled on, otherwise it would be re-applied forever -
    // judged on the character's own controller, never on a preview.
    if (preview || controller != MainFor(ch))
        return;

    Appearance result;

    if (!ReadLook(controller, &result))
        return;

    AcquireSRWLockExclusive(&g_desiredLock);

    for (int i = 0; decorations && i < DECORATION_COUNT; ++i)
    {
        if (g_mask[ch].decoration[i] && g_desired[ch].decoration[i] == desired.decoration[i] &&
            result.decoration[i] != desired.decoration[i])
        {
            Log("  value %d: game kept %d instead of %d", i, result.decoration[i], desired.decoration[i]);
            g_desired[ch].decoration[i] = result.decoration[i];
        }
    }

    for (int slot = 0; slot < MESH_SLOT_COUNT; ++slot)
    {
        if (g_mask[ch].mesh[slot] && g_desired[ch].mesh[slot] == desired.mesh[slot] &&
            result.mesh[slot] != desired.mesh[slot])
        {
            Log("  mesh %d: game kept %d instead of %d", slot, result.mesh[slot], desired.mesh[slot]);
            g_desired[ch].mesh[slot] = result.mesh[slot];
        }
    }

    ReleaseSRWLockExclusive(&g_desiredLock);
}

// ---------------------------------------------------------------------------
// Head rebuild: the game reads the eye files again when the head is rebuilt
// ---------------------------------------------------------------------------
//
// Found by testing:
//  - re-queueing the same head, or going away and back at once, reuses the
//    loaded head (old eyes);
//  - the other head has to stay about a second, otherwise coming back also
//    reuses the old head;
//  - the head comes back without its skin colour, and re-sending the same
//    value does not fix it: the value is moved one step and back;
//  - a rebuild right after another (colours picked quickly) can come back to
//    eyes the game still holds: it is done again, away for longer, when the
//    eyes were not read on the way back.

enum HeadPhase { HEAD_IDLE, HEAD_AWAY, HEAD_BACK, HEAD_NUDGED };

static const int SKIN_COLOR = 22;
static const DWORD SKIN_NUDGE_AFTER_MS = 1200;
static const DWORD SKIN_RESTORE_AFTER_MS = 100;
static const int RETRY_AWAY_MS = 2500;

static volatile LONG g_headRequested[CHARACTER_COUNT] = {};
static volatile LONG g_headAwayMs[CHARACTER_COUNT] = { HEAD_AWAY_MS, HEAD_AWAY_MS, HEAD_AWAY_MS };
static volatile LONG g_headAwayOption[CHARACTER_COUNT] = { -1, -1, -1 };   // -1 = a neighbouring head
static HeadPhase g_headPhase = HEAD_IDLE;
static int g_headCharacter = -1;
static uint8_t g_headReturn = 0, g_headAway = 0, g_skin = 0;
static DWORD g_headAt = 0;
static LONG g_eyeReadsAtBack = 0;
static volatile LONG g_headRetried[CHARACTER_COUNT] = {};

void GameReloadHead(int ch, int awayMs, int awayOption, bool retry)
{
    if (!ValidCharacter(ch))
        return;

    InterlockedExchange(&g_headAwayMs[ch], awayMs);
    InterlockedExchange(&g_headAwayOption[ch], awayOption);
    InterlockedExchange(&g_headRetried[ch], retry ? 0 : 1);
    InterlockedExchange(&g_headRequested[ch], 1);
}

static HeadChooser g_headChooser = NULL;

void GameSetHeadChooser(HeadChooser chooser)
{
    g_headChooser = chooser;
}

bool GameHeadRebuilding(int ch)
{
    return ValidCharacter(ch) && (g_headRequested[ch] || (g_headPhase != HEAD_IDLE && g_headCharacter == ch));
}

static void SwitchHead(uintptr_t controller, uint8_t from, uint8_t to)
{
    uint8_t change[3] = { (uint8_t)MESH_HEAD, from, to };
    g_queueMeshChange(controller + 0xF8, change);

    uintptr_t mesh;
    uint32_t count;

    if (ReadArrayHeader(controller, 0xA0, &mesh, &count) && (uint32_t)MESH_HEAD < count)
        *(uint8_t*)(mesh + MESH_HEAD) = to;

    g_rebuild(controller);
}

// Starts the rebuild. Returns false if the head cannot be switched.
static bool StartHeadReload(uintptr_t controller, int ch, DWORD now)
{
    Appearance current;

    if (!ReadLook(controller, &current))
        return false;

    uint8_t head = current.mesh[MESH_HEAD];
    uint32_t options = ReadMeshOptionCount(controller, MESH_HEAD);

    // An empty slot shows option 0.
    if (head == MESH_NONE)
        head = 0;

    if (options < 2)
        return false;

    LONG away = g_headAwayOption[ch];

    if (away < 0 && g_headChooser)
        away = g_headChooser(ch, head);

    if (away < 0 || away == head || (uint32_t)away >= options)
        away = head + 1 < (int)options ? head + 1 : head - 1;

    SwitchHead(controller, head, (uint8_t)away);
    g_headCharacter = ch;
    g_headReturn = head;
    g_headAway = (uint8_t)away;
    g_skin = current.decoration[SKIN_COLOR];
    g_headAt = now;
    g_headPhase = HEAD_AWAY;
    Log("head rebuild (%S): head %d -> %d", CHARACTER_NAMES[ch], head, (int)away);
    return true;
}

// Once per request, unless another one is already waiting.
static void RetryIfEyesKept(int ch)
{
    if (EyesReadCount(ch) != g_eyeReadsAtBack || g_headRequested[ch] || InterlockedExchange(&g_headRetried[ch], 1))
        return;

    Log("head rebuild (%S): the eyes were not read again - once more, away for longer", CHARACTER_NAMES[ch]);
    InterlockedExchange(&g_headAwayMs[ch], RETRY_AWAY_MS);
    InterlockedExchange(&g_headRequested[ch], 1);
}

// Returns true while this character's rebuild is in progress (its look must
// not be enforced in between).
static bool StepHeadReload(uintptr_t controller, Tracked* t, DWORD now)
{
    int ch = t->character;

    if (g_headPhase == HEAD_IDLE)
    {
        if (!InterlockedExchange(&g_headRequested[ch], 0))
            return false;

        return StartHeadReload(controller, ch, now);
    }

    if (ch != g_headCharacter)
        return false;   // another character's rebuild is running; this one waits

    switch (g_headPhase)
    {
    case HEAD_AWAY:
        if (now - g_headAt < (DWORD)g_headAwayMs[ch])
            return true;

        g_eyeReadsAtBack = EyesReadCount(ch);
        SwitchHead(controller, g_headAway, g_headReturn);
        g_headAt = now;
        g_headPhase = HEAD_BACK;
        t->meshChangedAt = now;
        t->recolorStep = 0;
        Log("head rebuild (%S): back to head %d", CHARACTER_NAMES[ch], g_headReturn);
        return true;

    case HEAD_BACK:
        if (now - g_headAt < SKIN_NUDGE_AFTER_MS)
            return true;

        if (!HasDecorations(controller))
        {
            g_headPhase = HEAD_IDLE;
            g_headCharacter = -1;
            Log("head rebuild (%S): done", CHARACTER_NAMES[ch]);
            RetryIfEyesKept(ch);
            return false;
        }

        g_setDecoration(controller, SKIN_COLOR, (uint8_t)(g_skin > 0 ? g_skin - 1 : g_skin + 1));
        g_rebuild(controller);
        g_headAt = now;
        g_headPhase = HEAD_NUDGED;
        return true;

    case HEAD_NUDGED:
        if (now - g_headAt < SKIN_RESTORE_AFTER_MS)
            return true;

        g_setDecoration(controller, SKIN_COLOR, g_skin);
        g_rebuild(controller);
        g_headPhase = HEAD_IDLE;
        g_headCharacter = -1;
        Log("head rebuild (%S): done (skin colour %d restored)", CHARACTER_NAMES[ch], g_skin);
        RetryIfEyesKept(ch);
        return false;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Controller updates
// ---------------------------------------------------------------------------

// Diagnostics: every controller with a full mesh list, the first time it is
// seen (a few dozen at most), with its marker count.
static void LogCandidate(uintptr_t controller)
{
    static const int MAX_LOGGED = 30;
    static uintptr_t logged[MAX_LOGGED];
    static int count = 0;

    uintptr_t mesh;
    uint32_t slots;
    uint8_t look[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

    if (count >= MAX_LOGGED || !ReadArrayHeader(controller, 0xA0, &mesh, &slots) || slots != MESH_SLOT_COUNT)
        return;

    for (int i = 0; i < count; ++i)
        if (logged[i] == controller)
            return;

    logged[count++] = controller;
    SafeRead(mesh, look, sizeof(look));
    uint32_t marker = 0;
    int ch = CharacterOf(controller, &marker);
    uint32_t kind = 0;
    SafeRead(controller + 0x1C, &kind, sizeof(kind));
    Log("controller %016llX: marker options %u (%S), body %d head %d hair %d, player-like %d, kind %u",
        (unsigned long long)controller, marker, ch >= 0 ? CHARACTER_NAMES[ch] : L"unknown",
        look[0], look[1], look[2], IsPlayerLike(controller) ? 1 : 0, kind);
}

static void OnControllerUpdate(uintptr_t controller)
{
    DWORD now = GetTickCount();

    if (now - g_seenCleared > 5000)
    {
        memset(g_seen, 0, sizeof(g_seen));
        g_seenCleared = now;
    }

    size_t h = (controller >> 6) % SEEN_SIZE;

    if (g_seen[h] != controller)
    {
        g_seen[h] = controller;
        g_seenPlayer[h] = IsPlayerLike(controller) && Track(controller, now) ? 1 : 0;
        LogCandidate(controller);
    }

    if (!g_seenPlayer[h])
        return;

    AcquireSRWLockShared(&g_trackedLock);
    Tracked* t = NULL;

    for (int i = 0; i < MAX_TRACKED; ++i)
    {
        if (g_tracked[i].controller == controller)
            t = &g_tracked[i];
    }

    ReleaseSRWLockShared(&g_trackedLock);

    // Seen even when another thread is applying, so a character updated at
    // the same moment as another still counts as present.
    if (t)
    {
        t->lastSeen = now;
        InterlockedIncrement(&t->updates);
    }

    // Only one thread applies at a time.
    if (InterlockedCompareExchange(&g_applying, 1, 0) != 0)
    {
        if (t)
            InterlockedIncrement(&t->busy);
        return;
    }

    // Preview copies (a shop's fitting, the barber's) get the chosen look too,
    // after the same settling time: otherwise a shop shows the look the game
    // has on record (for a woman Kliff, a male body). Changing them at once
    // crashed the barber, before the settling time existed. disable.txt
    // "previews" leaves them alone.
    if (t && RESEARCH_LOG)
        WatchBuild(controller, t, now);

    bool preview = t && t->preview;

    if (preview && PartDisabled("previews"))
        t = NULL;

    // A character is being built for a while after they appear; the game
    // gives them their appearance values once done. Changing meshes before
    // that crashed the game. Characters it never gives values to (Oongka) are
    // left alone until they have been around a while.
    // Preview copies wait much less. The game sets them up about 0.1 s after
    // they appear (owner at +0x10) and processes their build list by about
    // 0.3 s; the barber's copy has its values from the start, and changing its
    // meshes at once (19 ms) crashed the game.
    DWORD settle = !HasDecorations(controller) ? SETTLE_MS : preview ? PREVIEW_SETTLE_MS : SETTLE_MIN_MS;
    uintptr_t owner = 0;

    // Counted in the game's own updates of the copy as well, so a slower PC
    // (fewer updates per second) waits longer: 0.15 s was about 20 updates.
    if (t && (now - t->firstSeen < settle ||
        (preview && (t->updates < PREVIEW_SETTLE_UPDATES || !SafeRead(controller + 0x10, &owner, sizeof(owner)) || !owner))))
        t = NULL;

    // Settled without appearance values: create them once (not for preview
    // copies; disable.txt "values" leaves it to the barber). The look is
    // applied at the next check, not in the same frame as the rebuild.
    bool wanted = t && (g_valuesWanted[t->character] ||
        (now - t->firstSeen >= AUTO_VALUES_MS && HasSavedLook(t->character)));

    if (t && !preview && wanted && !HasDecorations(controller) && !t->valuesTried &&
        !PartDisabled("values"))
    {
        t->valuesTried = true;

        if (!g_valuesWanted[t->character])
            Log("%S: no appearance values after %lu s - creating them for the saved look", CHARACTER_NAMES[t->character],
                AUTO_VALUES_MS / 1000);

        if (CreateValues(controller, t->character))
        {
            // Used up: after another load it takes the editor again.
            InterlockedExchange(&g_valuesWanted[t->character], 0);
            Log("%S: rebuilding with the new appearance values", CHARACTER_NAMES[t->character]);
            g_rebuild(controller);
            Log("%S: rebuilt", CHARACTER_NAMES[t->character]);
            t->valuesCreatedAt = now;
            t = NULL;
        }
    }

    // Values just created: the character is rebuilt first, the look follows.
    if (t && t->valuesCreatedAt && now - t->valuesCreatedAt < VALUES_SETTLE_MS)
        t = NULL;

    // Without appearance values the look is not applied: changing the meshes
    // of such a character (while a save loads) crashed the game. They get
    // values from the editor (F6-F8) or a barber visit.
    if (t && !HasDecorations(controller))
    {
        if (!t->noValuesNoted)
        {
            t->noValuesNoted = true;
            Log("%S has no appearance values yet - the look is applied once the editor is opened for them (or after a barber visit)",
                CHARACTER_NAMES[t->character]);
        }

        t = NULL;
    }

    // A head rebuild is for the character's own controller.
    if (t && !preview && StepHeadReload(controller, t, now))
        t = NULL;

    if (t)
    {
        LONG version = g_desiredVersion[t->character];

        // Apply at once after a change, and re-check once a second so a look
        // the game restored from the save is corrected again.
        if (t->appliedVersion != version || now - t->lastCheck >= CHECK_INTERVAL_MS)
        {
            t->lastCheck = now;
            t->appliedVersion = version;
            InterlockedIncrement(&t->applied);

            if (IsPlayerLike(controller))
                ApplyDesired(controller, t, now);
            else
                Untrack(controller);
        }
        else if (t->recolorStep >= 0 && now - t->meshChangedAt >= RECOLOR_DELAYS_MS[t->recolorStep])
        {
            RequeueBeard(controller, t);
            ReapplyChosenValues(controller, t->character, t->recolorStep >= REBUILD_STEP);

            if (++t->recolorStep >= RECOLOR_STEPS)
                t->recolorStep = -1;
        }
    }

    InterlockedExchange(&g_applying, 0);
}

static uint64_t __fastcall HookedSlot(uint64_t self, uint64_t a, uint64_t b, uint64_t c)
{
    OnControllerUpdate((uintptr_t)self);
    return g_originalSlot(self, a, b, c);
}

void GameLogCharacter(int ch)
{
    DWORD now = GetTickCount();
    uintptr_t main = MainFor(ch);
    int found = 0;

    AcquireSRWLockShared(&g_trackedLock);

    for (int i = 0; i < MAX_TRACKED; ++i)
    {
        const Tracked& t = g_tracked[i];

        if (!t.controller || t.character != ch)
            continue;

        ++found;
        Log("  %S controller %016llX%s: seen %lu ms ago, %ld updates (%ld while busy), checked %ld times, head rebuild %s",
            CHARACTER_NAMES[ch], (unsigned long long)t.controller, t.controller == main ? " (main)" : "",
            now - t.lastSeen, t.updates, t.busy, t.applied,
            g_headRequested[ch] ? "requested" : g_headCharacter == ch ? "running" : "idle");
    }

    ReleaseSRWLockShared(&g_trackedLock);

    if (!found)
        Log("  %S: no controller known", CHARACTER_NAMES[ch]);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

// Checks that vtable[-1] points at RTTI naming the expected class, so a game
// update that moves things is detected instead of crashing.
static bool VerifyVtable(uintptr_t vtable, const char* rttiName)
{
    uint64_t col = 0;
    uint32_t locator[6];
    char name[128] = { 0 };

    if (!SafeRead(vtable - 8, &col, sizeof(col)) ||
        !SafeRead((uintptr_t)col, locator, sizeof(locator)) || locator[0] != 1)
        return false;

    if (!SafeRead(g_base + locator[3] + 0x10, name, sizeof(name) - 1))
        return false;

    return strcmp(name, rttiName) == 0;
}

bool GameInit()
{
    g_base = (uintptr_t)GetModuleHandleA(NULL);
    g_controllerVtable = AddressOf(ADDR_CONTROLLERVTABLE);

    if (!g_controllerVtable || !VerifyVtable(g_controllerVtable, CONTROLLER_RTTI))
    {
        Log("ERROR: this game version is not supported (controller class not found)");
        return false;
    }

    g_setDecoration = (SetDecorationFn)AddressOf(ADDR_SETDECORATION);
    g_queueMeshChange = (QueueMeshChangeFn)AddressOf(ADDR_QUEUEMESHCHANGE);
    g_rebuild = (RebuildFn)AddressOf(ADDR_REBUILD);

    if (!g_setDecoration || !g_queueMeshChange || !g_rebuild)
    {
        Log("ERROR: this game version is not supported (appearance functions not found)");
        return false;
    }

    uint64_t* entry = (uint64_t*)(g_controllerVtable + HOOK_SLOT * 8);
    DWORD oldProtect = 0;

    if (!VirtualProtect(entry, 8, PAGE_READWRITE, &oldProtect))
    {
        Log("ERROR: could not hook the controller");
        return false;
    }

    g_originalSlot = (SlotFn)*entry;
    *entry = (uint64_t)&HookedSlot;
    VirtualProtect(entry, 8, oldProtect, &oldProtect);

    Log("game hooks installed (module base %016llX)", (unsigned long long)g_base);
    return true;
}
