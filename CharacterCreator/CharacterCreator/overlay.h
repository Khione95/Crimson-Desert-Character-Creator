#pragma once

#include <d2d1_1.h>
#include <dwrite.h>

// Draws the editor into the game's own frames, so it works in exclusive
// fullscreen too. The game's DirectX 12 back buffer is wrapped for Direct2D
// right before each frame is presented.
//
// Keyboard input reaches the menu through the game window's message loop;
// while the menu is open those keys are kept from the game.

struct OverlayDrawContext
{
    ID2D1DeviceContext* dc;
    IDWriteFactory* write;
    float width;
    float height;
    unsigned generation;    // changes when the drawing objects were made anew:
                            // brushes, fonts and bitmaps made before are invalid
};

typedef void (*OverlayDrawFn)(const OverlayDrawContext& ctx);

// Called for every key press while the menu is open (virtual-key code).
typedef void (*OverlayKeyFn)(int vk);

// Call as early as possible: watches for the game creating its swap chain.
bool OverlayEarlyInit();

// Call later: finishes the setup (falls back to a probe device if the game's
// swap chain was not seen).
bool OverlayInit();

// How many key presses arrived as normal messages / as raw input (diagnostics).
void OverlayInputStats(long* keyMessages, long* rawKeys);
void OverlaySetCallbacks(OverlayDrawFn draw, OverlayKeyFn key);
void OverlaySetVisible(bool visible);
bool OverlayVisible();

// Keeps drawing while the editor is closed (for a notice); keys stay with the game.
void OverlaySetDrawing(bool drawing);

// Loads a PNG for drawing (cached by the caller). Only valid after the first
// frame has been drawn; returns NULL until then or on error.
ID2D1Bitmap* OverlayLoadImage(const wchar_t* path);
