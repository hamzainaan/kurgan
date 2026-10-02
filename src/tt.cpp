#include "tt.h"

#include "stats.h"

#include <algorithm>
#include <cstdint>
#include <new>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace
{
    tt::Cluster *table = nullptr;
    size_t clusterCount = 0;
    size_t tableBytes = 0;
    uint64_t clusterMask = 0;
    int generation = 0;
    uint64_t keySalt = 0;
    uint64_t saltState = 0;

    constexpr int CLEAR_GENERATION_STEP = 32;
    constexpr int MATE_PROTECT_AGE = 4;
    constexpr size_t HASHFULL_CLUSTERS = 250;
    constexpr size_t HUGE_PAGE = 2 * 1024 * 1024;

    uint64_t nextSalt()
    {
        uint64_t z = (saltState += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    void *allocateZeroed(size_t bytes)
    {
#if defined(_WIN32)
        return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
        const size_t span = bytes + HUGE_PAGE;
        void *raw = mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (raw == MAP_FAILED)
            return nullptr;

        const uintptr_t base = reinterpret_cast<uintptr_t>(raw);
        const uintptr_t aligned = (base + HUGE_PAGE - 1) & ~static_cast<uintptr_t>(HUGE_PAGE - 1);
        const uintptr_t end = aligned + bytes;
        if (aligned > base)
            munmap(raw, aligned - base);
        if (base + span > end)
            munmap(reinterpret_cast<void *>(end), base + span - end);
#if defined(MADV_HUGEPAGE)
        madvise(reinterpret_cast<void *>(aligned), bytes, MADV_HUGEPAGE);
#endif
        return reinterpret_cast<void *>(aligned);
#endif
    }

    void release(void *memory, size_t bytes)
    {
        if (memory == nullptr)
            return;
#if defined(_WIN32)
        (void)bytes;
        VirtualFree(memory, 0, MEM_RELEASE);
#else
        munmap(memory, bytes);
#endif
    }

    // A proven mate is a property of the position, not of the search that found it.
    bool protectedMate(const tt::Entry &e, uint64_t stored, int bound)
    {
        return e.bound() == tt::BOUND_EXACT && e.mate() && e.age(generation) < MATE_PROTECT_AGE
               && (e.key != stored || bound != tt::BOUND_EXACT);
    }

    void save(tt::Entry &e, uint64_t stored, Move move, int score, int depth, int bound, int ply, int eval)
    {
        // Store mix.
        if (bound == tt::BOUND_EXACT)
            ++stats::current().ttStoresExact;
        else if (bound == tt::BOUND_LOWER)
            ++stats::current().ttStoresLower;
        else
            ++stats::current().ttStoresUpper;

        e.key = stored;
        e.move = move;
        e.score = static_cast<int16_t>(tt::scoreToTT(score, ply));
        e.eval = static_cast<int16_t>(eval);
        e.depth = static_cast<uint8_t>(std::clamp(depth, 0, 255));
        e.genBound = static_cast<uint8_t>((generation << 2) | bound);
    }
}

int tt::scoreFromTT(int score, int ply)
{
    return score >= MATE_THRESHOLD ? score - ply : (score <= -MATE_THRESHOLD ? score + ply : score);
}

int tt::scoreToTT(int score, int ply)
{
    return score >= MATE_THRESHOLD ? score + ply : (score <= -MATE_THRESHOLD ? score - ply : score);
}

void tt::resize(size_t megabytes)
{
    const size_t clusters = megabytes * 1024 * 1024 / sizeof(Cluster);
    size_t n = 1;
    while ((n << 1) <= clusters)
        n <<= 1;

    const size_t bytes = n * sizeof(Cluster);
    void *memory = allocateZeroed(bytes);
    if (memory == nullptr)
        throw std::bad_alloc();

    release(table, tableBytes);
    table = static_cast<Cluster *>(memory);
    clusterCount = n;
    tableBytes = bytes;
    clusterMask = n - 1;
}

void tt::clear()
{
    keySalt = nextSalt();
    generation = (generation + CLEAR_GENERATION_STEP) & 63;
}

bool tt::allocated()
{
    return table != nullptr;
}

void tt::newSearch()
{
    generation = (generation + 1) & 63;
}

tt::Entry *tt::probe(uint64_t key)
{
    if (table == nullptr)
        return nullptr;

    stats::Tally &tally = stats::current();
    ++tally.ttProbes;

    // A different key in an occupied cluster is an index collision, an empty cluster is simply a miss.
    const uint64_t stored = key ^ keySalt;
    bool occupied = false;
    for (Entry &e : table[key & clusterMask].entry)
    {
        if (e.key == stored)
        {
            ++tally.ttHits;
            if (e.bound() == BOUND_EXACT)
                ++tally.ttExact;
            return &e;
        }
        occupied |= e.key != 0;
    }

    if (occupied)
        ++tally.ttCollisions;

    return nullptr;
}

void tt::store(uint64_t key, Move move, int score, int depth, int bound, int ply, int eval)
{
    if (table == nullptr)
        return;

    const uint64_t stored = key ^ keySalt;
    Cluster &cluster = table[key & clusterMask];

    // The entry already holding this position wins over an empty slot.
    Entry *freeSlot = nullptr;
    for (Entry &e : cluster.entry)
    {
        if (e.key == stored)
        {
            if (!protectedMate(e, stored, bound) && (bound == BOUND_EXACT || depth + 4 > e.depth || e.age(generation) != 0))
                save(e, stored, move == Move() ? e.move : move, score, depth, bound, ply, eval);
            return;
        }
        if (freeSlot == nullptr && e.empty())
            freeSlot = &e;
    }

    if (freeSlot != nullptr)
    {
        save(*freeSlot, stored, move, score, depth, bound, ply, eval);
        return;
    }

    // The cluster is full with other positions.
    Entry *victim = nullptr;
    int worst = 0;
    for (Entry &e : cluster.entry)
    {
        if (protectedMate(e, stored, bound))
            continue;

        const int value = e.age(generation) * 256 + e.depth;
        if (victim == nullptr || value < worst)
        {
            worst = value;
            victim = &e;
        }
    }

    if (victim != nullptr)
        save(*victim, stored, move, score, depth, bound, ply, eval);
}

int tt::hashfull()
{
    if (table == nullptr)
        return 0;

    const size_t sampled = std::min(clusterCount, HASHFULL_CLUSTERS);
    size_t used = 0;
    for (size_t i = 0; i < sampled; ++i)
        for (const Entry &e : table[i].entry)
            if (!e.empty() && e.age(generation) == 0)
                ++used;

    return static_cast<int>(used * 1000 / (sampled * CLUSTER_SIZE));
}