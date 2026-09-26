#pragma once

#include <stddef.h>
#include <stdint.h>

// Development helpers for exploring the game's memory through command.txt.
// Output goes to <plugin folder>\CharacterCreator\research.txt.
//
//   dumpclass <vtable rva hex> <bytes hex> [max objects]
//       finds objects whose first 8 bytes are that vtable and dumps them
//   dumpmem <address hex> <bytes hex>
//       dumps any memory
//   chartable <pointer array hex> <count> <entry bytes hex> [name filter]
//
// Every 8-byte value that points at readable text is shown with the text.

void ResearchInit(const char* folder);
void ResearchDumpClass(uintptr_t vtableRva, size_t bytes, int maxObjects);
void ResearchDumpMemory(uintptr_t address, size_t bytes);

// Walks an array of pointers to character entries, printing the entries whose
// name contains nameFilter and every entry holding a value the female
// animations file writes.
void ResearchCharacterTable(uintptr_t pointerArray, int count, size_t entryBytes, const char* nameFilter);

// Logs paths from an object to character table entries (pointers up to 3 deep).
void ResearchFindOwner(uintptr_t object);

// findfloat <value> [run]: logs where `run` floats equal to value lie in a row
// in writable memory (up to 300 places, with the memory around each).
void ResearchFindFloats(float value, int run);

// setfloat <address hex> <value> [count]: writes count floats.
void ResearchSetFloats(uintptr_t address, float value, int count);

// findptr <address hex> [span hex]: logs where memory holds a pointer into
// [address, address + span), with the class of the object holding it.
void ResearchFindPointers(uintptr_t target, size_t span);
