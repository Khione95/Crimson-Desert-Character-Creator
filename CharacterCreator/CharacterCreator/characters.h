#pragma once

// The three playable characters. Each has their own look, eye colour, gender
// and race, kept in their own files (see CharacterFile).
enum Character
{
    CHAR_KLIFF,
    CHAR_DAMIANE,
    CHAR_OONGKA,
    CHARACTER_COUNT
};

static const wchar_t* const CHARACTER_NAMES[CHARACTER_COUNT] = { L"Kliff", L"Damiane", L"Oongka" };

// Data file of a character: "profile" -> profile.txt for Kliff (the name the
// mod has always used), profile_damiane.txt, profile_oongka.txt.
inline void CharacterFile(char* out, size_t size, const char* folder, const char* name, int character)
{
    static const char* const SUFFIX[CHARACTER_COUNT] = { "", "_damiane", "_oongka" };
    sprintf_s(out, size, "%s\\%s%s.txt", folder, name, SUFFIX[character]);
}
