#pragma once

#include "movegen.h"
#include "position.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace search
{
    struct SearchLimits
    {
        int wtime = 0;    // ms remaining for white
        int btime = 0;    // ms remaining for black
        int winc = 0;     // ms increment for white
        int binc = 0;     // ms increment for black
        int movetime = 0; // fixed time per move
        int depth = 0;    // fixed depth (0 = unlimited)
        int nodes = 0;    // total node limit
        int mate = 0;     // search for a mate within this many moves
        bool ponder = false;
        std::vector<Move> searchmoves;
    };

    // Allocate the transposition table and initialize attack tables.
    void init();

    // Clear the transposition table.
    void clear();

    // Run a search (blocking) from the root position with the given limits.
    void go(const Position &root, const SearchLimits &limits);

    // Request the search to stop at the next opportunity.
    void stop();

    // The opponent played the pondered move: continue with real time.
    void ponderhit();

    bool isRunning();

    Move bestMove();
    uint64_t totalNodes();

    // UCI spin option ranges.
    constexpr int HASH_MIN = 1;
    constexpr int HASH_MAX = 65536;
    constexpr int THREADS_MIN = 1;
    constexpr int MULTIPV_MIN = 1;
    constexpr int MULTIPV_MAX = 64;

    int clampOption(const char *name, int value, int minValue, int maxValue);

    // UCI options.
    void setHashSize(int megabytes);
    void setThreads(int count);
    void setMultiPV(int value);
    void setPonder(bool enabled);
    int hashSize();
    int threadCount();
    int maxThreadCount();
    int threadSetting();
    int multiPV();
    bool ponderEnabled();

    // Suppress all search output (used by the benchmark).
    void setSilent(bool enabled);
    std::string tuningOptionsUci();
    bool setTuningOption(const std::string &name, int value);
}
