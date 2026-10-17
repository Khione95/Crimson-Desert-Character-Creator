#pragma once

// Each character's chosen look, stored in the plugin's data folder as
// profile.txt (Kliff), profile_damiane.txt and profile_oongka.txt:
//
//   decoration <index> <value>
//   mesh <slot> <option>
//   details 5 <option>     face details (see ProfileDetails)

void ProfileInit(const char* folder);

// Face details: a head of the same gender mixed into the chosen head, as a
// mesh slot 5 option (slot 5 holds the heads too, without their skeleton) -
// or -1: none, slot 5 follows the head.
int ProfileDetails(int ch);
void ProfileSetDetails(int ch, int option);

// Loads every character's profile.
void ProfileLoad();

// Saves each character's desired look if it changed since the last save.
void ProfileSaveIfChanged();
