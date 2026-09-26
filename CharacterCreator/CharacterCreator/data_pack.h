#pragma once

// The menu data and icons travel inside CharacterCreator.asi (a resource), so a
// mod manager only has to install the .asi. At start they are written to the
// data folder - once per version of the plugin.
//
// Returns false if the plugin carries no data (then an existing menu.txt in
// the folder is used, as with a manual install of older versions).
bool DataPackUnpack(HMODULE module, const char* folder);
