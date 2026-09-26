#pragma once

// Live height preview: each character's drawn body scale is set while they
// are in the game, so a new height shows at once. Only the drawing follows:
// the skeleton and animations keep the scale the character was built with
// (arms and shoulders look off when moving) until the game is restarted and
// the base character file brings the height in (see identity.h). Changing the
// build data in memory as well made it worse.
//
// Every character (NPCs too) has a scale component whose object holds the
// body scale the game draws them with:
//
//   controller +0x10          the character entity
//   scale component           class at module+0x5B4C6C0, +0x10 = the entity
//     +0x1C0                  scale object, class at module+0x5B4D168
//       +0x84 float           body scale (CharacterScale of the base file)
//       +0x88 float           head scale
//
// The component is found by scanning memory once per character controller.
// The base character file also gets the height when it is parsed (see
// identity.h), so a new start is right from the beginning.

void HeightInit();
