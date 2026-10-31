#pragma once

// Face Shape sliders: bones of the face rig moved in the head's face shape.
//
// Every head of the mod's lists is skinned to one face rig (about 205 bones
// under B_face_com). A head's face shape is its skeleton variation (.pabc),
// which the game keeps in memory as 0x90-byte records - the bone's local
// matrix, scale, rotation and position, its name hash at +0x80 - and applies
// to the skeleton when the head is built. A slider adds offsets to a group
// of those positions (tools/face_sliders.py, faceshape_data.inc); the head is
// then rebuilt (a head swap, as for eye colour) so the game applies them.
//
// The loaded shape is found by the positions of three of its bones (each
// head's shape has its own). The game shares a loaded shape with anyone
// wearing the same head, so each character's lists name their own copy of
// it, marked by one bone moved a little (tools/private_shapes.py): only the
// character's marked copies are changed, never an NPC's.

int FaceSliderCount();

// -100 .. 100; 0 is the head's own shape.
int FaceValue(int ch, int slider);
bool FaceAny(int ch);

// From the editor: applied a moment after the last change, with a head
// refresh.
void FaceSetValue(int ch, int slider, int value);

// All of a character's face sliders back to 0 (another head: its own shape).
void FaceResetAll(int ch);

// From the profile: applied once the character is in the world.
void FaceSetLoaded(int ch, int slider, int value);

// Whether the values changed since FaceMarkSaved (the profile saves them).
bool FaceChangedSinceSave(int ch);
void FaceMarkSaved(int ch);

// Body Shape sliders: bones' scale in the body's shape (the same -100 .. 100,
// the same refresh). Separate bones for men and women (tools/face_sliders.py).
int BodySliderCount();
int BodyValue(int ch, int slider);
bool BodyAny(int ch);
void BodySetValue(int ch, int slider, int value);
void BodySetLoaded(int ch, int slider, int value);
bool BodyChangedSinceSave(int ch);
void BodyMarkSaved(int ch);

// CC's main loop (every 100 ms): writes changed values into the character's
// shapes and rebuilds them.
void FacePoll();

#if CC_RESEARCH
// Research (command.txt "facetest <slider> <value> [print]"): one slider on
// a face shape by its print (default: Kliff's own head), written without
// the character being known, then the last player-like character rebuilt.
void FaceResearchTest(int slider, int value, int print);
#endif
