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
// file's unpacked size, which is compared with the package's table and with
// Female Armor Fit's (the same table with the armor copies added).
//
// gameFolder: the game's folder (the one holding meta and the groups).
// dataFolder: the plugin's folder (private_heads.txt: DMM 4.5 merges other mods'
// parts into the table, which is still ours if it has the heads' copies).
// sizes, count: the tables' sizes (parttable.txt in the data folder); none:
// nothing can be told.
bool PartTableIsOurs(const char* gameFolder, const char* dataFolder, const uint32_t* sizes, int count);
