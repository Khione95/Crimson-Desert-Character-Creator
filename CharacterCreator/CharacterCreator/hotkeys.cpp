#include "pch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hotkeys.h"
#include "characters.h"
#include "log.h"

struct Hotkey
{
    int key;            // 0 = none
    bool ctrl, shift, alt;
    wchar_t name[48];
};

static Hotkey g_keys[CHARACTER_COUNT] = {
    { VK_F6, false, false, false, L"F6" },
    { VK_F7, false, false, false, L"F7" },
    { VK_F8, false, false, false, L"F8" },
};

struct NamedKey
{
    const char* name;
    int key;
};

static const NamedKey NAMED_KEYS[] = {
    { "Insert", VK_INSERT }, { "Delete", VK_DELETE }, { "Home", VK_HOME }, { "End", VK_END },
    { "PageUp", VK_PRIOR }, { "PageDown", VK_NEXT }, { "Pause", VK_PAUSE }, { "ScrollLock", VK_SCROLL },
    { "Backspace", VK_BACK }, { "Tab", VK_TAB }, { "CapsLock", VK_CAPITAL }, { "Space", VK_SPACE },
    { "Up", VK_UP }, { "Down", VK_DOWN }, { "Left", VK_LEFT }, { "Right", VK_RIGHT },
    { "NumpadAdd", VK_ADD }, { "NumpadSubtract", VK_SUBTRACT }, { "NumpadMultiply", VK_MULTIPLY },
    { "NumpadDivide", VK_DIVIDE }, { "NumpadDecimal", VK_DECIMAL },
    { "Mouse3", VK_MBUTTON }, { "Mouse4", VK_XBUTTON1 }, { "Mouse5", VK_XBUTTON2 },
    { "Minus", VK_OEM_MINUS }, { "Plus", VK_OEM_PLUS }, { "Comma", VK_OEM_COMMA }, { "Period", VK_OEM_PERIOD },
    { "Tilde", VK_OEM_3 },
};

static int KeyOf(const char* name)
{
    size_t len = strlen(name);

    if (len == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')))
        return name[0];

    if (len == 1 && name[0] >= 'a' && name[0] <= 'z')
        return name[0] - 'a' + 'A';

    int n = 0;

    if ((name[0] == 'F' || name[0] == 'f') && sscanf_s(name + 1, "%d", &n) == 1 && n >= 1 && n <= 24)
        return VK_F1 + n - 1;

    if (_strnicmp(name, "Numpad", 6) == 0 && len == 7 && name[6] >= '0' && name[6] <= '9')
        return VK_NUMPAD0 + name[6] - '0';

    for (const NamedKey& k : NAMED_KEYS)
        if (_stricmp(name, k.name) == 0)
            return k.key;

    return 0;
}

static void Trim(char* s)
{
    char* start = s;

    while (*start == ' ' || *start == '\t')
        ++start;

    memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);

    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = 0;
}

// "Ctrl+Shift+F6" -> the key and its modifiers. False for an unknown key.
static bool Parse(const char* text, Hotkey* out)
{
    Hotkey key = {};
    char copy[64];
    strcpy_s(copy, text);
    Trim(copy);

    if (!copy[0] || _stricmp(copy, "NONE") == 0)
    {
        *out = key;
        return true;
    }

    char* context = NULL;

    for (char* part = strtok_s(copy, "+", &context); part; part = strtok_s(NULL, "+", &context))
    {
        Trim(part);

        if (_stricmp(part, "Ctrl") == 0 || _stricmp(part, "Control") == 0)
            key.ctrl = true;
        else if (_stricmp(part, "Shift") == 0)
            key.shift = true;
        else if (_stricmp(part, "Alt") == 0)
            key.alt = true;
        else if (key.key || !(key.key = KeyOf(part)))
            return false;
    }

    if (!key.key)
        return false;

    char name[64];
    strcpy_s(name, text);
    Trim(name);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, key.name, _countof(key.name));
    *out = key;
    return true;
}

static const char DEFAULT_INI[] =
    "; Character Creator - hotkeys\r\n"
    ";\r\n"
    "; The keys that open and close the editor for each character.\r\n"
    "; Key names: F1-F24, A-Z, 0-9, Numpad0-Numpad9, Insert, Delete, Home, End,\r\n"
    "; PageUp, PageDown, Pause, ScrollLock, Mouse4, Mouse5\r\n"
    "; Add Ctrl+, Shift+ or Alt+ in front for a combination, e.g. Ctrl+F6.\r\n"
    "; NONE leaves a character without a key.\r\n"
    "; Changes apply the next time the game starts.\r\n"
    "\r\n"
    "[Hotkeys]\r\n"
    "Kliff = F6\r\n"
    "Damiane = F7\r\n"
    "Oongka = F8\r\n"
    "\r\n"
    "; Fixes: 1 = on, 0 = off.\r\n"
    "; Glide - each character glides their own way whatever their gender (Kliff\r\n"
    ";   with the crow wings, Damiane with her glider, Oongka with his rocket).\r\n"
    ";   Set 0 when another mod handles gliding.\r\n"
    "\r\n"
    "[Fixes]\r\n"
    "Glide = 1\r\n";

// The [Fixes] part of the default ini, added to an ini made by an older
// version that has no [Fixes] section yet.
static const char FIXES_INI[] =
    "\r\n"
    "; Fixes: 1 = on, 0 = off.\r\n"
    "; Glide - each character glides their own way whatever their gender (Kliff\r\n"
    ";   with the crow wings, Damiane with her glider, Oongka with his rocket).\r\n"
    ";   Set 0 when another mod handles gliding.\r\n"
    "\r\n"
    "[Fixes]\r\n"
    "Glide = 1\r\n";

static char g_iniPath[MAX_PATH] = "";

// DMM installs only the plugin into bin64: the ini is written there on the
// first start, where DMM's ASI config editor finds it.
static void WriteDefault(const char* path)
{
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
        return;

    FILE* f = NULL;
    fopen_s(&f, path, "wb");

    if (!f)
        return;

    fwrite(DEFAULT_INI, 1, sizeof(DEFAULT_INI) - 1, f);
    fclose(f);
    Log("hotkeys: wrote the default %s", path);
}

void HotkeysLoad(const char* pluginFolder)
{
    static const char* const ENTRIES[CHARACTER_COUNT] = { "Kliff", "Damiane", "Oongka" };
    char path[MAX_PATH];
    sprintf_s(path, "%s\\CharacterCreator.ini", pluginFolder);
    WriteDefault(path);
    strcpy_s(g_iniPath, path);

    char probe[8] = "";

    if (!GetPrivateProfileStringA("Fixes", NULL, "", probe, sizeof(probe), path))
    {
        FILE* f = NULL;
        fopen_s(&f, path, "ab");

        if (f)
        {
            fwrite(FIXES_INI, 1, sizeof(FIXES_INI) - 1, f);
            fclose(f);
            Log("hotkeys: added the [Fixes] section to %s", path);
        }
    }

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        char text[64] = "";
        GetPrivateProfileStringA("Hotkeys", ENTRIES[ch], "\x01", text, sizeof(text), path);

        if (text[0] == '\x01')
            continue;   // not in the file: the default

        Hotkey key;

        if (Parse(text, &key))
            g_keys[ch] = key;
        else
            Log("hotkeys: unknown key \"%s\" for %S - keeping %S", text, CHARACTER_NAMES[ch], g_keys[ch].name);
    }

    Log("hotkeys: Kliff %S, Damiane %S, Oongka %S", g_keys[0].key ? g_keys[0].name : L"none",
        g_keys[1].key ? g_keys[1].name : L"none", g_keys[2].key ? g_keys[2].name : L"none");
}

static bool Held(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool FixEnabled(const char* name)
{
    return !g_iniPath[0] || GetPrivateProfileIntA("Fixes", name, 1, g_iniPath) != 0;
}

bool HotkeyDown(int ch)
{
    if (ch < 0 || ch >= CHARACTER_COUNT || !g_keys[ch].key)
        return false;

    const Hotkey& k = g_keys[ch];

    // Modifiers must match exactly, so Ctrl+F6 and F6 can be different keys.
    return Held(k.key) && Held(VK_CONTROL) == k.ctrl && Held(VK_SHIFT) == k.shift && Held(VK_MENU) == k.alt;
}

const wchar_t* HotkeyName(int ch)
{
    return ch >= 0 && ch < CHARACTER_COUNT && g_keys[ch].key ? g_keys[ch].name : L"";
}
