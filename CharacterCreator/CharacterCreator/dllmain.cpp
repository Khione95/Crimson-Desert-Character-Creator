// Character Creator by Khione
//
// Edit your character's appearance anywhere in the game.

#include "pch.h"
#include "addresses.h"
#include "commands.h"
#include "game.h"
#include "height.h"
#include "identity.h"
#include "lipsync.h"
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

// <folder of the .asi>\CharacterCreator
static void DataFolder(char* out, size_t size)
{
    GetModuleFileNameA(g_module, out, (DWORD)size);

    char* slash = strrchr(out, '\\');

    if (slash)
        *slash = 0;

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
    Log("Character Creator %s, game build %08lX / %08lX", PLUGIN_VERSION,
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
    SwitchesLoad(folder);

    // Before the game creates its swap chain.
    if (!PartDisabled("overlay"))
        OverlayEarlyInit();

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
        Log("this game version is not supported by Character Creator " PLUGIN_VERSION " - the editor only shows a notice");
        MenuSetUnsupported();
    }

    ProfileInit(folder);
    CommandsInit(folder);
    MenuInit(folder);
    ProfileLoad();
    LipSyncInit();

    if (gameReady && !PartDisabled("height"))
        HeightInit();

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


        // F6 / F7 / F8 open and close the editor for Kliff / Damiane / Oongka.
        // Uses the "is down" bit: the "pressed since last call" bit is shared
        // between programs.
        static const int KEYS[CHARACTER_COUNT] = { VK_F6, VK_F7, VK_F8 };

        for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
        {
            bool down = (GetAsyncKeyState(KEYS[ch]) & 0x8000) != 0;

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
