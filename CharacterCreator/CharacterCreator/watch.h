#pragma once

#include <stdint.h>

// Research (command.txt): hardware breakpoints on up to four 4-byte places.
//   watch <address hex> [address ...]   start (reads and writes are counted)
//   watchreset                          forget the counts so far
//   watchlog                            log the code (rva after the access) and counts
//   unwatch                             clear the breakpoints
// Threads started after "watch" are not watched.
void WatchStart(const uintptr_t* addresses, int count);
void WatchReset();
void WatchLog();
void WatchStop();

// forcetype <reader rva hex> <value hex|off>: that reader gets the value
// instead of the watched one (472B24, 1EC3743, 1F78AB6).
void WatchForce(uintptr_t rva, bool on, uint32_t value);

// forcestate <value hex> <state id hex> ...: entering those states, the
// state reader (1EC3743) gets the value (no ids: off).
void WatchForceStates(const uint32_t* ids, int count, uint32_t value);
