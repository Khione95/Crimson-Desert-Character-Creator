#include "pch.h"
#include "lipsync.h"
#include "addresses.h"
#include "log.h"
#include "switches.h"

#include <string.h>

#include "MinHook.h"

// Builds "character/motion/<folder>/99_autofacial/<language>/<line>.paa"
// into out (folder e.g. "1_pc/2_phw", line e.g. "unique_kliff_..._00001").
typedef void* (__fastcall* BuildPathFn)(void* out, const char* folder, const char* line);
static BuildPathFn g_originalBuild = NULL;

// First bytes of the function on the known build.
static const BYTE BUILD_PROLOGUE[] = { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53 };

// Whose lines are where.
static const struct { const char* prefix; const char* folder; } OWNERS[] =
{
    { "unique_kliff_", "1_phm" },
    { "unique_oongka_", "1_phm" },
    { "unique_damian_", "2_phw" },
};

static const char* FolderOf(const char* line)
{
    for (const auto& o : OWNERS)
        if (_strnicmp(line, o.prefix, strlen(o.prefix)) == 0)
            return o.folder;

    return NULL;
}

// The last part of the folder, if it is one of the two player folders the
// lines are kept in.
static const char* PlayerLeaf(const char* folder)
{
    const char* slash = strrchr(folder, '/');
    const char* leaf = slash ? slash + 1 : folder;

    return _stricmp(leaf, "1_phm") == 0 || _stricmp(leaf, "2_phw") == 0 ? leaf : NULL;
}

// Writes folder with its last part replaced by leaf; false if it does not fit.
static bool Redirect(const char* folder, const char* leaf, char* out, size_t size)
{
    const char* slash = strrchr(folder, '/');
    size_t keep = slash ? (size_t)(slash + 1 - folder) : 0;

    if (keep + strlen(leaf) + 1 > size)
        return false;

    memcpy(out, folder, keep);
    strcpy_s(out + keep, size - keep, leaf);
    return true;
}

static void* __fastcall HookedBuild(void* out, const char* folder, const char* line)
{
    char redirected[128];
    const char* use = folder;

    __try
    {
        const char* wanted = folder && line ? FolderOf(line) : NULL;
        const char* leaf = wanted ? PlayerLeaf(folder) : NULL;

        if (leaf && _stricmp(leaf, wanted) != 0 && Redirect(folder, wanted, redirected, sizeof(redirected)))
        {
            use = redirected;

            static volatile LONG logged = 0;

            if (InterlockedIncrement(&logged) <= 5)
                Log("lip sync: %s from %s instead of %s", line, redirected, folder);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        use = folder;
    }

    return g_originalBuild(out, use, line);
}

void LipSyncInit()
{
    if (PartDisabled("lipsync"))
        return;

    BYTE* target = (BYTE*)AddressOf(ADDR_LIPSYNCPATH);

    // On other game builds the address comes from checked patterns (addresses.h).
    if (!FunctionHookable(target, BUILD_PROLOGUE, sizeof(BUILD_PROLOGUE), "lip sync path builder"))
    {
        Log("lip sync: path builder not found - lines of a character played as another gender keep a generic mouth");
        return;
    }

    MH_STATUS init = MH_Initialize();

    if ((init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) ||
        MH_CreateHook(target, (void*)&HookedBuild, (void**)&g_originalBuild) != MH_OK ||
        MH_EnableHook(target) != MH_OK)
    {
        Log("ERROR: could not hook the lip sync path builder");
        return;
    }

    Log("lip sync path builder hooked");
}
