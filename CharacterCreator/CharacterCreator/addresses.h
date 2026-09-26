#pragma once

#include <stdint.h>

// The game addresses the plugin uses. On the game build they were taken from
// they are known; on any other build (a game update) they are looked up:
//
//  - vtables of classes with RTTI by their class name,
//  - functions, globals and the other vtables by byte patterns of the code
//    that uses them (signatures.inc, made by tools/signatures.py), reading the
//    address from the matched instruction; a function also by its first bytes.
//
// An address none of whose patterns match (or whose patterns disagree) is 0,
// and the part of the plugin that needs it stays off.

enum AddressId
{
    ADDR_SETDECORATION,
    ADDR_QUEUEMESHCHANGE,
    ADDR_REBUILD,
    ADDR_GROWBYTES,
    ADDR_LOADXML,
    ADDR_PARSEAPPEARANCE,
    ADDR_MANAGERPOINTER,
    ADDR_SCALECOMPONENTVTABLE,
    ADDR_SCALEOBJECTVTABLE,
    ADDR_CONTROLLERVTABLE,
    ADDR_MANAGERVTABLE,
    ADDR_LIPSYNCPATH,
    ADDR_COUNT
};

// Looks the addresses up (once, before the game hooks are installed).
void AddressesInit();

// Absolute address in the game, 0 if it could not be found.
uintptr_t AddressOf(AddressId id);

// True on the game build the addresses were taken from.
bool AddressesKnownBuild();

// Whether a function is safe to hook: on the known build its first bytes
// must be the expected ones, unless another mod has put a jump there (then
// the plugin hooks on top of it). Logs which case it was.
bool FunctionHookable(const void* target, const unsigned char* prologue, size_t size, const char* what);
