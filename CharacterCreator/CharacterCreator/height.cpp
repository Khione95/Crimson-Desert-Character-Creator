#include "pch.h"
#include "height.h"
#include "addresses.h"
#include "game.h"
#include "identity.h"
#include "log.h"

static const size_t OWNER = 0x10;           // component / controller -> entity
static const size_t SCALE_OBJECT = 0x1C0;   // component -> scale object
static const size_t BODY_SCALE = 0x84;      // scale object -> float

static const DWORD POLL_MS = 100;

static uintptr_t g_componentVtable = 0;
static uintptr_t g_objectVtable = 0;

struct Found
{
    uintptr_t controller;
    uintptr_t entity;
    uintptr_t component;
    uintptr_t object;
    int searches;           // scans for this controller
    DWORD searchedAt;
};

// A character may still be being built when their controller appears.
static const int SEARCHES_MAX = 3;
static const DWORD SEARCH_RETRY_MS = 10000;

static Found g_found[CHARACTER_COUNT];

static bool ReadPtr(uintptr_t address, uintptr_t* out)
{
    __try
    {
        *out = *(const uintptr_t*)address;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The component still belongs to the entity and still holds the object.
static bool StillValid(const Found& f)
{
    uintptr_t vtable = 0, owner = 0, object = 0, objectVtable = 0;

    return ReadPtr(f.component, &vtable) && vtable == g_componentVtable &&
        ReadPtr(f.component + OWNER, &owner) && owner == f.entity &&
        ReadPtr(f.component + SCALE_OBJECT, &object) && object == f.object &&
        ReadPtr(f.object, &objectVtable) && objectVtable == g_objectVtable;
}

// Looks through one memory region for scale components owned by the wanted
// entities.
static void ScanRegion(uintptr_t start, size_t size, const uintptr_t* entities, uintptr_t* components)
{
    __try
    {
        const uintptr_t* p = (const uintptr_t*)start;
        size_t count = size / sizeof(uintptr_t);

        for (size_t i = 0; i + (SCALE_OBJECT / 8) < count; ++i)
        {
            if (p[i] != g_componentVtable)
                continue;

            uintptr_t owner = p[i + OWNER / 8];

            for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
                if (entities[ch] && owner == entities[ch] && !components[ch])
                    components[ch] = (uintptr_t)(p + i);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void Scan(const uintptr_t* entities, uintptr_t* components)
{
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0x10000;

    while (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)))
    {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE)
            ScanRegion((uintptr_t)mbi.BaseAddress, mbi.RegionSize, entities, components);

        if (next <= address)
            break;

        address = next;
    }
}

// Finds the scale objects of characters whose controller is new.
static void FindObjects()
{
    uintptr_t entities[CHARACTER_COUNT] = {};
    uintptr_t components[CHARACTER_COUNT] = {};
    bool any = false;
    DWORD now = GetTickCount();

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        Found& f = g_found[ch];
        uintptr_t controller = GameController(ch);

        if (controller != f.controller)
            f = { controller };

        if (!controller)
            continue;

        // Only a height that differs from the one the character was built
        // with needs the preview; the search reads gigabytes of memory, and
        // running it for everyone slowed some players' games down.
        if (!f.object && IdentityHeight(ch) == IdentityLoadedHeight(ch))
            continue;

        if (f.object)
        {
            if (StillValid(f))
                continue;

            Log("height: %S's scale object is gone - looking again", CHARACTER_NAMES[ch]);
            f.object = 0;
            f.searches = 0;
        }

        if (f.searches >= SEARCHES_MAX || (f.searches && now - f.searchedAt < SEARCH_RETRY_MS))
            continue;

        if (ReadPtr(controller + OWNER, &f.entity) && f.entity)
        {
            entities[ch] = f.entity;
            any = true;
        }

        ++f.searches;
        f.searchedAt = now;
    }

    if (!any)
        return;

    DWORD started = GetTickCount();
    Scan(entities, components);

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        if (!entities[ch])
            continue;

        Found& f = g_found[ch];
        uintptr_t object = 0, vtable = 0;

        if (components[ch] && ReadPtr(components[ch] + SCALE_OBJECT, &object) && object &&
            ReadPtr(object, &vtable) && vtable == g_objectVtable)
        {
            f.component = components[ch];
            f.object = object;
            Log("height: %S's scale object found (%016llX, %lu ms)", CHARACTER_NAMES[ch], (unsigned long long)object,
                GetTickCount() - started);
        }
        else
        {
            Log("height: %S's scale object not found (%lu ms, try %d of %d)", CHARACTER_NAMES[ch],
                GetTickCount() - started, f.searches, SEARCHES_MAX);
        }
    }
}

static void Apply()
{
    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        Found& f = g_found[ch];
        float base = IdentityBaseScale(ch);

        if (!f.object || base <= 0 || !StillValid(f))
            continue;

        float wanted = base * (100 + IdentityHeight(ch)) / 100.0f;
        float* scale = (float*)(f.object + BODY_SCALE);

        __try
        {
            float current = *scale;

            if (current - wanted > 0.00001f || wanted - current > 0.00001f)
            {
                *scale = wanted;
                Log("height: %S %+d%% (body scale %.6f -> %.6f)", CHARACTER_NAMES[ch], IdentityHeight(ch), current, wanted);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            f.object = 0;
        }
    }
}

static DWORD WINAPI HeightThread(LPVOID)
{
    while (true)
    {
        FindObjects();
        Apply();
        Sleep(POLL_MS);
    }
}

void HeightInit()
{
    g_componentVtable = AddressOf(ADDR_SCALECOMPONENTVTABLE);
    g_objectVtable = AddressOf(ADDR_SCALEOBJECTVTABLE);

    if (!g_componentVtable || !g_objectVtable)
    {
        Log("height: scale classes not found on this game version - height applies after a restart only");
        return;
    }

    HANDLE thread = CreateThread(NULL, 0, HeightThread, NULL, 0, NULL);

    if (thread)
        CloseHandle(thread);
}
