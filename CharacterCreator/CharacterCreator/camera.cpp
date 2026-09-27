#include "pch.h"
#include "camera.h"
#include "addresses.h"
#include "identity.h"
#include "log.h"
#include "switches.h"

#include <stdint.h>

// The game's camera settings, one after another in its data (the sliders of
// Accessibility, 0-100): distance = 0.5 + slider / 100 (a multiplier),
// vertical and horizontal offset = slider / 50. Larger horizontal values put
// the character further left.
static const size_t VERTICAL_OFFSET = 0xA0, HORIZONTAL_OFFSET = 0xF0;

struct View
{
    float distance, vertical, horizontal;
};

// Tuned in game on a human man, Oongka and Damiane: the game already fits its
// camera to the character's size, only women need it a little lower.
static const View BODY[2] = { { 0.6f, -0.4f, 1.5f }, { 0.6f, -0.6f, 1.5f } };   // man, woman
static const View FACE[2] = { { 0.16f, 0.0f, 0.4f }, { 0.16f, -0.2f, 0.4f } };

static volatile LONG g_open = 0;
static bool g_face = false;
static int g_woman = 0;
static float g_saved[3];
static bool g_savedValid = false;

static float* Setting(size_t offset)
{
    uintptr_t base = AddressOf(ADDR_CAMERASETTINGS);
    return base ? (float*)(base + offset) : NULL;
}

static bool SettingsUsable()
{
    return Setting(0) && !PartDisabled("camera");
}

static void ApplyView(const View& v)
{
    if (!SettingsUsable())
        return;

    __try
    {
        *Setting(0) = v.distance;
        *Setting(VERTICAL_OFFSET) = v.vertical;
        *Setting(HORIZONTAL_OFFSET) = v.horizontal;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static const View& CurrentView()
{
    return g_face ? FACE[g_woman] : BODY[g_woman];
}

void CameraMenuOpened(int ch)
{
    if (!SettingsUsable())
        return;

    __try
    {
        // Saved only when the editor was closed before (switching characters
        // closes and opens it again).
        if (!g_savedValid)
        {
            g_saved[0] = *Setting(0);
            g_saved[1] = *Setting(VERTICAL_OFFSET);
            g_saved[2] = *Setting(HORIZONTAL_OFFSET);
            g_savedValid = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }

    int gender = 0, race = 0;
    g_woman = IdentityLoaded(ch, &gender, &race) && gender == GENDER_FEMALE ? 1 : 0;
    InterlockedExchange(&g_open, 1);
    ApplyView(CurrentView());
}

void CameraMenuClosed()
{
    InterlockedExchange(&g_open, 0);

    if (!g_savedValid || !SettingsUsable())
        return;

    __try
    {
        *Setting(0) = g_saved[0];
        *Setting(VERTICAL_OFFSET) = g_saved[1];
        *Setting(HORIZONTAL_OFFSET) = g_saved[2];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    g_savedValid = false;
}

void CameraToggleView()
{
    if (!g_open)
        return;

    g_face = !g_face;
    ApplyView(CurrentView());
}
