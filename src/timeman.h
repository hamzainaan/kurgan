#pragma once

#include "position.h"
#include "search.h"

#include <chrono>
#include <cstdint>

namespace manager
{
    void analyzeRoot(const Position &root, int searchMoveCount);
    void computeDeadline(const search::SearchLimits &limits, Color us);
    void setMoveOverhead(int ms);
    void observe(int score, int scoreDrop, int stableIterations);
    bool extend();
    void recordScore(int score);
    void reset();

    bool budgeted();
    int64_t elapsedMs();
    void rootFailing(bool failing);
    bool shouldStop(const Position &pos, int score, int scoreDrop, int stableIterations, int64_t lastIterationMs);

    std::chrono::steady_clock::time_point deadline();
    void postpone();
}
