#include "search.h"

#include "evaluate.h"
#include "see.h"
#include "tuned_params.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
    constexpr int INF = 32000;
    constexpr int MATE = 32000;
    constexpr int MATE_THRESHOLD = MATE - 128;
    constexpr int MAX_PLY = 128;
    constexpr int MAX_DEPTH = 64;
    constexpr int LMP_MAX_DEPTH = 10;

    // Late-move-pruning thresholds indexed by [improving][depth].
    constexpr int LMP_TABLE[2][LMP_MAX_DEPTH + 1] = {
        {0, 2, 3, 5, 9, 13, 18, 25, 34, 45, 55},
        {0, 5, 6, 9, 14, 21, 30, 41, 55, 69, 84},
    };

    // Runtime-tunable pruning parameters, exposed as UCI spin options.
    struct TuningParam
    {
        const char *name;
        int *value;
        int minValue;
        int maxValue;
    };

    const TuningParam tuningParams[] = {
        {"RFP Depth", &tuned::RFP_DEPTH, 0, 16},
        {"RFP Margin", &tuned::RFP_MARGIN, 0, 400},
        {"Futility Depth", &tuned::FUTILITY_DEPTH, 0, 8},
        {"Futility Margin", &tuned::FUTILITY_MARGIN, 0, 500},
        {"Move Futility Depth", &tuned::MOVE_FUTILITY_DEPTH, 0, 12},
        {"Move Futility Margin", &tuned::MOVE_FUTILITY_MARGIN, 0, 500},
        {"SEE Quiet Depth", &tuned::SEE_QUIET_DEPTH, 0, 16},
        {"NMP Min Depth", &tuned::NMP_MIN_DEPTH, 2, 12},
        {"NMP Base", &tuned::NMP_BASE, 1, 12},
        {"NMP Depth Div", &tuned::NMP_DEPTH_DIV, 1, 16},
        {"NMP Eval Div", &tuned::NMP_EVAL_DIV, 50, 1000},
        {"NMP Verify Depth", &tuned::NMP_VERIFY_DEPTH, 2, 32},
        {"Razor Depth", &tuned::RAZOR_DEPTH, 0, 8},
        {"Razor Margin", &tuned::RAZOR_MARGIN, 0, 600},
        {"ProbCut Depth", &tuned::PROBCUT_DEPTH, 3, 12},
        {"ProbCut Margin", &tuned::PROBCUT_MARGIN, 0, 300},
        {"IID Depth", &tuned::IID_DEPTH, 2, 12},
        {"LMP Depth", &tuned::LMP_DEPTH, 0, 10},
    };

    // Transposition table bounds.
    constexpr int BOUND_NONE = 0;
    constexpr int BOUND_EXACT = 1;
    constexpr int BOUND_LOWER = 2;
    constexpr int BOUND_UPPER = 3;

    struct TTEntry
    {
        uint64_t key = 0;
        Move move;
        int16_t score = 0;
        int16_t depth = 0;
        int8_t bound = 0;
    };

    std::vector<TTEntry> tt;
    uint64_t ttMask = 0;

    // Frequently accessed atomics are isolated on separate cache lines to
    // avoid false sharing: without this, a thread writing to a counter would
    // invalidate the cache line holding stopFlag (read at every node) and
    // per-thread throughput (and reported nps) would drop as threads grow.
    alignas(64) std::atomic<bool> stopFlag{false};
    alignas(64) std::atomic<uint64_t> searchedNodes{0};
    alignas(64) std::atomic<uint64_t> ttFilled{0};
    alignas(64) std::atomic<uint64_t> globalNodes{0};
    alignas(64) std::atomic<bool> searching{false};
    alignas(64) std::atomic<bool> ponderFlag{false};
    alignas(64) std::atomic<bool> ponderHitFlag{false};
    alignas(64) std::atomic<bool> bestmoveEmitted{false};

    std::chrono::steady_clock::time_point deadline;     // hard limit: always stop here
    std::chrono::steady_clock::time_point softDeadline; // optimum: stop early when stable
    std::chrono::steady_clock::time_point searchStart;
    int maxDepth = MAX_DEPTH;
    int hashSizeMb = 16;
    int threadCountSetting = 1;
    int multiPVSetting = 1;
    bool ponderSetting = false;
    int64_t nodesLimit = 0;
    int mateGoal = 0;
    Color activeUs = WHITE;
    search::SearchLimits activeLimits;

    std::mutex outputMutex;
    std::mutex bestMutex;
    bool silentOutput = false;
    int completedDepth = 0;
    Move finalBestMove;

    // Per-thread search state.
    thread_local uint64_t nodes = 0;
    thread_local int seldepth = 0;
    thread_local int workerId = 0;
    thread_local int currentMultiPV = 1;
    thread_local bool inNullVerification = false;
    thread_local std::vector<Move> excludedRootMoves;
    thread_local Move killers[2][MAX_PLY];
    thread_local int history[COLOR_NB][SQUARE_NB][SQUARE_NB];
    thread_local int captureHistory[PIECE_NB][SQUARE_NB][PIECE_TYPE_NB];
    thread_local Move counterMoves[COLOR_NB][SQUARE_NB][SQUARE_NB];
    thread_local Move moveStack[MAX_PLY + 2];
    thread_local int staticEvalStack[MAX_PLY + 2];
    thread_local bool inIID = false;
    thread_local Move pvTable[MAX_PLY][MAX_PLY];
    thread_local int pvLength[MAX_PLY];
    thread_local uint64_t ttFilledLocal = 0;

    int countrZero(Bitboard b)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_ctzll(b);
#elif defined(_MSC_VER)
        unsigned long index = 0;
        _BitScanForward64(&index, b);
        return static_cast<int>(index);
#else
        int n = 0;
        while ((b & 1) == 0)
        {
            b >>= 1;
            ++n;
        }
        return n;
#endif
    }

    Square lsb(Bitboard b)
    {
        return static_cast<Square>(countrZero(b));
    }

    int popCount(Bitboard b)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_popcountll(b);
#elif defined(_MSC_VER)
        return static_cast<int>(__popcnt64(b));
#else
        int n = 0;
        while (b)
        {
            b &= b - 1;
            ++n;
        }
        return n;
#endif
    }

    Square kingSquare(const Position &pos, Color c)
    {
        const Bitboard k = pos.byColor[c] & pos.byType[KING];
        return k ? lsb(k) : SQ_NONE;
    }

    // Hardware concurrency of the machine the engine is running on.
    int hardwareThreads()
    {
        const unsigned n = std::thread::hardware_concurrency();
        return n < 1 ? 1 : static_cast<int>(n);
    }

    // True when neither side has mating material.
    bool isInsufficientMaterial(const Position &pos)
    {
        if (pos.byType[PAWN] || pos.byType[ROOK] || pos.byType[QUEEN])
            return false;

        const Bitboard lightSquares = 0x55AA55AA55AA55AAULL;
        const Bitboard minors = pos.byType[KNIGHT] | pos.byType[BISHOP];
        const int knights = popCount(pos.byType[KNIGHT]);
        const int bishops = popCount(pos.byType[BISHOP]);

        // K vs K, or K + one minor vs K.
        if (knights + bishops <= 1)
            return true;

        // K + two knights vs K cannot force mate.
        if (knights == 2 && bishops == 0 &&
            (popCount(minors & pos.byColor[WHITE]) == 2 || popCount(minors & pos.byColor[BLACK]) == 2))
            return true;

        // All bishops on the same color square.
        if (knights == 0 && bishops >= 2)
        {
            const Bitboard b = pos.byType[BISHOP];
            if (!(b & lightSquares) || !(b & ~lightSquares))
                return true;
        }

        return false;
    }

    int scoreFromTT(int s, int ply)
    {
        return s >= MATE_THRESHOLD ? s - ply : (s <= -MATE_THRESHOLD ? s + ply : s);
    }

    int scoreToTT(int s, int ply)
    {
        return s >= MATE_THRESHOLD ? s + ply : (s <= -MATE_THRESHOLD ? s - ply : s);
    }

    void ttStore(uint64_t key, Move move, int score, int depth, int bound, int ply)
    {
        TTEntry &e = tt[key & ttMask];
        if (e.key == 0)
        {
            // Count newly filled entries locally and flush to the shared
            // counter only periodically: a per-store atomic RMW would
            // serialize all threads on one cache line and tank nps.
            if ((++ttFilledLocal & 1023) == 0)
                ttFilled.fetch_add(1024, std::memory_order_relaxed);
        }
        e.key = key;
        e.move = move;
        e.score = static_cast<int16_t>(scoreToTT(score, ply));
        e.depth = static_cast<int16_t>(depth);
        e.bound = static_cast<int8_t>(bound);
    }

    // Dynamic time management
    void computeDeadline(const search::SearchLimits &limits, Color us)
    {
        searchStart = std::chrono::steady_clock::now();

        if (limits.movetime > 0)
        {
            deadline = searchStart + std::chrono::milliseconds(limits.movetime);
            softDeadline = deadline;
        }
        else if (limits.wtime > 0 || limits.btime > 0)
        {
            const int64_t myTime = us == WHITE ? limits.wtime : limits.btime;
            const int64_t myInc = us == WHITE ? limits.winc : limits.binc;

            int64_t optimum = myTime / 40 + myInc / 2;
            int64_t maximum = optimum * 2;
            if (maximum > myTime / 8)
                maximum = myTime / 8;

            // Never risk flagging
            if (maximum > myTime - 50)
                maximum = myTime - 50;
            if (maximum < 1)
                maximum = 1;
            if (optimum > maximum)
                optimum = maximum;
            if (optimum < 1)
                optimum = 1;

            softDeadline = searchStart + std::chrono::milliseconds(optimum);
            deadline = searchStart + std::chrono::milliseconds(maximum);
        }
        else
        {
            deadline = searchStart + std::chrono::hours(24);
            softDeadline = deadline;
        }
    }

    void checkTime()
    {
        // Accumulate the node bucket flushed by the caller. Doing it here (and
        // not per node) keeps writes to the shared counter infrequent, and it
        // lets quiescence and alpha-beta nodes be counted uniformly.
        searchedNodes.fetch_add(2048, std::memory_order_relaxed);

        if (stopFlag.load(std::memory_order_relaxed))
            return;

        if (nodesLimit > 0 && searchedNodes.load(std::memory_order_relaxed) >= static_cast<uint64_t>(nodesLimit))
        {
            stopFlag.store(true, std::memory_order_relaxed);
            return;
        }

        if (std::chrono::steady_clock::now() < deadline)
            return;

        if (ponderFlag.load(std::memory_order_relaxed) && !ponderHitFlag.load(std::memory_order_relaxed))
        {
            bool expected = false;
            if (bestmoveEmitted.compare_exchange_strong(expected, true))
            {
                Move bm;
                {
                    std::lock_guard<std::mutex> lock(bestMutex);
                    bm = finalBestMove;
                }
                if (!silentOutput)
                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    std::cout << "bestmove " << (bm == Move() ? "0000" : moveToUci(bm)) << std::endl;
                }
            }
            deadline = std::chrono::steady_clock::now() + std::chrono::hours(24);
            return;
        }

        stopFlag.store(true, std::memory_order_relaxed);
    }

    void clearTT()
    {
        std::fill(tt.begin(), tt.end(), TTEntry{});
        ttFilled.store(0, std::memory_order_relaxed);
    }

    void ttResize(size_t megabytes)
    {
        size_t bytes = megabytes * 1024 * 1024;
        size_t entries = bytes / sizeof(TTEntry);
        size_t e = 1;
        while ((e << 1) <= entries)
            e <<= 1;
        tt.resize(e);
        ttMask = e - 1;
        clearTT();
    }

    TTEntry &ttEntry(uint64_t key)
    {
        return tt[key & ttMask];
    }

    bool isTactical(const Position &pos, Move m)
    {
        // Castling is a quiet move whose "to" square holds our own rook.
        return m.isPromotion() || m.isEnPassant() || (!m.isCastling() && pos.board[m.to()] != NO_PIECE);
    }

    // The captured piece, treating en passant as capturing a pawn.
    Piece victimOf(const Position &pos, Move m)
    {
        return m.isEnPassant() ? makePiece(static_cast<Color>(pos.sideToMove ^ 1), PAWN)
                               : pos.board[m.to()];
    }
    int &captureHistoryEntry(const Position &pos, Move m)
    {
        return captureHistory[pos.board[m.from()]][m.to()][typeOf(victimOf(pos, m))];
    }
    int captureScore(const Position &pos, Move m)
    {
        int score = pieceValue(typeOf(victimOf(pos, m))) * 32 + captureHistoryEntry(pos, m) / 16;
        if (m.isPromotion())
            score += pieceValue(m.promoType()) * 32;
        return score;
    }

    // History entries are bounded to [-HISTORY_MAX, HISTORY_MAX].
    constexpr int HISTORY_MAX = 16384;

    void updateHistory(int &h, int bonus)
    {
        bonus = std::clamp(bonus, -HISTORY_MAX, HISTORY_MAX);
        const int magnitude = bonus < 0 ? -bonus : bonus;
        h += bonus - h * magnitude / HISTORY_MAX;
    }

    struct ScoredMove
    {
        int score;
        Move move;
        uint16_t order = 0; // generation index, used as a stable tie-break
    };

    constexpr int TT_MOVE_SCORE = 10000000;
    constexpr int CAPTURE_BAND = 6000000;
    constexpr int KILLER1_SCORE = 5000000;
    constexpr int KILLER2_SCORE = 4900000;
    constexpr int COUNTERMOVE_SCORE = 4800000;

    struct MovePicker
    {
        ScoredMove moves[MoveList::MAX_MOVES];
        Move deferred[MoveList::MAX_MOVES];
        int count = 0;
        int index = 0;
        int deferredCount = 0;
        int deferredIndex = 0;
        Move ttMove;
        int lastSee = 0;          // SEE of the move returned by the last next()
        bool lastSeeValid = false; // false when SEE was not evaluated for it

        void init(const Position &pos, const MoveList &list, Move ttBest, Move counterMove,
                  int ply, bool capturesOnly)
        {
            const Color us = pos.sideToMove;
            ttMove = ttBest;
            count = 0;
            index = 0;
            deferredCount = 0;
            deferredIndex = 0;

            for (int i = 0; i < list.size; ++i)
            {
                const Move m = list.moves[i];
                const bool tactical = isTactical(pos, m);
                if (capturesOnly && !tactical)
                    continue;

                int score;
                if (m == ttMove)
                    score = TT_MOVE_SCORE;
                else if (tactical)
                    score = CAPTURE_BAND + captureScore(pos, m);
                else if (m == killers[0][ply])
                    score = KILLER1_SCORE;
                else if (m == killers[1][ply])
                    score = KILLER2_SCORE;
                else if (m == counterMove)
                    score = COUNTERMOVE_SCORE;
                else
                    score = history[us][m.from()][moveTarget(m)];

                moves[count++] = {score, m, static_cast<uint16_t>(count)};
            }


            std::sort(moves, moves + count, [](const ScoredMove &a, const ScoredMove &b)
                      {
                          if (a.score != b.score)
                              return a.score > b.score;
                          return a.order < b.order;
                      });
        }

        // Next move to search, or Move() when the node is exhausted.
        Move next(const Position &pos)
        {
            while (index < count)
            {
                const Move m = moves[index++].move;
                lastSeeValid = false;

                if (m == ttMove || !isTactical(pos, m))
                    return m;

                lastSee = see::evaluate(pos, m);
                lastSeeValid = true;
                if (lastSee < 0)
                {
                    deferred[deferredCount++] = m;
                    continue;
                }
                return m;
            }

            if (deferredIndex < deferredCount)
            {
                // Every deferred move was rejected by SEE, so the score is
                // known to be negative without re-running the evaluation.
                lastSee = -1;
                lastSeeValid = true;
                return deferred[deferredIndex++];
            }

            lastSeeValid = false;
            return Move();
        }
    };

    bool isExcludedRootMove(Move m)
    {
        for (Move x : excludedRootMoves)
            if (x == m)
                return true;
        return false;
    }

    bool inSearchMoves(Move m)
    {
        if (activeLimits.searchmoves.empty())
            return true;
        for (Move x : activeLimits.searchmoves)
            if (x == m)
                return true;
        return false;
    }

    void printInfo(int depth, int score, uint64_t nodeCount, long long elapsedMs, const Move *pv, int pvLen, int mpv = 1)
    {
        if (silentOutput)
            return;

        std::lock_guard<std::mutex> lock(outputMutex);

        std::cout << "info depth " << depth << " seldepth " << seldepth << " multipv " << mpv;

        if (score >= MATE_THRESHOLD)
            std::cout << " score mate " << (MATE - score + 1) / 2;
        else if (score <= -MATE_THRESHOLD)
            std::cout << " score mate " << -(MATE + score) / 2;
        else
            std::cout << " score cp " << score;

        std::cout << " nodes " << nodeCount;
        if (elapsedMs > 0)
            std::cout << " nps " << (nodeCount * 1000 / elapsedMs);
        const uint64_t filled = ttFilled.load(std::memory_order_relaxed);
        const int permille = tt.empty() ? 0 : static_cast<int>(std::min<uint64_t>(1000, filled * 1000 / tt.size()));
        std::cout << " hashfull " << permille;
        std::cout << " time " << elapsedMs;

        std::cout << " pv";
        for (int i = 0; i < pvLen; ++i)
            std::cout << ' ' << moveToUci(pv[i]);
        std::cout << std::endl;
    }

    // Emit up to 'depth' root moves in a single line (worker 0 only).
    // Called once per depth instead of printing a currmove line per move
    // during the search, which costs nps.
    /*
    void printRootMoves(const Position &pos, int depth)
    {
        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);

        MovePicker picker;
        picker.init(pos, list, Move(), Move(), 0, false);

        const int limit = std::min(picker.count, depth);

        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << "info depth " << depth;
        for (int i = 0; i < limit; ++i)
            std::cout << " currmove " << moveToUci(picker.moves[i].move);
        std::cout << std::endl;
    }
    */
    int quiescence(Position &pos, int alpha, int beta, int ply);

    // Integer floor(log2(n)) for n >= 1.
    int log2Floor(int n)
    {
        int r = 0;
        while (n > 1)
        {
            n >>= 1;
            ++r;
        }
        return r;
    }

    // Late Move Reduction.
    //
    // Components:
    //  - Move-Count-Based LMR: reduction grows with the move index (logarithmic).
    //  - Dynamic LMR:          reduction grows with depth (logarithmic).
    //  - PV / Non-PV LMR:      PV nodes are reduced less.
    //  - CutNode LMR:          non-PV (cut) nodes are reduced more.
    //  - History-Based LMR:    better history score -> less reduction.
    int lmrReduction(bool pvNode, int depth, int moveCount, int historyScore)
    {
        if (moveCount <= 1 || depth < 3)
            return 0;

        // Base: depth and move count scale the reduction.
        int r = (log2Floor(depth) * log2Floor(moveCount)) / 2;

        // PV nodes search the principal variation: reduce less.
        if (pvNode)
            r -= 1;
        else if (moveCount > 6)
            r += 1; // Cut node: expect a beta cutoff, reduce more.

        // Good history -> the move is promising, reduce less.
        if (historyScore < 0)
            r += 1;
        else if (historyScore > 2048)
            r -= 1;

        return std::clamp(r, 0, depth - 1);
    }

    int alphaBeta(Position &pos, int depth, int alpha, int beta, int ply, bool pvNode)
    {
        // Every node owns exactly one PV slot. Reset it before any early
        // return so a parent never copies moves left over at this ply by an
        // earlier search (which would print an illegal principal variation).
        pvLength[ply] = ply;

        if (ply >= MAX_PLY - 1)
            return evaluate::evaluate(pos);

        if ((++nodes & 2047) == 0)
            checkTime();
        if (stopFlag.load(std::memory_order_relaxed))
            return 0;

        if (ply > seldepth)
            seldepth = ply;

        // Mate distance pruning
        if (alpha < -MATE + ply)
            alpha = -MATE + ply;
        if (beta > MATE - ply - 1)
            beta = MATE - ply - 1;
        if (alpha >= beta)
            return alpha;

        const Color us = pos.sideToMove;
        const int originalAlpha = alpha;
        const uint64_t key = pos.zobristKey;

        const Move prevMove = moveStack[ply];
        const Move counter = prevMove == Move() ? Move() : counterMoves[us][prevMove.from()][moveTarget(prevMove)];

        // Deterministic draws are cached in the transposition table.
        if (ply > 0 && (pos.halfmoveClock >= 100 || isInsufficientMaterial(pos)))
        {
            ttStore(key, Move(), 0, depth, BOUND_EXACT, ply);
            return 0;
        }
        if (ply > 0 && pos.isRepetition(ply))
            return 0;

        // Transposition table probe.
        TTEntry &tte = ttEntry(key);
        Move ttMove;
        if (tte.key == key)
        {
            ttMove = tte.move;
            const int s = scoreFromTT(tte.score, ply);

            if (tte.bound == BOUND_EXACT
                && (s >= MATE_THRESHOLD || s <= -MATE_THRESHOLD))
            {
                // The root still has to name a move. The extra MultiPV lines
                // must walk their own moves, so leave them alone.
                if (ply == 0 && ttMove != Move() && excludedRootMoves.empty())
                {
                    pvTable[0][0] = ttMove;
                    pvLength[0] = 1;
                }
                return s;
            }

            if (tte.depth >= depth && !pvNode)
            {
                if (tte.bound == BOUND_EXACT
                    || (tte.bound == BOUND_LOWER && s >= beta)
                    || (tte.bound == BOUND_UPPER && s <= alpha))
                    return s;
            }
        }

        if (depth <= 0)
            return quiescence(pos, alpha, beta, ply);

        const Square ksq = kingSquare(pos, us);
        const bool inCheck = ksq != SQ_NONE &&
                             movegen::squareAttacked(pos, ksq, static_cast<Color>(us ^ 1));

        // Static evaluation drives the pruning decisions below. It is resolved
        // for every node (not only non-PV ones) because `improving` compares it
        // against the value two plies up the current line.
        const int staticEval = inCheck ? -MATE + ply : evaluate::evaluate(pos);
        staticEvalStack[ply] = staticEval;
        const bool improving = !inCheck && ply >= 2 && staticEval >= staticEvalStack[ply - 2];
        const bool mateWindow = beta >= MATE_THRESHOLD;

        if (!pvNode)
        {
            // Razoring: the static eval is so far below beta that even a
            // quiescence search cannot reach it, so its result is final.
            if (!inCheck && depth <= tuned::RAZOR_DEPTH && staticEval + tuned::RAZOR_MARGIN < beta)
            {
                const int razorScore = quiescence(pos, alpha, beta, ply);
                if (razorScore < beta)
                    return razorScore;
            }

            // Reverse Futility Pruning (parent-node futility): if the static
            // eval is so far above beta that a shallow search cannot drop
            // below it, return immediately.
            if (!inCheck && depth <= tuned::RFP_DEPTH && staticEval - tuned::RFP_MARGIN * depth >= beta)
                return staticEval;

            // Child-Node Futility Pruning: at pre-frontier nodes a quiet
            // position whose eval cannot reach alpha returns immediately.
            if (!inCheck && depth <= tuned::FUTILITY_DEPTH && staticEval + tuned::FUTILITY_MARGIN * depth <= alpha)
                return staticEval;
        }

        // --- Null Move Pruning ---
        // Skip in PV nodes, shallow nodes, pawn-only endings (zugzwang risk),
        // and when in check. Only try a null move when the static eval already
        // fails high; otherwise it rarely produces a cutoff.
        const Bitboard nonPawn = pos.byType[KNIGHT] | pos.byType[BISHOP] |
                                 pos.byType[ROOK] | pos.byType[QUEEN];

        if (!pvNode && !inNullVerification && !inCheck && depth >= tuned::NMP_MIN_DEPTH && (nonPawn & pos.byColor[us]) && staticEval >= beta)
        {
            // Dynamic null move reduction: deeper nodes and a larger eval
            // margin above beta allow a more aggressive reduction.
            int R = tuned::NMP_BASE + depth / tuned::NMP_DEPTH_DIV +
                    std::min(2, (staticEval - beta) / tuned::NMP_EVAL_DIV);
            R = std::min(R, depth - 1);

            // Null move search.
            moveStack[ply + 1] = Move();
            pos.do_null_move();
            const int nullScore = -alphaBeta(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
            pos.undo_null_move();

            if (nullScore >= beta)
            {
                // Do not trust mate scores produced by a null move.
                const int cutoffScore = nullScore >= MATE_THRESHOLD ? beta : nullScore;

                // Verification search at deep nodes to guard against
                // zugzwang-induced false cutoffs.
                if (depth >= tuned::NMP_VERIFY_DEPTH)
                {
                    inNullVerification = true;
                    const int verify = alphaBeta(pos, depth - R, beta - 1, beta, ply, false);
                    inNullVerification = false;

                    if (verify >= beta)
                        return cutoffScore;
                }
                else
                {
                    return cutoffScore;
                }
            }
        }

        // Thanks to the Xiphos
        // --- ProbCut ---
        // A good capture that already beats a raised beta by a wide margin is
        // assumed to fail high, so the full-width search is skipped.
        if (!pvNode && !inCheck && !mateWindow && depth >= tuned::PROBCUT_DEPTH)
        {
            const int probcutBeta = beta + tuned::PROBCUT_MARGIN;

            MoveList probcutList;
            movegen::generate_pseudo_legal_moves(pos, probcutList);

            for (int i = 0; i < probcutList.size; ++i)
            {
                const Move m = probcutList.moves[i];
                const bool quiet = !m.isPromotion() && !m.isEnPassant() && !m.isCastling() &&
                                   pos.board[m.to()] == NO_PIECE;
                if (quiet || see::evaluate(pos, m) < probcutBeta - staticEval)
                    continue;

                if (!pos.do_move(m))
                    continue;

                moveStack[ply + 1] = m;
                int score = -quiescence(pos, -probcutBeta, -probcutBeta + 1, ply + 1);
                if (score >= probcutBeta)
                    score = -alphaBeta(pos, depth - tuned::PROBCUT_DEPTH + 1, -probcutBeta,
                                       -probcutBeta + 1, ply + 1, false);
                pos.undo_move(m);

                if (stopFlag.load(std::memory_order_relaxed))
                    return 0;
                if (score >= probcutBeta)
                    return score;
            }
        }

        // Thanks to the Xiphos
        // --- Internal Iterative Deepening ---
        // A PV node without a TT move gets a shallower search first, purely to
        // obtain a move worth ordering first on the real pass.
        if (pvNode && !inIID && !inCheck && !mateWindow && ttMove == Move() &&
            depth >= tuned::IID_DEPTH)
        {
            inIID = true;
            alphaBeta(pos, depth - 2, alpha, beta, ply, true);
            inIID = false;

            if (stopFlag.load(std::memory_order_relaxed))
                return 0;
            if (tte.key == key)
                ttMove = tte.move;
        }

        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);

        MovePicker picker;
        picker.init(pos, list, ttMove, counter, ply, false);

        Move bestMove;
        int bestScore = -INF;
        int legalMoves = 0;


        Move searchedQuiets[MoveList::MAX_MOVES];
        int searchedQuietCount = 0;
        Move searchedCaptures[MoveList::MAX_MOVES];
        int searchedCaptureCount = 0;

        int moveCount = 0;
        for (Move m = picker.next(pos); m != Move(); m = picker.next(pos))
        {
            const bool quiet = !m.isPromotion() && !m.isEnPassant() && !m.isCastling() && pos.board[m.to()] == NO_PIECE;
            ++moveCount;

            // Late Move Pruning: at shallow depth the tail of the quiet move
            // list is hopeless. `legalMoves >= 1` keeps one move searched, so a
            // node is never mistaken for mate/stalemate.
            if (!pvNode && !inCheck && quiet && legalMoves >= 1 && depth <= tuned::LMP_DEPTH &&
                moveCount > LMP_TABLE[improving ? 1 : 0][std::min(depth, LMP_MAX_DEPTH)])
                continue;

            // Move-Level Futility Pruning: skip quiet moves that cannot raise
            // alpha even with a generous positional gain.
            if (!pvNode && !inCheck && quiet && depth <= tuned::MOVE_FUTILITY_DEPTH &&
                legalMoves >= 1 && staticEval + tuned::MOVE_FUTILITY_MARGIN * depth <= alpha)
                continue;

            // SEE-Based Quiet Pruning: skip quiet moves that hang material.
            if (!pvNode && !inCheck && quiet && legalMoves >= 1 &&
                depth <= tuned::SEE_QUIET_DEPTH && see::evaluate(pos, m) < 0)
                continue;

            if (ply == 0 && (!inSearchMoves(m) || isExcludedRootMove(m)))
                continue;

            if (!pos.do_move(m))
                continue;

            moveStack[ply + 1] = m;
            ++legalMoves;
            if (quiet)
                searchedQuiets[searchedQuietCount++] = m;
            else if (!m.isCastling())
                searchedCaptures[searchedCaptureCount++] = m;

            int score;
            if (legalMoves == 1)
            {
                // TT move (or first legal move): full depth and full window.
                score = -alphaBeta(pos, depth - 1, -beta, -alpha, ply + 1, pvNode);
            }
            else
            {
                // Late Move Reduction: reduce late, quiet moves.
                int r = 0;
                if (quiet)
                    r = lmrReduction(pvNode, depth, legalMoves, history[us][m.from()][moveTarget(m)]);

                const int newDepth = std::max(0, depth - 1 - r);

                // Reduced-depth null-window search.
                score = -alphaBeta(pos, newDepth, -alpha - 1, -alpha, ply + 1, false);

                // LMR re-search: verify at full depth if the reduced search beat alpha.
                if (r > 0 && score > alpha)
                    score = -alphaBeta(pos, depth - 1, -alpha - 1, -alpha, ply + 1, false);

                if (score > alpha && score < beta)
                    score = -alphaBeta(pos, depth - 1, -beta, -alpha, ply + 1, true);
            }

            pos.undo_move(m);

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = m;

                if (score > alpha)
                {
                    alpha = score;

                    pvTable[ply][ply] = m;
                    for (int j = ply + 1; j < pvLength[ply + 1]; ++j)
                        pvTable[ply][j] = pvTable[ply + 1][j];
                    pvLength[ply] = pvLength[ply + 1];

                    if (alpha >= beta)
                    {
                        if (quiet)
                        {
                            if (prevMove != Move())
                                counterMoves[us][prevMove.from()][moveTarget(prevMove)] = m;

                            if (killers[0][ply] != m)
                            {
                                killers[1][ply] = killers[0][ply];
                                killers[0][ply] = m;
                            }

                            const int bonus = depth * depth;
                            updateHistory(history[us][m.from()][moveTarget(m)], bonus);
                            for (int k = 0; k < searchedQuietCount - 1; ++k)
                                updateHistory(history[us][searchedQuiets[k].from()][moveTarget(searchedQuiets[k])], -bonus);
                        }
                        else
                        {
                            const int bonus = depth * depth;
                            updateHistory(captureHistoryEntry(pos, m), bonus);
                            for (int k = 0; k < searchedCaptureCount - 1; ++k)
                                updateHistory(captureHistoryEntry(pos, searchedCaptures[k]), -bonus);
                        }
                        break;
                    }
                }
            }
        }

        if (legalMoves == 0)
        {
            const int score = inCheck ? -MATE + ply : 0;
            ttStore(key, Move(), score, depth, BOUND_EXACT, ply);
            return score;
        }

        if (!stopFlag.load(std::memory_order_relaxed))
        {
            int bound;
            if (bestScore <= originalAlpha)
                bound = BOUND_UPPER;
            else if (bestScore >= beta)
                bound = BOUND_LOWER;
            else
                bound = BOUND_EXACT;

            ttStore(key, bestMove, bestScore, depth, bound, ply);
        }

        return bestScore;
    }

    int quiescence(Position &pos, int alpha, int beta, int ply)
    {
        // As in alphaBeta: a node that returns immediately must not leave a
        // stale PV behind for its parent to copy.
        pvLength[ply] = ply;

        if (ply >= MAX_PLY - 1)
            return evaluate::evaluate(pos);

        if ((++nodes & 2047) == 0)
            checkTime();
        if (stopFlag.load(std::memory_order_relaxed))
            return 0;

        if (ply > seldepth)
            seldepth = ply;

        const Color us = pos.sideToMove;
        const Color them = static_cast<Color>(us ^ 1);
        const Square ksq = kingSquare(pos, us);
        const bool inCheck = ksq != SQ_NONE && movegen::squareAttacked(pos, ksq, them);

        const Move prevMove = moveStack[ply];
        const Move counter = prevMove == Move() ? Move() : counterMoves[us][prevMove.from()][moveTarget(prevMove)];

        if (pos.halfmoveClock >= 100 || pos.isRepetition(ply) || isInsufficientMaterial(pos))
            return 0;

        const int standPat = evaluate::evaluate(pos);
        if (!inCheck)
        {
            if (standPat >= beta)
                return standPat;
            if (standPat > alpha)
                alpha = standPat;
        }

        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);

        MovePicker picker;
        picker.init(pos, list, Move(), counter, ply, !inCheck);

        int legalMoves = 0;
        for (Move m = picker.next(pos); m != Move(); m = picker.next(pos))
        {
            if (!inCheck)
            {
                const int seeScore = picker.lastSeeValid ? picker.lastSee : see::evaluate(pos, m);
                if (seeScore < 0)
                    continue;
            }

            if (!pos.do_move(m))
                continue;

            moveStack[ply + 1] = m;
            ++legalMoves;
            const int score = -quiescence(pos, -beta, -alpha, ply + 1);
            pos.undo_move(m);

            if (score >= beta)
                return score;
            if (score > alpha)
                alpha = score;
        }

        // In check with no legal moves: checkmate.
        if (inCheck && legalMoves == 0)
            return -MATE + ply;

        return alpha;
    }

    // Search the root at the given depth, re-searching with a widening
    // aspiration window around prevScore on fail-low/fail-high.
    int aspirationSearch(Position &pos, int depth, int prevScore)
    {
        constexpr int INITIAL_DELTA = 16;
        constexpr int ASPIRATION_MIN_DEPTH = 4;

        int delta = INITIAL_DELTA;
        int alpha = -INF;
        int beta = INF;

        // Only start with a narrow window for stable, non-mate scores.
        if (depth >= ASPIRATION_MIN_DEPTH && prevScore > -MATE_THRESHOLD && prevScore < MATE_THRESHOLD)
        {
            alpha = std::max(-INF, prevScore - delta);
            beta = std::min(INF, prevScore + delta);
        }

        while (true)
        {
            const int score = alphaBeta(pos, depth, alpha, beta, 0, true);

            if (stopFlag.load(std::memory_order_relaxed))
                return score;

            if (score <= alpha)
            {
                if (alpha <= -INF)
                    return score;
                // Fail-low: lower alpha and tighten beta toward the true value.
                beta = (alpha + beta) / 2;
                alpha = (score <= -MATE_THRESHOLD) ? -INF : std::max(-INF, score - delta);
            }
            else if (score >= beta)
            {
                beta = (score >= MATE_THRESHOLD) ? INF : std::min(INF, score + delta);
            }
            else
            {
                return score;
            }

            delta += delta / 2;
        }
    }

    int extendPVFromTT(Position &pos, Move *pv, int length)
    {
        uint64_t seen[MAX_PLY + 1];
        int seenCount = 0;
        int played = 0;
        bool complete = true;

        seen[seenCount++] = pos.zobristKey;

        for (int i = 0; i < length && played < MAX_PLY - 1; ++i)
        {
            if (!pos.do_move(pv[i]))
            {
                complete = false;
                break;
            }
            seen[seenCount++] = pos.zobristKey;
            ++played;
        }

        while (complete && played < MAX_PLY - 1)
        {
            const TTEntry &e = ttEntry(pos.zobristKey);
            if (e.key != pos.zobristKey || e.move == Move())
                break;

            const Move m = e.move;
            if (!pos.do_move(m))
                break;

            bool repeated = false;
            for (int i = 0; i < seenCount; ++i)
                if (seen[i] == pos.zobristKey)
                    repeated = true;
            if (repeated)
            {
                pos.undo_move(m);
                break;
            }

            seen[seenCount++] = pos.zobristKey;
            pv[played++] = m;
        }

        for (int i = played - 1; i >= 0; --i)
            pos.undo_move(pv[i]);

        return played;
    }

    void iterativeDeepening(Position &pos)
    {
        nodes = 0;
        seldepth = 0;
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        std::memset(captureHistory, 0, sizeof(captureHistory));
        std::memset(counterMoves, 0, sizeof(counterMoves));
        std::fill(moveStack, moveStack + MAX_PLY + 2, Move());
        std::memset(pvTable, 0, sizeof(pvTable));
        std::memset(pvLength, 0, sizeof(pvLength));
        excludedRootMoves.clear();
        ttFilledLocal = 0;

        const bool isMain = (workerId == 0);
        const auto start = std::chrono::steady_clock::now();
        int previousScore = 0;

        // Dynamic time management state
        Move stableMove;
        int stableIterations = 0;
        int scoreDrop = 0;

        for (int depth = 1; depth <= maxDepth; ++depth)
        {
            const int mpvCount = isMain ? multiPVSetting : 1;
            excludedRootMoves.clear();

            /*if (isMain && depth >= 12)
                printRootMoves(pos, depth);
            */
            for (int mpv = 1; mpv <= mpvCount; ++mpv)
            {
                currentMultiPV = mpv;
                seldepth = 0;
                const int score = (mpv == 1)
                                      ? aspirationSearch(pos, depth, previousScore)
                                      : alphaBeta(pos, depth, -INF, INF, 0, true);

                if (stopFlag.load(std::memory_order_relaxed))
                    break;

                if (mpv == 1)
                {
                    scoreDrop = previousScore - score; // > 0 means the score fell
                    previousScore = score;
                }

                // Only the main thread reports.
                if (isMain)
                {
                    const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::steady_clock::now() - start)
                                                  .count();
                    // Report aggregate nodes across all threads so nps scales
                    // with the thread count instead of reflecting one worker's
                    // share of the work.
                    const int pvLen = extendPVFromTT(pos, pvTable[0], pvLength[0]);
                    printInfo(depth, score, searchedNodes.load(std::memory_order_relaxed), elapsed, pvTable[0], pvLen, mpv);

                    if (mpv == 1)
                    {
                        std::lock_guard<std::mutex> lock(bestMutex);
                        if (depth > completedDepth)
                        {
                            completedDepth = depth;
                            finalBestMove = pvTable[0][0];
                        }
                    }
                }

                // Stop once a mate within the requested distance has been found.
                if (mateGoal > 0 && score >= MATE_THRESHOLD && (MATE - score + 1) / 2 <= mateGoal)
                {
                    stopFlag.store(true, std::memory_order_relaxed);
                    break;
                }

                excludedRootMoves.push_back(pvTable[0][0]);
            }

            if (stopFlag.load(std::memory_order_relaxed))
                break;

            if (isMain && depth >= 4 &&
                !(ponderFlag.load(std::memory_order_relaxed) && !ponderHitFlag.load(std::memory_order_relaxed)))
            {
                if (pvTable[0][0] == stableMove)
                    ++stableIterations;
                else
                {
                    stableMove = pvTable[0][0];
                    stableIterations = 0;
                }

                if (stableIterations >= 3 && scoreDrop <= 0 &&
                    std::chrono::steady_clock::now() >= softDeadline)
                    stopFlag.store(true, std::memory_order_relaxed);
            }

            if (depth >= maxDepth)
                break;
        }

        globalNodes.fetch_add(nodes, std::memory_order_relaxed);
        ttFilled.fetch_add(ttFilledLocal, std::memory_order_relaxed);
    }

    void worker(Position *pos, int id)
    {
        workerId = id;
        iterativeDeepening(*pos);
    }
}

int search::clampOption(const char *name, int value, int minValue, int maxValue)
{
    const int clamped = std::clamp(value, minValue, maxValue);
    if (clamped != value)
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << "info string " << name << " value " << value << " is out of range ["
                  << minValue << ", " << maxValue << "], set to " << clamped << std::endl;
    }
    return clamped;
}

void search::init()
{
    movegen::init();
    ttResize(static_cast<size_t>(hashSizeMb));
}

void search::clear()
{
    clearTT();
}

void search::go(const Position &root, const SearchLimits &limits)
{
    // Lazily initialize the attack tables and transposition table if needed.
    if (tt.empty())
    {
        movegen::init();
        ttResize(static_cast<size_t>(hashSizeMb));
    }

    activeLimits = limits;
    activeUs = root.sideToMove;
    nodesLimit = limits.nodes;
    mateGoal = limits.mate;
    // Never ponder unless it has been enabled.
    ponderFlag.store(limits.ponder && ponderSetting, std::memory_order_relaxed);
    ponderHitFlag.store(false, std::memory_order_relaxed);
    bestmoveEmitted.store(false, std::memory_order_relaxed);
    stopFlag.store(false, std::memory_order_relaxed);
    searching.store(true, std::memory_order_relaxed);
    globalNodes.store(0, std::memory_order_relaxed);
    searchedNodes.store(0, std::memory_order_relaxed);
    completedDepth = 0;
    finalBestMove = Move();

    computeDeadline(limits, root.sideToMove);
    maxDepth = limits.depth > 0 ? limits.depth : MAX_DEPTH;

    const unsigned nThreads = static_cast<unsigned>(threadCount());

    std::vector<Position> positions;
    positions.reserve(nThreads);
    for (unsigned i = 0; i < nThreads; ++i)
        positions.push_back(root);

    std::vector<std::thread> threads;
    threads.reserve(nThreads - 1);
    for (unsigned i = 1; i < nThreads; ++i)
        threads.emplace_back(worker, &positions[i], static_cast<int>(i));

    worker(&positions[0], 0);

    for (auto &t : threads)
        t.join();

    // Never report 0000 while the root position has a legal move.
    if (finalBestMove == Move())
    {
        MoveList list;
        movegen::generate_pseudo_legal_moves(root, list);
        for (int i = 0; i < list.size; ++i)
        {
            Position next = root;
            if (next.do_move(list.moves[i]))
            {
                finalBestMove = list.moves[i];
                break;
            }
        }
    }

    if (!bestmoveEmitted.load(std::memory_order_relaxed) && !silentOutput)
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << "bestmove " << (finalBestMove == Move() ? "0000" : moveToUci(finalBestMove)) << std::endl;
    }

    stopFlag.store(false, std::memory_order_relaxed);
    searching.store(false, std::memory_order_relaxed);
}

void search::stop()
{
    stopFlag.store(true, std::memory_order_relaxed);
}

bool search::isRunning()
{
    return searching.load(std::memory_order_relaxed);
}

Move search::bestMove()
{
    std::lock_guard<std::mutex> lock(bestMutex);
    return finalBestMove;
}

uint64_t search::totalNodes()
{
    return globalNodes.load(std::memory_order_relaxed);
}

void search::setHashSize(int megabytes)
{
    megabytes = clampOption("Hash", megabytes, HASH_MIN, HASH_MAX);
    if (megabytes == hashSizeMb)
        return;

    // The table is allocated lazily; only a live table can fail to grow.
    if (tt.empty())
    {
        hashSizeMb = megabytes;
        return;
    }

    try
    {
        ttResize(static_cast<size_t>(megabytes));
    }
    catch (const std::bad_alloc &)
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << "info string Hash " << megabytes << " MB could not be allocated, keeping "
                  << hashSizeMb << " MB" << std::endl;
        return;
    }
    hashSizeMb = megabytes;
}

void search::setThreads(int count)
{
    threadCountSetting = clampOption("Threads", count, THREADS_MIN, maxThreadCount());
}

int search::threadSetting()
{
    return threadCountSetting;
}

void search::setSilent(bool enabled)
{
    silentOutput = enabled;
}

int search::hashSize()
{
    return hashSizeMb;
}

int search::threadCount()
{
    return threadCountSetting;
}

int search::maxThreadCount()
{
    return hardwareThreads();
}

void search::setMultiPV(int value)
{
    multiPVSetting = clampOption("MultiPV", value, MULTIPV_MIN, MULTIPV_MAX);
}

void search::setPonder(bool enabled)
{
    ponderSetting = enabled;
}

int search::multiPV()
{
    return multiPVSetting;
}

bool search::ponderEnabled()
{
    return ponderSetting;
}

void search::ponderhit()
{
    if (!ponderFlag.load(std::memory_order_relaxed))
        return;
    ponderHitFlag.store(true, std::memory_order_relaxed);
    bestmoveEmitted.store(false, std::memory_order_relaxed);
    computeDeadline(activeLimits, activeUs);
}

std::string search::tuningOptionsUci()
{
    std::string out;
    for (const TuningParam &p : tuningParams)
    {
        out += "option name ";
        out += p.name;
        out += " type spin default ";
        out += std::to_string(*p.value);
        out += " min ";
        out += std::to_string(p.minValue);
        out += " max ";
        out += std::to_string(p.maxValue);
        out += '\n';
    }
    return out;
}

bool search::setTuningOption(const std::string &name, int value)
{
    for (const TuningParam &p : tuningParams)
    {
        if (name == p.name)
        {
            *p.value = clampOption(p.name, value, p.minValue, p.maxValue);
            return true;
        }
    }
    return false;
}
