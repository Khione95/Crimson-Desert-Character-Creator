#include "pch.h"
#include "menu_data.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

#include <unordered_map>

static std::wstring Widen(const char* s)
{
    wchar_t buffer[512];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, buffer, 512);
    return buffer;
}

bool MenuDataLoad(const char* folder, MenuData* out)
{
    std::unordered_map<std::string, std::string> eyeParts;     // head mesh -> eye part
    char path[MAX_PATH];
    sprintf_s(path, "%s\\menu.txt", folder);

    FILE* f = NULL;
    fopen_s(&f, path, "r");

    if (!f)
    {
        Log("ERROR: menu data not found (%s)", path);
        return false;
    }

    memset(out->textureCounts, 0, sizeof(out->textureCounts));
    memset(out->params, 0, sizeof(out->params));
    out->loadedGender = -1;
    out->loadedRace = -1;
    out->markerSlot = -1;
    out->markerCount = 0;

    char line[4096];
    int meshes = 0, colors = 0, params = 0;

    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;

        if (line[0] == '#' || !line[0])
            continue;

        char kind[16] = { 0 };
        sscanf_s(line, "%15s", kind, (unsigned)sizeof(kind));

        if (strcmp(kind, "mesh") == 0)
        {
            MeshOption m;
            char race[16], icon[260], mesh[260];
            int shown = 1;

            int fields = sscanf_s(line, "mesh %d %d %15s %d %259s %259s %d %d", &m.slot, &m.index[0],
                race, (unsigned)sizeof(race), &shown, icon, (unsigned)sizeof(icon), mesh, (unsigned)sizeof(mesh),
                &m.index[1], &m.index[2]);

            if (fields < 6)
                continue;

            if (fields < 8)
                m.index[1] = m.index[2] = m.index[0];   // older menu data: one order for everyone

            m.race = strcmp(race, "-") == 0 ? "" : race;
            m.shown = shown != 0;
            m.mesh = mesh;

            if (strcmp(icon, "-") != 0)
            {
                char full[MAX_PATH];
                sprintf_s(full, "%s\\icons\\%s", folder, icon);
                m.icon = Widen(full);
            }

            out->meshes.push_back(m);
            ++meshes;
        }
        else if (strcmp(kind, "color") == 0)
        {
            int palette, index, r, g, b, number;
            char name[64];

            if (sscanf_s(line, "color %d %d %d %d %d %63s %d", &palette, &index, &r, &g, &b,
                name, (unsigned)sizeof(name), &number) != 7 || palette < 0 || palette > 255)
                continue;

            std::vector<PaletteColor>& p = out->palettes[palette];

            if ((int)p.size() <= index)
                p.resize(index + 1);

            wchar_t label[96];
            swprintf_s(label, L"%s %d", Widen(name).c_str(), number);
            p[index] = { (uint8_t)r, (uint8_t)g, (uint8_t)b, label };
            ++colors;
        }
        else if (strcmp(kind, "param") == 0)
        {
            int index, lo, hi, def, palette;
            char key[64];

            if (sscanf_s(line, "param %d %63s %d %d %d %d", &index, key, (unsigned)sizeof(key),
                &lo, &hi, &def, &palette) != 6 || index < 0 || index >= 250)
                continue;

            out->params[index] = { true, lo, hi, def, palette };
            ++params;
        }
        else if (strcmp(kind, "marker") == 0)
        {
            int slot, count;

            if (sscanf_s(line, "marker %d %d", &slot, &count) == 2 && slot >= 0 && slot < 16 && count > 0)
            {
                out->markerSlot = slot;
                out->markerCount = count;
            }
        }
        else if (strcmp(kind, "loaded") == 0)
        {
            int gender, race;

            if (sscanf_s(line, "loaded %d %d", &gender, &race) == 2 && gender >= 0 && gender < 2 && race >= 0 && race < 4)
            {
                out->loadedGender = gender;
                out->loadedRace = race;
            }
        }
        else if (strcmp(kind, "base") == 0)
        {
            int gender, race;
            char v[6][128];

            if (sscanf_s(line, "base %d %d %127s %127s %127s %127s %127s %127s", &gender, &race,
                v[0], 128, v[1], 128, v[2], 128, v[3], 128, v[4], 128, v[5], 128) == 8 &&
                gender >= 0 && gender < 2 && race >= 0 && race < 4)
            {
                auto text = [](const char* x) { return std::string(strcmp(x, "-") == 0 ? "" : x); };
                out->bases[gender][race] = { true, text(v[0]), text(v[1]), text(v[2]), text(v[3]), text(v[4]), text(v[5]) };
            }
        }
        else if (strcmp(kind, "eyes") == 0)
        {
            char mesh[260], part[260];

            if (sscanf_s(line, "eyes %259s %259s", mesh, (unsigned)sizeof(mesh), part, (unsigned)sizeof(part)) == 2)
                eyeParts[mesh] = part;
        }
        else if (strcmp(kind, "start") == 0)
        {
            int ch = -1, used = 0;

            if (sscanf_s(line, "start %d%n", &ch, &used) == 1 && ch >= 0 && ch < 3)
            {
                const char* p = line + used;
                int i = 0;

                for (; i < 250; ++i)
                {
                    int value = 0, n = 0;

                    if (sscanf_s(p, "%d%n", &value, &n) != 1)
                        break;

                    out->start[ch][i] = (uint8_t)value;
                    p += n;
                }

                out->startKnown[ch] = i == 250;
            }
        }
        else if (strcmp(kind, "own") == 0)
        {
            int ch;
            char v[6][128];

            if (sscanf_s(line, "own %d %127s %127s %127s %127s %127s %127s", &ch,
                v[0], 128, v[1], 128, v[2], 128, v[3], 128, v[4], 128, v[5], 128) == 7 && ch >= 0 && ch < 3)
            {
                auto text = [](const char* x) { return std::string(strcmp(x, "-") == 0 ? "" : x); };
                out->own[ch] = { true, text(v[0]), text(v[1]), text(v[2]), text(v[3]), text(v[4]), text(v[5]) };
            }
        }
        else if (strcmp(kind, "texture") == 0)
        {
            int palette, index;

            if (sscanf_s(line, "texture %d %d", &palette, &index) == 2 && palette >= 0 && palette < 256 &&
                index + 1 > out->textureCounts[palette])
                out->textureCounts[palette] = index + 1;
        }
    }

    fclose(f);

    for (MeshOption& m : out->meshes)
    {
        auto part = eyeParts.find(m.mesh);

        if (part != eyeParts.end())
            m.eyes = part->second;
    }

    Log("menu data: %d mesh options, %d colours, %d parameters", meshes, colors, params);
    return meshes > 0;
}
