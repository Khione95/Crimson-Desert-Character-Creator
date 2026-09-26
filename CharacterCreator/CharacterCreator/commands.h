#pragma once

// Development helper: commands written to <plugin folder>\CharacterCreator\command.txt
// are carried out and the file is deleted. One command per line:
//
//   decoration <index> <value>    change one appearance value
//   mesh <slot> <option>          change one mesh (0 body, 1 head, 2 hair, ...)
//   dump                          write the player's current look to the log
//   menu                          open / close the editor
//   clear                         forget all chosen values
//   dumpclass / dumpmem / chartable   memory research, see research.h
//   charfields <n>                show the skeleton / appearance fields of character n
//   copychar <from> <to>          copy those fields between characters (see chartable.h)

void CommandsInit(const char* folder);
void CommandsPoll();
