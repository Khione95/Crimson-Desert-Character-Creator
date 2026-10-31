// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include "framework.h"

// The research tools (command.txt: memory dumps and searches, hardware
// breakpoints) are in research builds only (Debug): a release has none of
// them - they are what antivirus programs take a game mod for malware by.
#ifndef CC_RESEARCH
#define CC_RESEARCH 0
#endif

// The Barber edition (msbuild /p:CcBarber=1): no editor and no hotkeys - the
// look is changed at the game's barber, which shows the package's lists, and
// the plugin keeps no look of its own. Everything else (glide, two-handed
// grip, part table, eyes) is the same in both editions.
#ifndef CC_BARBER
#define CC_BARBER 0
#endif

#endif //PCH_H
