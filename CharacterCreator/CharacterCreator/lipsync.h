#pragma once

// Lip sync of a character played as another gender.
//
// A spoken line's mouth animation is read from
//   character/motion/<model folder>/99_autofacial/<language>/<line>.paa
// where the model folder is the one of the character's body model (1_pc/1_phm
// for men, 1_pc/2_phw for women). Kliff's and Oongka's lines only exist under
// 1_phm and Damiane's under 2_phw, so with the other gender's body the game
// finds none and plays a generic talking loop. The plugin sends the game to
// the folder that has the character's lines.

void LipSyncInit();
