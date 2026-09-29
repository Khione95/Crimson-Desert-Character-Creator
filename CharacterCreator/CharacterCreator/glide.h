#pragma once

#include <stdint.h>

// Each character glides their own way (Kliff crow wings, Damiane glider,
// Oongka rocket), whatever gender and animations they have (see glide.cpp).
void GlideInit();

// Research: log the states the player's characters enter; add glide states.
void GlideLogStates(bool on);
void GlideExtraStates(const uint32_t* ids, int count);
