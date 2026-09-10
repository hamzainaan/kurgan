#include "search.h"

#include "evaluate.h"
#include "see.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
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

    // Futility pruning tuning (depth-dependent margins).
    constexpr int RFP_DEPTH = 7;             // reverse (parent-node) futility depth limit
    constexpr int RFP_MARGIN = 80;           // per-ply reverse futility margin
    constexpr int FUTILITY_DEPTH = 1;        // child-node futility depth limit
    constexpr int FUTILITY_MARGIN = 120;     // per-ply child-node futility margin
    constexpr int MOVE_FUTILITY_DEPTH = 4;   // move-level futility depth limit
    constexpr int MOVE_FUTILITY_MARGIN = 90; // per-ply move-level futility margin
    constexpr int SEE_QUIET_DEPTH = 6;       // depth limit for SEE-based quiet pruning

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

    std::atomic<bool> stopFlag{false};
    std::atomic<bool> searching{false};
    std::atomic<bool> ponderFlag{false};
    std::atomic<bool> ponderHitFlag{false};
    std::atomic<bool> bestmoveEmitted{false};
    std::atomic<uint64_t> globalNodes{0};
    std::atomic<uint64_t> ttWrites{0};
    std::atomic<uint64_t> ttFilled{0};
    std::atomic<uint64_t> searchedNodes{0};

    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point searchStart;
    int maxDepth = MAX_DEPTH;
    int hashSizeMb = 16;
    int threadCountSetting = 0; // 0 = auto
    int multiPVSetting = 1;
    int64_t nodesLimit = 0;
    int mateGoal = 0;
    Color activeUs = WHITE;
    search::SearchLimits activeLimits;

    std::mutex outputMutex;
    std::mutex bestMutex;
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
    thread_local Move pvTable[MAX_PLY][MAX_PLY];
    thread_local int pvLength[MAX_PLY];

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

    // True when neither side has mating material.
    bool isInsufficientMaterial(const Position &pos)
    {
        if (pos.byType[PAWN] || pos.byType[ROOK] || pos.byType[QUEEN])
            return false;

        const Bitboard lightSquares = 0x55AA55AA55AA55AAULL;
        const int knights = popCount(pos.byType[KNIGHT]);
        const int bishops = popCount(pos.byType[BISHOP]);

        // K vs K, or K + one minor vs K.
        if (knights + bishops <= 1)
            return true;

        // K + two knights vs K cannot force mate.
        if (knights == 2 && bishops == 0)
            return true;

        // All bishops on the same color square.
        if (knights == 0 && bishops >= 2)
        {
            const Bitboard b = pos.byType[BISHOP];
            if (!(b & lightSquares) || !(b & ~lightSquares))
                return true;
        }

        // King + bishop vs king + knight (one minor each).
        if (knights == 1 && bishops == 1)
        {
            const Bitboard minors = pos.byType[KNIGHT] | pos.byType[BISHOP];
            const int w = popCount(minors & pos.byColor[WHITE]);
            const int b = popCount(minors & pos.byColor[BLACK]);
            if (w == 1 && b == 1)
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
            ttFilled.fetch_add(1, std::memory_order_relaxed);
        e.key = key;
        e.move = move;
        e.score = static_cast<int16_t>(scoreToTT(score, ply));
        e.depth = static_cast<int16_t>(depth);
        e.bound = static_cast<int8_t>(bound);
        ttWrites.fetch_add(1, std::memory_order_relaxed);
    }

    std::string moveToUci(Move m);

    void computeDeadline(const search::SearchLimits &limits, Color us)
    {
        searchStart = std::chrono::steady_clock::now();

        if (limits.movetime > 0)
            deadline = searchStart + std::chrono::milliseconds(limits.movetime);
        else if (limits.wtime > 0 || limits.btime > 0)
        {
            const int64_t myTime = us == WHITE ? limits.wtime : limits.btime;
            const int64_t myInc = us == WHITE ? limits.winc : limits.binc;
            int64_t alloc = myTime / 20 + myInc / 2;
            if (alloc > myTime - 50)
                alloc = myTime - 50;
            if (alloc < 1)
                alloc = 1;
            deadline = searchStart + std::chrono::milliseconds(alloc);
        }
        else
            deadline = searchStart + std::chrono::hours(24);
    }

    void checkTime()
    {
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
                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << "bestmove " << (bm == Move() ? "0000" : moveToUci(bm)) << std::endl;
            }
            deadline = std::chrono::steady_clock::now() + std::chrono::hours(24);
            return;
        }

        stopFlag.store(true, std::memory_order_relaxed);
    }

    void clearTT()
    {
        std::fill(tt.begin(), tt.end(), TTEntry{});
        ttWrites.store(0, std::memory_order_relaxed);
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

    std::string moveToUci(Move m)
    {
        std::string s;
        s += static_cast<char>('a' + (m.from() & 7));
        s += static_cast<char>('1' + (m.from() >> 3));
        s += static_cast<char>('a' + (m.to() & 7));
        s += static_cast<char>('1' + (m.to() >> 3));
        if (m.isPromotion())
            s += "nbrq"[m.promoType() - KNIGHT];
        return s;
    }

    int mvvLva(const Position &pos, Move m)
    {
        const Color them = static_cast<Color>(pos.sideToMove ^ 1);
        const Piece victim = m.isEnPassant() ? makePiece(them, PAWN) : pos.board[m.to()];
        const Piece attacker = pos.board[m.from()];
        int score = pieceValue(typeOf(victim)) * 10 - pieceValue(typeOf(attacker));
        if (m.isPromotion())
            score += pieceValue(m.promoType());
        return score;
    }

    struct ScoredMove
    {
        Move move;
        int score;
    };

    void orderMoves(const Position &pos, const MoveList &list, ScoredMove *out, int &count,
                    Move ttMove, int ply, bool capturesOnly)
    {
        const Color us = pos.sideToMove;
        count = 0;
        for (int i = 0; i < list.size; ++i)
        {
            const Move m = list.moves[i];
            const bool tactical = m.isPromotion() || m.isEnPassant() || pos.board[m.to()] != NO_PIECE;
            if (capturesOnly && !tactical)
                continue;

            int score = 0;
            if (m == ttMove)
                score = 10000000;
            else if (tactical)
                score = 1000000 + mvvLva(pos, m);
            else if (m == killers[0][ply])
                score = 900000;
            else if (m == killers[1][ply])
                score = 800000;
            else
                score = history[us][m.from()][m.to()];

            out[count++] = {m, score};
        }
        std::sort(out, out + count, [](const ScoredMove &a, const ScoredMove &b)
                  { return a.score > b.score; });
    }

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
    void printRootMoves(const Position &pos, int depth)
    {
        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);

        ScoredMove scored[MoveList::MAX_MOVES];
        int count = 0;
        orderMoves(pos, list, scored, count, Move(), 0, false);

        const int limit = std::min(count, depth);

        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << "info depth " << depth;
        for (int i = 0; i < limit; ++i)
            std::cout << " currmove " << moveToUci(scored[i].move);
        std::cout << std::endl;
    }

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
        if (ply >= MAX_PLY - 1)
            return evaluate::evaluate(pos);

        if ((++nodes & 2047) == 0)
        {
            searchedNodes.fetch_add(2048, std::memory_order_relaxed);
            checkTime();
        }
        if (stopFlag.load(std::memory_order_relaxed))
            return 0;

        if (ply > seldepth)
            seldepth = ply;

        const Color us = pos.sideToMove;
        const int originalAlpha = alpha;
        const uint64_t key = pos.zobristKey;

        // Deterministic draws are cached in the transposition table.
        if (pos.halfmoveClock >= 100 || isInsufficientMaterial(pos))
        {
            ttStore(key, Move(), 0, depth, BOUND_EXACT, ply);
            return 0;
        }
        if (pos.isRepetition(ply))
            return 0;

        // Transposition table probe.
        TTEntry &tte = ttEntry(key);
        Move ttMove;
        if (tte.key == key)
        {
            ttMove = tte.move;
            if (!pvNode && tte.depth >= depth)
            {
                const int s = scoreFromTT(tte.score, ply);
                if (tte.bound == BOUND_EXACT)
                    return s;
                if (tte.bound == BOUND_LOWER && s >= beta)
                    return s;
                if (tte.bound == BOUND_UPPER && s <= alpha)
                    return s;
            }
        }

        if (depth <= 0)
            return quiescence(pos, alpha, beta, ply);

        const Square ksq = kingSquare(pos, us);
        const bool inCheck = ksq != SQ_NONE &&
                             movegen::squareAttacked(pos, ksq, static_cast<Color>(us ^ 1));

        // Static evaluation drives the pruning decisions below (non-PV only).
        int staticEval = 0;
        if (!pvNode)
        {
            staticEval = evaluate::evaluate(pos);

            // Reverse Futility Pruning (parent-node futility): if the static
            // eval is so far above beta that a shallow search cannot drop
            // below it, return immediately.
            if (!inCheck && depth <= RFP_DEPTH && staticEval - RFP_MARGIN * depth >= beta)
                return staticEval;

            // Child-Node Futility Pruning: at pre-frontier nodes a quiet
            // position whose eval cannot reach alpha returns immediately.
            if (!inCheck && depth <= FUTILITY_DEPTH && staticEval + FUTILITY_MARGIN * depth <= alpha)
                return staticEval;
        }

        // --- Null Move Pruning ---
        // Skip in PV nodes, shallow nodes, pawn-only endings (zugzwang risk),
        // and when in check. Only try a null move when the static eval already
        // fails high; otherwise it rarely produces a cutoff.
        const Bitboard nonPawn = pos.byType[KNIGHT] | pos.byType[BISHOP] |
                                 pos.byType[ROOK] | pos.byType[QUEEN];

        if (!pvNode && !inNullVerification && !inCheck && depth >= 3 && (nonPawn & pos.byColor[us]) && staticEval >= beta)
        {
            // Dynamic null move reduction: deeper nodes and a larger eval
            // margin above beta allow a more aggressive reduction.
            int R = 3 + depth / 4 + std::min(2, (staticEval - beta) / 200);
            R = std::min(R, depth - 1);

            // Null move search.
            pos.do_null_move();
            const int nullScore = -alphaBeta(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
            pos.undo_null_move();

            if (nullScore >= beta)
            {
                // Do not trust mate scores produced by a null move.
                const int cutoffScore = nullScore >= MATE_THRESHOLD ? beta : nullScore;

                // Verification search at deep nodes to guard against
                // zugzwang-induced false cutoffs.
                if (depth >= 12)
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

        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);

        ScoredMove scored[MoveList::MAX_MOVES];
        int count = 0;
        orderMoves(pos, list, scored, count, ttMove, ply, false);

        Move bestMove;
        int bestScore = -INF;
        int legalMoves = 0;
        pvLength[ply] = ply;

        for (int i = 0; i < count; ++i)
        {
            const Move m = scored[i].move;
            const bool quiet = !m.isPromotion() && !m.isEnPassant() && !m.isCastling() && pos.board[m.to()] == NO_PIECE;

            // Move-Level Futility Pruning: skip quiet moves that cannot raise
            // alpha even with a generous positional gain.
            if (!pvNode && !inCheck && quiet && depth <= MOVE_FUTILITY_DEPTH &&
                legalMoves >= 1 && staticEval + MOVE_FUTILITY_MARGIN * depth <= alpha)
                continue;

            // SEE-Based Quiet Pruning: skip quiet moves that hang material.
            if (!pvNode && !inCheck && quiet && depth <= SEE_QUIET_DEPTH &&
                see::evaluate(pos, m) < 0)
                continue;

            if (ply == 0 && (!inSearchMoves(m) || isExcludedRootMove(m)))
                continue;

            if (!pos.do_move(m))
                continue;

            ++legalMoves;

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
                    r = lmrReduction(pvNode, depth, legalMoves, history[us][m.from()][m.to()]);

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
                            if (killers[0][ply] != m)
                            {
                                killers[1][ply] = killers[0][ply];
                                killers[0][ply] = m;
                            }
                            history[us][m.from()][m.to()] += depth * depth;
                            if (history[us][m.from()][m.to()] > 16384)
                                history[us][m.from()][m.to()] = 16384;
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
        if (ply >= MAX_PLY - 1)
            return evaluate::evaluate(pos);

        if ((++nodes & 2047) == 0)
            checkTime();
        if (stopFlag.load(std::memory_order_relaxed))
            return 0;

        if (ply > seldepth)
            seldepth = ply;

        // Let the parent copy a valid principal variation back up.
        pvLength[ply] = ply;

        const Color us = pos.sideToMove;
        const Color them = static_cast<Color>(us ^ 1);
        const Square ksq = kingSquare(pos, us);
        const bool inCheck = ksq != SQ_NONE && movegen::squareAttacked(pos, ksq, them);

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

        ScoredMove scored[MoveList::MAX_MOVES];
        int count = 0;
        orderMoves(pos, list, scored, count, Move(), ply, !inCheck);

        int legalMoves = 0;
        for (int i = 0; i < count; ++i)
        {
            const Move m = scored[i].move;

            // SEE-Based Capture Pruning: skip losing exchanges when not in check.
            if (!inCheck && see::evaluate(pos, m) < 0)
                continue;

            if (!pos.do_move(m))
                continue;

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
                // Fail-low: lower alpha and tighten beta toward the true value.
                beta = (alpha + beta) / 2;
                alpha = std::max(-INF, score - delta);
            }
            else if (score >= beta)
            {
                // Fail-high: raise beta.
                beta = std::min(INF, score + delta);
            }
            else
            {
                return score;
            }

            delta += delta / 2;
        }
    }

    void iterativeDeepening(Position &pos)
    {
        nodes = 0;
        seldepth = 0;
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        std::memset(pvTable, 0, sizeof(pvTable));
        std::memset(pvLength, 0, sizeof(pvLength));
        excludedRootMoves.clear();

        const auto start = std::chrono::steady_clock::now();
        int previousScore = 0;

        for (int depth = 1; depth <= maxDepth; ++depth)
        {
            const int mpvCount = workerId == 0 ? multiPVSetting : 1;
            excludedRootMoves.clear();

            if (workerId == 0 && depth >= 12)
                printRootMoves(pos, depth);

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
                    previousScore = score;

                const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - start)
                                              .count();
                printInfo(depth, score, nodes, elapsed, pvTable[0], pvLength[0], mpv);

                if (mpv == 1)
                {
                    std::lock_guard<std::mutex> lock(bestMutex);
                    if (depth > completedDepth)
                    {
                        completedDepth = depth;
                        finalBestMove = pvTable[0][0];
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

            if (depth >= maxDepth)
                break;
        }

        globalNodes.fetch_add(nodes, std::memory_order_relaxed);
    }

    void worker(Position *pos, int id)
    {
        workerId = id;
        iterativeDeepening(*pos);
    }
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
    ponderFlag.store(limits.ponder, std::memory_order_relaxed);
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

    if (!bestmoveEmitted.load(std::memory_order_relaxed))
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
    if (megabytes < 1)
        megabytes = 1;
    if (megabytes > 65536)
        megabytes = 65536;
    if (megabytes != hashSizeMb)
    {
        hashSizeMb = megabytes;
        if (!tt.empty())
            ttResize(static_cast<size_t>(hashSizeMb));
    }
}

void search::setThreads(int count)
{
    if (count < 1)
        count = 1;
    if (count > 256)
        count = 256;
    threadCountSetting = count;
}

int search::hashSize()
{
    return hashSizeMb;
}

int search::threadCount()
{
    if (threadCountSetting > 0)
        return threadCountSetting;

    unsigned n = std::thread::hardware_concurrency();
    if (n < 1)
        n = 1;
    if (n > 32)
        n = 32;
    return static_cast<int>(n);
}

void search::setMultiPV(int value)
{
    if (value < 1)
        value = 1;
    if (value > 64)
        value = 64;
    multiPVSetting = value;
}

int search::multiPV()
{
    return multiPVSetting;
}

void search::ponderhit()
{
    if (!ponderFlag.load(std::memory_order_relaxed))
        return;
    ponderHitFlag.store(true, std::memory_order_relaxed);
    bestmoveEmitted.store(false, std::memory_order_relaxed);
    computeDeadline(activeLimits, activeUs);
}
