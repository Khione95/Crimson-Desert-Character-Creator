#include "pch.h"
#include "glide.h"
#include "addresses.h"
#include "game.h"
#include "log.h"

#include <string.h>

#include <initializer_list>

// ---------------------------------------------------------------------------
// Each character glides their own way: Kliff with the crow wings, Damiane
// with her glider, Oongka with his rocket - whatever their gender.
//
// The action charts keep variants of their states per action type: the
// character's control component holds the type (+0x18: 0 for Kliff,
// hashlittle("phw") for Damiane, hashlittle("oongka") for Oongka), built from
// the character table - which the gender choice changes, so a woman gets
// Damiane's walk, but also her glider. When the chart moves to a new state,
// the game reads the type once and takes that state's variant. This hook sits
// on that read: entering a glide state, the character being played gets their
// own type; every other state keeps the type the gender gave.
// ---------------------------------------------------------------------------

// mov rbx,[rax+40h] / mov ebx,[rbx+18h] / mov [rsp+30h],ebx / mov rax,[rcx] /
// mov dl,2 / call [rax+188h]: in the state change (rva 1EC373C on the known
// build). The first 7 bytes are replaced by a jump.
static const unsigned char STATE_CHANGE_CODE[] = {
    0x48, 0x8B, 0x58, 0x40, 0x8B, 0x5B, 0x18, 0x89, 0x5C, 0x24, 0x30, 0x48, 0x8B, 0x01, 0xB2, 0x02,
    0xFF, 0x90, 0x88, 0x01, 0x00, 0x00 };
static const size_t PATCH_SIZE = 7;
static const uintptr_t KNOWN_RVA = 0x1EC373C;

// The flight states (basic_upper_glide / basic_lower_glide), by the id at
// +0x18 of the state ([r15+0x10] at the hook): starting, gliding, fast and
// directional flight, the dash, slow glide, running out of stamina, starting
// again after a cancel, and the start of the landing. The landing's last
// states (E4EF0545, and the lower body's A2F1D2E7) are left out: the chart
// hands back to walking there, and a flight variant then crashed the game.
static const uint32_t GLIDE_STATES[] = {
    0x9C0C7E5B, 0x15914F6D, 0xEB340761, 0x1445ED2D, 0xD28ABCEC, 0x766AA1E9, 0x164E8B07, 0x8ED159F1,
    0x4ABEBF9D, 0x1D49E726, 0x05615459, 0x01EBBDBD, 0xFE7C74E7, 0x2DCECD55, 0x815B8344, 0xB3BC3663,
    0x3AC4A94E, 0x61B7853E, 0xE906B0EA, 0x36D65FB4 };

// Research (command.txt): "glidelog 1" logs the states the player's
// characters enter; "glidestates <id> ..." adds states to the list above.
static const int EXTRA_MAX = 32;
static volatile LONG g_extraCount = 0;
static uint32_t g_extra[EXTRA_MAX];
static volatile LONG g_logStates = 0;

static const uint32_t OWN_TYPE[CHARACTER_COUNT] = { 0, 0x4CB714A1, 0xEDBDEF3D };

static uintptr_t g_containerVtable = 0;

static uint32_t StateId(uintptr_t holder)
{
    __try
    {
        uintptr_t state = holder ? *(uintptr_t*)(holder + 0x10) : 0;
        return state ? *(uint32_t*)(state + 0x18) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// Which of the player's characters the component belongs to, -1 for anyone
// else. The component's actor (+0x08) is a child of the player's actor
// (+0xA0), whose child container component lists them (+0x18: actor,
// flags pairs) as Kliff, Damiane, Oongka, then the others.
static int CharacterOf(uintptr_t component)
{
    __try
    {
        uintptr_t actor = *(uintptr_t*)(component + 0x08);
        uintptr_t user = actor ? *(uintptr_t*)(actor + 0xA0) : 0;
        uintptr_t* components = user ? *(uintptr_t**)(user + 0x68) : NULL;

        if (!components || !g_containerVtable)
            return -1;

        for (int i = 0; i < 16; ++i)
        {
            if (!components[i] || *(uintptr_t*)components[i] != g_containerVtable)
                continue;

            uintptr_t* children = *(uintptr_t**)(components[i] + 0x18);

            for (int c = 0; children && c < CHARACTER_COUNT && children[c * 2]; ++c)
                if (children[c * 2] == actor)
                    return c;

            return -1;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return -1;
}

// Called from the jump: component (rbx before the read), the state holder
// (r15). Returns the type the state change goes on with.
static uint32_t __cdecl ChooseType(uintptr_t component, uintptr_t holder)
{
    uint32_t type = *(uint32_t*)(component + 0x18);
    uint32_t id = StateId(holder);
    bool glide = false;

    for (uint32_t g : GLIDE_STATES)
        glide = glide || g == id;

    for (LONG i = 0; i < g_extraCount; ++i)
        glide = glide || g_extra[i] == id;

    if (g_logStates)
    {
        int ch = CharacterOf(component);

        if (ch >= 0)
            Log("glide log: %S enters state %08X%s", CHARACTER_NAMES[ch], id, glide ? " (glide)" : "");
    }

    if (!glide)
        return type;

    int ch = CharacterOf(component);
    return ch >= 0 ? OWN_TYPE[ch] : type;
}

static void* AllocateNear(uintptr_t target, size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t step = si.dwAllocationGranularity;
    uintptr_t lo = target > 0x70000000 ? target - 0x70000000 : step;

    for (uintptr_t a = (target & ~(step - 1)) - step; a > lo; a -= step)
    {
        void* p = VirtualAlloc((void*)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

        if (p)
            return p;
    }

    return NULL;
}

static uintptr_t FindSite()
{
    uintptr_t module = (uintptr_t)GetModuleHandleW(NULL);
    uintptr_t known = module + KNOWN_RVA;

    __try
    {
        if (memcmp((void*)known, STATE_CHANGE_CODE, sizeof(STATE_CHANGE_CODE)) == 0)
            return known;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    // Another build: look through the code section.
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(module + ((IMAGE_DOS_HEADER*)module)->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
    {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;

        const unsigned char* p = (const unsigned char*)(module + sec->VirtualAddress);
        size_t n = sec->Misc.VirtualSize;

        for (size_t o = 0; o + sizeof(STATE_CHANGE_CODE) <= n; ++o)
            if (p[o] == STATE_CHANGE_CODE[0] && memcmp(p + o, STATE_CHANGE_CODE, sizeof(STATE_CHANGE_CODE)) == 0)
                return (uintptr_t)(p + o);
    }

    return 0;
}

void GlideInit()
{
    g_containerVtable = AddressOf(ADDR_CHILDCONTAINERVTABLE);
    uintptr_t site = FindSite();

    if (!site)
    {
        Log("glide: the state change was not found - characters glide as their gender does");
        return;
    }

    unsigned char* stub = (unsigned char*)AllocateNear(site, 256);

    if (!stub)
    {
        Log("glide: no memory near the game's code - characters glide as their gender does");
        return;
    }

    unsigned char code[128];
    size_t n = 0;
    auto put = [&](std::initializer_list<unsigned char> bytes) { for (unsigned char b : bytes) code[n++] = b; };

    put({ 0x48, 0x8B, 0x58, 0x40 });                                // mov rbx,[rax+40h]   (component)
    put({ 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 });  // push rax rcx rdx r8-r11
    put({ 0x48, 0x89, 0xD9 });                                      // mov rcx,rbx
    put({ 0x4C, 0x89, 0xFA });                                      // mov rdx,r15
    put({ 0x48, 0x83, 0xEC, 0x28 });                                // sub rsp,28h
    put({ 0x48, 0xB8 });                                            // mov rax,ChooseType
    uintptr_t fn = (uintptr_t)&ChooseType;
    memcpy(code + n, &fn, 8); n += 8;
    put({ 0xFF, 0xD0 });                                            // call rax
    put({ 0x48, 0x83, 0xC4, 0x28 });                                // add rsp,28h
    put({ 0x89, 0xC3 });                                            // mov ebx,eax
    put({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 });  // pop r11-r8 rdx rcx rax
    put({ 0xE9 });                                                  // jmp back
    int32_t back = (int32_t)((site + PATCH_SIZE) - ((uintptr_t)stub + n + 4));
    memcpy(code + n, &back, 4); n += 4;

    memcpy(stub, code, n);
    FlushInstructionCache(GetCurrentProcess(), stub, n);

    unsigned char jump[PATCH_SIZE] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    int32_t to = (int32_t)((uintptr_t)stub - (site + 5));
    memcpy(jump + 1, &to, 4);

    DWORD old;

    if (!VirtualProtect((void*)site, PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old))
    {
        Log("glide: could not patch the state change");
        return;
    }

    memcpy((void*)site, jump, PATCH_SIZE);
    VirtualProtect((void*)site, PATCH_SIZE, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)site, PATCH_SIZE);

    Log("glide: each character glides their own way (state change at +0x%llX)",
        (unsigned long long)(site - (uintptr_t)GetModuleHandleW(NULL)));
}

void GlideLogStates(bool on)
{
    InterlockedExchange(&g_logStates, on ? 1 : 0);
    Log("glide: state log %s", on ? "on" : "off");
}

void GlideExtraStates(const uint32_t* ids, int count)
{
    InterlockedExchange(&g_extraCount, 0);

    for (int i = 0; i < count && i < EXTRA_MAX; ++i)
        g_extra[i] = ids[i];

    InterlockedExchange(&g_extraCount, count < EXTRA_MAX ? count : EXTRA_MAX);
    Log("glide: %d more glide states", (int)g_extraCount);
}
