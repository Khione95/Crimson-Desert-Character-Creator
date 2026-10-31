#include "pch.h"
#include "faceshape.h"
#include "game.h"
#include "log.h"
#include "switches.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

struct FaceMove
{
    uint8_t slider;
    uint32_t bone;
    float dx, dy, dz;
};

struct FacePrint
{
    int gender;         // 0 men's rig, 1 women's
    float pos[9];       // FACE_PRINT_BONES' positions
    float tag[3];       // FACE_TAG_BONE's, unmarked
};

// Body Shape (data only so far; the writing and the editor tab are to come).
struct BodyMove
{
    uint8_t slider;
    uint32_t bone;
    float sx, sy, sz;   // scale added on the bone's local axes at +100
};

struct BodyPrint
{
    int gender;
    float tag[3];       // BODY_TAG_BONE's position, unmarked
    int records;
};

#include "faceshape_data.inc"

// A loaded face shape: records of 0x90 bytes, one per face bone.
static const size_t RECORD = 0x90;
static const size_t RECORD_MATRIX_POSITION = 0x30;  // the local matrix's last row
static const size_t RECORD_POSITION = 0x5C;
static const size_t RECORD_HASH = 0x80;
static const float PRINT_TOLERANCE = 3e-4f;         // the loaded floats differ from the file's slightly

// A refresh waits for the character to have been in the world this long (a
// head swap during loading froze the loading screen, see game.cpp).
static const DWORD SETTLE_MS = 20000;
static const DWORD QUIET_MS = 350;                  // after the last slider change

static SRWLOCK g_lock = SRWLOCK_INIT;
static int8_t g_value[CHARACTER_COUNT][FACE_SLIDER_COUNT];
static int8_t g_savedValue[CHARACTER_COUNT][FACE_SLIDER_COUNT];
static LONG g_version[CHARACTER_COUNT];
static LONG g_written[CHARACTER_COUNT];             // the version last written (0: none to write)
static DWORD g_changedAt[CHARACTER_COUNT];

struct Block
{
    uintptr_t base;                     // the first record
    int print;
    int mark;                           // the copy's mark: 1 Kliff's, 2 Damiane's, 3 Oongka's, 0 the game's
    std::vector<uint32_t> hashes;       // per record
    std::vector<float> original;        // 3 floats per record: the shape as loaded
    std::vector<float> written;         // what was written last
};

static std::vector<Block> g_blocks;
static int g_shownPrints[CHARACTER_COUNT][2] = { { -2, -2 }, { -2, -2 }, { -2, -2 } };
static DWORD g_presentSince[CHARACTER_COUNT];
static DWORD g_refreshDue[CHARACTER_COUNT];
static bool g_rebuildSeen[CHARACTER_COUNT];
static DWORD g_nextSearch[CHARACTER_COUNT];        // a shape not loaded yet is looked for again later
static const DWORD SEARCH_AGAIN_MS = 5000;
static int g_lastMeshes[CHARACTER_COUNT] = { -2, -2, -2 };
static DWORD g_rescanAt[CHARACTER_COUNT];
static int g_rescans[CHARACTER_COUNT];
static const DWORD RESCAN_AFTER_MS = 1200;
static const DWORD RESCAN_AGAIN_MS = 3000;
static bool g_bodySwapped[CHARACTER_COUNT];        // once per time in the world
static DWORD g_bodyBackAt[CHARACTER_COUNT];
static uint8_t g_bodyBackTo[CHARACTER_COUNT];
static const DWORD BODY_AWAY_MS = 1000;

static bool Valid(int ch, int slider)
{
    return ch >= 0 && ch < CHARACTER_COUNT && slider >= 0 && slider < FACE_SLIDER_COUNT;
}

int FaceSliderCount()
{
    return FACE_SLIDER_COUNT;
}

int FaceValue(int ch, int slider)
{
    return Valid(ch, slider) ? g_value[ch][slider] : 0;
}

bool FaceAny(int ch)
{
    for (int i = 0; ch >= 0 && ch < CHARACTER_COUNT && i < FACE_SLIDER_COUNT; ++i)
        if (g_value[ch][i])
            return true;

    return false;
}

static int Clamp(int value)
{
    return value < -100 ? -100 : value > 100 ? 100 : value;
}

void FaceSetValue(int ch, int slider, int value)
{
    if (!Valid(ch, slider))
        return;

    AcquireSRWLockExclusive(&g_lock);
    g_value[ch][slider] = (int8_t)Clamp(value);
    g_changedAt[ch] = GetTickCount();
    ++g_version[ch];
    ReleaseSRWLockExclusive(&g_lock);
}

void FaceResetAll(int ch)
{
    for (int i = 0; i < FACE_SLIDER_COUNT; ++i)
        if (FaceValue(ch, i))
            FaceSetValue(ch, i, 0);
}

void FaceSetLoaded(int ch, int slider, int value)
{
    if (!Valid(ch, slider))
        return;

    g_value[ch][slider] = g_savedValue[ch][slider] = (int8_t)Clamp(value);
    ++g_version[ch];
}

bool FaceChangedSinceSave(int ch)
{
    return ch >= 0 && ch < CHARACTER_COUNT && memcmp(g_value[ch], g_savedValue[ch], sizeof(g_value[ch])) != 0;
}

void FaceMarkSaved(int ch)
{
    if (ch >= 0 && ch < CHARACTER_COUNT)
        memcpy(g_savedValue[ch], g_value[ch], sizeof(g_value[ch]));
}

// ---------------------------------------------------------------------------
// Finding a loaded face shape
// ---------------------------------------------------------------------------

static std::unordered_set<uint32_t> g_faceBones;

static bool IsFaceBone(uint32_t hash)
{
    if (g_faceBones.empty())
        g_faceBones.insert(FACE_BONES, FACE_BONES + sizeof(FACE_BONES) / sizeof(FACE_BONES[0]));

    return g_faceBones.count(hash) != 0;
}

static bool Near3(const float* a, const float* b)
{
    for (int i = 0; i < 3; ++i)
        if (fabsf(a[i] - b[i]) > PRINT_TOLERANCE)
            return false;

    return true;
}

// The block around a record of the print's first bone, if it is that shape
// with that mark (each character's lists name their own copy of a shape, its
// tag bone moved by FACE_TAG_STEP x the mark, tools/private_shapes.py): its
// first record and record count. (Plain memory reads, inside ScanRegion's
// __try: no C++ objects here.)
static bool BlockBounds(uintptr_t record, uintptr_t start, uintptr_t end, int print, int mark, uintptr_t* firstOut,
                        size_t* countOut)
{
    uintptr_t first = record;

    while (first - RECORD >= start && IsFaceBone(*(const uint32_t*)(first - RECORD + RECORD_HASH)))
        first -= RECORD;

    uintptr_t last = record;

    while (last + 2 * RECORD <= end && IsFaceBone(*(const uint32_t*)(last + RECORD + RECORD_HASH)))
        last += RECORD;

    size_t count = (last - first) / RECORD + 1;

    if (count < 150 || count > 300)
        return false;

    const FacePrint& p = FACE_PRINTS[print];
    int matched = 0;
    bool marked = false;

    for (size_t i = 0; i < count; ++i)
    {
        uintptr_t r = first + i * RECORD;
        uint32_t hash = *(const uint32_t*)(r + RECORD_HASH);

        // A live shape has each position twice (the matrix's last row, then
        // the position) and the matrix's corner 1: a copy left in freed
        // memory is not written to.
        const float* m = (const float*)(r + RECORD_MATRIX_POSITION);
        const float* q = (const float*)(r + RECORD_POSITION);

        if (fabsf(m[0] - q[0]) > 1e-5f || fabsf(m[1] - q[1]) > 1e-5f || fabsf(m[2] - q[2]) > 1e-5f || m[3] != 1.0f)
            return false;

        for (int b = 0; b < 3; ++b)
            if (hash == FACE_PRINT_BONES[b] && Near3(q, p.pos + 3 * b))
                ++matched;

        if (hash == FACE_TAG_BONE)
            marked = fabsf(q[0] - p.tag[0]) <= PRINT_TOLERANCE && fabsf(q[1] - p.tag[1]) <= PRINT_TOLERANCE &&
                     fabsf(q[2] - (p.tag[2] + mark * FACE_TAG_STEP)) <= FACE_TAG_STEP / 3;
    }

    // Two of the three: the game moves some eye bones of a loaded shape itself.
    if (matched < 2 || !marked)
        return false;

    *firstOut = first;
    *countOut = count;
    return true;
}

// The shapes of a print with a mark in one memory region (at most max of them).
static int ScanRegion(uintptr_t start, size_t size, int print, int mark, uintptr_t* firsts, size_t* counts, int max)
{
    int found = 0;

    __try
    {
        uintptr_t end = start + size;

        for (uintptr_t r = (start + 15) & ~(uintptr_t)15; r + RECORD <= end && found < max; r += 16)
        {
            if (*(const uint32_t*)(r + RECORD_HASH) != FACE_PRINT_BONES[0] ||
                !Near3((const float*)(r + RECORD_POSITION), FACE_PRINTS[print].pos))
                continue;

            if (BlockBounds(r, start, end, print, mark, &firsts[found], &counts[found]))
            {
                r = firsts[found] + counts[found] * RECORD - 16;
                ++found;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return found;
}

static bool CopyRecords(uintptr_t first, size_t count, uint32_t* hashes, float* positions)
{
    __try
    {
        for (size_t i = 0; i < count; ++i)
        {
            uintptr_t r = first + i * RECORD;
            hashes[i] = *(const uint32_t*)(r + RECORD_HASH);
            memcpy(&positions[i * 3], (const void*)(r + RECORD_POSITION), 12);
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static std::vector<Block> FindShapes(int print, int mark)
{
    std::vector<Block> found;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0x10000;
    DWORD started = GetTickCount();

    while (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)))
    {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE)
        {
            uintptr_t firsts[8];
            size_t counts[8];
            int n = ScanRegion((uintptr_t)mbi.BaseAddress, mbi.RegionSize, print, mark, firsts, counts, 8);

            for (int i = 0; i < n; ++i)
            {
                Block b;
                b.base = firsts[i];
                b.print = print;
                b.mark = mark;
                b.hashes.resize(counts[i]);
                b.original.resize(counts[i] * 3);

                if (CopyRecords(b.base, counts[i], b.hashes.data(), b.original.data()))
                {
                    b.written = b.original;
                    found.push_back(b);
                }
            }
        }

        if (next <= address)
            break;

        address = next;
    }

    Log("face shape: print %d (mark %d) found %d time(s) in %lu ms", print, mark, (int)found.size(), GetTickCount() - started);
    return found;
}

// The block still holds the shape: the same bones. (Its positions are not
// compared: the game moves some eye bones itself, and a shape read again
// after the plugin wrote it would take the sliders for the head's own shape.)
static bool StillThere(const Block& b)
{
    __try
    {
        for (size_t i = 0; i < b.hashes.size(); ++i)
        {
            uintptr_t r = b.base + i * RECORD;
            const float* m = (const float*)(r + RECORD_MATRIX_POSITION);
            const float* q = (const float*)(r + RECORD_POSITION);

            if (*(const uint32_t*)(r + RECORD_HASH) != b.hashes[i] || m[3] != 1.0f ||
                fabsf(m[0] - q[0]) > 1e-5f || fabsf(m[1] - q[1]) > 1e-5f || fabsf(m[2] - q[2]) > 1e-5f)
                return false;
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The shape's positions as first loaded, per print and mark: a copy found later
// may be an old one the sliders were written to (the game keeps those in memory
// looking alive, and reads the file again when a head comes back).
static std::unordered_map<int, std::vector<float>> g_faceBase;

// The loaded copies of a print with a mark: the known ones still there. A new
// search if there are none, or on rescan (a head or body swapped, or a head
// rebuilt: the game may have read the shape again, and uses that copy).
static std::vector<Block*> Shapes(int print, int mark, bool rescan = false)
{
    std::vector<Block*> out;

    for (size_t i = 0; i < g_blocks.size();)
    {
        if (!StillThere(g_blocks[i]))
        {
            g_blocks.erase(g_blocks.begin() + i);
            continue;
        }

        ++i;
    }

    for (Block& b : g_blocks)
        if (b.print == print && b.mark == mark)
            out.push_back(&b);

    if (!out.empty() && !rescan)
        return out;

    out.clear();

    for (Block& b : FindShapes(print, mark))
    {
        bool known = false;

        for (const Block& k : g_blocks)
            known = known || k.base == b.base;

        if (known)
            continue;

        std::vector<float>& base = g_faceBase[print * 4 + mark];

        if (base.empty())
            base = b.original;
        else if (base.size() == b.original.size())
            b.original = base;

        g_blocks.push_back(b);
    }

    for (Block& b : g_blocks)
        if (b.print == print && b.mark == mark)
            out.push_back(&b);

    return out;
}

// ---------------------------------------------------------------------------
// Writing the sliders
// ---------------------------------------------------------------------------

// Writes the positions that differ from the last written ones (both copies
// of each: the matrix's last row and the position).
static bool WritePositions(uintptr_t base, size_t count, const float* target, const float* written)
{
    __try
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (memcmp(&target[i * 3], &written[i * 3], 12) == 0)
                continue;

            uintptr_t r = base + i * RECORD;
            memcpy((void*)(r + RECORD_MATRIX_POSITION), &target[i * 3], 12);
            memcpy((void*)(r + RECORD_POSITION), &target[i * 3], 12);
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The number of bones moved (0: the shape already as wanted), -1 if it
// could not be written.
static int Write(Block& b, const int8_t* values)
{
    const FaceMove* moves = FACE_PRINTS[b.print].gender ? FACE_MOVES_WOMEN : FACE_MOVES_MEN;
    size_t count = FACE_PRINTS[b.print].gender ? sizeof(FACE_MOVES_WOMEN) / sizeof(FaceMove)
                                               : sizeof(FACE_MOVES_MEN) / sizeof(FaceMove);
    std::unordered_map<uint32_t, size_t> record;

    for (size_t i = 0; i < b.hashes.size(); ++i)
        record[b.hashes[i]] = i;

    std::vector<float> target = b.original;

    for (size_t m = 0; m < count; ++m)
    {
        int v = values[moves[m].slider];
        auto it = record.find(moves[m].bone);

        if (!v || it == record.end())
            continue;

        float* p = &target[it->second * 3];
        p[0] += moves[m].dx * v / 100.0f;
        p[1] += moves[m].dy * v / 100.0f;
        p[2] += moves[m].dz * v / 100.0f;
    }

    int moved = 0;

    for (size_t i = 0; i < b.hashes.size(); ++i)
        moved += memcmp(&target[i * 3], &b.written[i * 3], 12) != 0;

    if (moved && !WritePositions(b.base, b.hashes.size(), target.data(), b.written.data()))
        return -1;

    b.written = target;
    return moved;
}

// ---------------------------------------------------------------------------
// Body Shape
// ---------------------------------------------------------------------------
//
// A body's shape loads like a face's (0x90-byte records). The sliders change
// a bone's scale and its local matrix's rows with it: both show when the
// character is rebuilt (the shape scale, +0x68, does not).

static const size_t RECORD_SCALE = 0x40;
static const size_t RECORD_ROWS = 48;       // the matrix's first three rows, bytes

static int8_t g_bodyValue[CHARACTER_COUNT][BODY_SLIDER_COUNT];
static int8_t g_savedBodyValue[CHARACTER_COUNT][BODY_SLIDER_COUNT];
static bool g_bodyTouched[CHARACTER_COUNT];     // a shape of theirs was changed: it goes back when the sliders do
static bool g_faceTouched[CHARACTER_COUNT];
static int g_shownBody[CHARACTER_COUNT] = { -2, -2, -2 };

struct BodyBlock
{
    uintptr_t base;
    int print;
    int mark;
    std::vector<uint32_t> hashes;
    std::vector<float> rows;        // 12 floats per record, as loaded
    std::vector<float> scale;       // 3 floats per record, as loaded
    std::vector<float> factors;     // 3 floats per record: what is written now
};

static std::vector<BodyBlock> g_bodies;
static std::unordered_set<uint32_t> g_bodyBones;

static bool IsBodyBone(uint32_t hash)
{
    if (g_bodyBones.empty())
        g_bodyBones.insert(BODY_BONES, BODY_BONES + sizeof(BODY_BONES) / sizeof(BODY_BONES[0]));

    return g_bodyBones.count(hash) != 0;
}

int BodySliderCount()
{
    return BODY_SLIDER_COUNT;
}

int BodyValue(int ch, int slider)
{
    return ch >= 0 && ch < CHARACTER_COUNT && slider >= 0 && slider < BODY_SLIDER_COUNT ? g_bodyValue[ch][slider] : 0;
}

bool BodyAny(int ch)
{
    for (int i = 0; ch >= 0 && ch < CHARACTER_COUNT && i < BODY_SLIDER_COUNT; ++i)
        if (g_bodyValue[ch][i])
            return true;

    return false;
}

void BodySetValue(int ch, int slider, int value)
{
    if (ch < 0 || ch >= CHARACTER_COUNT || slider < 0 || slider >= BODY_SLIDER_COUNT)
        return;

    AcquireSRWLockExclusive(&g_lock);
    g_bodyValue[ch][slider] = (int8_t)Clamp(value);
    g_changedAt[ch] = GetTickCount();
    ++g_version[ch];
    ReleaseSRWLockExclusive(&g_lock);
}

void BodySetLoaded(int ch, int slider, int value)
{
    if (ch < 0 || ch >= CHARACTER_COUNT || slider < 0 || slider >= BODY_SLIDER_COUNT)
        return;

    g_bodyValue[ch][slider] = g_savedBodyValue[ch][slider] = (int8_t)Clamp(value);
    ++g_version[ch];
}

bool BodyChangedSinceSave(int ch)
{
    return ch >= 0 && ch < CHARACTER_COUNT && memcmp(g_bodyValue[ch], g_savedBodyValue[ch], sizeof(g_bodyValue[ch])) != 0;
}

void BodyMarkSaved(int ch)
{
    if (ch >= 0 && ch < CHARACTER_COUNT)
        memcpy(g_savedBodyValue[ch], g_bodyValue[ch], sizeof(g_bodyValue[ch]));
}

// The block around a record of the mark bone, if it is the body with that
// mark: the mark bone where it should be, as many records as the file has.
static bool BodyBounds(uintptr_t record, uintptr_t start, uintptr_t end, int print, int mark, uintptr_t* firstOut)
{
    const BodyPrint& p = BODY_PRINTS[print];
    const float* tag = (const float*)(record + RECORD_POSITION);

    if (fabsf(tag[0] - p.tag[0]) > PRINT_TOLERANCE || fabsf(tag[1] - p.tag[1]) > PRINT_TOLERANCE ||
        fabsf(tag[2] - (p.tag[2] + mark * FACE_TAG_STEP)) > FACE_TAG_STEP / 3)
        return false;

    uintptr_t first = record;

    while (first - RECORD >= start && IsBodyBone(*(const uint32_t*)(first - RECORD + RECORD_HASH)))
        first -= RECORD;

    uintptr_t last = record;

    while (last + 2 * RECORD <= end && IsBodyBone(*(const uint32_t*)(last + RECORD + RECORD_HASH)))
        last += RECORD;

    if ((last - first) / RECORD + 1 != (size_t)p.records)
        return false;

    for (uintptr_t r = first; r <= last; r += RECORD)
    {
        const float* m = (const float*)(r + RECORD_MATRIX_POSITION);
        const float* q = (const float*)(r + RECORD_POSITION);

        if (fabsf(m[0] - q[0]) > 1e-5f || fabsf(m[1] - q[1]) > 1e-5f || fabsf(m[2] - q[2]) > 1e-5f || m[3] != 1.0f)
            return false;
    }

    *firstOut = first;
    return true;
}

static int ScanBodyRegion(uintptr_t start, size_t size, int print, int mark, uintptr_t* firsts, int max)
{
    int found = 0;

    __try
    {
        uintptr_t end = start + size;

        for (uintptr_t r = (start + 15) & ~(uintptr_t)15; r + RECORD <= end && found < max; r += 16)
            if (*(const uint32_t*)(r + RECORD_HASH) == BODY_TAG_BONE && BodyBounds(r, start, end, print, mark, &firsts[found]))
                ++found;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return found;
}

static bool CopyBodyRecords(uintptr_t first, size_t count, uint32_t* hashes, float* rows, float* scale)
{
    __try
    {
        for (size_t i = 0; i < count; ++i)
        {
            uintptr_t r = first + i * RECORD;
            hashes[i] = *(const uint32_t*)(r + RECORD_HASH);
            memcpy(&rows[i * 12], (const void*)r, RECORD_ROWS);
            memcpy(&scale[i * 3], (const void*)(r + RECORD_SCALE), 12);
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool BodyStillThere(const BodyBlock& b)
{
    __try
    {
        for (size_t i = 0; i < b.hashes.size(); ++i)
        {
            uintptr_t r = b.base + i * RECORD;
            const float* m = (const float*)(r + RECORD_MATRIX_POSITION);
            const float* q = (const float*)(r + RECORD_POSITION);

            if (*(const uint32_t*)(r + RECORD_HASH) != b.hashes[i] || m[3] != 1.0f ||
                fabsf(m[0] - q[0]) > 1e-5f || fabsf(m[1] - q[1]) > 1e-5f || fabsf(m[2] - q[2]) > 1e-5f)
                return false;
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// A body's rows and scale as first loaded, per print and mark (see g_faceBase).
struct BodyBase
{
    std::vector<float> rows, scale;
};

static std::unordered_map<int, BodyBase> g_bodyBase;

static bool SliderMovesBone(bool women, uint32_t bone)
{
    const BodyMove* moves = women ? BODY_MOVES_WOMEN : BODY_MOVES_MEN;
    size_t count = women ? sizeof(BODY_MOVES_WOMEN) / sizeof(BodyMove) : sizeof(BODY_MOVES_MEN) / sizeof(BodyMove);

    for (size_t m = 0; m < count; ++m)
        if (moves[m].bone == bone)
            return true;

    return false;
}

// A new copy starts from the first loaded values on the bones the sliders
// change: an old copy holds what was written to it, which is written over.
static void FromBodyBase(BodyBlock& b)
{
    BodyBase& base = g_bodyBase[b.print * 4 + b.mark];

    if (base.rows.empty())
    {
        base.rows = b.rows;
        base.scale = b.scale;
        return;
    }

    if (base.rows.size() != b.rows.size())
        return;

    bool women = BODY_PRINTS[b.print].gender != 0;

    for (size_t i = 0; i < b.hashes.size(); ++i)
    {
        if (!SliderMovesBone(women, b.hashes[i]) || (memcmp(&b.rows[i * 12], &base.rows[i * 12], RECORD_ROWS) == 0 &&
                                                     memcmp(&b.scale[i * 3], &base.scale[i * 3], 12) == 0))
            continue;

        memcpy(&b.rows[i * 12], &base.rows[i * 12], RECORD_ROWS);
        memcpy(&b.scale[i * 3], &base.scale[i * 3], 12);
        b.factors[i * 3] = b.factors[i * 3 + 1] = b.factors[i * 3 + 2] = 0;     // differs from any wanted factor
    }
}

static std::vector<BodyBlock*> BodyShapes(int print, int mark, bool rescan = false)
{
    for (size_t i = 0; i < g_bodies.size();)
    {
        if (!BodyStillThere(g_bodies[i]))
        {
            g_bodies.erase(g_bodies.begin() + i);
            continue;
        }

        ++i;
    }

    std::vector<BodyBlock*> out;

    for (BodyBlock& b : g_bodies)
        if (b.print == print && b.mark == mark)
            out.push_back(&b);

    if (!out.empty() && !rescan)
        return out;

    out.clear();

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0x10000;
    DWORD started = GetTickCount();
    int found = 0;

    while (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)))
    {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE)
        {
            uintptr_t firsts[8];
            int n = ScanBodyRegion((uintptr_t)mbi.BaseAddress, mbi.RegionSize, print, mark, firsts, 8);

            for (int i = 0; i < n; ++i)
            {
                bool known = false;

                for (const BodyBlock& k : g_bodies)
                    known = known || k.base == firsts[i];

                if (known)
                    continue;

                size_t count = (size_t)BODY_PRINTS[print].records;
                BodyBlock b;
                b.base = firsts[i];
                b.print = print;
                b.mark = mark;
                b.hashes.resize(count);
                b.rows.resize(count * 12);
                b.scale.resize(count * 3);
                b.factors.assign(count * 3, 1.0f);

                if (CopyBodyRecords(b.base, count, b.hashes.data(), b.rows.data(), b.scale.data()))
                {
                    FromBodyBase(b);
                    g_bodies.push_back(b);
                    ++found;
                }
            }
        }

        if (next <= address)
            break;

        address = next;
    }

    Log("body shape: print %d (mark %d) found %d time(s) in %lu ms", print, mark, found, GetTickCount() - started);

    for (BodyBlock& b : g_bodies)
        if (b.print == print && b.mark == mark)
            out.push_back(&b);

    return out;
}

// Writes the records whose factors changed: the rows (each by its axis's
// factor) and the scale.
static bool WriteBodyRecords(uintptr_t base, size_t count, const float* rows, const float* scale, const float* target, const float* written)
{
    __try
    {
        for (size_t i = 0; i < count; ++i)
        {
            const float* f = &target[i * 3];

            if (memcmp(f, &written[i * 3], 12) == 0)
                continue;

            uintptr_t r = base + i * RECORD;
            float* m = (float*)r;
            float* s = (float*)(r + RECORD_SCALE);

            for (int axis = 0; axis < 3; ++axis)
            {
                for (int k = 0; k < 3; ++k)
                    m[axis * 4 + k] = rows[i * 12 + axis * 4 + k] * f[axis];

                s[axis] = scale[i * 3 + axis] * f[axis];
            }
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static int WriteBody(BodyBlock& b, const int8_t* values)
{
    bool women = BODY_PRINTS[b.print].gender != 0;
    const BodyMove* moves = women ? BODY_MOVES_WOMEN : BODY_MOVES_MEN;
    size_t count = women ? sizeof(BODY_MOVES_WOMEN) / sizeof(BodyMove) : sizeof(BODY_MOVES_MEN) / sizeof(BodyMove);
    std::unordered_map<uint32_t, size_t> record;

    for (size_t i = 0; i < b.hashes.size(); ++i)
        record[b.hashes[i]] = i;

    std::vector<float> target(b.hashes.size() * 3, 1.0f);

    for (size_t m = 0; m < count; ++m)
    {
        int v = values[moves[m].slider];
        auto it = record.find(moves[m].bone);

        if (!v || it == record.end())
            continue;

        float* f = &target[it->second * 3];
        f[0] += moves[m].sx * v / 100.0f;
        f[1] += moves[m].sy * v / 100.0f;
        f[2] += moves[m].sz * v / 100.0f;
    }

    for (float& f : target)
        f = f < 0.2f ? 0.2f : f;

    int moved = 0;

    for (size_t i = 0; i < b.hashes.size(); ++i)
        moved += memcmp(&target[i * 3], &b.factors[i * 3], 12) != 0;

    if (moved && !WriteBodyRecords(b.base, b.hashes.size(), b.rows.data(), b.scale.data(), target.data(), b.factors.data()))
        return -1;

    b.factors = target;
    return moved;
}

// Research builds without the package (CC does not know the characters then):
// the sliders go to the controller that stands in for them (game.cpp), on
// their own head and body.
static bool StandIn(int ch)
{
#if CC_RESEARCH
    return !GameCharacterPresent(ch) && GameResearchStandIn(ch);
#else
    (void)ch;
    return false;
#endif
}

static bool Present(int ch)
{
    return GameCharacterPresent(ch) || StandIn(ch);
}

static void RefreshHead(int ch)
{
#if CC_RESEARCH
    if (StandIn(ch))
    {
        GameResearchRebuild(ch);
        return;
    }
#endif
    GameRequestRebuild(ch);
}

// The prints of the character's head (slot 1) and face details (slot 5).
static void CurrentPrints(int ch, int prints[2])
{
    prints[0] = prints[1] = -1;
    Appearance look;

    if (StandIn(ch))
    {
        prints[0] = FACE_OPTION_PRINT[ch][0][0];
        return;
    }

    if (!GameReadAppearance(ch, &look))
        return;

    if (look.mesh[MESH_HEAD] != MESH_NONE)
        prints[0] = FACE_OPTION_PRINT[ch][0][look.mesh[MESH_HEAD]];

    if (look.mesh[MESH_WHISKERS] != MESH_NONE)
        prints[1] = FACE_OPTION_PRINT[ch][1][look.mesh[MESH_WHISKERS]];

    if (prints[1] == prints[0])
        prints[1] = -1;
}

// The print of the character's body (slot 0; their own when it is empty).
static int CurrentBody(int ch)
{
    Appearance look;
    int own = BODY_OWN_OPTION[ch];

    if (StandIn(ch) || !GameReadAppearance(ch, &look) || look.mesh[MESH_BODY] == MESH_NONE)
        return own >= 0 ? BODY_OPTION_PRINT[ch][own] : -1;

    return BODY_OPTION_PRINT[ch][look.mesh[MESH_BODY]];
}

static void Apply(int ch, DWORD now, bool rescan)
{
    int8_t values[FACE_SLIDER_COUNT];
    int8_t bodyValues[BODY_SLIDER_COUNT];
    AcquireSRWLockShared(&g_lock);
    memcpy(values, g_value[ch], sizeof(values));
    memcpy(bodyValues, g_bodyValue[ch], sizeof(bodyValues));
    LONG version = g_version[ch];
    ReleaseSRWLockShared(&g_lock);

    int prints[2];
    CurrentPrints(ch, prints);
    int shapes = 0, moved = 0;
    bool wantFace = FaceAny(ch) || g_faceTouched[ch];
    bool wantBody = BodyAny(ch) || g_bodyTouched[ch];
    int body = CurrentBody(ch);

    // The character's own copies (their mark); the stand-in of a research
    // build without the package has the game's own shapes.
    int mark = StandIn(ch) ? 0 : ch + 1;

    for (int print : prints)
    {
        if (print < 0 || !wantFace)
            continue;

        for (Block* b : Shapes(print, mark, rescan))
        {
            int n = Write(*b, values);

            if (n >= 0)
            {
                ++shapes;
                moved += n;
            }
        }
    }

    int bodies = 0;

    if (wantBody && body >= 0)
    {
        // Slot 0 empty (Damiane, Oongka: their file gives the body): their own
        // option, so the list's marked copy is the shape they use.
        Appearance look;

        if (!StandIn(ch) && GameReadAppearance(ch, &look) && look.mesh[MESH_BODY] == MESH_NONE && BODY_OWN_OPTION[ch] >= 0)
            GameSetMesh(ch, MESH_BODY, (uint8_t)BODY_OWN_OPTION[ch]);

        for (BodyBlock* b : BodyShapes(body, mark, rescan))
        {
            int n = WriteBody(*b, bodyValues);

            if (n >= 0)
            {
                ++bodies;
                moved += n;
            }
        }
    }

    g_faceTouched[ch] = g_faceTouched[ch] || (wantFace && shapes);
    g_bodyTouched[ch] = g_bodyTouched[ch] || (wantBody && bodies);

    if ((wantFace && !shapes && (prints[0] >= 0 || prints[1] >= 0)) || (wantBody && !bodies && body >= 0))
    {
        // Not loaded yet (or not found): looked for again in a moment. What
        // was found still shows (a body while the head is not found).
        Log("%S's shape not found in memory (face %d: %d, %d; body %d: %d) - looked for again shortly", CHARACTER_NAMES[ch], shapes, prints[0], prints[1], bodies, body);
        g_nextSearch[ch] = now + SEARCH_AGAIN_MS;

        // A body that comes from the character's base file (their own, or the
        // gender's or race's they were built with): chosen in the list it is the
        // same part, so the game does not read the list's marked copy of its
        // shape. Through another body of the skeleton and back, it does.
        Appearance worn;
        int away = -1;

        if (wantBody && !bodies && body >= 0 && !StandIn(ch) && !g_bodySwapped[ch] &&
            (int)(now - g_presentSince[ch] - SETTLE_MS) >= 0 && GameReadAppearance(ch, &worn) && worn.mesh[MESH_BODY] != MESH_NONE)
        {
            for (int option = 0; option < 255 && away < 0; ++option)
            {
                int other = BODY_OPTION_PRINT[ch][option];

                if (option != worn.mesh[MESH_BODY] && other >= 0 && other != body && BODY_PRINT_FOLDER[other] == BODY_PRINT_FOLDER[body])
                    away = option;
            }

            if (away >= 0 && (uint32_t)away < GameMeshOptionCount(ch, MESH_BODY))
            {
                g_bodySwapped[ch] = true;
                g_bodyBackAt[ch] = now + BODY_AWAY_MS;
                g_bodyBackTo[ch] = worn.mesh[MESH_BODY];
                GameSetMesh(ch, MESH_BODY, (uint8_t)away);
                Log("%S: body through option %d and back to %d, to load its shape", CHARACTER_NAMES[ch], away, worn.mesh[MESH_BODY]);
            }
        }
    }
    else
    {
        g_written[ch] = version;
        g_shownPrints[ch][0] = prints[0];
        g_shownPrints[ch][1] = prints[1];
        g_shownBody[ch] = body;
    }

    // Nothing moved (the shape is already as wanted): no refresh.
    if (!moved)
        return;

    // The game applies a face shape when the head is built: refreshed once
    // the character has settled, unless the game rebuilds the head first.
    DWORD earliest = g_presentSince[ch] + SETTLE_MS;
    g_refreshDue[ch] = (int)(earliest - now) > 0 ? earliest : now;
    g_rebuildSeen[ch] = false;
    Log("shape: %S - %d bone(s) changed in %d face and %d body shape(s) (face %d, %d; body %d)", CHARACTER_NAMES[ch], moved, shapes, bodies,
        prints[0], prints[1], body);
}

#if CC_RESEARCH
void FaceResearchTest(int slider, int value, int print)
{
    static int8_t values[FACE_SLIDER_COUNT];

    if (print < 0)
        print = FACE_OPTION_PRINT[CHAR_KLIFF][0][0];

    if (slider < 0 || slider >= FACE_SLIDER_COUNT || print < 0 || print >= (int)(sizeof(FACE_PRINTS) / sizeof(FACE_PRINTS[0])))
    {
        Log("facetest: slider %d / print %d out of range", slider, print);
        return;
    }

    values[slider] = (int8_t)Clamp(value);
    int written = 0;

    for (Block* b : Shapes(print, 0))
        written += Write(*b, values) >= 0 ? 1 : 0;

    Log("facetest: %S = %d on print %d: %d shape(s) written - rebuilding", FACE_SLIDER_NAMES[slider], values[slider], print, written);

    if (written)
        GameResearchRebuild(CHAR_KLIFF);
}
#endif

void FacePoll()
{
    if (PartDisabled("faceshape"))
        return;

    DWORD now = GetTickCount();

    for (int ch = 0; ch < CHARACTER_COUNT; ++ch)
    {
        if (!Present(ch))
        {
            g_presentSince[ch] = 0;
            g_shownPrints[ch][0] = g_shownPrints[ch][1] = -2;
            g_shownBody[ch] = -2;
            g_refreshDue[ch] = 0;
            g_bodySwapped[ch] = false;
            g_bodyBackAt[ch] = 0;
            continue;
        }

        if (!g_presentSince[ch])
            g_presentSince[ch] = now;

        if (g_bodyBackAt[ch])
        {
            if ((int)(now - g_bodyBackAt[ch]) >= 0)
            {
                g_bodyBackAt[ch] = 0;
                GameSetMesh(ch, MESH_BODY, g_bodyBackTo[ch]);
            }

            continue;
        }

        bool rebuilding = GameHeadRebuilding(ch);

        // A head rebuilt, or a head or body swapped: the game may read their
        // shapes again and use the new copies. Looked for twice (the reading
        // can take a moment); the sliders go onto what is found.
        Appearance look;
        int meshes = !StandIn(ch) && GameReadAppearance(ch, &look)
            ? look.mesh[MESH_HEAD] | look.mesh[MESH_WHISKERS] << 8 | look.mesh[MESH_BODY] << 16 : -1;

        if (rebuilding || meshes != g_lastMeshes[ch])
        {
            g_lastMeshes[ch] = meshes;
            g_rescanAt[ch] = now + RESCAN_AFTER_MS;
            g_rescans[ch] = 2;
        }

        if (rebuilding)
        {
            g_rebuildSeen[ch] = true;
            continue;
        }

        if (g_refreshDue[ch] && (int)(now - g_refreshDue[ch]) >= 0)
        {
            if (!g_rebuildSeen[ch])
                RefreshHead(ch);

            g_refreshDue[ch] = 0;
            continue;
        }

        // Nothing to do for a character without sliders whose shape was never changed.
        bool changed = g_written[ch] != g_version[ch];

        if (!changed && !FaceAny(ch) && !BodyAny(ch))
            continue;

        if (g_nextSearch[ch] && (int)(now - g_nextSearch[ch]) < 0)
            continue;

        g_nextSearch[ch] = 0;
        int prints[2];
        CurrentPrints(ch, prints);
        bool otherHead = (FaceAny(ch) && (prints[0] != g_shownPrints[ch][0] || prints[1] != g_shownPrints[ch][1])) ||
            (BodyAny(ch) && CurrentBody(ch) != g_shownBody[ch]);

        bool rescan = g_rescans[ch] && (int)(now - g_rescanAt[ch]) >= 0;

        if (rescan)
        {
            --g_rescans[ch];
            g_rescanAt[ch] = now + RESCAN_AGAIN_MS;
        }

        if ((changed && now - g_changedAt[ch] >= QUIET_MS) || otherHead || rescan)
            Apply(ch, now, rescan);
    }
}
