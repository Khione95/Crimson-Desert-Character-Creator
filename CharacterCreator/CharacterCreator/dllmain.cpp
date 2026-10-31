// Character Creator by Khione
//
// Edit your character's appearance anywhere in the game.

#include "pch.h"
#include "addresses.h"
#include "commands.h"
#include "barbertabs.h"
#include "game.h"
#include "glide.h"
#include "twohand.h"
#include "height.h"
#include "hotkeys.h"
#include "identity.h"
#include "lipsync.h"
#include "eyes.h"
#include "faceshape.h"
#include "log.h"
#include "menu.h"
#include "overlay.h"
#include "profile.h"
#include "switches.h"
#include "data_pack.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

static HMODULE g_module = NULL;

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

// The folder of the .asi.
static void PluginFolder(char* out, size_t size)
{
    GetModuleFileNameA(g_module, out, (DWORD)size);

    char* slash = strrchr(out, '\\');

    if (slash)
        *slash = 0;
}

// <folder of the .asi>\CharacterCreator
static void DataFolder(char* out, size_t size)
{
    PluginFolder(out, size);
    strcat_s(out, size, "\\CharacterCreator");
    CreateDirectoryA(out, NULL);
}


// Which build of the game this is, for bug reports: the executable's link
// time stamp and image size (the same for everyone on one game version).
static void LogGameBuild()
{
    const BYTE* base = (const BYTE*)GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    Log("%s %s, game build %08lX / %08lX", PLUGIN_NAME, PLUGIN_VERSION,
        (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)nt->OptionalHeader.SizeOfImage);
}

static DWORD WINAPI MainThread(LPVOID)
{
    char folder[MAX_PATH];
    DataFolder(folder, sizeof(folder));

    char logPath[MAX_PATH];
    sprintf_s(logPath, "%s\\CharacterCreator.log", folder);
    LogOpen(logPath);
    Log("Character Creator starting");

#if CC_RESEARCH
    // Before the game loads its customization files (the bone list).
    EyesResearchEarly(folder);
#endif
    SwitchesLoad(folder);

#if !CC_BARBER
    char pluginFolder[MAX_PATH];
    PluginFolder(pluginFolder, sizeof(pluginFolder));
    HotkeysLoad(pluginFolder);

    // Before the game creates its swap chain.
    if (!PartDisabled("overlay"))
        OverlayEarlyInit();
#endif

    DataPackUnpack(g_module, folder);

    // Give the game time to unpack and initialise before touching it.
    Sleep(5000);
    LogGameBuild();

    // Where the game's functions are: known on this game build, looked up on
    // others (a game update).
    AddressesInit();

    // On a game version the plugin does not know, nothing of the game is
    // touched; the editor still opens and says so.
    bool gameReady = !PartDisabled("controller") && GameInit();

    if (!PartDisabled("controller") && !gameReady)
    {
#if CC_BARBER
        Log("this game version is not supported by " PLUGIN_NAME " " PLUGIN_VERSION " - nothing of the game is changed");
#else
        Log("this game version is not supported by " PLUGIN_NAME " " PLUGIN_VERSION " - the editor only shows a notice");
#endif
        MenuSetUnsupported();
    }

    ProfileInit(folder);
    CommandsInit(folder);
    MenuInit(folder);
#if CC_BARBER
    // The look is the one the barber gave, kept in the game's save: no saved
    // look of the plugin's (the Anywhere edition's choices) is put over it.
    Log("Barber edition: the look is changed at the game's barber - no editor, no saved look of its own");
#else
    ProfileLoad();
#endif
    LipSyncInit();

    if (gameReady && !PartDisabled("glide") && FixEnabled("Glide"))
        GlideInit();
    else if (gameReady && !FixEnabled("Glide"))
        Log("glide: off in CharacterCreator.ini ([Fixes] Glide = 0) - characters glide as their gender does");

    if (gameReady && !PartDisabled("twohand") && FixEnabled("TwoHandGrip"))
        TwoHandInit();
    else if (gameReady && !FixEnabled("TwoHandGrip"))
        Log("two-handed grip: off in CharacterCreator.ini ([Fixes] TwoHandGrip = 0)");

    if (gameReady && !PartDisabled("height"))
        HeightInit();

#if CC_BARBER
    if (gameReady && !PartDisabled("barbertabs"))
        BarberTabsInit();

    while (true)
    {
        CommandsPoll();
        if (!PartDisabled("table"))
            IdentityPoll();
        MenuPoll();
        if (gameReady && !PartDisabled("barbertabs"))
            BarberTabsPoll();
        if (gameReady)
            FacePoll();
        Sleep(100);
    }
#else
    if (!PartDisabled("overlay") && OverlayInit())
        OverlaySetCallbacks(MenuDraw, MenuKey);

    DWORD lastSave = GetTickCount();
    bool keyDown[CHARACTER_COUNT] = {};

    while (true)
    {
        CommandsPoll();
        if (!PartDisabled("table"))
            IdentityPoll();
        MenuPoll();
        if (gameReady)
            FacePoll();


        // F6 / F7 / F8 (CharacterCreator.ini) open and close the editor for
        // Kliff / Damiane / Oongka. Uses the "is down" bit: the "pressed since
        // last call" bit is shared between programs.
        for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        {
            bool down = HotkeyDown(ch);

            if (down && !keyDown[ch])
                MenuToggle(ch);

            keyDown[ch] = down;
        }

        DWORD now = GetTickCount();

        // While the editor is open changes are only a preview; they are
        // saved when kept (Space / Enter) and dropped with Esc.
        if (now - lastSave >= 2000 && !OverlayVisible())
        {
            ProfileSaveIfChanged();
            lastSave = now;
        }

        Sleep(100);
    }
#endif

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        g_module = hModule;

        if (!IsGameProcess())
            return TRUE;

        HANDLE thread = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);

        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
