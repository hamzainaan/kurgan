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
        int movestogo = 0; // moves until the next time control (0 = sudden death)
        int depth = 0;    // fixed depth (0 = unlimited)
        int nodes = 0;    // total node limit
        int softNodes = 0;
        int mate = 0;     // search for a mate within this many moves
        bool ponder = false;
        // `go infinite`: keep searching until `stop`, never answer on our own.
        bool infinite = false;
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

    // Establish per-search handshake state on the command thread before the
    // search thread is launched, so a pipelined stop/ponderhit cannot race it.
    void prepare(const Position &root, const SearchLimits &limits);

    // The opponent played the pondered move: continue with real time.
    void ponderhit();

    Move bestMove();
    int bestScore();
    uint64_t totalNodes();

    // UCI spin option ranges.
    constexpr int HASH_MIN = 1;
    constexpr int HASH_MAX = 65536;
    constexpr int THREADS_MIN = 1;
    constexpr int MULTIPV_MIN = 1;
    constexpr int MULTIPV_MAX = 64;
    constexpr int CONTEMPT_MIN = -200;
    constexpr int CONTEMPT_MAX = 200;
    constexpr int MOVE_OVERHEAD_MIN = 0;
    constexpr int MOVE_OVERHEAD_MAX = 5000;
    constexpr int MOVE_OVERHEAD_DEFAULT = 10;

    int clampOption(const char *name, int value, int minValue, int maxValue);

    // UCI options.
    void setHashSize(int megabytes);
    void setThreads(int count);
    void setMultiPV(int value);
    void setContempt(int value);
    void setPonder(bool enabled);
    void setMoveOverhead(int ms);
    int hashSize();
    int threadCount();
    int maxThreadCount();
    int threadSetting();

    // Suppress all search output (used by the benchmark).
    void setSilent(bool enabled);
    bool setTuningOption(const std::string &name, int value);
}
