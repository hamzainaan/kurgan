#include "tt.h"

#include "stats.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace
{
    std::vector<tt::Cluster> table;
    uint64_t clusterMask = 0;
    int generation = 0;

    // Occupancy is counted per thread and flushed in batches.
    std::atomic<uint64_t> filled{0};
    thread_local uint64_t filledLocal = 0;
    constexpr uint64_t FILLED_BATCH = 1024;

    // A proven mate is a property of the position, not of the search that found it.
    bool protectedMate(const tt::Entry &e, uint64_t key, int bound)
    {
        return e.bound() == tt::BOUND_EXACT && e.mate() && (e.key != key || bound != tt::BOUND_EXACT);
    }

    void save(tt::Entry &e, uint64_t key, Move move, int score, int depth, int bound, int ply, int eval)
    {
        if (e.empty() && ++filledLocal == FILLED_BATCH)
        {
            filled.fetch_add(FILLED_BATCH, std::memory_order_relaxed);
            filledLocal = 0;
        }

        // Store mix.
        if (bound == tt::BOUND_EXACT)
            ++stats::current().ttStoresExact;
        else if (bound == tt::BOUND_LOWER)
            ++stats::current().ttStoresLower;
        else
            ++stats::current().ttStoresUpper;

        e.key = key;
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

    table.resize(n);
    clusterMask = n - 1;
    clear();
}

void tt::clear()
{
    if (!table.empty())
        std::memset(static_cast<void *>(table.data()), 0, table.size() * sizeof(Cluster));

    filled.store(0, std::memory_order_relaxed);
    filledLocal = 0;
}

bool tt::allocated()
{
    return !table.empty();
}

void tt::newSearch()
{
    generation = (generation + 1) & 63;
}

tt::Entry *tt::probe(uint64_t key)
{
    if (table.empty())
        return nullptr;

    stats::Tally &tally = stats::current();
    ++tally.ttProbes;

    // A different key in an occupied cluster is an index collision, an empty cluster is simply a miss.
    bool occupied = false;
    for (Entry &e : table[key & clusterMask].entry)
    {
        if (e.key == key)
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
    if (table.empty())
        return;

    Cluster &cluster = table[key & clusterMask];

    // The entry already holding this position wins over an empty slot.
    Entry *freeSlot = nullptr;
    for (Entry &e : cluster.entry)
    {
        if (e.key == key)
        {
            if (!protectedMate(e, key, bound) && (bound == BOUND_EXACT || depth + 4 > e.depth || e.age(generation) != 0))
                save(e, key, move == Move() ? e.move : move, score, depth, bound, ply, eval);
            return;
        }
        if (freeSlot == nullptr && e.empty())
            freeSlot = &e;
    }

    if (freeSlot != nullptr)
    {
        save(*freeSlot, key, move, score, depth, bound, ply, eval);
        return;
    }

    // The cluster is full with other positions.
    Entry *victim = nullptr;
    int worst = 0;
    for (Entry &e : cluster.entry)
    {
        if (protectedMate(e, key, bound))
            continue;

        const int value = e.age(generation) * 256 + e.depth;
        if (victim == nullptr || value < worst)
        {
            worst = value;
            victim = &e;
        }
    }

    if (victim != nullptr)
        save(*victim, key, move, score, depth, bound, ply, eval);
}

int tt::hashfull()
{
    if (table.empty())
        return 0;

    const uint64_t entries = table.size() * CLUSTER_SIZE;
    return static_cast<int>(std::min<uint64_t>(1000, filled.load(std::memory_order_relaxed) * 1000 / entries));
}

void tt::flushFilled()
{
    filled.fetch_add(filledLocal, std::memory_order_relaxed);
    filledLocal = 0;
}
