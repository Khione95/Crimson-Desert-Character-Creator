
// CC Research v6 - appearance snapshots, call counters, live watcher,
// write test (F7 hair colour, F8 hairstyle) and data breakpoints (F9)
//
// F10 : snapshot every CharacterCustomizationController in memory
//       (C:\temp\cc_research\snapshot_N.txt) and start watching them. The
//       first F10 also installs the call counters.
// F11 : write a marker into the logs and re-scan for controllers.
//
// While watching, the 250 appearance values and 16 mesh choices of every
// customizable controller are compared ten times a second and every change is
// written to C:\temp\cc_research\watch.log.
//
// Call counters replace entries in the controller / barber vtables with a
// tiny stub that counts the call, remembers its first four arguments and then
// jumps straight to the game's original function. Nothing else is changed.
//
// Output: C:\temp\cc_research\calls.log, research_v3.log, snapshot_N.txt

#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static uintptr_t g_base = 0;
static uintptr_t g_end = 0;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

static FILE* g_log = NULL;
static FILE* g_calls = NULL;
static FILE* g_out = NULL;

static void WriteLine(FILE* f, const char* fmt, va_list args)
{
    if (!f) return;

    vfprintf(f, fmt, args);
    fputc('\n', f);
}

static void Logf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteLine(g_log, fmt, args);
    va_end(args);

    if (g_log) fflush(g_log);
}

static void Callf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteLine(g_calls, fmt, args);
    va_end(args);
}

static void Outf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteLine(g_out, fmt, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Memory helpers
// ---------------------------------------------------------------------------

// The ASI loader is also picked up by helper programs in the game folder.
// Only the game itself should run this plugin.
static bool IsGameProcess()
{
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, path, MAX_PATH);

    const char* name = strrchr(path, '\\');
    name = name ? name + 1 : path;

    return _stricmp(name, "CrimsonDesert.exe") == 0;
}

static bool ReadMem(uintptr_t address, void* out, SIZE_T size)
{
    SIZE_T got = 0;

    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, out, size, &got) &&
        got == size;
}

static bool IsScannable(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE)
        return false;

    DWORD p = mbi.Protect;

    if (p & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    return p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE;
}

static bool IsModulePointer(uint64_t v)
{
    return v >= g_base && v < g_end;
}

// Checks that vtable[-1] points at a CompleteObjectLocator whose type
// descriptor carries the expected class name.
static bool VerifyVtable(uintptr_t vtable, const char* rttiName)
{
    uint64_t colPtr = 0;

    if (!ReadMem(vtable - 8, &colPtr, sizeof(colPtr)))
        return false;

    uint32_t col[6];

    if (!ReadMem((uintptr_t)colPtr, col, sizeof(col)) || col[0] != 1)
        return false;

    char name[128] = { 0 };

    if (!ReadMem(g_base + col[3] + 0x10, name, sizeof(name) - 1))
        return false;

    return strcmp(name, rttiName) == 0;
}

// ---------------------------------------------------------------------------
// Classes of interest (vtable RVAs from the game 2.03.01 dump)
// ---------------------------------------------------------------------------

struct HookedClass
{
    const char* label;
    const char* rtti;
    uintptr_t rva;
    int slotCount;
    uintptr_t vtable;
};

static HookedClass g_hooked[] =
{
    { "ctrl",    ".?AVCharacterCustomizationController@pa@@", 0x559DD98, 136, 0 },
    { "barber",  ".?AVUIGamePlayControlRootBarberShop@uiCommonScript@pa@@", 0x56C5E80, 168, 0 },
    { "itemuse", ".?AVItemUseData_CustomizeCharacter@pa@@", 0x59BF378, 8, 0 },
};

static const int HOOKED_COUNT = sizeof(g_hooked) / sizeof(g_hooked[0]);

static uintptr_t g_controllerVtable = 0;

// ---------------------------------------------------------------------------
// Call counters
// ---------------------------------------------------------------------------

struct SlotStat
{
    volatile LONG64 count;
    uint64_t rcx, rdx, r8, r9;
    uint64_t original;
    int classIndex;
    int slot;
    LONG64 reported;
};

static const int MAX_SLOTS = 512;
static SlotStat* g_stats = NULL;   // allocated, so the stubs can use absolute addresses
static int g_statCount = 0;
static BYTE* g_stubs = NULL;
static const int STUB_SIZE = 48;

static void WriteStub(BYTE* p, SlotStat* stat, uint64_t original)
{
    int i = 0;

    // mov r10, imm64 (&stat)
    p[i++] = 0x49; p[i++] = 0xBA;
    memcpy(p + i, &stat, 8); i += 8;
    // lock inc qword ptr [r10]
    p[i++] = 0xF0; p[i++] = 0x49; p[i++] = 0xFF; p[i++] = 0x02;
    // mov [r10+8], rcx
    p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x4A; p[i++] = 0x08;
    // mov [r10+16], rdx
    p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x52; p[i++] = 0x10;
    // mov [r10+24], r8
    p[i++] = 0x4D; p[i++] = 0x89; p[i++] = 0x42; p[i++] = 0x18;
    // mov [r10+32], r9
    p[i++] = 0x4D; p[i++] = 0x89; p[i++] = 0x4A; p[i++] = 0x20;
    // jmp qword ptr [rip+0] ; original
    p[i++] = 0xFF; p[i++] = 0x25; p[i++] = 0; p[i++] = 0; p[i++] = 0; p[i++] = 0;
    memcpy(p + i, &original, 8); i += 8;

    while (i < STUB_SIZE)
        p[i++] = 0xCC;
}

static void InstallHooks()
{
    g_stats = (SlotStat*)VirtualAlloc(NULL, sizeof(SlotStat) * MAX_SLOTS,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_stubs = (BYTE*)VirtualAlloc(NULL, STUB_SIZE * MAX_SLOTS,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!g_stats || !g_stubs)
    {
        Logf("hook memory allocation failed");
        return;
    }

    for (int c = 0; c < HOOKED_COUNT; ++c)
    {
        HookedClass& hc = g_hooked[c];
        uintptr_t vt = g_base + hc.rva;

        if (!VerifyVtable(vt, hc.rtti))
        {
            Logf("%s: vtable did not verify at %016llX - not hooked", hc.label, (unsigned long long)vt);
            continue;
        }

        hc.vtable = vt;

        if (c == 0)
            g_controllerVtable = vt;

        DWORD oldProtect = 0;

        if (!VirtualProtect((LPVOID)vt, hc.slotCount * 8, PAGE_READWRITE, &oldProtect))
        {
            Logf("%s: VirtualProtect failed", hc.label);
            continue;
        }

        int hooked = 0;

        for (int s = 0; s < hc.slotCount && g_statCount < MAX_SLOTS; ++s)
        {
            uint64_t* entry = (uint64_t*)(vt + s * 8);
            uint64_t original = *entry;

            if (!IsModulePointer(original))
                continue;

            SlotStat* stat = &g_stats[g_statCount];
            BYTE* stub = g_stubs + g_statCount * STUB_SIZE;

            stat->original = original;
            stat->classIndex = c;
            stat->slot = s;
            WriteStub(stub, stat, original);

            *entry = (uint64_t)stub;
            ++g_statCount;
            ++hooked;
        }

        VirtualProtect((LPVOID)vt, hc.slotCount * 8, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), g_stubs, STUB_SIZE * MAX_SLOTS);

        Logf("%s: hooked %d slots of vtable %016llX", hc.label, hooked, (unsigned long long)vt);
    }
}

static void ReportCalls(DWORD now)
{
    if (!g_calls || !g_stats)
        return;

    bool any = false;

    for (int i = 0; i < g_statCount; ++i)
    {
        SlotStat& st = g_stats[i];
        LONG64 count = st.count;

        if (count == st.reported)
            continue;

        Callf("%8lu %s[%d] +%lld (total %lld) rcx=%016llX rdx=%016llX r8=%016llX r9=%016llX",
            now, g_hooked[st.classIndex].label, st.slot,
            count - st.reported, count,
            (unsigned long long)st.rcx, (unsigned long long)st.rdx,
            (unsigned long long)st.r8, (unsigned long long)st.r9);

        st.reported = count;
        any = true;
    }

    if (any)
        fflush(g_calls);
}

// ---------------------------------------------------------------------------
// Snapshot of every controller
// ---------------------------------------------------------------------------

static const int MAX_CONTROLLERS = 600;
static uintptr_t g_controllers[MAX_CONTROLLERS];
static int g_controllerCount = 0;

static void ScanControllers()
{
    g_controllerCount = 0;

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    uintptr_t address = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t maxAddress = (uintptr_t)si.lpMaximumApplicationAddress;

    const SIZE_T CHUNK = 0x100000;
    BYTE* buffer = (BYTE*)VirtualAlloc(NULL, CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!buffer)
        return;

    while (address < maxAddress && g_controllerCount < MAX_CONTROLLERS)
    {
        MEMORY_BASIC_INFORMATION mbi;

        if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        uintptr_t rs = (uintptr_t)mbi.BaseAddress;
        uintptr_t re = rs + mbi.RegionSize;

        if (re <= rs)
            break;

        if (IsScannable(mbi) && rs != (uintptr_t)buffer)
        {
            for (uintptr_t p = rs; p < re; p += CHUNK)
            {
                SIZE_T want = (SIZE_T)(re - p) < CHUNK ? (SIZE_T)(re - p) : CHUNK;

                if (!ReadMem(p, buffer, want))
                    continue;

                const uint64_t* q = (const uint64_t*)buffer;

                for (SIZE_T i = 0; i < want / 8; ++i)
                {
                    if (q[i] == g_controllerVtable && g_controllerCount < MAX_CONTROLLERS)
                        g_controllers[g_controllerCount++] = p + i * 8;
                }
            }
        }

        address = re;
    }

    VirtualFree(buffer, 0, MEM_RELEASE);
}

static void DumpBytes(uintptr_t address, SIZE_T size, const char* indent)
{
    BYTE row[16];

    for (SIZE_T off = 0; off < size; off += 16)
    {
        SIZE_T n = size - off < 16 ? size - off : 16;

        if (!ReadMem(address + off, row, n))
        {
            Outf("%s+%03llX  [unreadable]", indent, (unsigned long long)off);
            return;
        }

        char line[128];
        int len = sprintf_s(line, "%s+%03llX ", indent, (unsigned long long)off);

        for (SIZE_T i = 0; i < n; ++i)
            len += sprintf_s(line + len, sizeof(line) - len, " %02X", row[i]);

        Outf("%s", line);
    }
}

static const SIZE_T CONTROLLER_SIZE = 0x140;

// Dumps the object, then the memory behind every heap pointer it holds.
// A pointer followed by two small counts {count, capacity} is treated as an
// array and dumped in full (up to 4 KB).
static void DumpController(int index, uintptr_t object)
{
    Outf("");
    Outf("  object #%d at %016llX", index, (unsigned long long)object);
    DumpBytes(object, CONTROLLER_SIZE, "    ");

    uint64_t q[CONTROLLER_SIZE / 8];

    if (!ReadMem(object, q, sizeof(q)))
        return;

    for (SIZE_T i = 1; i < CONTROLLER_SIZE / 8; ++i)
    {
        uint64_t v = q[i];

        if (v < 0x10000 || IsModulePointer(v) || (v & 7))
            continue;

        BYTE probe;

        if (!ReadMem((uintptr_t)v, &probe, 1))
            continue;

        SIZE_T size = 0x100;

        if (i + 1 < CONTROLLER_SIZE / 8)
        {
            uint32_t count = (uint32_t)(q[i + 1] & 0xFFFFFFFF);
            uint32_t cap = (uint32_t)(q[i + 1] >> 32);

            if (count > 0 && count <= cap && cap <= 0x1000)
            {
                size = (SIZE_T)cap * 8;

                if (size > 0x1000)
                    size = 0x1000;
            }
        }

        Outf("    pointer at +%03llX -> %016llX (%llu bytes)",
            (unsigned long long)(i * 8), (unsigned long long)v, (unsigned long long)size);
        DumpBytes((uintptr_t)v, size, "      ");
    }
}

static void TakeSnapshot(int number)
{
    char path[MAX_PATH];
    sprintf_s(path, "C:\\temp\\cc_research\\snapshot_%d.txt", number);

    fopen_s(&g_out, path, "w");

    if (!g_out)
    {
        Logf("could not create %s", path);
        return;
    }

    DWORD t0 = GetTickCount();
    ScanControllers();

    Outf("CC RESEARCH v3 SNAPSHOT %d  tick %lu", number, t0);
    Outf("Module base %016llX", (unsigned long long)g_base);
    Outf("Controllers: %d (scan %lu ms)", g_controllerCount, GetTickCount() - t0);

    for (int i = 0; i < g_controllerCount; ++i)
        DumpController(i, g_controllers[i]);

    fclose(g_out);
    g_out = NULL;
}

// ---------------------------------------------------------------------------
// Live watcher
// ---------------------------------------------------------------------------

static const int DECO_COUNT = 250;
static const int MESH_COUNT = 16;
static const int MAX_WATCHED = 128;

struct Watched
{
    uintptr_t object;
    BYTE deco[DECO_COUNT];
    BYTE mesh[MESH_COUNT];
    bool hasMesh;
    bool alive;
};

static Watched g_watched[MAX_WATCHED];
static int g_watchedCount = 0;
static FILE* g_watch = NULL;

static void Watchf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteLine(g_watch, fmt, args);
    va_end(args);

    if (g_watch) fflush(g_watch);
}

// Reads {pointer, count} at object+offset and copies up to size bytes.
static bool ReadArray(uintptr_t object, SIZE_T offset, uint32_t expected, BYTE* out, SIZE_T size)
{
    uint64_t header[2];

    if (!ReadMem(object + offset, header, sizeof(header)))
        return false;

    uint32_t count = (uint32_t)(header[1] & 0xFFFFFFFF);

    if (!header[0] || (expected && count != expected) || count < size)
        return false;

    return ReadMem((uintptr_t)header[0], out, size);
}

static bool ReadWatched(Watched& w, BYTE* deco, BYTE* mesh, bool& hasMesh)
{
    uint64_t vt = 0;

    if (!ReadMem(w.object, &vt, sizeof(vt)) || vt != g_controllerVtable)
        return false;

    if (!ReadArray(w.object, 0xB0, DECO_COUNT, deco, DECO_COUNT))
        return false;

    hasMesh = ReadArray(w.object, 0xA0, 0, mesh, MESH_COUNT);
    return true;
}

static void HexString(const BYTE* data, int count, char* out, SIZE_T outSize)
{
    int len = 0;

    for (int i = 0; i < count && len + 4 < (int)outSize; ++i)
        len += sprintf_s(out + len, outSize - len, "%02X ", data[i]);
}

static void RebuildWatchList(DWORD now)
{
    ScanControllers();

    Watched fresh[MAX_WATCHED];
    int freshCount = 0;

    for (int i = 0; i < g_controllerCount && freshCount < MAX_WATCHED; ++i)
    {
        Watched w = {};
        w.object = g_controllers[i];
        w.alive = true;

        if (!ReadWatched(w, w.deco, w.mesh, w.hasMesh))
            continue;

        bool known = false;

        for (int k = 0; k < g_watchedCount; ++k)
        {
            if (g_watched[k].object == w.object && g_watched[k].alive)
            {
                known = true;
                break;
            }
        }

        if (!known)
        {
            char mesh[64] = { 0 }, deco[64] = { 0 };
            HexString(w.mesh, MESH_COUNT, mesh, sizeof(mesh));
            HexString(w.deco, 16, deco, sizeof(deco));
            Watchf("%8lu NEW %016llX mesh[%s] deco[%s...]",
                now, (unsigned long long)w.object, w.hasMesh ? mesh : "none", deco);
        }

        fresh[freshCount++] = w;
    }

    memcpy(g_watched, fresh, sizeof(Watched) * freshCount);
    g_watchedCount = freshCount;
    Watchf("%8lu watching %d customizable controllers", now, g_watchedCount);
}

static void CheckWatched(DWORD now)
{
    for (int i = 0; i < g_watchedCount; ++i)
    {
        Watched& w = g_watched[i];

        if (!w.alive)
            continue;

        BYTE deco[DECO_COUNT], mesh[MESH_COUNT];
        bool hasMesh = false;

        if (!ReadWatched(w, deco, mesh, hasMesh))
        {
            Watchf("%8lu GONE %016llX", now, (unsigned long long)w.object);
            w.alive = false;
            continue;
        }

        for (int k = 0; k < DECO_COUNT; ++k)
        {
            if (deco[k] != w.deco[k])
                Watchf("%8lu %016llX deco[%d] %d -> %d", now, (unsigned long long)w.object, k, w.deco[k], deco[k]);
        }

        if (hasMesh && w.hasMesh)
        {
            for (int k = 0; k < MESH_COUNT; ++k)
            {
                if (mesh[k] != w.mesh[k])
                    Watchf("%8lu %016llX mesh[%d] %d -> %d", now, (unsigned long long)w.object, k, (int)(signed char)w.mesh[k], (int)(signed char)mesh[k]);
            }
        }

        memcpy(w.deco, deco, DECO_COUNT);
        memcpy(w.mesh, mesh, MESH_COUNT);
        w.hasMesh = hasMesh;
    }
}

// ---------------------------------------------------------------------------
// Write test (F7 / F8)
// ---------------------------------------------------------------------------

static const int HAIR_COLOR_PARAM = 6;  // decorationparam index seen changing in the barber
static const int HAIR_MESH_SLOT = 2;    // meshparam index 2 = Hair

// The player's controller is the one with a 16-entry mesh list whose body
// slot is in use (another controller has a mesh list full of 0xFF).
static uintptr_t FindPlayerController()
{
    if (!g_controllerVtable)
    {
        uintptr_t vt = g_base + g_hooked[0].rva;

        if (!VerifyVtable(vt, g_hooked[0].rtti))
        {
            Logf("controller vtable did not verify");
            return 0;
        }

        g_controllerVtable = vt;
    }

    ScanControllers();

    uintptr_t found = 0;

    for (int i = 0; i < g_controllerCount; ++i)
    {
        BYTE mesh[MESH_COUNT];

        if (!ReadArray(g_controllers[i], 0xA0, MESH_COUNT, mesh, MESH_COUNT))
            continue;

        if (mesh[0] == 0xFF)
            continue;

        Logf("  player candidate %016llX mesh[0..3] %d %d %d %d",
            (unsigned long long)g_controllers[i], mesh[0], mesh[1], mesh[2], mesh[3]);

        if (!found)
            found = g_controllers[i];
    }

    return found;
}

static bool WriteArrayByte(uintptr_t object, SIZE_T offset, int index, BYTE value, BYTE* oldValue)
{
    uint64_t header[2];

    if (!ReadMem(object + offset, header, sizeof(header)) || !header[0])
        return false;

    uint32_t count = (uint32_t)(header[1] & 0xFFFFFFFF);

    if (index >= (int)count)
        return false;

    BYTE* p = (BYTE*)header[0] + index;
    *oldValue = *p;
    *p = value;
    return true;
}

// The game's own appearance functions (game 2.03.01 RVAs).
//   SetDecoration(controller, index, value) : writes one appearance value,
//       marks the controller as changed and refreshes its materials.
//   QueueMeshChange(controller + 0xF8, change) : registers a mesh swap; the
//       barber then writes the new mesh index into the mesh list itself.
static const uintptr_t RVA_SET_DECORATION = 0x72BF80;
static const uintptr_t RVA_QUEUE_MESH_CHANGE = 0x72CFB0;
// Called by the barber right after it copies a saved look into a controller.
static const uintptr_t RVA_REBUILD_APPEARANCE = 0x726C50;
// Readiness check used by the refresh; returns false if refresh would skip.
static const uintptr_t RVA_IS_READY = 0x726570;

typedef void(__fastcall* RebuildAppearanceFn)(uintptr_t controller);
typedef bool(__fastcall* IsReadyFn)(uintptr_t controller);

// The material refresh bumps this counter each time it actually runs.
static uint32_t RefreshCounter(uintptr_t controller)
{
    uint32_t v = 0;
    ReadMem(controller + 0xD4, &v, sizeof(v));
    return v;
}

static void Rebuild(uintptr_t controller, const char* who)
{
    IsReadyFn isReady = (IsReadyFn)(g_base + RVA_IS_READY);
    bool ready = isReady(controller);

    RebuildAppearanceFn rebuild = (RebuildAppearanceFn)(g_base + RVA_REBUILD_APPEARANCE);
    rebuild(controller);

    Logf("%s: ready=%d, rebuild called", who, ready);
}

typedef bool(__fastcall* SetDecorationFn)(uintptr_t controller, uint32_t index, uint8_t value);
typedef void(__fastcall* QueueMeshChangeFn)(uintptr_t queue, const uint8_t* change);

struct MeshChange
{
    uint8_t slot;
    uint8_t oldIndex;
    uint8_t newIndex;
};

// Requests are made on the plugin thread and carried out on the game thread,
// inside a controller function the game calls every frame.
static volatile LONG g_requestHairColor = 0;
static volatile LONG g_requestHairMesh = 0;
static uintptr_t g_targets[4] = {};
static volatile LONG g_targetCount = 0;

typedef uint64_t(__fastcall* ControllerSlotFn)(uint64_t, uint64_t, uint64_t, uint64_t);
static ControllerSlotFn g_originalSlot1 = NULL;
static bool g_gameThreadHookInstalled = false;
static DWORD g_gameThreadId = 0;

static void ApplyHairColor(uintptr_t controller)
{
    BYTE deco[DECO_COUNT];

    if (!ReadArray(controller, 0xB0, DECO_COUNT, deco, DECO_COUNT))
        return;

    uint8_t next = (uint8_t)((deco[HAIR_COLOR_PARAM] + 40) % 200);
    uint32_t before = RefreshCounter(controller);

    SetDecorationFn setDecoration = (SetDecorationFn)(g_base + RVA_SET_DECORATION);
    bool ok = setDecoration(controller, HAIR_COLOR_PARAM, next);

    uint32_t after = RefreshCounter(controller);
    ReadArray(controller, 0xB0, DECO_COUNT, deco, DECO_COUNT);
    Logf("F7: %016llX hair colour %d requested, now %d (setter returned %d, refresh counter %u -> %u, thread %lu)",
        (unsigned long long)controller, next, deco[HAIR_COLOR_PARAM], ok, before, after, GetCurrentThreadId());

    Rebuild(controller, "F7");
}

static uint32_t MeshOptionCount(uintptr_t controller, uint8_t slot)
{
    uint64_t meshTable = 0, slots = 0;
    uint32_t slotCount = 0, optionCount = 0;

    if (!ReadMem(controller + 0x128, &meshTable, sizeof(meshTable)) || !meshTable)
        return 0;

    if (!ReadMem((uintptr_t)meshTable + 0x30, &slotCount, sizeof(slotCount)) || slot >= slotCount)
        return 0;

    if (!ReadMem((uintptr_t)meshTable + 0x28, &slots, sizeof(slots)) || !slots)
        return 0;

    ReadMem((uintptr_t)slots + slot * 0x58 + 8, &optionCount, sizeof(optionCount));
    return optionCount;
}

// Moves one mesh category (0 body, 1 head, 2 hair, 3 beard, 5, 6 eyebrows)
// to its next option, the same way the barber swaps a mesh.
static void ApplyNextMesh(uintptr_t controller, uint8_t slot, const char* who)
{
    BYTE mesh[MESH_COUNT];

    if (!ReadArray(controller, 0xA0, MESH_COUNT, mesh, MESH_COUNT))
        return;

    uint32_t options = MeshOptionCount(controller, slot);

    if (options == 0)
    {
        Logf("%s: %016llX slot %d has no options", who, (unsigned long long)controller, slot);
        return;
    }

    MeshChange change;
    change.slot = slot;
    change.oldIndex = mesh[slot];
    change.newIndex = (uint8_t)((mesh[slot] == 0xFF ? 0 : mesh[slot] + 1) % options);

    QueueMeshChangeFn queueMeshChange = (QueueMeshChangeFn)(g_base + RVA_QUEUE_MESH_CHANGE);
    queueMeshChange(controller + 0xF8, (const uint8_t*)&change);

    BYTE old = 0;
    WriteArrayByte(controller, 0xA0, slot, change.newIndex, &old);

    Logf("%s: %016llX slot %d option %d -> %d of %u (thread %lu)",
        who, (unsigned long long)controller, slot, change.oldIndex, change.newIndex, options, GetCurrentThreadId());

    Rebuild(controller, who);
}

static void ApplyHairMesh(uintptr_t controller)
{
    ApplyNextMesh(controller, HAIR_MESH_SLOT, "F8");
}

static volatile LONG g_requestBody = 0;
static volatile LONG g_requestHead = 0;

static uint64_t __fastcall HookedSlot1(uint64_t self, uint64_t a, uint64_t b, uint64_t c)
{
    LONG count = g_targetCount;

    for (LONG i = 0; i < count; ++i)
    {
        if (g_targets[i] != self)
            continue;

        if (InterlockedExchange(&g_requestHairColor, 0))
            ApplyHairColor((uintptr_t)self);

        if (InterlockedExchange(&g_requestHairMesh, 0))
            ApplyHairMesh((uintptr_t)self);

        if (InterlockedExchange(&g_requestBody, 0))
            ApplyNextMesh((uintptr_t)self, 0, "F3 body");

        if (InterlockedExchange(&g_requestHead, 0))
            ApplyNextMesh((uintptr_t)self, 1, "F4 head");
    }

    return g_originalSlot1(self, a, b, c);
}

static bool InstallGameThreadHook()
{
    if (g_gameThreadHookInstalled)
        return true;

    uintptr_t vt = g_base + g_hooked[0].rva;

    if (!VerifyVtable(vt, g_hooked[0].rtti))
    {
        Logf("controller vtable did not verify - hook not installed");
        return false;
    }

    uint64_t* entry = (uint64_t*)(vt + 1 * 8);
    DWORD oldProtect = 0;

    if (!VirtualProtect(entry, 8, PAGE_READWRITE, &oldProtect))
        return false;

    g_originalSlot1 = (ControllerSlotFn)*entry;
    *entry = (uint64_t)&HookedSlot1;
    VirtualProtect(entry, 8, oldProtect, &oldProtect);

    g_gameThreadHookInstalled = true;
    Logf("game thread hook installed on controller slot 1");
    return true;
}

// Picks every controller with a real mesh list (the player, plus any preview
// copy of the player such as the barber's) as a target.
static void RefreshTargets()
{
    FindPlayerController();

    LONG n = 0;

    for (int i = 0; i < g_controllerCount && n < 4; ++i)
    {
        BYTE mesh[MESH_COUNT];

        if (ReadArray(g_controllers[i], 0xA0, MESH_COUNT, mesh, MESH_COUNT) && mesh[0] != 0xFF)
            g_targets[n++] = g_controllers[i];
    }

    g_targetCount = n;
    Logf("targets: %ld", n);
}

static void TestHairColor()
{
    if (!InstallGameThreadHook())
        return;

    RefreshTargets();
    g_requestHairMesh = 0;
    g_requestHairColor = 1;
}

static void TestMeshSlot(volatile LONG* request)
{
    if (!InstallGameThreadHook())
        return;

    RefreshTargets();
    *request = 1;
}

static void TestHairMesh()
{
    if (!InstallGameThreadHook())
        return;

    RefreshTargets();
    g_requestHairColor = 0;
    g_requestHairMesh = 1;
}

// ---------------------------------------------------------------------------
// Data breakpoints (F9)
//
// Arms the CPU debug registers on every game thread so that a write to the
// hair colour byte or the hairstyle byte of the player / barber preview
// controllers raises a single-step exception. A vectored handler records the
// writing instruction and the return addresses found on the stack, then lets
// the game continue.
// ---------------------------------------------------------------------------

struct WatchHit
{
    int reg;
    uint64_t rip, rsp, rax, rcx, rdx, r8, r9, rbx, rsi, rdi;
    uint64_t returns[24];
    int returnCount;
};

static const int MAX_HITS_LOG = 512;
static WatchHit g_hits[MAX_HITS_LOG];
static volatile LONG g_hitCount = 0;
static int g_hitsWritten = 0;
static uintptr_t g_watchAddress[4] = {};
static const char* g_watchLabel[4] = {};
static FILE* g_bp = NULL;
static DWORD g_mainThreadId = 0;

static LONG CALLBACK BreakpointHandler(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    CONTEXT* c = info->ContextRecord;
    DWORD64 status = c->Dr6 & 0xF;

    if (!status)
        return EXCEPTION_CONTINUE_SEARCH;

    LONG index = InterlockedIncrement(&g_hitCount) - 1;

    if (index < MAX_HITS_LOG)
    {
        WatchHit& h = g_hits[index];
        h.reg = (status & 1) ? 0 : (status & 2) ? 1 : (status & 4) ? 2 : 3;
        h.rip = c->Rip; h.rsp = c->Rsp;
        h.rax = c->Rax; h.rcx = c->Rcx; h.rdx = c->Rdx; h.r8 = c->R8; h.r9 = c->R9;
        h.rbx = c->Rbx; h.rsi = c->Rsi; h.rdi = c->Rdi;
        h.returnCount = 0;

        // Walk the stack of the current thread looking for return addresses
        // into the game module. The stack base comes from the thread block.
        uint64_t stackBase = __readgsqword(0x08);
        uint64_t* sp = (uint64_t*)c->Rsp;

        for (int i = 0; i < 0x400 && (uint64_t)(sp + i) < stackBase && h.returnCount < 24; ++i)
        {
            uint64_t v = sp[i];

            if (v >= g_base + 0x1000 && v < g_base + 0x51EB000)
                h.returns[h.returnCount++] = v;
        }
    }

    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void ArmThreads(bool enable)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

    if (snap == INVALID_HANDLE_VALUE)
        return;

    THREADENTRY32 te = {};
    te.dwSize = sizeof(te);
    DWORD pid = GetCurrentProcessId();
    int armed = 0;

    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == g_mainThreadId)
            continue;

        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
            FALSE, te.th32ThreadID);

        if (!t)
            continue;

        if (SuspendThread(t) != (DWORD)-1)
        {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;

            if (GetThreadContext(t, &c))
            {
                c.Dr0 = g_watchAddress[0];
                c.Dr1 = g_watchAddress[1];
                c.Dr2 = g_watchAddress[2];
                c.Dr3 = g_watchAddress[3];
                c.Dr6 = 0;

                DWORD64 dr7 = 0;

                if (enable)
                {
                    for (int r = 0; r < 4; ++r)
                    {
                        if (!g_watchAddress[r])
                            continue;

                        dr7 |= 1ull << (r * 2);          // local enable
                        dr7 |= 1ull << (16 + r * 4);     // RW = 01 : break on write
                                                          // LEN = 00 : 1 byte
                    }
                }

                c.Dr7 = dr7;

                if (SetThreadContext(t, &c))
                    ++armed;
            }

            ResumeThread(t);
        }

        CloseHandle(t);
    }

    CloseHandle(snap);
    Logf("data breakpoints %s on %d threads", enable ? "armed" : "cleared", armed);
}

static bool ArrayElementAddress(uintptr_t object, SIZE_T offset, int index, uintptr_t* out)
{
    uint64_t header[2];

    if (!ReadMem(object + offset, header, sizeof(header)) || !header[0])
        return false;

    if (index >= (int)(header[1] & 0xFFFFFFFF))
        return false;

    *out = (uintptr_t)header[0] + index;
    return true;
}

static void SetupBreakpoints()
{
    FindPlayerController();   // resolves the vtable and fills g_controllers

    memset(g_watchAddress, 0, sizeof(g_watchAddress));
    int reg = 0;

    for (int i = 0; i < g_controllerCount && reg < 4; ++i)
    {
        BYTE mesh[MESH_COUNT];

        if (!ReadArray(g_controllers[i], 0xA0, MESH_COUNT, mesh, MESH_COUNT) || mesh[0] == 0xFF)
            continue;

        uintptr_t a;

        if (ArrayElementAddress(g_controllers[i], 0xB0, HAIR_COLOR_PARAM, &a))
        {
            g_watchAddress[reg] = a;
            g_watchLabel[reg] = "hair colour";
            Logf("  DR%d = hair colour of %016llX at %016llX", reg, (unsigned long long)g_controllers[i], (unsigned long long)a);
            ++reg;
        }

        if (reg < 4 && ArrayElementAddress(g_controllers[i], 0xA0, HAIR_MESH_SLOT, &a))
        {
            g_watchAddress[reg] = a;
            g_watchLabel[reg] = "hairstyle";
            Logf("  DR%d = hairstyle of %016llX at %016llX", reg, (unsigned long long)g_controllers[i], (unsigned long long)a);
            ++reg;
        }
    }

    ArmThreads(reg > 0);
}

static void FlushHits()
{
    LONG total = g_hitCount;

    if (total > MAX_HITS_LOG)
        total = MAX_HITS_LOG;

    if (!g_bp || g_hitsWritten >= total)
        return;

    for (; g_hitsWritten < total; ++g_hitsWritten)
    {
        WatchHit& h = g_hits[g_hitsWritten];

        fprintf(g_bp, "%lu HIT #%d DR%d (%s at %016llX) rip=%016llX (rva %llX)\n",
            GetTickCount(), g_hitsWritten, h.reg, g_watchLabel[h.reg] ? g_watchLabel[h.reg] : "?",
            (unsigned long long)g_watchAddress[h.reg],
            (unsigned long long)h.rip, (unsigned long long)(h.rip - g_base));
        fprintf(g_bp, "   rax=%016llX rcx=%016llX rdx=%016llX r8=%016llX r9=%016llX\n",
            (unsigned long long)h.rax, (unsigned long long)h.rcx, (unsigned long long)h.rdx,
            (unsigned long long)h.r8, (unsigned long long)h.r9);
        fprintf(g_bp, "   rbx=%016llX rsi=%016llX rdi=%016llX rsp=%016llX\n",
            (unsigned long long)h.rbx, (unsigned long long)h.rsi, (unsigned long long)h.rdi,
            (unsigned long long)h.rsp);
        fprintf(g_bp, "   stack:");

        for (int i = 0; i < h.returnCount; ++i)
            fprintf(g_bp, " %llX", (unsigned long long)(h.returns[i] - g_base));

        fprintf(g_bp, "\n");
    }

    fflush(g_bp);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static bool KeyPressed(int vk, bool& wasDown)
{
    // Edge-detect using the "is down" bit. The "pressed since last call"
    // bit is shared between processes and can be consumed by others.
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

DWORD WINAPI MainThread(LPVOID)
{
    CreateDirectoryA("C:\\temp", NULL);
    CreateDirectoryA("C:\\temp\\cc_research", NULL);

    Sleep(5000);

    g_base = (uintptr_t)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_base;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(g_base + dos->e_lfanew);
    g_end = g_base + nt->OptionalHeader.SizeOfImage;

    fopen_s(&g_log, "C:\\temp\\cc_research\\research_v5.log", "w");
    fopen_s(&g_calls, "C:\\temp\\cc_research\\calls.log", "w");
    fopen_s(&g_watch, "C:\\temp\\cc_research\\watch.log", "w");
    fopen_s(&g_bp, "C:\\temp\\cc_research\\breakpoints.log", "w");

    g_mainThreadId = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, BreakpointHandler);

    Logf("CC RESEARCH v6 - module base %016llX size %llX",
        (unsigned long long)g_base, (unsigned long long)(g_end - g_base));

    bool installed = false;
    bool f10 = false, f11 = false, f7 = false, f8 = false, f9 = false, f3 = false, f4 = false;
    int snapshot = 1;
    int marker = 1;
    DWORD lastReport = GetTickCount();
    DWORD lastWatch = lastReport;
    bool watching = false;

    while (true)
    {
        DWORD now = GetTickCount();

        if (KeyPressed(VK_F10, f10))
        {
            MessageBeep(MB_OK);

            if (!installed)
            {
                InstallHooks();
                installed = true;
            }

            ReportCalls(now);
            Callf("%8lu ===== MARK snapshot %d =====", now, snapshot);
            Logf("F10 - snapshot %d", snapshot);
            TakeSnapshot(snapshot);
            Logf("snapshot %d written (%d controllers)", snapshot, g_controllerCount);
            Watchf("%8lu ===== MARK snapshot %d =====", now, snapshot);
            RebuildWatchList(now);
            watching = true;
            ++snapshot;
            MessageBeep(MB_ICONASTERISK);
        }

        if (KeyPressed(VK_F11, f11))
        {
            ReportCalls(now);
            Callf("%8lu ===== MARK F11 #%d =====", now, marker);
            Watchf("%8lu ===== MARK F11 #%d =====", now, marker);
            Logf("F11 marker %d", marker);

            if (watching)
            {
                CheckWatched(now);
                RebuildWatchList(now);
            }
            ++marker;
            MessageBeep(MB_OK);
        }

        if (KeyPressed(VK_F9, f9))
        {
            MessageBeep(MB_OK);
            Logf("F9 - arming data breakpoints");
            SetupBreakpoints();
        }

        FlushHits();

        if (KeyPressed(VK_F3, f3))
        {
            MessageBeep(MB_OK);
            TestMeshSlot(&g_requestBody);
        }

        if (KeyPressed(VK_F4, f4))
        {
            MessageBeep(MB_OK);
            TestMeshSlot(&g_requestHead);
        }

        if (KeyPressed(VK_F7, f7))
        {
            MessageBeep(MB_OK);
            TestHairColor();
        }

        if (KeyPressed(VK_F8, f8))
        {
            MessageBeep(MB_OK);
            TestHairMesh();
        }

        if (watching && now - lastWatch >= 100)
        {
            CheckWatched(now);
            lastWatch = now;
        }

        if (now - lastReport >= 250)
        {
            ReportCalls(now);
            lastReport = now;
        }

        Sleep(20);
    }

    return 0;
}

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        if (!IsGameProcess())
            return TRUE;

        HANDLE thread = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);

        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
