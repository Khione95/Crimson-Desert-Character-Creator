#pragma once

// Each character's chosen look, stored in the plugin's data folder as
// profile.txt (Kliff), profile_damiane.txt and profile_oongka.txt:
//
//   decoration <index> <value>
//   mesh <slot> <option>

void ProfileInit(const char* folder);

// Loads every character's profile.
void ProfileLoad();

// Saves each character's desired look if it changed since the last save.
void ProfileSaveIfChanged();
