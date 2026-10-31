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

    // [gender][race]: the eyebrow and eyelash prefabs of a character built as
    // that (since the 2026-10-30 update a human head has none of its own);
    // empty: none (the head brings its own). browsKnown: the data has them.
    bool browsKnown;
    std::string brows[2][4][2];

    // Saved looks of 9.1.16 and before (profile.cpp converts them once): the
    // option and colour numbers as they are since the 2026-10-30 update; -1:
    // unchanged.
    static const int RENUMBER_SLOTS = 8;
    std::vector<int16_t> renumber;  // [character][slot][old option]
    std::vector<int16_t> recolour;  // [setting][old colour]

    int Renumbered(int ch, int slot, int option) const
    {
        size_t i = ((size_t)ch * RENUMBER_SLOTS + slot) * 256 + option;
        return slot < RENUMBER_SLOTS && option < 256 && i < renumber.size() && renumber[i] >= 0 ? renumber[i] : option;
    }

    int Recoloured(int setting, int colour) const
    {
        size_t i = (size_t)setting * 256 + colour;
        return colour < 256 && i < recolour.size() && recolour[i] >= 0 ? recolour[i] : colour;
    }

    // [character]: starting appearance values (their customization file).
    bool startKnown[3];
    uint8_t start[3][250];
};

bool MenuDataLoad(const char* folder, MenuData* out);
