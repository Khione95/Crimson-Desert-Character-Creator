#pragma once

#include <stdint.h>

#include "overlay.h"

// The editor panel. MenuDraw runs on the render thread, MenuKey on the game
// window's thread, MenuToggle on the plugin thread.

void MenuInit(const char* folder);

// The slot 5 option that shows the same head as head option `head`, or -1.
int MenuHeadDetails(int ch, int head);

// A head to swap to for a moment so the eye files are read again, or -1.
int MenuAwayHead(int ch, int head);

// The options of a slot that fit what the character was built as, in order;
// their count.
int MenuFittingOptions(int ch, int slot, uint8_t* out, int room);

// Whether a slot's option suits what the character was built as. Always, for a
// character built as themselves.
bool MenuOptionFits(int ch, int slot, int option);

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
