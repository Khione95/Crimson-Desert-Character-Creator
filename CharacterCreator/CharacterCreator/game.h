#pragma once

#include <stdint.h>
#include <stdio.h>

#include "characters.h"

// Everything that touches the game's own appearance code lives here.
//
// A character's look is held by a CharacterCustomizationController:
//   +0xA0  { BYTE* mesh, uint32 count }        16 mesh choices (0 body, 1 head,
//                                              2 hair, 3 beard, 5, 6 eyebrows)
//   +0xB0  { BYTE* decoration, uint32 count }  250 appearance values, one per
//                                              ParamDesc of decorationparam_player.xml
//
// The mod keeps a "desired look" per playable character: every value chosen
// for them. On the game's own update of that character, anything that differs
// from it is applied through the game's setters, exactly like the barber
// does. This also restores the look whenever the game reloads it from the save.

static const int DECORATION_COUNT = 250;
static const int MESH_SLOT_COUNT = 16;

// Mesh option meaning "no mesh" (for example no beard).
static const uint8_t MESH_NONE = 0xFF;

enum MeshSlot
{
    MESH_BODY = 0,
    MESH_HEAD = 1,
    MESH_HAIR = 2,
    MESH_BEARD = 3,
    MESH_WHISKERS = 5,
    MESH_EYEBROWS = 6,
};

struct Appearance
{
    uint8_t decoration[DECORATION_COUNT];
    uint8_t mesh[MESH_SLOT_COUNT];
};

// Which values of an Appearance are chosen by the player.
struct AppearanceMask
{
    bool decoration[DECORATION_COUNT];
    bool mesh[MESH_SLOT_COUNT];
};

bool GameInit();

#if CC_BARBER
// The game's barber changed a mesh it would not rebuild for by itself (barbertabs.cpp).
void GameBarberMeshChanged(uintptr_t controller);

// The character of a controller, or -1, and what it shows in a mesh slot.
int GameBarberCharacter(uintptr_t controller);
int GameBarberMesh(uintptr_t controller, int slot);
int GameBarberValue(uintptr_t controller, int index);
bool GameBarberIsPreview(uintptr_t controller);

// The look on the screen: the barber's copy while it is open (preview), else
// the character's own.
bool GameBarberLook(int ch, Appearance* out, uintptr_t* controller, bool* preview);

// The head of the barber's copy swapped on the game's thread (eye colours).
void GameBarberSwapHead(uintptr_t controller, int option);

// Rows the Barber edition adds to the game's settings (tools/barber_lists.py):
// the sliders of Face and Body Shape, 0..100 with 50 the middle, then the eye
// colour. Nearly all at 0 or 1 is a save from before them, not a choice.
static const int BARBER_SHAPE_ROW = 184, BARBER_FACE_ROWS = 24, BARBER_BODY_ROWS = 19, BARBER_EYE_ROW = 227;
// Gender 0 as last chosen, 1 male, 2 female; race 0 as last chosen, then RACE_* + 1;
// height 1..100 with 50 the middle (0: a save from before it).
static const int BARBER_GENDER_ROW = 229, BARBER_RACE_ROW = 230, BARBER_HEIGHT_ROW = 231;
bool GameBarberShapesUnset(const Appearance& look);
#endif

// Which character a controller belongs to: the package gives Kliff's mesh list
// kliffCount options in the marker slot, Damiane's one more, Oongka's two more.
void GameSetMarker(int slot, uint32_t kliffCount);

// Changes a character's desired look. Safe from any thread; applied on the
// next game update of that character.
void GameSetDecoration(int ch, int index, uint8_t value);
void GameSetMesh(int ch, int slot, uint8_t option);
void GameSetDesired(int ch, const Appearance& values, const AppearanceMask& mask);
void GameGetDesired(int ch, Appearance* values, AppearanceMask* mask);

// True if the game currently has the character (their controller is updated).
bool GameCharacterPresent(int ch);

// True when characters are seen without the mod package's marker options and
// none was ever recognised: the package's game files are not loaded (not
// applied in DMM, or another mod replaces the same character files).
bool GamePackageMissing();

// False if the character is present but has no appearance values yet: the
// game creates them on the character's first barber visit (Oongka has none
// until then). Colours and sliders do nothing, meshes still change.
bool GameCharacterHasValues(int ch);

// Copies a character's current look. Returns false if they are not present.
bool GameReadAppearance(int ch, Appearance* out);

// Starting appearance values for a character the game has not given any yet
// (it does on their first barber visit): the plugin creates them.
void GameSetStartValues(int ch, const uint8_t* values);

// Asks for the values to be created (the editor was opened for the character,
// so they are in the world, not on a loading screen).
void GameRequestValues(int ch);

// Writes what the plugin knows about a character's controllers to the log.
void GameLogCharacter(int ch);

#if CC_RESEARCH
// Research: the controller's arrays, and writing one bone value (game.cpp).
void GameResearchArrays(int ch);
void GameResearchBoneGrow(int ch);
void GameResearchBone(int ch, size_t offset, bool object, uint32_t slot, uint32_t value, int width, bool rebuild);
void GameResearchRebuild(int ch);
bool GameResearchStandIn(int ch);   // research builds without the package: a controller stands in for them
#endif

// A player character's appearance controller (Kliff's first), 0 if none yet.
uintptr_t GameMainController();

// Kliff, Damiane or Oongka for an appearance controller, -1 for anyone else.
int GameCharacterOfController(uintptr_t controller);

// The character's own appearance controller, 0 if they are not present.
uintptr_t GameController(int ch);

// Number of options the game has loaded for a character's mesh slot (0 if unknown).
uint32_t GameMeshOptionCount(int ch, int slot);

// Rebuilds a character's head (switching to another head for a moment and
// back), which makes the game read the eye files again. awayOption is the
// head shown in between (-1 = a neighbouring one).
static const int HEAD_AWAY_MS = 1000;
// retry: done once more (away for longer) when the eyes were not read again
// on the way back - wanted for a new eye colour, not for the face shape.
void GameReloadHead(int ch, int awayMs = HEAD_AWAY_MS, int awayOption = -1, bool retry = true);

// Rebuilds the character (what the barber does): shows changed face and body shapes.
void GameRebuild(int ch);

// Picks the head shown during a rebuild when none was given (the menu knows
// which heads have other eyes): chooser(character, current head) -> option.
typedef int (*HeadChooser)(int ch, int currentHead);
void GameSetHeadChooser(HeadChooser chooser);

// True while a character's head rebuild is requested or running (their head
// is briefly another one).
bool GameHeadRebuilding(int ch);

// The character's values sent again and the game's Rebuild, on a game thread, a
// moment after the last request (the barber's way: no head swap).
void GameRequestRebuild(int ch);
