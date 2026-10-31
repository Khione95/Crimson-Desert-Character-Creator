#pragma once

#include <stddef.h>
#include <stdint.h>

// The game's character table (CharacterInfoManager). Every character has a
// parsed entry; the player is entry 0 ("Kliff"). A few fields decide the
// skeleton, animations and appearance the character is built with - the same
// fields Female Animations.field.json changes:
//
//   +0x7C  u16  appearance_name        +0x88  u16  lookup_24
//   +0x7A  u16  character_prefab_path  +0x80  u16  lookup_25
//   +0x8E  u16  skeleton_name          +0x94  u16  f36
//   +0xE0  u32  default_action_action_index
//
// (Since the 2026-10-30 update. Told by the values: Kliff 7, 8, 9, 11, 12, f36
// 1, action 0; Damian 20, 21, 22, 24, 25, f36 2, action 0x4CB714A1.)
//
// Changing them takes effect when the character is next built (loading a save).

struct CharacterFields
{
    uint16_t appearance, prefab, skeleton, lookup24, lookup25, f36;
    uint32_t action;
};

// True once the game has loaded the table (cheap; reads the game's pointer).
bool CharTableAvailable();

// Identifies the table the game currently uses; changes if it is rebuilt.
uintptr_t CharTableInstance();

// The manager's list of entries: replaced while the game starts.
uintptr_t CharTableList();

// Finds the table, falling back to a slow memory search if the game's
// pointer is not where this game version keeps it.
bool CharTableFind();

int CharTableIndexOf(const char* name);
bool CharTableReadRaw(int index, size_t offset, void* out, size_t size);
bool CharTableWriteRaw(int index, size_t offset, const void* data, size_t size);

int CharTableCount();
uintptr_t CharTableEntry(int index);     // address of an entry, 0 if unknown
bool CharTableName(int index, char* out, size_t size);
bool CharTableRead(int index, CharacterFields* out);
bool CharTableWrite(int index, const CharacterFields& fields);
