#include "pch.h"
#include "parttable.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

static const char TABLE_FOLDER[] = "character/bin__";
static const char TABLE_FILE[] = "partprefabtable.pappt";

static bool ReadFile(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* f = NULL;

    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
        return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 512 * 1024 * 1024)
    {
        fclose(f);
        return false;
    }

    out->resize((size_t)size);
    bool ok = fread(out->data(), 1, out->size(), f) == out->size();
    fclose(f);
    return ok;
}

static bool U32(const std::vector<uint8_t>& d, size_t pos, uint32_t* out)
{
    if (pos + 4 > d.size())
        return false;

    memcpy(out, &d[pos], 4);
    return true;
}

// The group folders in the order the game looks in them: the names at the
// end of meta/0.papgt ("dmmgen", "dmmsa", "0000", ...), each ended by a zero.
static std::vector<std::string> Groups(const std::vector<uint8_t>& papgt)
{
    std::vector<std::string> groups;
    std::string name;

    for (uint8_t c : papgt)
    {
        bool plain = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '_';

        if (plain)
            name += (char)c;
        else
        {
            if (c == 0 && name.size() >= 4)
                groups.push_back(name);

            name.clear();
        }
    }

    return groups;
}

// A name block: pieces of (u32 parent offset or 0xFFFFFFFF, u8 length, text);
// a name is its parent's name followed by its own text.
struct Names
{
    std::map<uint32_t, std::pair<uint32_t, std::string>> pieces;

    std::string Full(uint32_t offset) const
    {
        std::string text;

        for (int depth = 0; depth < 32; ++depth)
        {
            auto it = pieces.find(offset);

            if (it == pieces.end())
                break;

            text = it->second.second + text;

            if (it->second.first == 0xFFFFFFFF)
                break;

            offset = it->second.first;
        }

        return text;
    }
};

static bool ReadNames(const std::vector<uint8_t>& d, size_t start, size_t size, Names* out)
{
    size_t pos = 0;

    while (pos + 5 <= size)
    {
        uint32_t parent;
        memcpy(&parent, &d[start + pos], 4);
        uint8_t length = d[start + pos + 4];

        if (pos + 5 + length > size)
            return false;

        out->pieces[(uint32_t)pos] = { parent, std::string((const char*)&d[start + pos + 5], length) };
        pos += 5 + length;
    }

    return true;
}

// Since the 2026-10-30 update a piece is (varint distance back to its parent
// piece, 0 for none; u8 length; text).
static bool ReadNamesCompact(const std::vector<uint8_t>& d, size_t start, size_t size, Names* out)
{
    size_t pos = 0;

    while (pos < size)
    {
        size_t piece = pos;
        uint32_t back = 0;
        int shift = 0;

        while (true)
        {
            if (pos >= size || shift > 28)
                return false;

            uint8_t b = d[start + pos++];
            back |= (uint32_t)(b & 0x7F) << shift;
            shift += 7;

            if (!(b & 0x80))
                break;
        }

        if (pos >= size)
            return false;

        uint8_t length = d[start + pos++];

        if (pos + length > size || back > piece)
            return false;

        out->pieces[(uint32_t)piece] = { back ? (uint32_t)(piece - back) : 0xFFFFFFFF,
                                         std::string((const char*)&d[start + pos], length) };
        pos += length;
    }

    return true;
}

// The table's unpacked size in one group's index, 0 if the group does not
// have it, -1 if the index could not be read. compact: the index layout of
// the 2026-10-30 update (compact names, extensions in a list at the end).
static long long TableSize(const std::string& pamtPath, bool compact)
{
    std::vector<uint8_t> d;
    uint32_t archives, size, count;

    if (!ReadFile(pamtPath, &d) || !U32(d, 4, &archives) || archives > 4096)
        return -1;

    size_t pos = 12 + (size_t)archives * 12;
    Names folders, files;
    auto read = compact ? ReadNamesCompact : ReadNames;

    if (!U32(d, pos, &size) || pos + 4 + size > d.size() || !read(d, pos + 4, size, &folders))
        return -1;

    pos += 4 + size;

    if (!U32(d, pos, &size) || pos + 4 + size > d.size() || !read(d, pos + 4, size, &files))
        return -1;

    pos += 4 + size;

    if (!U32(d, pos, &count) || pos + 4 + (size_t)count * 16 > d.size())
        return -1;

    size_t folderRows = pos + 4;
    size_t fileTable = folderRows + (size_t)count * 16 + 4;
    uint32_t fileCount = 0;
    std::vector<std::string> extensions;

    if (compact)
    {
        uint32_t listSize;

        if (!U32(d, fileTable - 4, &fileCount) || fileTable + (size_t)fileCount * 20 + 4 > d.size() ||
            !U32(d, fileTable + (size_t)fileCount * 20, &listSize) || fileTable + (size_t)fileCount * 20 + 4 + listSize > d.size())
            return -1;

        const char* list = (const char*)&d[fileTable + (size_t)fileCount * 20 + 4];
        std::string current;

        for (uint32_t i = 0; i < listSize; ++i)
        {
            if (list[i])
                current += list[i];
            else
            {
                extensions.push_back(current);
                current.clear();
            }
        }
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t name, first, n;
        U32(d, folderRows + i * 16 + 4, &name);
        U32(d, folderRows + i * 16 + 8, &first);
        U32(d, folderRows + i * 16 + 12, &n);

        if (folders.Full(name) != TABLE_FOLDER)
            continue;

        for (uint32_t f = first; f < first + n; ++f)
        {
            size_t row = fileTable + (size_t)f * 20;
            uint32_t fileName, unpacked;

            if (!U32(d, row, &fileName) || !U32(d, row + 12, &unpacked))
                return -1;

            std::string full = files.Full(fileName);

            if (compact && row + 20 <= d.size())
            {
                uint8_t ext = d[row + 19];
                full += ext < extensions.size() ? extensions[ext] : "";
            }

            if (full == TABLE_FILE)
                return unpacked;
        }
    }

    return 0;
}

static long long TableSize(const std::string& pamtPath)
{
    long long size = TableSize(pamtPath, true);

    if (size > 0)
        return size;

    long long old = TableSize(pamtPath, false);
    return old > 0 ? old : size;
}

// DMM 4.5 (the game's archive index is signed since game 2.04) changes no
// archive: its plugin serves the files of bin64\dmm\manifest.tsv, lines
// "file <game path> <file>". The table it serves, "" if none.
static std::string ServedTablePath(const std::string& root)
{
    std::vector<uint8_t> manifest;

    if (!ReadFile(root + "\\bin64\\dmm\\manifest.tsv", &manifest))
        return std::string();

    std::string text(manifest.begin(), manifest.end());
    std::string key = std::string("file\t") + TABLE_FOLDER + "/" + TABLE_FILE + "\t";
    size_t at = text.find(key);

    if (at == std::string::npos)
        return std::string();

    at += key.size();
    size_t end = text.find_first_of("\r\n", at);
    return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

// DMM 4.5 merges every mod's new parts into the table, so its size says little:
// the table is the package's if it knows the characters' copies of the heads
// (a sample of them: a table without the copies has none). Names are plain in it.
static bool HasOwnHeads(const std::vector<uint8_t>& table, const char* dataFolder)
{
    FILE* f = NULL;

    if (fopen_s(&f, (std::string(dataFolder) + "\\private_heads.txt").c_str(), "r") != 0 || !f)
        return false;

    const char* start = (const char*)table.data();
    const char* end = start + table.size();
    char line[128];
    int checked = 0, index = 0;
    bool all = true;

    while (all && fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;

        if (strlen(line) < 6 || index++ % 25 != 0)
            continue;

        for (char letter : { 'k', 'd', 'o' })
        {
            // The copies' names: tools/private_eyes.py, eyes.cpp's MarkOf.
            std::string name = line;

            if (name.compare(0, 3, "cd_") == 0)
                name[0] = 'z', name[1] = letter;
            else
                name[4] = letter;

            all = all && std::search(start, end, name.begin(), name.end()) != end;
            ++checked;
        }
    }

    fclose(f);
    return all && checked > 0;
}

bool PartTableIsOurs(const char* gameFolder, const char* dataFolder, const uint32_t* sizes, int count)
{
    std::string root = gameFolder;
    std::vector<uint8_t> papgt;

    // Without the list or the expected sizes nothing can be told: the
    // package's own heads are used (as before this check).
    if (count <= 0 || !ReadFile(root + "\\meta\\0.papgt", &papgt))
    {
        Log("part table: could not be checked - using the characters' own heads");
        return true;
    }

    std::string servedPath = ServedTablePath(root);
    std::vector<uint8_t> servedTable;

    if (!servedPath.empty() && ReadFile(servedPath, &servedTable))
    {
        long long served = (long long)servedTable.size();
        bool ours = false;

        for (int i = 0; i < count; ++i)
            ours = ours || served == sizes[i];

        bool merged = !ours && HasOwnHeads(servedTable, dataFolder);
        Log("part table: DMM's plugin serves one of %lld bytes - %s", served,
            ours ? "the package's (or Female Armor Fit's)" : merged ? "the package's with other mods' parts added"
                 : "another mod's: the characters' own heads (and eye colour) are off");
        return ours || merged;
    }

    for (const std::string& group : Groups(papgt))
    {
        long long size = TableSize(root + "\\" + group + "\\0.pamt");

        if (size <= 0)
            continue;

        bool ours = false;

        for (int i = 0; i < count; ++i)
            ours = ours || size == sizes[i];

        Log("part table: the game loads the one in %s (%lld bytes) - %s", group.c_str(), size,
            ours ? "the package's (or Female Armor Fit's)" : "another mod's: the characters' own heads (and eye colour) are off");
        return ours;
    }

    Log("part table: not found in the archive groups - using the characters' own heads");
    return true;
}
