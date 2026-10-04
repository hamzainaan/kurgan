#include "search.h"

#include "endgame.h"
#include "evaluate.h"
#include "see.h"
#include "stats.h"
#include "timeman.h"
#include "tt.h"
#include "tuned_params.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
    constexpr int MAX_PLY = 128;
    constexpr int MAX_DEPTH = 64;

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
        {"Razor Depth", &tuned::RAZOR_DEPTH, 0, 8},
        {"Razor Margin", &tuned::RAZOR_MARGIN, 0, 800},
        {"NMP Min Depth", &tuned::NMP_MIN_DEPTH, 2, 12},
        {"NMP Base", &tuned::NMP_BASE, 1, 12},
        {"NMP Depth Div", &tuned::NMP_DEPTH_DIV, 1, 16},
        {"NMP Eval Div", &tuned::NMP_EVAL_DIV, 50, 1000},
        {"NMP Verify Depth", &tuned::NMP_VERIFY_DEPTH, 2, 32},
        {"ProbCut Depth", &tuned::PROBCUT_DEPTH, 3, 12},
        {"ProbCut Margin", &tuned::PROBCUT_MARGIN, 0, 400},
        {"IIR Depth", &tuned::IIR_DEPTH, 2, 12},
        {"LMP Depth", &tuned::LMP_DEPTH, 0, 16},
        {"LMP Base", &tuned::LMP_BASE, 0, 16},
        {"History Prune Depth", &tuned::HIST_PRUNE_DEPTH, 0, 12},
        {"History Prune Margin", &tuned::HIST_PRUNE_MARGIN, 0, 16384},
        {"Futility Depth", &tuned::FUTILITY_DEPTH, 0, 16},
        {"Futility Base", &tuned::FUTILITY_BASE, 0, 500},
        {"Futility Margin", &tuned::FUTILITY_MARGIN, 0, 500},
        {"SEE Quiet Margin", &tuned::SEE_QUIET_MARGIN, 0, 200},
        {"SEE Capture Margin", &tuned::SEE_CAPTURE_MARGIN, 0, 400},
        {"Capture Futility Depth", &tuned::CAPTURE_FUTILITY_DEPTH, 0, 16},
        {"Capture Futility Base", &tuned::CAPTURE_FUTILITY_BASE, 0, 800},
        {"Capture Futility Margin", &tuned::CAPTURE_FUTILITY_MARGIN, 0, 800},
        {"SEE Order Div", &tuned::SEE_ORDER_DIV, 8, 1024},
        {"QS Futility Margin", &tuned::QS_FUTILITY_MARGIN, 0, 800},
        {"SE Depth", &tuned::SE_DEPTH, 4, 16},
        {"SE Margin", &tuned::SE_MARGIN, 0, 64},
        {"SE Double Margin", &tuned::SE_DOUBLE_MARGIN, 0, 200},
        {"LMR Base", &tuned::LMR_BASE, 0, 300},
        {"LMR Divisor", &tuned::LMR_DIVISOR, 100, 600},
        {"LMR History Div", &tuned::LMR_HIST_DIV, 1000, 32768},
        {"LMR Capture Base", &tuned::LMR_CAPTURE_BASE, 0, 6},
        {"LMR Capture History Div", &tuned::LMR_CAPHIST_DIV, 1000, 32768},
        {"History Bonus Mul", &tuned::HIST_BONUS_MUL, 1, 128},
        {"History Bonus Max", &tuned::HIST_BONUS_MAX, 256, 16384},
    };

    // Transposition table bounds.
    constexpr int MATE = tt::MATE;
    constexpr int MATE_THRESHOLD = tt::MATE_THRESHOLD;
    constexpr int BOUND_NONE = tt::BOUND_NONE;
    constexpr int BOUND_EXACT = tt::BOUND_EXACT;
    constexpr int BOUND_LOWER = tt::BOUND_LOWER;
    constexpr int BOUND_UPPER = tt::BOUND_UPPER;
    constexpr int NO_EVAL = tt::NO_EVAL;

    // Frequently accessed atomics are isolated on separate cache lines to
    // avoid false sharing: without this, a thread writing to a counter would
    // invalidate the cache line holding stopFlag (read at every node) and
    // per-thread throughput (and reported nps) would drop as threads grow.
    alignas(64) std::atomic<bool> stopFlag{false};
    alignas(64) std::atomic<uint64_t> searchedNodes{0};
    alignas(64) std::atomic<uint64_t> globalNodes{0};
    alignas(64) std::atomic<bool> ponderFlag{false};
    alignas(64) std::atomic<bool> ponderHitFlag{false};

    int maxDepth = MAX_DEPTH;
    int hashSizeMb = 16;
    int threadCountSetting = 1;
    int multiPVSetting = 1;
    int contemptSetting = 0;
    bool ponderSetting = false;
    int64_t nodesLimit = 0;
    int mateGoal = 0;
    Color activeUs = WHITE;
    search::SearchLimits activeLimits;

    std::mutex outputMutex;
    std::mutex bestMutex;
    bool silentOutput = false;
    int completedDepth = 0;
    int finalScore = 0;
    Move finalBestMove;
    Move finalPonderMove;

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

    // Continuation history
    constexpr int CONT_PLIES = 4;
    constexpr int CONT_DISTANCE[CONT_PLIES] = {1, 2, 4, 6};
    constexpr int CONT_WEIGHT[CONT_PLIES] = {2, 2, 1, 1};
    constexpr int CONT_WEIGHT_SUM = 6;
    constexpr int PIECE_TO_NB = static_cast<int>(PIECE_NB) * static_cast<int>(SQUARE_NB);
    constexpr int PAWN_HISTORY_NB = 4096;
    constexpr int CONT_SCALE = 100; // percent of the continuation term
    constexpr int PAWN_SCALE = 100; // percent of the pawn-history term
    constexpr size_t CONT_ENTRIES = static_cast<size_t>(CONT_PLIES) * PIECE_TO_NB * PIECE_TO_NB;
    constexpr size_t PAWN_ENTRIES = static_cast<size_t>(PAWN_HISTORY_NB) * PIECE_TO_NB;
    constexpr size_t COUNTER_ENTRIES = static_cast<size_t>(COLOR_NB) * SQUARE_NB * SQUARE_NB;
    constexpr int CORR_NB = 16384;
    constexpr int CORR_LIMIT = 1024;
    constexpr int CORR_DIV = 14;
    constexpr size_t PAWN_CORR_ENTRIES = static_cast<size_t>(COLOR_NB) * CORR_NB;
    constexpr size_t NON_PAWN_CORR_ENTRIES = static_cast<size_t>(COLOR_NB) * COLOR_NB * CORR_NB;
    std::mutex historyMutex;
    std::vector<std::vector<int16_t>> contSlices; // one slice per worker slot
    std::vector<std::vector<int16_t>> pawnSlices;
    std::vector<std::vector<Move>> counterSlices;
    std::vector<std::vector<int16_t>> pawnCorrSlices;
    std::vector<std::vector<int16_t>> nonPawnCorrSlices;
    thread_local int16_t *contHistory = nullptr; // this worker's slice
    thread_local int16_t *pawnHistory = nullptr;
    thread_local Move *counterMoves = nullptr;
    thread_local int16_t *pawnCorr = nullptr;
    thread_local int16_t *nonPawnCorr = nullptr;
    thread_local int16_t *contRows[CONT_PLIES][MAX_PLY + 2];
    thread_local Piece movedPieceStack[MAX_PLY + 2];

    thread_local Move moveStack[MAX_PLY + 2];
    thread_local int staticEvalStack[MAX_PLY + 2];
    thread_local Move pvTable[MAX_PLY][MAX_PLY];
    thread_local int pvLength[MAX_PLY];
    thread_local Move excludedStack[MAX_PLY + 2];
    thread_local int doubleExtensions[MAX_PLY + 2];
    thread_local int rootDepth = 0;
    thread_local Move partialBest;
    thread_local Move partialPonder;
    int lmrTable[MAX_DEPTH][64];

    constexpr int SKIP_PATTERNS = 20;
    constexpr int SKIP_SIZE[SKIP_PATTERNS] = {1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4};
    constexpr int SKIP_PHASE[SKIP_PATTERNS] = {0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 7};

    void initReductions()
    {
        for (int d = 1; d < MAX_DEPTH; ++d)
            for (int m = 1; m < 64; ++m)
                lmrTable[d][m] = static_cast<int>(tuned::LMR_BASE / 100.0 + std::log(d) * std::log(m) * 100.0 / tuned::LMR_DIVISOR);
    }

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

    int drawScore(const Position &pos)
    {
        return pos.sideToMove == activeUs ? -contemptSetting : contemptSetting;
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

        if (workerId != 0 || std::chrono::steady_clock::now() < manager::deadline())
            return;

        if (ponderFlag.load(std::memory_order_relaxed) && !ponderHitFlag.load(std::memory_order_relaxed))
        {
            // Ponder search must not answer before ponderhit or stop.
            manager::postpone();
            return;
        }

        if (manager::extend())
            return;

        stopFlag.store(true, std::memory_order_relaxed);
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

    bool givesCheck(const Position &pos, Move m)
    {
        const Color us = pos.sideToMove;
        const Square ksq = kingSquare(pos, static_cast<Color>(us ^ 1));
        if (ksq == SQ_NONE)
            return false;

        const Square from = m.from();
        Square to = m.to();
        PieceType pt = m.isPromotion() ? m.promoType() : typeOf(pos.board[from]);
        Bitboard occ = (pos.byColor[WHITE] | pos.byColor[BLACK]) ^ (1ULL << from);
        if (m.isCastling())
        {
            occ = (occ & ~(1ULL << to)) | (1ULL << castlingKingTo(m)) | (1ULL << castlingRookTo(m));
            to = castlingRookTo(m);
            pt = ROOK;
        }
        else
        {
            occ |= 1ULL << to;
            if (m.isEnPassant())
                occ ^= 1ULL << (to + (us == WHITE ? -8 : 8));
        }

        const Bitboard kbb = 1ULL << ksq;
        if (pt == PAWN ? (movegen::pawnAttacksFrom(us, to) & kbb) != 0 : pt != KING && (movegen::attacks(pt, to, occ) & kbb) != 0)
            return true;

        const Bitboard ours = pos.byColor[us] & occ & ~(1ULL << m.to());
        return (movegen::attacks(BISHOP, ksq, occ) & ours & (pos.byType[BISHOP] | pos.byType[QUEEN])) != 0 ||
               (movegen::attacks(ROOK, ksq, occ) & ours & (pos.byType[ROOK] | pos.byType[QUEEN])) != 0;
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

    void updateHistory(int16_t &h, int bonus)
    {
        int value = h;
        updateHistory(value, bonus);
        h = static_cast<int16_t>(value);
    }

    // Index of a (piece, target) pair inside one history row.
    constexpr int pieceToIndex(Piece piece, Square to)
    {
        return static_cast<int>(piece) * static_cast<int>(SQUARE_NB) + static_cast<int>(to);
    }

    // Attach this worker to its history slice, allocating it on first use.
    void acquireHistorySlices()
    {
        if (contHistory != nullptr)
            return;

        std::lock_guard<std::mutex> lock(historyMutex);
        const size_t slot = static_cast<size_t>(workerId);
        if (contSlices.size() <= slot)
        {
            contSlices.resize(slot + 1);
            pawnSlices.resize(slot + 1);
            counterSlices.resize(slot + 1);
            pawnCorrSlices.resize(slot + 1);
            nonPawnCorrSlices.resize(slot + 1);
        }
        if (contSlices[slot].empty())
        {
            contSlices[slot].assign(CONT_ENTRIES, 0);
            pawnSlices[slot].assign(PAWN_ENTRIES, 0);
            counterSlices[slot].assign(COUNTER_ENTRIES, Move());
            pawnCorrSlices[slot].assign(PAWN_CORR_ENTRIES, 0);
            nonPawnCorrSlices[slot].assign(NON_PAWN_CORR_ENTRIES, 0);
        }

        contHistory = contSlices[slot].data();
        pawnHistory = pawnSlices[slot].data();
        counterMoves = counterSlices[slot].data();
        pawnCorr = pawnCorrSlices[slot].data();
        nonPawnCorr = nonPawnCorrSlices[slot].data();
    }

    void clearHistorySlices()
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        for (std::vector<int16_t> &slice : contSlices)
            std::fill(slice.begin(), slice.end(), 0);
        for (std::vector<int16_t> &slice : pawnSlices)
            std::fill(slice.begin(), slice.end(), 0);
        for (std::vector<Move> &slice : counterSlices)
            std::fill(slice.begin(), slice.end(), Move());
        for (std::vector<int16_t> &slice : pawnCorrSlices)
            std::fill(slice.begin(), slice.end(), 0);
        for (std::vector<int16_t> &slice : nonPawnCorrSlices)
            std::fill(slice.begin(), slice.end(), 0);
    }

    Move &counterMoveEntry(Color c, Move prev)
    {
        return counterMoves[(static_cast<size_t>(c) * SQUARE_NB + prev.from()) * SQUARE_NB + moveTarget(prev)];
    }

    int16_t &pawnHistoryEntry(const Position &pos, Piece piece, Square to)
    {
        const size_t row = static_cast<uint32_t>(pos.pawnKey) & (PAWN_HISTORY_NB - 1);
        return pawnHistory[row * PIECE_TO_NB + pieceToIndex(piece, to)];
    }

    int16_t &pawnCorrEntry(const Position &pos)
    {
        return pawnCorr[static_cast<size_t>(pos.sideToMove) * CORR_NB + (pos.pawnKey & (CORR_NB - 1))];
    }

    int16_t &nonPawnCorrEntry(const Position &pos, Color c)
    {
        return nonPawnCorr[(static_cast<size_t>(c) * COLOR_NB + pos.sideToMove) * CORR_NB + (pos.nonPawnKey[c] & (CORR_NB - 1))];
    }

    int correctedEval(const Position &pos, int raw)
    {
        const int corr = pawnCorrEntry(pos) + nonPawnCorrEntry(pos, WHITE) + nonPawnCorrEntry(pos, BLACK);
        return std::clamp(raw + corr / CORR_DIV, -MATE_THRESHOLD + 1, MATE_THRESHOLD - 1);
    }

    void updateCorrection(int16_t &h, int bonus)
    {
        int value = h;
        value += bonus - value * std::abs(bonus) / CORR_LIMIT;
        h = static_cast<int16_t>(std::clamp(value, -CORR_LIMIT, CORR_LIMIT));
    }

    void updateCorrections(const Position &pos, int diff, int depth)
    {
        const int bonus = std::clamp(diff * depth / 8, -CORR_LIMIT / 4, CORR_LIMIT / 4);
        updateCorrection(pawnCorrEntry(pos), bonus);
        updateCorrection(nonPawnCorrEntry(pos, WHITE), bonus);
        updateCorrection(nonPawnCorrEntry(pos, BLACK), bonus);
    }

    void setContinuationRows(const Position &pos, int ply)
    {
        movedPieceStack[ply] = moveStack[ply] == Move() ? NO_PIECE : pos.board[moveTarget(moveStack[ply])];

        for (int i = 0; i < CONT_PLIES; ++i)
        {
            const int back = ply + 1 - CONT_DISTANCE[i];
            const Piece piece = back < 0 ? NO_PIECE : movedPieceStack[back];
            contRows[i][ply] = piece == NO_PIECE
                                   ? nullptr
                                   : &contHistory[(static_cast<size_t>(i) * PIECE_TO_NB +
                                                   pieceToIndex(piece, moveTarget(moveStack[back]))) *
                                                  PIECE_TO_NB];
        }
    }

    int quietHistoryScore(const Position &pos, Move m, int ply)
    {
        const Piece piece = pos.board[m.from()];
        const Square to = moveTarget(m);
        int cont = 0;
        for (int i = 0; i < CONT_PLIES; ++i)
            if (contRows[i][ply] != nullptr)
                cont += CONT_WEIGHT[i] * contRows[i][ply][pieceToIndex(piece, to)];

        return history[pos.sideToMove][m.from()][to] + CONT_SCALE * (cont / CONT_WEIGHT_SUM) / 100 +
               PAWN_SCALE * pawnHistoryEntry(pos, piece, to) / 100;
    }

    struct ScoredMove
    {
        int score;
        Move move;
        uint16_t order; // generation index, used as a stable tie-break
        int history;
    };

    constexpr int TT_MOVE_SCORE = 10000000;
    constexpr int CAPTURE_BAND = 6000000;
    constexpr int KILLER1_SCORE = 5000000;
    constexpr int KILLER2_SCORE = 4900000;
    constexpr int COUNTERMOVE_SCORE = 4800000;

    struct MovePicker
    {
        ScoredMove moves[MoveList::MAX_MOVES];
        ScoredMove deferred[MoveList::MAX_MOVES];
        int count = 0;
        int index = 0;
        int deferredCount = 0;
        int deferredIndex = 0;
        Move ttMove{};
        int lastSee = 0;          // SEE of the move returned by the last next()
        bool lastSeeValid = false; // false when SEE was not evaluated for it
        bool qsearch = false;
        int lastHistory = 0;

        void init(const Position &pos, const MoveList &list, Move ttBest, Move counterMove,
                  int ply, bool capturesOnly)
        {
            ttMove = ttBest;
            qsearch = capturesOnly;
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

                const int h = tactical ? captureHistoryEntry(pos, m) : quietHistoryScore(pos, m, ply);
                int score;
                if (m == ttMove)
                    score = TT_MOVE_SCORE;
                else if (m.isPromotion() && m.promoType() != QUEEN)
                    score = h;
                else if (tactical)
                    score = CAPTURE_BAND + captureScore(pos, m);
                else if (m == killers[0][ply])
                    score = KILLER1_SCORE;
                else if (m == killers[1][ply])
                    score = KILLER2_SCORE;
                else if (m == counterMove)
                    score = COUNTERMOVE_SCORE;
                else
                    score = h;

                moves[count++] = {score, m, static_cast<uint16_t>(count), h};
            }


            for (int i = 1; i < count; ++i)
            {
                const ScoredMove key = moves[i];
                int j = i - 1;
                while (j >= 0 && (moves[j].score < key.score
                                  || (moves[j].score == key.score && moves[j].order > key.order)))
                {
                    moves[j + 1] = moves[j];
                    --j;
                }
                moves[j + 1] = key;
            }
        }

        // Next move to search, or Move() when the node is exhausted.
        Move next(const Position &pos)
        {
            while (index < count)
            {
                const ScoredMove sm = moves[index++];
                const Move m = sm.move;
                lastSeeValid = false;
                lastHistory = sm.history;

                if (m == ttMove || sm.score < CAPTURE_BAND)
                    return m;

                lastSee = see::ge(pos, m, qsearch ? 0 : -(sm.score - CAPTURE_BAND) / tuned::SEE_ORDER_DIV) ? 1 : -1;
                lastSeeValid = true;
                if (lastSee < 0)
                {
                    deferred[deferredCount++] = sm;
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
                lastHistory = deferred[deferredIndex].history;
                return deferred[deferredIndex++].move;
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
        std::cout << " hashfull " << tt::hashfull();
        std::cout << " time " << elapsedMs;

        std::cout << " pv";
        for (int i = 0; i < pvLen; ++i)
            std::cout << ' ' << moveToUci(pv[i]);
        std::cout << std::endl;
    }

    int quiescence(Position &pos, int alpha, int beta, int ply);

    // True when the side to move is checkmated.
    bool isCheckmated(Position &pos)
    {
        const Color us = pos.sideToMove;
        const Square ksq = kingSquare(pos, us);
        if (ksq == SQ_NONE || !movegen::squareAttacked(pos, ksq, static_cast<Color>(us ^ 1)))
            return false;

        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);
        for (int i = 0; i < list.count(); ++i)
            if (movegen::is_legal(pos, list[i]))
                return false;

        return true;
    }

    bool hasMateProof(Position &pos, int ply, int score)
    {
        const int need = (score > 0 ? MATE - score : MATE + score) - ply;
        if (need <= 0 || need > MAX_PLY - 1 - ply)
            return false;

        const Color us = pos.sideToMove;
        const Color mated = score > 0 ? static_cast<Color>(us ^ 1) : us;
        int n = 0;
        while (n < need)
        {
            const tt::Entry *e = tt::probe(pos.zobristKey);
            if (e == nullptr || e->move == Move() || !pos.do_move(e->move))
                break;

            pvTable[ply][ply + n] = e->move;
            ++n;
        }


        if (n == need - 1)
        {
            MoveList list;
            movegen::generate_pseudo_legal_moves(pos, list);
            for (int i = 0; i < list.count(); ++i)
            {
                const Move m = list[i];
                if (!movegen::is_legal(pos, m) || !pos.do_move(m))
                    continue;

                if (pos.sideToMove == mated && isCheckmated(pos))
                {
                    // Leave the move applied: the undo loop below pops it.
                    pvTable[ply][ply + n] = m;
                    ++n;
                    break;
                }

                pos.undo_move(m);
            }
        }

        const bool mate = n == need && pos.sideToMove == mated && isCheckmated(pos);
        while (n > 0)
            pos.undo_move(pvTable[ply][ply + --n]);

        if (mate)
            pvLength[ply] = ply + need;

        return mate;
    }

    int historyBonus(int depth)
    {
        return std::min(tuned::HIST_BONUS_MUL * depth * depth, tuned::HIST_BONUS_MAX);
    }

    int alphaBeta(Position &pos, int depth, int alpha, int beta, int ply, bool pvNode, bool cutNode)
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
        const Move excluded = excludedStack[ply];

        const Move prevMove = moveStack[ply];
        const Move counter = prevMove == Move() ? Move() : counterMoveEntry(us, prevMove);

        if (ply > 0 && (pos.halfmoveClock >= 100 || isInsufficientMaterial(pos) || pos.isRepetition(ply)))
            return drawScore(pos) + 1 - static_cast<int>(nodes & 2);

        // Transposition table probe.
        const tt::Entry *tte = excluded == Move() ? tt::probe(key) : nullptr;
        const bool ttHit = tte != nullptr;
        Move ttMove{};
        int ttScore = 0;
        int ttDepth = 0;
        int ttBound = BOUND_NONE;
        int ttEval = NO_EVAL;
        if (ttHit)
        {
            ttMove = tte->move;
            ttScore = tt::scoreFromTT(tte->score, ply);
            ttDepth = tte->depth();
            ttBound = tte->bound();
            ttEval = tte->eval;

            const bool mate = ttScore >= MATE_THRESHOLD || ttScore <= -MATE_THRESHOLD;
            if (ttBound == BOUND_EXACT && ply > 0 && mate
                && (!pvNode || hasMateProof(pos, ply, ttScore)))
            {
                ++stats::current().ttCutoffs;
                return ttScore;
            }

            if (ttDepth >= depth && !pvNode)
            {
                if (ttBound == BOUND_EXACT
                    || (ttBound == BOUND_LOWER && ttScore >= beta)
                    || (ttBound == BOUND_UPPER && ttScore <= alpha))
                {
                    ++stats::current().ttCutoffs;
                    return ttScore;
                }
            }
        }

        if (depth <= 0)
            return quiescence(pos, alpha, beta, ply);

        const bool ttPv = pvNode || (ttHit && tte->pv());

        setContinuationRows(pos, ply);

        const Square ksq = kingSquare(pos, us);
        const bool inCheck = ksq != SQ_NONE &&
                             movegen::squareAttacked(pos, ksq, static_cast<Color>(us ^ 1));

        int staticEval;
        int rawEval = NO_EVAL;
        if (inCheck)
            staticEval = NO_EVAL;
        else if (excluded != Move())
            staticEval = staticEvalStack[ply];
        else
        {
            rawEval = ttEval != NO_EVAL ? ttEval : evaluate::evaluate(pos);
            staticEval = correctedEval(pos, rawEval);
        }
        staticEvalStack[ply] = staticEval;

        int eval = staticEval;
        if (!inCheck && ttHit && ttScore > -MATE_THRESHOLD && ttScore < MATE_THRESHOLD
            && (ttBound == BOUND_EXACT || (ttBound == BOUND_LOWER && ttScore > eval) || (ttBound == BOUND_UPPER && ttScore < eval)))
            eval = ttScore;

        const int pastEval = ply >= 2 && staticEvalStack[ply - 2] != NO_EVAL ? staticEvalStack[ply - 2]
                             : ply >= 4                                     ? staticEvalStack[ply - 4]
                                                                            : NO_EVAL;
        const bool improving = !inCheck && pastEval != NO_EVAL && staticEval > pastEval;
        const bool mateWindow = beta >= MATE_THRESHOLD || beta <= -MATE_THRESHOLD;

        const Bitboard nonPawnPieces = pos.byType[KNIGHT] | pos.byType[BISHOP] |
                                       pos.byType[ROOK] | pos.byType[QUEEN];
        const bool hasNonPawn = (nonPawnPieces & pos.byColor[us]) != 0;

        if (!pvNode && !inCheck && !mateWindow)
        {
            if (depth <= tuned::RAZOR_DEPTH && eval + tuned::RAZOR_MARGIN * depth < alpha)
            {
                const int razorScore = quiescence(pos, alpha, beta, ply);
                if (razorScore <= alpha)
                {
                    ++stats::current().razorCutoffs;
                    return razorScore;
                }
            }

            if (!ttPv && depth <= tuned::RFP_DEPTH && eval - tuned::RFP_MARGIN * (depth - improving) >= beta)
            {
                ++stats::current().rfpCutoffs;
                return (eval + beta) / 2;
            }
        }

        const bool nmpGate = !pvNode && !inNullVerification && !inCheck && !mateWindow && excluded == Move() &&
                             prevMove != Move() && depth >= tuned::NMP_MIN_DEPTH && hasNonPawn;
        if (nmpGate)
            ++stats::current().nmpEligible;
        if (nmpGate && eval >= beta && staticEval >= beta)
        {
            int R = tuned::NMP_BASE + depth / tuned::NMP_DEPTH_DIV +
                    std::min(2, (eval - beta) / tuned::NMP_EVAL_DIV);
            R = std::min(R, depth - 1);

            moveStack[ply + 1] = Move();
            pos.do_null_move();
            const int nullScore = -alphaBeta(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, false, !cutNode);
            pos.undo_null_move();

            if (stopFlag.load(std::memory_order_relaxed))
                return 0;

            if (nullScore >= beta)
            {
                const int cutoffScore = nullScore >= MATE_THRESHOLD ? beta : nullScore;

                if (depth >= tuned::NMP_VERIFY_DEPTH)
                {
                    inNullVerification = true;
                    const int verify = alphaBeta(pos, depth - R, beta - 1, beta, ply, false, false);
                    inNullVerification = false;

                    if (verify >= beta)
                    {
                        ++stats::current().nmpCutoffs;
                        return cutoffScore;
                    }
                }
                else
                {
                    ++stats::current().nmpCutoffs;
                    return cutoffScore;
                }
            }
        }

        // Thanks to the Xiphos
        const int probcutBeta = beta + tuned::PROBCUT_MARGIN;
        if (!pvNode && !inCheck && !mateWindow && excluded == Move() && depth >= tuned::PROBCUT_DEPTH &&
            probcutBeta < MATE_THRESHOLD && !(ttHit && ttDepth >= depth - 3 && ttScore < probcutBeta))
        {
            ++stats::current().probcutEligible;

            MoveList probcutList;
            movegen::generate_tactical_moves(pos, probcutList);

            for (int i = 0; i < probcutList.size; ++i)
            {
                const Move m = probcutList.moves[i];
                if (!see::ge(pos, m, probcutBeta - staticEval) || !pos.do_move(m))
                    continue;

                moveStack[ply + 1] = m;
                int score = -quiescence(pos, -probcutBeta, -probcutBeta + 1, ply + 1);
                if (score >= probcutBeta)
                    score = -alphaBeta(pos, depth - tuned::PROBCUT_DEPTH + 1, -probcutBeta,
                                       -probcutBeta + 1, ply + 1, false, !cutNode);
                pos.undo_move(m);

                if (stopFlag.load(std::memory_order_relaxed))
                    return 0;
                if (score >= probcutBeta)
                {
                    ++stats::current().probcutCutoffs;
                    tt::store(key, m, score, depth - tuned::PROBCUT_DEPTH + 2, BOUND_LOWER, ply, rawEval, ttPv);
                    return score;
                }
            }
        }

        if ((pvNode || cutNode) && excluded == Move() && ttMove == Move() && depth >= tuned::IIR_DEPTH)
        {
            ++stats::current().iidCount;
            --depth;
        }

        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);

        MovePicker picker;
        picker.init(pos, list, ttMove, counter, ply, false);

        Move bestMove{};
        int bestScore = -INF;
        int legalMoves = 0;
        const bool ttCapture = ttMove != Move() && isTactical(pos, ttMove);

        Move searchedQuiets[MoveList::MAX_MOVES];
        int searchedQuietCount = 0;
        Move searchedCaptures[MoveList::MAX_MOVES];
        int searchedCaptureCount = 0;

        int moveCount = 0;
        ++stats::current().listNodes;
        for (Move m = picker.next(pos); m != Move(); m = picker.next(pos))
        {
            if (m == excluded || (ply == 0 && (!inSearchMoves(m) || isExcludedRootMove(m))))
                continue;

            const bool quiet = !isTactical(pos, m);
            const bool checks = givesCheck(pos, m);
            const int histScore = picker.lastHistory;
            ++moveCount;

            if (ply > 0 && hasNonPawn && bestScore > -MATE_THRESHOLD)
            {
                const int lmrDepth = std::max(0, depth - 1 - lmrTable[std::min(depth, MAX_DEPTH - 1)][std::min(moveCount, 63)]);

                if (quiet && !inCheck && depth <= tuned::LMP_DEPTH &&
                    moveCount > (tuned::LMP_BASE + depth * depth) / (2 - improving))
                {
                    ++stats::current().lmpPruned;
                    continue;
                }

                if (quiet && !checks)
                {
                    if (lmrDepth < tuned::HIST_PRUNE_DEPTH && histScore < -tuned::HIST_PRUNE_MARGIN * depth)
                    {
                        ++stats::current().futilityPruned;
                        continue;
                    }

                    if (!inCheck && lmrDepth <= tuned::FUTILITY_DEPTH &&
                        staticEval + tuned::FUTILITY_BASE + tuned::FUTILITY_MARGIN * lmrDepth <= alpha)
                    {
                        ++stats::current().futilityPruned;
                        continue;
                    }

                    if (!see::ge(pos, m, -tuned::SEE_QUIET_MARGIN * lmrDepth * lmrDepth))
                    {
                        ++stats::current().seeQuietPruned;
                        continue;
                    }
                }
                else
                {
                    if (!quiet && !checks && !inCheck && lmrDepth < tuned::CAPTURE_FUTILITY_DEPTH &&
                        staticEval + tuned::CAPTURE_FUTILITY_BASE + tuned::CAPTURE_FUTILITY_MARGIN * lmrDepth +
                                pieceValue(typeOf(victimOf(pos, m))) <= alpha)
                    {
                        ++stats::current().futilityPruned;
                        continue;
                    }

                    if (!see::ge(pos, m, -tuned::SEE_CAPTURE_MARGIN * depth))
                    {
                        ++stats::current().seeQuietPruned;
                        continue;
                    }
                }
            }

            int extension = 0;
            if (ply > 0 && ply < 2 * rootDepth && m == ttMove && excluded == Move() && depth >= tuned::SE_DEPTH &&
                ttDepth >= depth - 3 && (ttBound == BOUND_LOWER || ttBound == BOUND_EXACT) &&
                ttScore > -MATE_THRESHOLD && ttScore < MATE_THRESHOLD)
            {
                const int singularBeta = ttScore - depth * tuned::SE_MARGIN / 8;

                excludedStack[ply] = m;
                const int singularScore = alphaBeta(pos, (depth - 1) / 2, singularBeta - 1, singularBeta, ply, false, cutNode);
                excludedStack[ply] = Move();
                pvLength[ply] = ply;

                if (stopFlag.load(std::memory_order_relaxed))
                    return 0;

                if (singularScore < singularBeta)
                    extension = !pvNode && singularScore < singularBeta - tuned::SE_DOUBLE_MARGIN && doubleExtensions[ply] < 6 ? 2 : 1;
                else if (singularBeta >= beta)
                    return singularBeta;
                else if (ttScore >= beta || cutNode)
                    extension = -1;
            }
            else if (checks && ply < 2 * rootDepth)
                extension = 1;

            if (!pos.do_move(m))
                continue;

            moveStack[ply + 1] = m;
            doubleExtensions[ply + 1] = doubleExtensions[ply] + (extension == 2);
            ++legalMoves;
            ++stats::current().movesSearched;
            if (quiet)
                searchedQuiets[searchedQuietCount++] = m;
            else if (!m.isCastling())
                searchedCaptures[searchedCaptureCount++] = m;

            const int newDepth = depth - 1 + extension;

            int score;
            if (legalMoves == 1)
            {
                score = -alphaBeta(pos, newDepth, -beta, -alpha, ply + 1, pvNode, !pvNode && !cutNode);
            }
            else
            {
                int r = 0;
                if (depth >= 3)
                {
                    if (quiet)
                    {
                        r = lmrTable[std::min(depth, MAX_DEPTH - 1)][std::min(legalMoves, 63)];
                        r += !pvNode + !improving;
                        r -= m == killers[0][ply] || m == killers[1][ply] || m == counter;
                        r += ttCapture;
                        r -= histScore / tuned::LMR_HIST_DIV;
                    }
                    else
                        r = tuned::LMR_CAPTURE_BASE - histScore / tuned::LMR_CAPHIST_DIV;

                    r -= checks + ttPv;
                    r = std::clamp(r, 0, std::max(0, newDepth - 1));
                }
                if (r > 0)
                {
                    ++stats::current().lmrCount;
                    stats::current().lmrReduction += static_cast<uint64_t>(r);
                }

                score = -alphaBeta(pos, newDepth - r, -alpha - 1, -alpha, ply + 1, false, r > 0 || !cutNode);

                if (r > 0 && score > alpha)
                {
                    ++stats::current().lmrFailHigh;
                    ++stats::current().lmrResearch;
                    const uint64_t reSearchStart = nodes;
                    score = -alphaBeta(pos, newDepth, -alpha - 1, -alpha, ply + 1, false, !cutNode);
                    stats::current().lmrResearchNodes += nodes - reSearchStart;
                    if (score > alpha)
                        ++stats::current().lmrVerified;
                    else
                        ++stats::current().lmrRefuted;
                }

                if (pvNode && score > alpha && score < beta)
                    score = -alphaBeta(pos, newDepth, -beta, -alpha, ply + 1, true, false);
            }

            pos.undo_move(m);

            if (stopFlag.load(std::memory_order_relaxed))
                return 0;

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
                        ++stats::current().cutNodes;
                        if (legalMoves == 1)
                            ++stats::current().firstCutoffs;
                        if (legalMoves <= 3)
                            ++stats::current().earlyCutoffs;
                        if (ttMove != Move() && m == ttMove)
                            ++stats::current().ttMoveCutoffs;

                        const int bonus = historyBonus(depth);
                        if (quiet)
                        {
                            if (prevMove != Move())
                                counterMoveEntry(us, prevMove) = m;

                            if (killers[0][ply] != m)
                            {
                                killers[1][ply] = killers[0][ply];
                                killers[0][ply] = m;
                            }

                            updateHistory(history[us][m.from()][moveTarget(m)], bonus);
                            for (int k = 0; k < searchedQuietCount - 1; ++k)
                                updateHistory(history[us][searchedQuiets[k].from()][moveTarget(searchedQuiets[k])], -bonus);

                            const Piece piece = pos.board[m.from()];
                            const Square to = moveTarget(m);
                            for (int i = 0; i < CONT_PLIES; ++i)
                            {
                                if (contRows[i][ply] == nullptr)
                                    continue;
                                updateHistory(contRows[i][ply][pieceToIndex(piece, to)], bonus);
                                for (int k = 0; k < searchedQuietCount - 1; ++k)
                                {
                                    const Move q = searchedQuiets[k];
                                    updateHistory(contRows[i][ply][pieceToIndex(pos.board[q.from()], moveTarget(q))], -bonus);
                                }
                            }

                            updateHistory(pawnHistoryEntry(pos, piece, to), bonus);
                            for (int k = 0; k < searchedQuietCount - 1; ++k)
                            {
                                const Move q = searchedQuiets[k];
                                updateHistory(pawnHistoryEntry(pos, pos.board[q.from()], moveTarget(q)), -bonus);
                            }
                        }
                        else
                            updateHistory(captureHistoryEntry(pos, m), bonus);

                        for (int k = 0; k < searchedCaptureCount - !quiet; ++k)
                            updateHistory(captureHistoryEntry(pos, searchedCaptures[k]), -bonus);
                        break;
                    }
                }
            }
        }

        if (legalMoves == 0)
        {
            if (excluded != Move())
                return alpha;
            const int score = inCheck ? -MATE + ply : drawScore(pos);
            tt::store(key, Move(), score, depth, BOUND_EXACT, ply, NO_EVAL, ttPv);
            return score;
        }

        if (excluded == Move())
        {
            int bound;
            if (bestScore <= originalAlpha)
                bound = BOUND_UPPER;
            else if (bestScore >= beta)
                bound = BOUND_LOWER;
            else
                bound = BOUND_EXACT;

            tt::store(key, bound == BOUND_UPPER ? Move() : bestMove, bestScore, depth, bound, ply, rawEval, ttPv);

            if (!inCheck && (bound == BOUND_UPPER || !isTactical(pos, bestMove))
                && !(bound == BOUND_LOWER && bestScore <= staticEval)
                && !(bound == BOUND_UPPER && bestScore >= staticEval)
                && bestScore > -MATE_THRESHOLD && bestScore < MATE_THRESHOLD)
                updateCorrections(pos, bestScore - staticEval, depth);
        }

        return bestScore;
    }

    int quiescence(Position &pos, int alpha, int beta, int ply)
    {
        ++stats::current().qnodes;

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
        const uint64_t key = pos.zobristKey;
        const int originalAlpha = alpha;

        const Move prevMove = moveStack[ply];
        const Move counter = prevMove == Move() ? Move() : counterMoveEntry(us, prevMove);

        if (pos.halfmoveClock >= 100 || pos.isRepetition(ply) || isInsufficientMaterial(pos))
            return drawScore(pos);

        const tt::Entry *tte = tt::probe(key);
        Move ttMove{};
        int ttEval = NO_EVAL;
        if (tte != nullptr)
        {
            ttMove = tte->move;
            ttEval = tte->eval;
            const int s = tt::scoreFromTT(tte->score, ply);
            if (tte->bound() == BOUND_EXACT || (tte->bound() == BOUND_LOWER && s >= beta) || (tte->bound() == BOUND_UPPER && s <= alpha))
                return s;
        }

        int rawEval = NO_EVAL;
        int standPat = NO_EVAL;
        int bestScore = -MATE + ply;
        if (!inCheck)
        {
            rawEval = ttEval != NO_EVAL ? ttEval : evaluate::evaluate(pos);
            standPat = correctedEval(pos, rawEval);
            bestScore = standPat;
            if (standPat >= beta)
            {
                ++stats::current().standPatCutoffs;
                if (tte == nullptr)
                    tt::store(key, Move(), standPat, 0, BOUND_LOWER, ply, rawEval);
                return standPat;
            }
            if (standPat > alpha)
                alpha = standPat;
        }

        MoveList list;
        if (inCheck)
            movegen::generate_pseudo_legal_moves(pos, list);
        else
            movegen::generate_tactical_moves(pos, list);

        setContinuationRows(pos, ply);

        MovePicker picker;
        picker.init(pos, list, ttMove, counter, ply, !inCheck);

        const int futilityBase = standPat + tuned::QS_FUTILITY_MARGIN;
        Move bestMove{};
        int legalMoves = 0;
        for (Move m = picker.next(pos); m != Move(); m = picker.next(pos))
        {
            if (!inCheck)
            {
                const int seeScore = picker.lastSeeValid ? picker.lastSee : (see::ge(pos, m, 0) ? 1 : -1);
                if (seeScore < 0)
                {
                    ++stats::current().seeRejects;
                    continue;
                }

                if (!m.isPromotion() && !givesCheck(pos, m))
                {
                    const int futility = futilityBase + pieceValue(typeOf(victimOf(pos, m)));
                    if (futility <= alpha)
                    {
                        bestScore = std::max(bestScore, futility);
                        continue;
                    }
                    if (futilityBase <= alpha && !see::ge(pos, m, 1))
                    {
                        bestScore = std::max(bestScore, futilityBase);
                        continue;
                    }
                }
            }
            else if (bestScore > -MATE_THRESHOLD && !isTactical(pos, m))
                continue;

            if (!pos.do_move(m))
                continue;

            moveStack[ply + 1] = m;
            ++legalMoves;
            const int score = -quiescence(pos, -beta, -alpha, ply + 1);
            pos.undo_move(m);

            if (stopFlag.load(std::memory_order_relaxed))
                return 0;

            if (score > bestScore)
            {
                bestScore = score;
                if (score > alpha)
                {
                    alpha = score;
                    bestMove = m;
                    pvTable[ply][ply] = m;
                    for (int j = ply + 1; j < pvLength[ply + 1]; ++j)
                        pvTable[ply][j] = pvTable[ply + 1][j];
                    pvLength[ply] = pvLength[ply + 1];

                    if (score >= beta)
                    {
                        ++stats::current().qsearchCutoffs;
                        break;
                    }
                }
            }
        }

        if (inCheck && legalMoves == 0)
            return -MATE + ply;

        const int bound = bestScore >= beta ? BOUND_LOWER : bestScore > originalAlpha ? BOUND_EXACT : BOUND_UPPER;
        tt::store(key, bestMove, bestScore, 0, bound, ply, rawEval, tte != nullptr && tte->pv());
        return bestScore;
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

        partialBest = Move();
        partialPonder = Move();
        const bool isMain = workerId == 0 && currentMultiPV == 1;

        while (true)
        {
            const int score = alphaBeta(pos, depth, alpha, beta, 0, true, false);

            if (pvLength[0] > 0)
            {
                partialBest = pvTable[0][0];
                partialPonder = pvLength[0] > 1 ? pvTable[0][1] : Move();
            }

            if (stopFlag.load(std::memory_order_relaxed))
                return score;

            if (score <= alpha)
            {
                if (alpha <= -INF)
                {
                    if (isMain)
                        manager::rootFailing(false);
                    return score;
                }
                if (isMain)
                    manager::rootFailing(true);
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
                if (isMain)
                    manager::rootFailing(false);
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
            const tt::Entry *e = tt::probe(pos.zobristKey);
            if (e == nullptr || e->move == Move())
                break;

            const Move m = e->move;
            if (!pos.do_move(m))
                break;

            bool repeated = pos.isRepetition(played + 1);
            for (int i = 0; i < seenCount && !repeated; ++i)
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

    void waitForStop()
    {
        if (!activeLimits.infinite && !ponderFlag.load(std::memory_order_relaxed))
            return;

        while (!stopFlag.load(std::memory_order_relaxed)
               && std::chrono::steady_clock::now() < manager::deadline())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    void iterativeDeepening(Position &pos)
    {
        nodes = 0;
        seldepth = 0;
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        std::memset(captureHistory, 0, sizeof(captureHistory));
        acquireHistorySlices();
        std::fill(moveStack, moveStack + MAX_PLY + 2, Move());
        std::fill(excludedStack, excludedStack + MAX_PLY + 2, Move());
        doubleExtensions[0] = 0;
        std::memset(pvTable, 0, sizeof(pvTable));
        std::memset(pvLength, 0, sizeof(pvLength));
        excludedRootMoves.clear();
        stats::current() = stats::Tally{};

        // A root without a single legal move (checkmate/stalemate) has nothing to search
        bool rootHasLegalMove = false;
        {
            MoveList rootList;
            movegen::generate_pseudo_legal_moves(pos, rootList);
            for (int i = 0; i < rootList.size && !rootHasLegalMove; ++i)
            {
                Position probe = pos;
                if (probe.do_move(rootList.moves[i]))
                    rootHasLegalMove = true;
            }
        }

        const bool isMain = (workerId == 0);

        if (isMain && rootHasLegalMove)
        {
            const tt::Entry *e = tt::probe(pos.zobristKey);
            if (e != nullptr && e->move != Move())
            {
                const int s = tt::scoreFromTT(e->score, 0);
                if ((s >= MATE_THRESHOLD || s <= -MATE_THRESHOLD) && hasMateProof(pos, 0, s))
                {
                    seldepth = pvLength[0];
                    printInfo(1, s, searchedNodes.load(std::memory_order_relaxed), 0, pvTable[0], pvLength[0]);
                    std::lock_guard<std::mutex> lock(bestMutex);
                    if (1 > completedDepth)
                    {
                        completedDepth = 1;
                        finalScore = s;
                        finalBestMove = pvTable[0][0];
                        finalPonderMove = Move();
                    }
                }
            }
        }

        const auto start = std::chrono::steady_clock::now();
        int previousScore = 0;
        int lastCompletedScore = 0;

        // Dynamic time management state
        Move stableMove{};
        int stableIterations = 0;
        int scoreDrop = 0;

        for (int depth = 1; depth <= maxDepth; ++depth)
        {
            if (!isMain && depth > 1 && depth < maxDepth)
            {
                const int pattern = (workerId - 1) % SKIP_PATTERNS;
                if (((depth + SKIP_PHASE[pattern]) / SKIP_SIZE[pattern]) % 2 != 0)
                    continue;
            }

            const int mpvCount = isMain ? multiPVSetting : 1;
            const int64_t iterationStart = manager::elapsedMs();
            excludedRootMoves.clear();

            for (int mpv = 1; mpv <= mpvCount; ++mpv)
            {
                currentMultiPV = mpv;
                rootDepth = depth;
                seldepth = 0;
                const int score = (mpv == 1)
                                      ? aspirationSearch(pos, depth, previousScore)
                                      : alphaBeta(pos, depth, -INF, INF, 0, true, false);

                if (stopFlag.load(std::memory_order_relaxed))
                {
                    if (isMain && mpv == 1 && partialBest != Move())
                    {
                        std::lock_guard<std::mutex> lock(bestMutex);
                        finalBestMove = partialBest;
                        finalPonderMove = partialPonder;
                    }
                    break;
                }

                if (mpv == 1)
                {
                    scoreDrop = previousScore - score; // > 0 means the score fell
                    previousScore = score;
                    lastCompletedScore = score;
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
                    // searchedNodes only advances in 2048-node batches (see
                    // checkTime), so a shallow search reported "nodes 0".
                    const uint64_t reported = searchedNodes.load(std::memory_order_relaxed)
                                              + static_cast<uint64_t>(nodes & 2047);
                    const int pvLen = extendPVFromTT(pos, pvTable[0], pvLength[0]);
                    printInfo(depth, score, reported, elapsed, pvTable[0], pvLen, mpv);

                    if (mpv == 1)
                    {
                        std::lock_guard<std::mutex> lock(bestMutex);
                        if (depth > completedDepth)
                        {
                            completedDepth = depth;
                            finalScore = score;
                            finalBestMove = pvTable[0][0];
                            finalPonderMove = pvLength[0] > 1 ? pvTable[0][1] : Move();
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

            // Nothing was searched at this depth.
            if (!rootHasLegalMove)
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

                manager::observe(previousScore, scoreDrop, stableIterations);

                if (manager::budgeted()
                    && manager::shouldStop(pos, previousScore, scoreDrop, stableIterations, depth,
                                           manager::elapsedMs() - iterationStart))
                    stopFlag.store(true, std::memory_order_relaxed);
            }

            if (isMain && activeLimits.softNodes > 0
                && searchedNodes.load(std::memory_order_relaxed) + (nodes & 2047)
                       >= static_cast<uint64_t>(activeLimits.softNodes))
                stopFlag.store(true, std::memory_order_relaxed);

            if (depth >= maxDepth)
                break;
        }

        if (isMain && completedDepth > 0)
            manager::recordScore(lastCompletedScore, completedDepth);

        globalNodes.fetch_add(nodes, std::memory_order_relaxed);

        stats::Tally &tally = stats::current();
        tally.nodes = nodes;
        stats::record(workerId, seldepth, isMain ? completedDepth : 0);

        waitForStop();
    }

    void worker(Position *pos, int id)
    {
        workerId = id;
        iterativeDeepening(*pos);
        if (id == 0)
            stopFlag.store(true, std::memory_order_relaxed);
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
    endgame::init();
    initReductions();
    tt::resize(static_cast<size_t>(hashSizeMb));
}

void search::clear()
{
    tt::clear();
    clearHistorySlices();
    manager::reset();
}

void search::prepare(const Position &root, const SearchLimits &limits)
{
    activeLimits = limits;

    // A searchmoves list without a single legal root move would leave the root
    // with nothing to search.
    if (!activeLimits.searchmoves.empty())
    {
        std::vector<Move> usable;
        for (Move requested : activeLimits.searchmoves)
        {
            Position probe = root;
            if (probe.do_move(requested))
                usable.push_back(requested);
        }

        if (usable.empty())
        {
            activeLimits.searchmoves.clear();
            if (!silentOutput)
            {
                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << "info string no legal searchmoves, searching all moves" << std::endl;
            }
        }
        else
        {
            activeLimits.searchmoves = std::move(usable);
        }
    }

    activeUs = root.sideToMove;
    nodesLimit = limits.nodes;
    mateGoal = limits.mate;
    // Never ponder unless it has been enabled.
    ponderFlag.store(limits.ponder && ponderSetting, std::memory_order_relaxed);
    ponderHitFlag.store(false, std::memory_order_relaxed);
    stopFlag.store(false, std::memory_order_relaxed);
    manager::analyzeRoot(root, static_cast<int>(activeLimits.searchmoves.size()));
    manager::computeDeadline(limits, root.sideToMove);
}

void search::go(const Position &root, const SearchLimits &limits)
{
    // Lazily initialize the attack tables and transposition table if needed.
    if (!tt::allocated())
    {
        movegen::init();
        tt::resize(static_cast<size_t>(hashSizeMb));
    }
    endgame::init();

    globalNodes.store(0, std::memory_order_relaxed);
    searchedNodes.store(0, std::memory_order_relaxed);
    completedDepth = 0;
    finalScore = 0;
    finalBestMove = Move();
    finalPonderMove = Move();

    tt::newSearch();

    // A reporting search starts from an empty record set.
    if (stats::reporting())
        stats::clearRecords();

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

    if (stats::reporting())
        stats::print();

    if (!silentOutput)
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << "bestmove " << (finalBestMove == Move() ? "0000" : moveToUci(finalBestMove));
        if (finalPonderMove != Move())
            std::cout << " ponder " << moveToUci(finalPonderMove);
        std::cout << std::endl;
    }

    stopFlag.store(false, std::memory_order_relaxed);
}

void search::stop()
{
    stopFlag.store(true, std::memory_order_relaxed);
}

Move search::bestMove()
{
    std::lock_guard<std::mutex> lock(bestMutex);
    return finalBestMove;
}

int search::bestScore()
{
    std::lock_guard<std::mutex> lock(bestMutex);
    return finalScore;
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
    if (!tt::allocated())
    {
        hashSizeMb = megabytes;
        return;
    }

    try
    {
        tt::resize(static_cast<size_t>(megabytes));
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

void search::setContempt(int value)
{
    const int clamped = clampOption("Contempt", value, CONTEMPT_MIN, CONTEMPT_MAX);
    if (clamped != contemptSetting)
    {
        contemptSetting = clamped;
        clear();
    }
}

void search::setPonder(bool enabled)
{
    ponderSetting = enabled;
}

void search::setMoveOverhead(int ms)
{
    manager::setMoveOverhead(clampOption("Move Overhead", ms, MOVE_OVERHEAD_MIN, MOVE_OVERHEAD_MAX));
}

void search::ponderhit()
{
    if (!ponderFlag.load(std::memory_order_relaxed))
        return;
    ponderHitFlag.store(true, std::memory_order_relaxed);
    manager::computeDeadline(activeLimits, activeUs);
}

bool search::setTuningOption(const std::string &name, int value)
{
    for (const TuningParam &p : tuningParams)
    {
        if (name == p.name)
        {
            *p.value = clampOption(p.name, value, p.minValue, p.maxValue);
            initReductions();
            return true;
        }
    }
    return false;
}
