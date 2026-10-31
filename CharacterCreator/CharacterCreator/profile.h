#pragma once

// Each character's chosen look, stored in the plugin's data folder as
// profile.txt (Kliff), profile_damiane.txt and profile_oongka.txt:
//
//   decoration <index> <value>
//   mesh <slot> <option>
//   details 5 <option>     face details (see ProfileDetails)
//   face <slider> <value>  a Face Shape slider, -100 .. 100 (faceshape.h)
//   body <slider> <value>  a Body Shape slider, the same

void ProfileInit(const char* folder);

// Face details: a head of the same gender mixed into the chosen head, as a
// mesh slot 5 option (slot 5 holds the heads too, without their skeleton) -
// or -1: none, slot 5 follows the head.
int ProfileDetails(int ch);

// The menu data's numbers for converting saved looks of 9.1.16 and before
// (written without the "layout" line): set before ProfileLoad.
struct MenuData;
void ProfileSetConversion(const MenuData* data);
void ProfileSetDetails(int ch, int option);

// Loads every character's profile.
void ProfileLoad();

// Saves each character's desired look if it changed since the last save.
void ProfileSaveIfChanged();
