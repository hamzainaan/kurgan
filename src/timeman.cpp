#include "timeman.h"

#include "movegen.h"
#include "see.h"
#include "tt.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>

namespace manager
{
    using Clock = std::chrono::steady_clock;

    constexpr double PRESSURE_EMA = 0.5;
    constexpr double PRESSURE_SCALE = 120.0;
    constexpr double SWING_SCALE = 50.0;
    constexpr double STABLE_ITERATIONS = 6.0;
    constexpr double DECIDED_SCORE = 500.0;
    constexpr double TENSION_MOVES = 3.0;
    constexpr double TIME_SCALE_MIN = 0.75;
    constexpr double TIME_SCALE_MAX = 1.40;
    constexpr double PRESSURE_W = 0.30;
    constexpr double SWING_W = 0.20;
    constexpr double INSTABILITY_W = 0.20;
    constexpr double STAGE_W = 0.10;
    constexpr double TENSION_W = 0.20;
    constexpr double DECIDED_W = 0.25;

    constexpr double TACTICAL_TENSION_W = 0.40;
    constexpr double TACTICAL_SWING_W = 0.35;
    constexpr double TACTICAL_INSTABILITY_W = 0.25;
    constexpr double TACTICAL_DECIDED_W = 0.30;
    constexpr double EXTEND_THRESHOLD = 0.35;
    constexpr double EXTEND_MIN = 0.15;
    constexpr double EXTEND_MAX = 0.50;
    constexpr double EXTEND_MAX_LOW = 0.80;
    constexpr int MAX_EXTENSIONS = 3;
    constexpr int64_t MIN_EXTENSION_MS = 5;
    constexpr int64_t CEILING_DIV = 5;
    constexpr double CEILING_SCALE = 1.75;
    constexpr double CEILING_SCALE_LOW = 2.50;
    constexpr double LOW_CLOCK_OPTIMUM_MS = 3000.0;
    constexpr double SINGLE_REPLY_SCALE = 0.10;

    constexpr int64_t HORIZON_MOVES = 40;
    constexpr int64_t MAX_CAP_MOVES = 6;
    constexpr int EASY_ITERATIONS = 3;
    constexpr int EASY_SWING = 12;
    constexpr double EASY_SCALE = 0.85;
    constexpr int EASY_MIN_DEPTH = 6;
    constexpr double EASY_DEPTH_PER_LOG2 = 1.0;
    constexpr int EASY_DEPTH_MAX = 18;
    constexpr int DEPTH_ANCHOR_SLACK = 2;
    constexpr double CLOCK_LEAD_W = 0.18;
    constexpr double CLOCK_LEAD_MIN = 0.85;
    constexpr double CLOCK_LEAD_MAX = 1.25;
    constexpr double BANK_W = 0.40;
    constexpr double BANK_REF_MS = 60000.0;
    constexpr double BANK_INCREMENT_MS = 1000.0;
    constexpr double BANK_MAX = 1.40;
    constexpr double DANGER_W = 0.15;
    constexpr double DECIDED_TENSION_W = 0.5;
    constexpr int UNSTABLE_ITERATIONS = 2;
    constexpr double UNSTABLE_SCALE = 1.30;
    constexpr double FALLING_SCALE = 60.0;
    constexpr double FALLING_W = 0.60;
    constexpr double ITERATION_GROWTH = 1.2;

    std::atomic<Clock::rep> startRep{0};
    std::atomic<Clock::rep> deadlineRep{0};
    std::atomic<int64_t> baseOptimumMs{0};
    std::atomic<int64_t> maximumMs{0};
    std::atomic<int64_t> ceilingMs{0};
    std::atomic<int> extensionsUsed{0};
    std::atomic<double> extendMax{EXTEND_MAX};
    std::atomic<bool> failingLow{false};
    std::atomic<int> easyStopDepth{0};

    int64_t lastScore = 0;
    bool hasLastScore = false;
    double pressure = 0.0;
    int previousDepth = 0;
    int64_t moveOverheadMs = search::MOVE_OVERHEAD_DEFAULT;

    double rootTension = 0.0;
    double rootDanger = 0.0;
    bool singleReply = false;

    int observedScore = 0;
    int observedSwing = 0;
    int observedStable = 0;

    double lerp(double from, double to, double t)
    {
        return from + (to - from) * std::clamp(t, 0.0, 1.0);
    }

    void setDeadline(Clock::time_point t)
    {
        deadlineRep.store(t.time_since_epoch().count(), std::memory_order_relaxed);
    }

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

        const double decided = std::min(1.0, decidedScore / DECIDED_SCORE)
                               * (1.0 - DECIDED_TENSION_W * rootTension);

        return std::clamp(PRESSURE_W * pressureTerm + SWING_W * swingTerm + INSTABILITY_W * instability
                              + STAGE_W * stage + TENSION_W * rootTension + DANGER_W * rootDanger
                              - DECIDED_W * decided,
                          0.0, 1.0);
    }

    double tacticality()
    {
        if (observedScore >= tt::MATE_THRESHOLD || observedScore <= -tt::MATE_THRESHOLD)
            return 0.0;

        const int decidedScore = observedScore < 0 ? -observedScore : observedScore;
        const double swingTerm = std::min(1.0, observedSwing / SWING_SCALE);
        const double instability = 1.0 - std::min(1.0, observedStable / STABLE_ITERATIONS);
        const double decided = std::min(1.0, decidedScore / DECIDED_SCORE);

        return std::clamp(TACTICAL_TENSION_W * rootTension + TACTICAL_SWING_W * swingTerm
                              + TACTICAL_INSTABILITY_W * instability - TACTICAL_DECIDED_W * decided,
                          0.0, 1.0);
    }

    int winningCaptures(const Position &pos, bool legalOnly)
    {
        MoveList list;
        movegen::generate_tactical_moves(pos, list);

        int count = 0;
        for (int i = 0; i < list.count(); ++i)
            if ((!legalOnly || movegen::is_legal(pos, list[i])) && see::ge(pos, list[i], 1))
                ++count;
        return count;
    }

    void analyzeRoot(const Position &root, int searchMoveCount)
    {
        const Color us = root.sideToMove;
        const Square ksq = root.kingSquare(us);
        const bool inCheck = ksq != SQ_NONE && movegen::squareAttacked(root, ksq, static_cast<Color>(us ^ 1));

        MoveList list;
        movegen::generate_pseudo_legal_moves(root, list);
        int legal = 0;
        for (int i = 0; i < list.count(); ++i)
            if (movegen::is_legal(root, list[i]))
                ++legal;
        if (searchMoveCount > 0)
            legal = std::min(legal, searchMoveCount);

        singleReply = legal == 1;
        observedScore = 0;
        observedSwing = 0;
        observedStable = 0;

        if (inCheck)
        {
            rootTension = 1.0;
            rootDanger = 1.0;
            return;
        }

        const int gains = winningCaptures(root, true);

        Position flipped = root;
        flipped.do_null_move();
        const int threats = winningCaptures(flipped, false);

        rootTension = std::min(1.0, (gains + threats) / TENSION_MOVES);
        // How much of that tension is the opponent threatening us.
        rootDanger = std::min(1.0, threats / TENSION_MOVES);
    }

    void computeDeadline(const search::SearchLimits &limits, Color us)
    {
        const auto start = Clock::now();
        startRep.store(start.time_since_epoch().count(), std::memory_order_relaxed);
        extensionsUsed.store(0, std::memory_order_relaxed);
        baseOptimumMs.store(0, std::memory_order_relaxed);
        maximumMs.store(0, std::memory_order_relaxed);
        ceilingMs.store(0, std::memory_order_relaxed);
        extendMax.store(EXTEND_MAX, std::memory_order_relaxed);
        failingLow.store(false, std::memory_order_relaxed);
        easyStopDepth.store(0, std::memory_order_relaxed);

        if (limits.movetime > 0)
        {
            setDeadline(start + std::chrono::milliseconds(std::max<int64_t>(1, limits.movetime - moveOverheadMs)));
        }
        else if (limits.wtime > 0 || limits.btime > 0)
        {
            const int64_t myTime = us == WHITE ? limits.wtime : limits.btime;
            const int64_t myInc = us == WHITE ? limits.winc : limits.binc;
            const int64_t oppTime = us == WHITE ? limits.btime : limits.wtime;
            const int64_t oppInc = us == WHITE ? limits.binc : limits.winc;
            const int64_t available = std::max<int64_t>(1, myTime - moveOverheadMs);
            const int64_t oppAvailable = std::max<int64_t>(1, oppTime - moveOverheadMs);
            const int64_t horizon = limits.movestogo > 0 ? std::min<int64_t>(limits.movestogo, HORIZON_MOVES) : HORIZON_MOVES;

            int64_t optimum = available / horizon + myInc / 2;

            const double bankRoom = std::clamp(static_cast<double>(available) / BANK_REF_MS - 1.0, 0.0, 1.0);
            const double incRelief = std::min(1.0, static_cast<double>(myInc) / BANK_INCREMENT_MS);
            const double bank = 1.0 + (BANK_MAX - 1.0) * bankRoom * incRelief;

            const double myBudget = static_cast<double>(available) + 0.5 * static_cast<double>(myInc) * static_cast<double>(horizon);
            const double oppBudget = static_cast<double>(oppAvailable) + 0.5 * static_cast<double>(oppInc) * static_cast<double>(horizon);
            const double ratio = myBudget / std::max(1.0, oppBudget);
            const double clockLead = std::clamp(1.0 + CLOCK_LEAD_W * std::log2(ratio), CLOCK_LEAD_MIN, CLOCK_LEAD_MAX);

            optimum = static_cast<int64_t>(static_cast<double>(optimum) * bank * clockLead);

            const double clockDepth = std::log2(static_cast<double>(std::max<int64_t>(1, available)) / 100.0);
            easyStopDepth.store(std::clamp(static_cast<int>(EASY_MIN_DEPTH + clockDepth * EASY_DEPTH_PER_LOG2),
                                           EASY_MIN_DEPTH, EASY_DEPTH_MAX),
                                std::memory_order_relaxed);

            const double lowClock = std::clamp(1.0 - static_cast<double>(optimum) / LOW_CLOCK_OPTIMUM_MS, 0.0, 1.0);
            extendMax.store(lerp(EXTEND_MAX, EXTEND_MAX_LOW, lowClock), std::memory_order_relaxed);

            int64_t maximum = optimum * 2;
            const int64_t maximumCap = available * 3 / (4 * std::min(horizon, MAX_CAP_MOVES));
            if (maximum > maximumCap)
                maximum = maximumCap;
            if (maximum < 1)
                maximum = 1;

            const double ceilingScale = lerp(CEILING_SCALE, CEILING_SCALE_LOW, lowClock);
            int64_t ceiling = std::min(available / CEILING_DIV, static_cast<int64_t>(static_cast<double>(maximum) * ceilingScale));

            if (singleReply)
            {
                maximum = std::max<int64_t>(1, static_cast<int64_t>(static_cast<double>(maximum) * SINGLE_REPLY_SCALE));
                ceiling = maximum;
            }

            if (optimum > maximum)
                optimum = maximum;
            if (optimum < 1)
                optimum = 1;
            if (ceiling < maximum)
                ceiling = maximum;

            baseOptimumMs.store(optimum, std::memory_order_relaxed);
            maximumMs.store(maximum, std::memory_order_relaxed);
            ceilingMs.store(ceiling, std::memory_order_relaxed);
            setDeadline(start + std::chrono::milliseconds(maximum));
        }
        else
        {
            setDeadline(start + std::chrono::hours(24));
        }
    }

    void setMoveOverhead(int ms)
    {
        moveOverheadMs = ms;
    }

    void observe(int score, int scoreDrop, int stableIterations)
    {
        observedScore = score;
        observedSwing = scoreDrop < 0 ? -scoreDrop : scoreDrop;
        observedStable = stableIterations;
    }

    bool extensible(double tactical)
    {
        return !singleReply && baseOptimumMs.load(std::memory_order_relaxed) > 0
               && extensionsUsed.load(std::memory_order_relaxed) < MAX_EXTENSIONS
               && (tactical >= EXTEND_THRESHOLD || failingLow.load(std::memory_order_relaxed));
    }

    bool extend()
    {
        const double tactical = tacticality();
        if (!extensible(tactical))
            return false;

        const auto now = Clock::now();
        const int64_t used = elapsedMs();
        const int64_t room = ceilingMs.load(std::memory_order_relaxed) - used;

        const double ceilingShare = extendMax.load(std::memory_order_relaxed);
        const double share = failingLow.load(std::memory_order_relaxed)
                                 ? ceilingShare
                                 : lerp(EXTEND_MIN, ceilingShare, (tactical - EXTEND_THRESHOLD) / (1.0 - EXTEND_THRESHOLD));
        const int64_t step = std::min(room, static_cast<int64_t>(static_cast<double>(maximumMs.load(std::memory_order_relaxed)) * share));
        if (step < MIN_EXTENSION_MS)
            return false;

        extensionsUsed.fetch_add(1, std::memory_order_relaxed);
        setDeadline(now + std::chrono::milliseconds(step));
        return true;
    }

    void recordScore(int score, int depth)
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
        previousDepth = std::max(0, depth);
    }

    void reset()
    {
        lastScore = 0;
        hasLastScore = false;
        pressure = 0.0;
        previousDepth = 0;
    }

    bool budgeted()
    {
        return baseOptimumMs.load(std::memory_order_relaxed) > 0;
    }

    int64_t elapsedMs()
    {
        const Clock::time_point start{Clock::duration{startRep.load(std::memory_order_relaxed)}};
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }

    void rootFailing(bool failing)
    {
        failingLow.store(failing, std::memory_order_relaxed);
    }

    bool shouldStop(const Position &pos, int score, int scoreDrop, int stableIterations, int depth, int64_t lastIterationMs)
    {
        const int swing = scoreDrop < 0 ? -scoreDrop : scoreDrop;

        // The cheap exit is gated.
        const int stopDepth = std::max(easyStopDepth.load(std::memory_order_relaxed),
                                       previousDepth - DEPTH_ANCHOR_SLACK);

        double factor = 1.0;
        if (depth >= stopDepth && stableIterations >= EASY_ITERATIONS && swing <= EASY_SWING)
            factor = EASY_SCALE;
        else
        {
            if (stableIterations < UNSTABLE_ITERATIONS)
                factor *= UNSTABLE_SCALE;
            if (scoreDrop > 0)
                factor *= 1.0 + FALLING_W * std::min(1.0, scoreDrop / FALLING_SCALE);
        }

        const double scale = lerp(TIME_SCALE_MIN, TIME_SCALE_MAX, criticality(pos, score, scoreDrop, stableIterations));
        const int64_t maximum = maximumMs.load(std::memory_order_relaxed);
        const int64_t soft = std::min(maximum, static_cast<int64_t>(static_cast<double>(baseOptimumMs.load(std::memory_order_relaxed)) * scale * factor));

        const int64_t elapsed = elapsedMs();
        if (elapsed >= soft)
            return true;

        const Clock::time_point start{Clock::duration{startRep.load(std::memory_order_relaxed)}};
        int64_t reach = std::chrono::duration_cast<std::chrono::milliseconds>(deadline() - start).count();
        if (extensible(tacticality()))
            reach = std::max(reach, ceilingMs.load(std::memory_order_relaxed));

        return static_cast<double>(elapsed) + ITERATION_GROWTH * static_cast<double>(std::max<int64_t>(0, lastIterationMs))
               > static_cast<double>(reach);
    }

    std::chrono::steady_clock::time_point deadline()
    {
        return Clock::time_point{Clock::duration{deadlineRep.load(std::memory_order_relaxed)}};
    }

    void postpone()
    {
        setDeadline(Clock::now() + std::chrono::hours(24));
    }
}
