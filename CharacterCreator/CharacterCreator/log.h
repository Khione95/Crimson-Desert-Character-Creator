#pragma once

// Log file next to the plugin: <game>\bin64\CharacterCreator\CharacterCreator.log
// Opened with shared read access so it can be read while the game runs.

void LogOpen(const char* path);
void Log(const char* fmt, ...);
