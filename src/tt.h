#pragma once

#include "movegen.h"

#include <cstddef>
#include <cstdint>

// Transposition table: 4-entry clusters addressed by the low key bits, with a
// depth/age replacement policy. A cluster is exactly one cache line, so a
// probe costs a single miss and the victim search never leaves that line.
namespace tt
{
    constexpr int MATE = 32000;
    constexpr int MATE_THRESHOLD = MATE - 128;

    constexpr int BOUND_NONE = 0;
    constexpr int BOUND_EXACT = 1;
    constexpr int BOUND_LOWER = 2;
    constexpr int BOUND_UPPER = 3;

    constexpr int NO_EVAL = 32767;

    constexpr int CLUSTER_SIZE = 4;

    struct Entry
    {
        uint64_t key = 0;
        Move move{};
        int16_t score = 0;
        int16_t eval = NO_EVAL;
        uint8_t depthPv = 0;
        // 6-bit write generation | 2-bit bound.
        uint8_t genBound = 0;

        bool empty() const { return key == 0; }
        int depth() const { return depthPv & 127; }
        bool pv() const { return (depthPv & 128) != 0; }
        int bound() const { return genBound & 3; }
        bool mate() const { return score >= MATE_THRESHOLD || score <= -MATE_THRESHOLD; }
        int age(int current) const { return (current - (genBound >> 2)) & 63; }
    };

    static_assert(sizeof(Entry) == 16, "an entry must stay 16 bytes");

    struct alignas(64) Cluster
    {
        Entry entry[CLUSTER_SIZE];
    };

    static_assert(sizeof(Cluster) == 64, "a cluster must fit one cache line");

    int scoreFromTT(int score, int ply);
    int scoreToTT(int score, int ply);

    void resize(size_t megabytes);
    void clear();
    bool allocated();
    void newSearch();

    Entry *probe(uint64_t key);

    void store(uint64_t key, Move move, int score, int depth, int bound, int ply, int eval = NO_EVAL, bool pv = false);

    int hashfull();
}