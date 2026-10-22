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
        // Time values are in milliseconds.
        int wtime = 0;
        int btime = 0;
        int winc = 0;
        int binc = 0;
        int movetime = 0;
        int movestogo = 0; // Moves until the next time control, or 0 for sudden death.
        int depth = 0; // Fixed depth, or 0 for no limit.
        int nodes = 0; // Hard node limit, or 0 for no limit.
        int softNodes = 0; // Stops after the iteration that reaches this node count.
        int mate = 0; // Mate distance goal in moves, or 0 when disabled.
        bool ponder = false;
        bool infinite = false; // Keeps searching until stop is called.
        std::vector<Move> searchmoves; // Restricts the search to these root moves.
    };

    // Initializes attack tables and allocates the transposition table.
    void init();

    // Clears the transposition table and search history.
    void clear();

    // Runs a blocking search from the root position.
    void go(const Position &root, const SearchLimits &limits);

    void stop();

    // Establishes handshake state before the search thread starts, so a
    // pipelined stop or ponderhit cannot race it.
    void prepare(const Position &root, const SearchLimits &limits);

    // Continues a pondered search with the real clock.
    void ponderhit();

    Move bestMove();
    int bestScore();
    uint64_t totalNodes();

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

    // Clamps an option value and reports out-of-range values.
    int clampOption(const char *name, int value, int minValue, int maxValue);

    void setHashSize(int megabytes);
    void setThreads(int count);
    void setMultiPV(int value);
    void setContempt(int value);
    void setAnalyseMode(bool enabled);
    void setShowRefutations(bool enabled);
    void setPonder(bool enabled);
    void setMoveOverhead(int ms);
    int hashSize();
    int threadCount();
    int maxThreadCount();
    int threadSetting();

    // Suppresses all search output.
    void setSilent(bool enabled);
    // Applies a runtime tuning option and returns false when it is unknown.
    bool setTuningOption(const std::string &name, int value);
}
