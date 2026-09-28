#pragma once

#include <stdint.h>

// Whether the game will load the package's part table (character/bin__/
// partprefabtable.pappt), the one that knows the characters' own copies of
// the heads (tools/private_eyes.py). Another mod's table (Cloak Remover and
// the like) wins over it when DMM mounts that mod as its own archive group;
// the copies are then unknown to the game and showed no face.
//
// The game takes a file from the first archive group in meta/0.papgt that
// has it (DMM's groups come first). Each group's index (0.pamt) gives the
// file's unpacked size, which is compared with the package's table.
//
// gameFolder: the game's folder (the one holding meta and the groups).
// expectedSize: the package's table size (parttable.txt in the data folder).
bool PartTableIsOurs(const char* gameFolder, uint32_t expectedSize);
