#pragma once

// The plugin's version: the log, and the file's version information (shown
// in its properties; antivirus programs trust a file with it more).
#define PLUGIN_VERSION "9.1.21"
#define PLUGIN_VERSION_NUMBERS 9, 1, 21, 0

// The edition (pch.h): the menu anywhere, or the game's barber.
#if CC_BARBER
#define PLUGIN_NAME "Character Creator Enhanced - Barber"
#else
#define PLUGIN_NAME "Character Creator Enhanced - Anywhere"
#endif
