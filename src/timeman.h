#pragma once

#include "position.h"
#include "search.h"

#include <chrono>
#include <cstdint>

namespace manager
{
    // Samples root tension and danger before the search begins.
    void analyzeRoot(const Position &root, int searchMoveCount);

    // Computes the time budget for the side to move.
    void computeDeadline(const search::SearchLimits &limits, Color us);
    void setMoveOverhead(int ms);

    // Supplies the latest score stability data for time decisions.
    void observe(int score, int scoreDrop, int stableIterations);

    // Extends the deadline for a tactical position and reports success.
    bool extend();
    void recordScore(int score, int depth);
    void reset();

    bool budgeted();
    int64_t elapsedMs();
    void rootFailing(bool failing);

    // Checks whether the current iteration should stop.
    bool shouldStop(const Position &pos, int score, int scoreDrop, int stableIterations, int depth, int64_t lastIterationMs);

    std::chrono::steady_clock::time_point deadline();
    void postpone();
}
