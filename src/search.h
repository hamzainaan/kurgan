#pragma once

#include "movegen.h"
#include "position.h"

#include <atomic>
#include <cstdint>
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

    // UCI options.
    void setHashSize(int megabytes);
    void setThreads(int count);
    void setMultiPV(int value);
    void setPonder(bool enabled);
    int hashSize();
    int threadCount();
    int multiPV();
    bool ponderEnabled();
}
