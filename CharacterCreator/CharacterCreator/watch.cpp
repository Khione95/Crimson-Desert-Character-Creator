#include "pch.h"
#include "watch.h"
#include "log.h"

#include <tlhelp32.h>

#include <map>

// Research: hardware breakpoints (debug registers 0-3) on reads and writes of
// up to four 4-byte places; a vectored handler counts the code that touched
// them. Nothing is changed in the watched memory.

static uintptr_t g_address[4];
static int g_count = 0;
static PVOID g_handler = NULL;
static SRWLOCK g_lock = SRWLOCK_INIT;
static std::map<uintptr_t, unsigned> g_hits[4];     // instruction after the access -> count

// At the action node lookup (rva 1F78AB6, edx = action type): the chart data
// ([rbp-0x19]) and the node index (word [rsi+0x18]) being resolved.
static const uintptr_t NODE_LOOKUP_RVA = 0x1F78AB6;
static std::map<std::pair<uintptr_t, unsigned>, unsigned> g_nodes;

// forcetype: the value a reader just loaded is replaced (in its register).
// The readers of the action type found so far and the register each loads.
struct Reader { uintptr_t rva; int reg; };     // 0 rbx, 1 rdx
static const Reader READERS[] = { { 0x472B24, 0 }, { 0x1EC3743, 0 }, { 0x1F78AB6, 1 } };
static const int READER_COUNT = sizeof(READERS) / sizeof(READERS[0]);
static bool g_force[READER_COUNT];

// At the state change reader (rva 1EC3743): the state being entered is
// [r15+0x10]; its id hash is at +0x18.
static const uintptr_t STATE_RVA = 0x1EC3743;
static std::map<uint32_t, unsigned> g_states;

// forcestate: on entering one of these states, the state reader gets the value.
static uint32_t g_forceStates[16];
static int g_forceStateCount = 0;
static uint32_t g_forceStateValue = 0;
static uint32_t g_forceValue[READER_COUNT];

static LONG CALLBACK Handler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD64 dr6 = e->ContextRecord->Dr6;

    if (!(dr6 & 0xF))
        return EXCEPTION_CONTINUE_SEARCH;

    uintptr_t rip = (uintptr_t)e->ContextRecord->Rip;

    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < 4; ++i)
        if (dr6 & (1ull << i))
            ++g_hits[i][rip];

    uintptr_t rva = rip - (uintptr_t)GetModuleHandleW(NULL);

    for (int i = 0; i < READER_COUNT; ++i)
        if (g_force[i] && READERS[i].rva == rva)
        {
            DWORD64* reg = READERS[i].reg == 0 ? &e->ContextRecord->Rbx : &e->ContextRecord->Rdx;
            *reg = g_forceValue[i];
        }

    if (rva == STATE_RVA && e->ContextRecord->R15)
    {
        uintptr_t state = *(uintptr_t*)(e->ContextRecord->R15 + 0x10);
        uint32_t id = 0;

        if (state && !IsBadReadPtr((void*)(state + 0x18), 4))
            id = *(uint32_t*)(state + 0x18);

        ++g_states[id];

        for (int i = 0; i < g_forceStateCount; ++i)
            if (g_forceStates[i] == id)
                e->ContextRecord->Rbx = g_forceStateValue;
    }

    if (rva == NODE_LOOKUP_RVA)
    {
        uintptr_t chart = *(uintptr_t*)(e->ContextRecord->Rbp - 0x19);
        unsigned node = *(uint16_t*)(e->ContextRecord->Rsi + 0x18);
        ++g_nodes[{ chart, node }];
    }
    ReleaseSRWLockExclusive(&g_lock);

    e->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void SetAllThreads(bool on)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

    if (snap == INVALID_HANDLE_VALUE)
        return;

    DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId();
    THREADENTRY32 te = { sizeof(te) };
    int threads = 0;

    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != process || te.th32ThreadID == self)
            continue;

        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);

        if (!t)
            continue;

        if (SuspendThread(t) != (DWORD)-1)
        {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;

            if (GetThreadContext(t, &c))
            {
                c.Dr0 = on && g_count > 0 ? g_address[0] : 0;
                c.Dr1 = on && g_count > 1 ? g_address[1] : 0;
                c.Dr2 = on && g_count > 2 ? g_address[2] : 0;
                c.Dr3 = on && g_count > 3 ? g_address[3] : 0;
                c.Dr7 = 0;

                // Local enable, break on read or write (11), 4 bytes (11).
                if (on)
                    for (int i = 0; i < g_count; ++i)
                        c.Dr7 |= (1ull << (i * 2)) | (0xFull << (16 + i * 4));

                c.Dr6 = 0;
                if (SetThreadContext(t, &c))
                    ++threads;
            }

            ResumeThread(t);
        }

        CloseHandle(t);
    }

    CloseHandle(snap);
    Log("watch: %s on %d threads", on ? "set" : "cleared", threads);
}

void WatchStart(const uintptr_t* addresses, int count)
{
    if (count < 1 || count > 4)
        return;

    if (!g_handler)
        g_handler = AddVectoredExceptionHandler(1, Handler);

    AcquireSRWLockExclusive(&g_lock);
    g_count = count;
    for (int i = 0; i < 4; ++i)
    {
        g_address[i] = i < count ? addresses[i] : 0;
        g_hits[i].clear();
    }
    g_nodes.clear();
    g_states.clear();
    ReleaseSRWLockExclusive(&g_lock);

    SetAllThreads(true);

    for (int i = 0; i < count; ++i)
        Log("watch %d: %llX", i, (unsigned long long)g_address[i]);
}

void WatchReset()
{
    AcquireSRWLockExclusive(&g_lock);
    for (auto& m : g_hits)
        m.clear();
    g_nodes.clear();
    g_states.clear();
    ReleaseSRWLockExclusive(&g_lock);
    Log("watch: counts reset");
}

void WatchLog()
{
    uintptr_t module = (uintptr_t)GetModuleHandleW(NULL);

    AcquireSRWLockShared(&g_lock);
    for (int i = 0; i < g_count; ++i)
        for (const auto& h : g_hits[i])
            Log("watch %d: rva %llX  x%u", i, (unsigned long long)(h.first - module), h.second);

    for (const auto& n : g_nodes)
        Log("watch node: chart %llX node %u  x%u", (unsigned long long)n.first.first, n.first.second, n.second);

    for (const auto& st : g_states)
        Log("watch state: %08X  x%u", st.first, st.second);
    ReleaseSRWLockShared(&g_lock);
}

void WatchStop()
{
    SetAllThreads(false);
    g_count = 0;
}

void WatchForce(uintptr_t rva, bool on, uint32_t value)
{
    for (int i = 0; i < READER_COUNT; ++i)
        if (READERS[i].rva == rva)
        {
            g_forceValue[i] = value;
            g_force[i] = on;
            Log("watch: reader %llX %s %08X", (unsigned long long)rva, on ? "forced to" : "back to its own value", value);
            return;
        }

    Log("watch: %llX is not a known reader", (unsigned long long)rva);
}

void WatchForceStates(const uint32_t* ids, int count, uint32_t value)
{
    AcquireSRWLockExclusive(&g_lock);
    g_forceStateCount = 0;
    for (int i = 0; i < count && i < 16; ++i)
        g_forceStates[g_forceStateCount++] = ids[i];
    g_forceStateValue = value;
    ReleaseSRWLockExclusive(&g_lock);
    Log("watch: %d states get %08X", g_forceStateCount, value);
}
