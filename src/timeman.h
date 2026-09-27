#pragma once

#include "position.h"
#include "search.h"

#include <chrono>
#include <cstdint>

namespace manager
{
    void computeDeadline(const search::SearchLimits &limits, Color us);
    void recordScore(int score);
    void reset();

    bool budgeted();
    int64_t optimumMs(const Position &pos, int score, int scoreDrop, int stableIterations);

    std::chrono::steady_clock::time_point deadline();
    void postpone();
}
