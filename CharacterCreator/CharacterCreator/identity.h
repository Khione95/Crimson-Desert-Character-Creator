#pragma once

#include <stdio.h>

#include "characters.h"
#include "menu_data.h"

// Gender and race of each playable character.
//
// Both are decided while the character is being built, so a new choice
// applies after restarting the game:
//
//  - Race (and the base body): the character's base character file
//    (cd_phm_macduff_00000.app_xml for Kliff, cd_phw_damian_00000 and
//    cd_phm_oongka_00000) gets the chosen race and gender's values while the
//    game parses it. As themselves, characters keep their own file.
//  - Gender (skeleton and animations): the character table fields that
//    Female Animations.field.json used to set are written directly - Damian's
//    values for female, Kliff's for male.
//
// Stored in identity.txt (Kliff), identity_damiane.txt and identity_oongka.txt
// as "gender race height femaleAnimations".

static const int GENDER_MALE = 0, GENDER_FEMALE = 1;
static const int RACE_HUMAN = 0, RACE_ORC = 1, RACE_DWARF = 2, RACE_GOBLIN = 3;

void IdentityInit(const char* folder, const MenuData* data);

// The choice for the next start.
void IdentityChoose(int ch, int gender, int race);
void IdentityChosen(int ch, int* gender, int* race);

// What the character is without the mod (Kliff human male, Damiane human
// female, Oongka orc male).
void IdentityNative(int ch, int* gender, int* race);

// A woman can keep the male animations (no female walk and poses, but also
// no glider in place of the crow wings and no two-handed sword trouble): the
// animation fields then come from the male entry. Applies after a restart.
void IdentityChooseFemaleMoves(int ch, bool femaleMoves);
bool IdentityFemaleMoves(int ch);        // chosen
bool IdentityStartFemaleMoves(int ch);   // as the game started

// Height in percent of the body scale, applied while the base character file
// is parsed - so, like gender and race, after a restart.
static const int HEIGHT_MIN = -20, HEIGHT_MAX = 20;
void IdentityChooseHeight(int ch, int height);
int IdentityHeight(int ch);          // chosen
int IdentityLoadedHeight(int ch);    // built with

// The body scale of the character's base character file before the height,
// 0 until the game has parsed it.
float IdentityBaseScale(int ch);

// What the character was built as. Returns false until the game has built
// them since the plugin started.
bool IdentityLoaded(int ch, int* gender, int* race);

// Call regularly: writes the gender fields into the character table as
// soon as the game has loaded it.
void IdentityPoll();
