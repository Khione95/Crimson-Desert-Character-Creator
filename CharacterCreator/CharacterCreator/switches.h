#pragma once

#include <stdio.h>
#include <string.h>

// Troubleshooting: <plugin folder>\CharacterCreator\disable.txt can name parts
// of the plugin to leave off, one or more of:
//   overlay     the menu panel (and its DirectX hooks)
//   direct      the panel's direct drawing (it is drawn through the HDR path)
//   eyes        the eye colour file hook
//   parser      the base character (race / gender) hook
//   controller  the appearance controller hook (no look is applied)
//   table       the character table gender fields
//   previews    the chosen look on preview copies (shop fitting, barber)
//   height      the live height preview
//   values      creating appearance values for a character who has none
//               (left to the game's first barber visit)
//   lipsync     lip sync from the character's own folder when played as
//               another gender

void SwitchesLoad(const char* folder);
bool PartDisabled(const char* part);
