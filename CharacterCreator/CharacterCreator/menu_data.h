#pragma once

#include <stdint.h>
#include <string>
#include <vector>

// Everything the editor lists, read from <plugin folder>\CharacterCreator\menu.txt
// (written by tools/build_data.py from the mod's XML files).

struct MeshOption
{
    int slot;
    int index[3];           // the option number in Kliff's, Damiane's and Oongka's
                            // list (each starts with the character's own options)
    std::string race;       // phm, phw, pom, pow, pdm, pdw, pgm, pgw, ptm or empty
    bool shown;             // ShowInGame
    std::wstring icon;      // full path of the icon (JPEG), empty if none
    std::string mesh;       // mesh file name
    std::string eyes;       // heads: the part holding the eyes (empty if unknown)
};

struct PaletteColor
{
    uint8_t r, g, b;
    std::wstring name;      // e.g. "Honey 15"
};

struct DecorationParam
{
    bool known;
    int min, max, defaultValue;
    int palette;            // colour or texture palette, -1 if none
};

// A race and gender's base character (the values of its app_xml).
struct BaseCharacter
{
    bool known;
    std::string customization;  // CustomizationFile
    std::string body, scale;    // Nude prefab Name, CharacterScale
    std::string head, headScale;
    std::string hair;
};

struct MenuData
{
    std::vector<MeshOption> meshes;
    std::vector<PaletteColor> palettes[256];
    int textureCounts[256];
    DecorationParam params[250];

    // Race and gender the character is built as (the package's base
    // character and skeleton). -1 if the data does not say.
    int loadedGender;
    int loadedRace;

    // Tells the characters apart (see GameSetMarker): -1 if not in the file.
    int markerSlot;
    int markerCount;

    BaseCharacter bases[2][4];  // [gender][race]
    BaseCharacter own[3];       // [character]: their own look (Kliff, Damiane, Oongka)

    // [character]: starting appearance values (their customization file).
    bool startKnown[3];
    uint8_t start[3][250];
};

bool MenuDataLoad(const char* folder, MenuData* out);
