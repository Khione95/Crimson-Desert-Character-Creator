# Character Creator Enhanced for Crimson Desert

Appearance options for Kliff, Damiane and Oongka, in two editions (install one):

- **Barber (recommended):** every option in the game's own barber screen, with its tabs, lists, sliders and preview. Changes are normal barber changes, kept in the save.
- **Anywhere:** an editor panel of its own that opens anywhere with **F6** (Kliff), **F7** (Damiane) or **F8** (Oongka).

Both have:

- Gender and race (Human, Orc, Dwarf, Goblin) for all three characters, height, skin
- 24 Face Shape sliders and 19 Body Shape sliders
- Every head, hair style and beard of every race and gender, eyebrows, 21 eye colours
- Face and body tattoos, scars, war paint and dirt
- Lip sync that matches when a character is played as the other gender
- **Female Armor Fit** (optional download on Nexus): male armor reshaped for female bodies

**Download:** [Nexus Mods](https://www.nexusmods.com/crimsondesert/mods/837) - or the [Releases](../../releases) page here if a Nexus file is still being reviewed.

## What's in this repository

- `CharacterCreator/` - the plugin (`CharacterCreator.asi`, C++, Visual Studio). `barbertabs.cpp` adds the Barber edition's tabs to the game's barber screen. The Anywhere edition draws its editor panel over the game (DirectX 12 hooks) and changes the player characters' appearance through the game's own functions, hooked with [MinHook](https://github.com/TsudaKageyu/minhook) (`external/minhook`). It reads and writes only its own files in `bin64\CharacterCreator` and makes no network connections.
- `tools/` - Python tools: the menu data build, the release zip, and the Female Armor Fit pipeline (reading the game's archives, skeletons and models, reshaping armor, packing an archive group).
- `release/` - README, third-party notices and the Nexus description.
- `research/` - scripts used while reverse-engineering the game.

## Building

1. `python tools/build_data.py` - builds the menu data the plugin embeds (needs the game installed).
2. Build `CharacterCreator/CharacterCreator/CharacterCreator.vcxproj` (Release, x64) with Visual Studio; add `/p:CcBarber=1` for the Barber edition.
3. `python tools/make_release.py` - assembles the release zips in `dist/`.

## Credits

Khione. Thanks to Slinky. MinHook by Tsuda Kageyu (BSD 2-Clause, see `release/THIRD_PARTY.txt`).
