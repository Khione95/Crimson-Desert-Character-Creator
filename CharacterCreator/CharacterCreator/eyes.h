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
// The mod swaps the iris texture name while the game reads those files. The
// shared eye files are left alone (NPCs read them too): the player
// characters wear copies of their heads with their own eye files, whose iris
// paths are marked (tools/private_eyes.py). The game keeps eye files loaded
// while a head uses them: a new colour is read after the menu rebuilds the
// head through one with other eyes.

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

// How many times the character's own eye files have been read (the head
// rebuild checks the eyes were read again on the way back).
LONG EyesReadCount(int ch);

