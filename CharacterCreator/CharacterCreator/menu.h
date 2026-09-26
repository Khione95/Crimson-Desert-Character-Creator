#pragma once

#include "overlay.h"

// The editor panel. MenuDraw runs on the render thread, MenuKey on the game
// window's thread, MenuToggle on the plugin thread.

void MenuInit(const char* folder);

// The game version is not supported: the editor only shows a notice.
void MenuSetUnsupported();

// Opens the editor for a character (F6 Kliff, F7 Damiane, F8 Oongka), or
// closes it (keeping the changes) if it is already open for them. Opening it
// for another character while it is open switches to them.
void MenuToggle(int ch);

void MenuDraw(const OverlayDrawContext& ctx);
void MenuKey(int vk);

// Call regularly: keeps body, head and hair fitting each character's loaded
// race and gender.
void MenuPoll();
