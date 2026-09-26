#pragma once

#include <stdint.h>
#include <stdio.h>

#include "characters.h"

// Eye colour of the player character.
//
// A player eye has no colour setting: its colour is the iris texture named in
// the eye's model property file (cd_phm_00_eyeleft_00_0001.pac_xml and the
// right, female and other variants). The game's pupil/iris appearance values
// write to material parameters those files do not declare, so they do
// nothing.
//
// The mod swaps the iris texture name while the game reads those files, so a
// new choice applies the next time the game loads the eyes: at once when the
// head is rebuilt (see GameReloadHead).

struct EyeColour
{
    const wchar_t* name;
    const char* texture;        // NULL = the game's own iris
    uint8_t r, g, b;            // swatch shown in the menu
};

extern const EyeColour EYE_COLOURS[];
extern const int EYE_COLOUR_COUNT;

void EyesInit(const char* folder);

// A character's colour (index into EYE_COLOURS), used the next time their
// eyes load. Stored in eyes.txt (Kliff), eyes_damiane.txt, eyes_oongka.txt.
void EyesChoose(int ch, int colour);
int EyesChosen(int ch);

// All characters read the same eye files; they get this character's colour
// (Kliff's unless another character's head is being rebuilt).
void EyesSetTarget(int ch);
