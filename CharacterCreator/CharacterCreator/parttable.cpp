#include "pch.h"
#include "parttable.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

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

// The table's unpacked size in one group's index, 0 if the group does not
// have it, -1 if the index could not be read.
static long long TableSize(const std::string& pamtPath)
{
    std::vector<uint8_t> d;
    uint32_t archives, size, count;

    if (!ReadFile(pamtPath, &d) || !U32(d, 4, &archives) || archives > 4096)
        return -1;

    size_t pos = 12 + (size_t)archives * 12;
    Names folders, files;

    if (!U32(d, pos, &size) || pos + 4 + size > d.size() || !ReadNames(d, pos + 4, size, &folders))
        return -1;

    pos += 4 + size;

    if (!U32(d, pos, &size) || pos + 4 + size > d.size() || !ReadNames(d, pos + 4, size, &files))
        return -1;

    pos += 4 + size;

    if (!U32(d, pos, &count) || pos + 4 + (size_t)count * 16 > d.size())
        return -1;

    size_t folderRows = pos + 4;
    size_t fileTable = folderRows + (size_t)count * 16 + 4;

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

            if (files.Full(fileName) == TABLE_FILE)
                return unpacked;
        }
    }

    return 0;
}

bool PartTableIsOurs(const char* gameFolder, uint32_t expectedSize)
{
    std::string root = gameFolder;
    std::vector<uint8_t> papgt;

    // Without the list or the expected size nothing can be told: the
    // package's own heads are used (as before this check).
    if (!expectedSize || !ReadFile(root + "\\meta\\0.papgt", &papgt))
    {
        Log("part table: could not be checked - using the characters' own heads");
        return true;
    }

    for (const std::string& group : Groups(papgt))
    {
        long long size = TableSize(root + "\\" + group + "\\0.pamt");

        if (size <= 0)
            continue;

        bool ours = size == expectedSize;
        Log("part table: the game loads the one in %s (%lld bytes) - %s", group.c_str(), size,
            ours ? "the package's" : "another mod's: the characters' own heads (and eye colour) are off");
        return ours;
    }

    Log("part table: not found in the archive groups - using the characters' own heads");
    return true;
}
