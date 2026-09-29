#include "timeman.h"

#include "tt.h"

#include <algorithm>
#include <bit>

namespace manager
{
    constexpr double PRESSURE_EMA = 0.5;
    constexpr double PRESSURE_SCALE = 120.0;
    constexpr double SWING_SCALE = 50.0;
    constexpr double STABLE_ITERATIONS = 6.0;
    constexpr double DECIDED_SCORE = 500.0;
    constexpr double TIME_SCALE_MIN = 0.75;
    constexpr double TIME_SCALE_MAX = 1.35;
    constexpr double PRESSURE_W = 0.40;
    constexpr double SWING_W = 0.25;
    constexpr double INSTABILITY_W = 0.20;
    constexpr double STAGE_W = 0.15;
    constexpr double DECIDED_W = 0.25;

    std::chrono::steady_clock::time_point deadlineTime;
    int64_t baseOptimumMs = 0;
    int64_t lastScore = 0;
    bool hasLastScore = false;
    double pressure = 0.0;
    int64_t moveOverheadMs = search::MOVE_OVERHEAD_DEFAULT;

    double criticality(const Position &pos, int score, int scoreDrop, int stableIterations)
    {
        const int swing = scoreDrop < 0 ? -scoreDrop : scoreDrop;
        const int decidedScore = score < 0 ? -score : score;

        const double pressureTerm = std::min(1.0, pressure / PRESSURE_SCALE);
        const double swingTerm = std::min(1.0, swing / SWING_SCALE);
        const double instability = 1.0 - std::min(1.0, stableIterations / STABLE_ITERATIONS);

        const double pieces = static_cast<double>(std::popcount(pos.byColor[WHITE] | pos.byColor[BLACK]));
        const double phase = std::clamp((32.0 - pieces) / 20.0, 0.0, 1.0);
        const double stage = 1.0 - 2.0 * (phase < 0.5 ? 0.5 - phase : phase - 0.5);

        const double decided = std::min(1.0, decidedScore / DECIDED_SCORE);

        return std::clamp(PRESSURE_W * pressureTerm + SWING_W * swingTerm + INSTABILITY_W * instability
                              + STAGE_W * stage - DECIDED_W * decided,
                          0.0, 1.0);
    }

    void computeDeadline(const search::SearchLimits &limits, Color us)
    {
        const auto start = std::chrono::steady_clock::now();
        baseOptimumMs = 0;

        if (limits.movetime > 0)
        {
            deadlineTime = start + std::chrono::milliseconds(std::max<int64_t>(1, limits.movetime - moveOverheadMs));
        }
        else if (limits.wtime > 0 || limits.btime > 0)
        {
            const int64_t myTime = us == WHITE ? limits.wtime : limits.btime;
            const int64_t myInc = us == WHITE ? limits.winc : limits.binc;
            const int64_t available = std::max<int64_t>(1, myTime - moveOverheadMs);

            baseOptimumMs = available / 40 + myInc / 2;

            int64_t maximum = baseOptimumMs * 2;
            if (maximum > available / 8)
                maximum = available / 8;
            if (maximum < 1)
                maximum = 1;
            if (baseOptimumMs > maximum)
                baseOptimumMs = maximum;
            if (baseOptimumMs < 1)
                baseOptimumMs = 1;

            deadlineTime = start + std::chrono::milliseconds(maximum);
        }
        else
        {
            deadlineTime = start + std::chrono::hours(24);
        }
    }

    void setMoveOverhead(int ms)
    {
        moveOverheadMs = ms;
    }

    void recordScore(int score)
    {
        const int64_t scoreCp = lastScore < 0 ? -lastScore : lastScore;
        const int64_t completedCp = score < 0 ? -score : score;

        if (hasLastScore && scoreCp < tt::MATE_THRESHOLD && completedCp < tt::MATE_THRESHOLD)
        {
            const int64_t drop = lastScore > score ? lastScore - score : 0;
            pressure += PRESSURE_EMA * (static_cast<double>(drop) - pressure);
        }

        lastScore = score;
        hasLastScore = true;
    }

    void reset()
    {
        lastScore = 0;
        hasLastScore = false;
        pressure = 0.0;
    }

    bool budgeted()
    {
        return baseOptimumMs > 0;
    }

    int64_t optimumMs(const Position &pos, int score, int scoreDrop, int stableIterations)
    {
        const double scale = TIME_SCALE_MIN
                             + (TIME_SCALE_MAX - TIME_SCALE_MIN)
                                   * criticality(pos, score, scoreDrop, stableIterations);
        return static_cast<int64_t>(static_cast<double>(baseOptimumMs) * scale);
    }

    std::chrono::steady_clock::time_point deadline()
    {
        return deadlineTime;
    }

    void postpone()
    {
        deadlineTime = std::chrono::steady_clock::now() + std::chrono::hours(24);
    }
}
