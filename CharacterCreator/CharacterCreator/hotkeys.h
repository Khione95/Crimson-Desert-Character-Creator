#pragma once

// The keys that open the editor for each character, from CharacterCreator.ini
// next to the plugin (DMM can edit it):
//
//   [Hotkeys]
//   Kliff = F6
//   Damiane = F7
//   Oongka = F8
//
// A key name (F1-F24, A-Z, 0-9, Numpad0-9, Insert, Delete, Home, End,
// PageUp, PageDown, Mouse4, Mouse5...), optionally after Ctrl+, Shift+ and
// Alt+ (e.g. Ctrl+F6). NONE leaves a character without a key. Missing or
// unknown entries keep the default.

void HotkeysLoad(const char* pluginFolder);

// True while the character's key (with its modifiers) is held down.
bool HotkeyDown(int ch);

// The key as written for the menu, e.g. L"Ctrl+F6"; empty without a key.
const wchar_t* HotkeyName(int ch);
