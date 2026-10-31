#include "pch.h"
#include "twohand.h"
#include "addresses.h"
#include "log.h"

#include <string.h>

#include <initializer_list>

// ---------------------------------------------------------------------------
// Two-handed swords in a woman's hands (the way sevnupp's Female Longsword Fix
// does it, nexusmods.com/crimsondesert/mods/3477 - thank you).
//
// The women's description (phw_description_player_001.xml) puts a two-handed
// sword (TwoHandWeapon_Sword) in the left hand; Kliff's puts it in the
// right. A woman plays the women's moves with it, made for the left hand, but
// Kliff's attacks from the two-handed chart, made for the right: there the
// sword sat in the wrong hand, held by the blade. The same description has a
// right-hand sword entry (MainWeapon_Sword_R, RHand_Socket); for the
// attacks listed below the sword goes to that hand, for everything else back
// to the left.
//
// Two places in the game:
//  - the two-handed chart's update for a character (COMBAT_CODE): for the
//    player it moves the sword to the hand the playing animation needs,
//    through the game's attach function;
//  - the attach function (ADDR_ATTACHPART: part, part manager, socket
//    record, ...): when the game attaches the sword itself during one of those
//    moves, it gets the record of the right hand.
// ---------------------------------------------------------------------------

// mov rax,[rsp+48h] / mov [r14+18h],rax / mov rax,[rsp+48h] / mov rcx,[rax+28h]:
// in the chart update (rva 2511284 on the known build, 25100D4 on the patch of 11:19); the hook replaces the
// last two (9 bytes, at +9). There: rbx = the action type (0x4CB714A1 for the
// women's), r12 = the chart layer (0: upper), r15 = the character, r14 = its
// upper body's action holder (before the 2026-10-30 update the other way
// round, with [rax+38h]).
static const unsigned char COMBAT_CODE[] = {
    0x48, 0x8B, 0x44, 0x24, 0x48, 0x49, 0x89, 0x46, 0x18, 0x48, 0x8B, 0x44, 0x24, 0x48, 0x48, 0x8B, 0x48, 0x28 };
static const size_t COMBAT_AT = 9, COMBAT_SIZE = 9;
static const uintptr_t COMBAT_KNOWN_RVA = 0x2514A04;

// mov rax,rsp / mov [rax+20h],r9: the attach function's first 7 bytes.
static const unsigned char ATTACH_CODE[] = { 0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20 };

static const uint32_t WOMEN_TYPE = 0x4CB714A1;

static const char DESCRIPTION[] = "character/descriptors/characterdescription/phw_description_player_001.xml";
static const char TWO_HAND_CHART[] = "upperaction/1_pc/1_phm/twohandsword_upper";
static const char SKILL_CHART[] = "upperaction/1_pc/1_phm/common_skill_upper";

// The skill moves (common_skill_upper) played with the two-handed sword.
static const uint32_t SKILL_MOVES[] = { 0xBD59041D, 0xBC5FB626, 0xE8E7668F };

// The moves played in the right hand: Kliff's attacks (the action ids of the
// chart's animations).
static const uint32_t RIGHT_HAND_MOVES[] = {
    0x56688D94, 0x2D791C1A, 0x80D76406, 0xBD59041D, 0xBC5FB626, 0xE8E7668F, 0x1707024A, 0x77EC8A0A,
    0xC5CACCE8, 0xDBA2E2E8, 0xE3EC6893, 0xE8A6548C, 0x62893233, 0x7C279069, 0x86FD359D, 0x8A9234F5,
    0x91C199B9, 0xD9EB025A, 0xDF187E2E, 0xF20E071A, 0x52972E09, 0xB49AF5EA, 0x92D259D1, 0xD991DEDC,
    0xBDCC251F, 0x210807BF, 0xE5E99970, 0x56A3D592, 0xE1B44999, 0x324C6A7A, 0x3B302564, 0x18416A99,
    0x0B49CD49 };

// The player character: the world ([ADDR_WORLDPOINTER]) through these.
static const uintptr_t PLAYER_CHAIN[] = { 0x30, 0x58, 0xD8, 0x68, 0x40 };

typedef void (*AttachFn)(uintptr_t part, uintptr_t manager, uint32_t* socket, uint32_t* child, uint32_t* a, uint32_t* b, float blend);

// The registers at a hook, as its stub pushes them (the hook may change them).
struct Registers
{
    uint64_t flags, r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rbx, rdx, rcx, rax;
};

struct Grip
{
    uintptr_t manager, part;
    uint32_t* rightRecord;
    uint32_t* leftRecord;
    uint32_t right, left, child;    // socket names
};

static volatile LONG g_on = 0;
static volatile LONG g_thread = 0;  // the thread the chart updates run on
static volatile LONG g_moved[2];    // moves to the left, right hand (logged once)
static AttachFn g_attach = NULL;
static uintptr_t g_world = 0, g_names = 0;

static uintptr_t Ptr(uintptr_t a) { return *(uintptr_t*)a; }
static uint32_t U32(uintptr_t a) { return *(uint32_t*)a; }

// A game string (text, length) is this text (with or without its zero).
static bool StringIs(uintptr_t s, const char* text, size_t n)
{
    if (!s)
        return false;

    const char* t = (const char*)Ptr(s);
    uint32_t len = U32(s + 8);
    return t && (len == n || (len == n + 1 && t[n] == 0)) && memcmp(t, text, n) == 0;
}

static const char* Name(uint32_t index)
{
    if (index - 1 > 0x1FFFE)
        return NULL;

    uintptr_t names = Ptr(g_names);
    uintptr_t table = names ? Ptr(names + 0x58) : 0;
    return table ? (const char*)Ptr(table + (uintptr_t)index * 16) : NULL;
}

static bool NameIs(uint32_t index, const char* text)
{
    const char* n = Name(index);
    return n && strcmp(n, text) == 0;
}

// A part's socket name (with "CD_" before the 2026-10-30 update).
static bool PartIs(uint32_t index, const char* text)
{
    const char* n = Name(index);

    if (n && strncmp(n, "CD_", 3) == 0)
        n += 3;

    return n && strcmp(n, text) == 0;
}

static uintptr_t Player()
{
    uintptr_t p = Ptr(g_world);

    for (uintptr_t off : PLAYER_CHAIN)
        p = p ? Ptr(p + off) : 0;

    return p;
}

static bool InTwoHandChart(uintptr_t action)
{
    uintptr_t chart = action ? Ptr(action + 0x10) : 0;
    uintptr_t name = chart ? Ptr(chart + 0x18) : 0;

    if (StringIs(name, TWO_HAND_CHART, sizeof(TWO_HAND_CHART) - 1))
        return true;

    for (uint32_t id : SKILL_MOVES)
        if (U32(action) == id)
            return StringIs(name, SKILL_CHART, sizeof(SKILL_CHART) - 1);

    return false;
}

static bool RightHanded(uint32_t id)
{
    for (uint32_t m : RIGHT_HAND_MOVES)
        if (m == id)
            return true;

    return false;
}

// The character's two-handed sword and its two hands (only with the women's
// description).
static bool FindGrip(uintptr_t character, Grip* g)
{
    uintptr_t description = Ptr(character + 0x220);

    if (!description || !StringIs(Ptr(description + 0x18), DESCRIPTION, sizeof(DESCRIPTION) - 1))
        return false;

    uint32_t count = U32(description + 0x2C);
    uintptr_t entries = Ptr(description + 0x40);

    if (count - 1 > 0x3FF || !entries)
        return false;

    uintptr_t twoHand = 0, swordR = 0;
    uint32_t twoHandName = 0;

    for (uint32_t i = 0; i < count; ++i)
    {
        uintptr_t e = Ptr(entries + (uintptr_t)i * 8);

        if (!e)
            continue;

        uint32_t name = U32(e + 4);

        if (PartIs(name, "TwoHandWeapon_Sword"))
        {
            if (twoHand)
                return false;

            twoHand = e + 8;
            twoHandName = name;
        }
        else if (PartIs(name, "MainWeapon_Sword_R"))
        {
            if (swordR)
                return false;

            swordR = e + 8;
        }
    }

    if (!twoHand || !swordR)
        return false;

    g->rightRecord = (uint32_t*)(swordR + 8);
    g->leftRecord = (uint32_t*)(twoHand + 8);
    g->right = *g->rightRecord;
    g->left = *g->leftRecord;
    g->child = U32(twoHand + 0x10);

    if (!NameIs(g->right, "RHand_Socket") || !NameIs(g->left, "LHand_Socket") || !NameIs(g->child, "Basic_ChildSocket"))
        return false;

    uintptr_t manager = Ptr(character + 0x130);

    for (uintptr_t off : { (uintptr_t)0, (uintptr_t)8, (uintptr_t)0xB0 })
        manager = manager ? Ptr(manager + off) : 0;

    if (!manager)
        return false;

    uint32_t parts = U32(manager + 0x60);
    uintptr_t list = Ptr(manager + 0x58);

    if (parts - 1 > 0x3FF || !list)
        return false;

    uintptr_t part = 0;

    for (uint32_t i = 0; i < parts; ++i)
        if (U32(list + (uintptr_t)i * 16) == twoHandName)
        {
            if (part)
                return false;

            part = Ptr(list + (uintptr_t)i * 16 + 8);
        }

    if (!part || U32(part + 0x48) != twoHandName)
        return false;

    g->manager = manager;
    g->part = part;
    return true;
}

static void Moved(bool right)
{
    if (InterlockedIncrement(&g_moved[right]) == 1)
        Log("two-handed grip: the sword went to the %s hand for the first time", right ? "right" : "left");
}

static void Fault(const char* where)
{
    if (InterlockedExchange(&g_on, 0))
        Log("two-handed grip: unexpected game data in the %s - switched off", where);
}

// The chart update: the player's sword to the hand the move needs.
static void __stdcall OnCombat(Registers* r)
{
    if (!g_on || r->r12 != 0 || (uint32_t)r->rbx != WOMEN_TYPE)
        return;

    __try
    {
        uintptr_t player = Player();

        if (!player || player != r->r15 || !r->r14 || Ptr(player + 0x88) != r->r14)
            return;

        uintptr_t action = Ptr(r->r14 + 0x18);
        Grip g;

        if (!action || !InTwoHandChart(action) || !FindGrip(player, &g))
            return;

        LONG self = (LONG)GetCurrentThreadId();
        InterlockedCompareExchange(&g_thread, self, 0);

        if (g_thread != self)
            return;

        uint32_t now = U32(g.part + 0x4C);

        if (now != g.right && now != g.left)
            return;

        bool right = RightHanded(U32(action));
        uint32_t want = right ? g.right : g.left;

        if (now == want)
            return;

        uint32_t child = g.child, a = U32(g.part + 0x54), b = U32(g.part + 0x50);
        g_attach(g.part, g.manager, &want, &child, &a, &b, 0.0f);

        if (U32(g.part + 0x4C) == want)
            Moved(right);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Fault("chart update");
    }
}

// The attach function (rcx part, rdx manager, r8 socket record): the game
// attaching the sword itself keeps the hand the move needs.
static void __stdcall OnAttach(Registers* r)
{
    if (!g_on || !g_thread || (LONG)GetCurrentThreadId() != g_thread)
        return;

    __try
    {
        uintptr_t player = Player();
        uintptr_t holder = player ? Ptr(player + 0x88) : 0;
        uintptr_t action = holder ? Ptr(holder + 0x18) : 0;
        Grip g;

        if (!action || !InTwoHandChart(action) || !FindGrip(player, &g))
            return;

        if (r->rcx != g.part || r->rdx != g.manager || !r->r8)
            return;

        uint32_t now = *(uint32_t*)r->r8;

        if (now != g.right && now != g.left)
            return;

        bool right = RightHanded(U32(action));

        if (now != (right ? g.right : g.left))
            r->r8 = (uint64_t)(right ? g.rightRecord : g.leftRecord);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Fault("attach function");
    }
}

// A hook in the middle of the game's code: the first `size` bytes at `site`
// (position-independent) move into a stub that saves the registers (and
// xmm0-5), calls `fn` with them, puts them back and runs those bytes.
static bool MidHook(uintptr_t site, size_t size, void (__stdcall* fn)(Registers*))
{
    unsigned char* stub = (unsigned char*)AllocateNear(site, 256);

    if (!stub)
        return false;

    unsigned char code[192];
    size_t n = 0;
    auto put = [&](std::initializer_list<unsigned char> bytes) { for (unsigned char b : bytes) code[n++] = b; };

    put({ 0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57 });                      // push rax rcx rdx rbx rbp rsi rdi
    put({ 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 });  // push r8-r15
    put({ 0x9C });                                                          // pushfq
    put({ 0x48, 0x89, 0xE3 });                                              // mov rbx,rsp  (the registers)
    put({ 0x48, 0x83, 0xE4, 0xF0 });                                        // and rsp,-16
    put({ 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 });                      // sub rsp,80h

    for (unsigned char x = 0; x < 6; ++x)
        put({ 0xF3, 0x0F, 0x7F, (unsigned char)(0x44 | (x << 3)), 0x24, (unsigned char)(0x20 + x * 16) });  // movdqu [rsp+20h+x*16],xmmx

    put({ 0x48, 0x89, 0xD9 });                                              // mov rcx,rbx
    put({ 0x48, 0xB8 });                                                    // mov rax,fn
    uintptr_t target = (uintptr_t)fn;
    memcpy(code + n, &target, 8); n += 8;
    put({ 0xFF, 0xD0 });                                                    // call rax

    for (unsigned char x = 0; x < 6; ++x)
        put({ 0xF3, 0x0F, 0x6F, (unsigned char)(0x44 | (x << 3)), 0x24, (unsigned char)(0x20 + x * 16) });  // movdqu xmmx,[rsp+20h+x*16]

    put({ 0x48, 0x89, 0xDC });                                              // mov rsp,rbx
    put({ 0x9D });                                                          // popfq
    put({ 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58 });  // pop r15-r8
    put({ 0x5F, 0x5E, 0x5D, 0x5B, 0x5A, 0x59, 0x58 });                      // pop rdi rsi rbp rbx rdx rcx rax
    memcpy(code + n, (void*)site, size); n += size;                         // the moved bytes
    put({ 0xE9 });                                                          // jmp back
    int32_t back = (int32_t)((site + size) - ((uintptr_t)stub + n + 4));
    memcpy(code + n, &back, 4); n += 4;

    memcpy(stub, code, n);
    FlushInstructionCache(GetCurrentProcess(), stub, n);

    unsigned char jump[16];
    memset(jump, 0x90, sizeof(jump));
    jump[0] = 0xE9;
    int32_t to = (int32_t)((uintptr_t)stub - (site + 5));
    memcpy(jump + 1, &to, 4);

    DWORD old;

    if (!VirtualProtect((void*)site, size, PAGE_EXECUTE_READWRITE, &old))
        return false;

    memcpy((void*)site, jump, size);
    VirtualProtect((void*)site, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)site, size);
    return true;
}

static bool Matches(uintptr_t at, const unsigned char* code, size_t size)
{
    __try
    {
        return memcmp((void*)at, code, size) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The chart update's code: at its known place, else looked for in the code.
static uintptr_t FindCombat()
{
    uintptr_t module = (uintptr_t)GetModuleHandleW(NULL);

    if (Matches(module + COMBAT_KNOWN_RVA, COMBAT_CODE, sizeof(COMBAT_CODE)))
        return module + COMBAT_KNOWN_RVA;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(module + ((IMAGE_DOS_HEADER*)module)->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    uintptr_t found = 0;

    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
    {
        // The game's code (its section names change from build to build).
        if (!(sec->Characteristics & IMAGE_SCN_CNT_CODE) || (sec->Characteristics & IMAGE_SCN_MEM_WRITE))
            continue;

        const unsigned char* p = (const unsigned char*)(module + sec->VirtualAddress);
        size_t n = sec->Misc.VirtualSize;

        for (size_t o = 0; o + sizeof(COMBAT_CODE) <= n; ++o)
            if (p[o] == COMBAT_CODE[0] && memcmp(p + o, COMBAT_CODE, sizeof(COMBAT_CODE)) == 0)
            {
                if (found)
                    return 0;   // not unique

                found = (uintptr_t)(p + o);
            }
    }

    return found;
}

void TwoHandInit()
{
    uintptr_t attach = AddressOf(ADDR_ATTACHPART);
    g_world = AddressOf(ADDR_WORLDPOINTER);
    g_names = AddressOf(ADDR_NAMESPOINTER);
    uintptr_t combat = FindCombat();

    if (!attach || !g_world || !g_names || !combat)
    {
        Log("two-handed grip: the game's code was not found - women hold two-handed swords as the game has it");
        return;
    }

    // Another mod (Female Longsword Fix) already there: it does the same.
    if (!Matches(combat + COMBAT_AT, COMBAT_CODE + COMBAT_AT, COMBAT_SIZE) || !Matches(attach, ATTACH_CODE, sizeof(ATTACH_CODE)))
    {
        Log("two-handed grip: another mod hooks the same code - leaving the grip to it");
        return;
    }

    g_attach = (AttachFn)attach;

    if (!MidHook(attach, sizeof(ATTACH_CODE), OnAttach) || !MidHook(combat + COMBAT_AT, COMBAT_SIZE, OnCombat))
    {
        Log("two-handed grip: could not hook the game's code - women hold two-handed swords as the game has it");
        return;
    }

    InterlockedExchange(&g_on, 1);
    uintptr_t module = (uintptr_t)GetModuleHandleW(NULL);
    Log("two-handed grip: right hand for Kliff's attacks, left for the women's moves (chart update +0x%llX, attach +0x%llX)",
        (unsigned long long)(combat + COMBAT_AT - module), (unsigned long long)(attach - module));
}
