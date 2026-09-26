#include "pch.h"
#include "switches.h"
#include "log.h"

static char g_disabled[512] = { 0 };

void SwitchesLoad(const char* folder)
{
    char path[MAX_PATH];
    sprintf_s(path, "%s\\disable.txt", folder);

    FILE* f = NULL;
    fopen_s(&f, path, "r");

    if (!f)
        return;

    size_t n = fread(g_disabled, 1, sizeof(g_disabled) - 1, f);
    g_disabled[n] = 0;
    fclose(f);
    _strlwr_s(g_disabled);
    Log("disable.txt: %s", g_disabled);
}

bool PartDisabled(const char* part)
{
    return strstr(g_disabled, part) != NULL;
}
