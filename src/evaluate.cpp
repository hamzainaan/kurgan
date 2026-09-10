#include "evaluate.h"

#include "movegen.h"
#include "position.h"

#include <array>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
    // --- Material values (centipawns) ---
    constexpr int PAWN_VALUE = 100;
    constexpr int KNIGHT_VALUE = 320;
    constexpr int BISHOP_VALUE = 330;
    constexpr int ROOK_VALUE = 500;
    constexpr int QUEEN_VALUE = 900;

    constexpr std::array<int, PIECE_TYPE_NB> materialValue = {
        PAWN_VALUE, KNIGHT_VALUE, BISHOP_VALUE, ROOK_VALUE, QUEEN_VALUE, 0, 0, 0};

    // --- Piece-square tables (white perspective, index 0 = a8 ... 63 = h1) ---
    constexpr std::array<int, SQUARE_NB> pawnPST = {
        0, 0, 0, 0, 0, 0, 0, 0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
        5, 5, 10, 25, 25, 10, 5, 5,
        0, 0, 0, 20, 20, 0, 0, 0,
        5, -5, -10, 0, 0, -10, -5, 5,
        5, 10, 10, -20, -20, 10, 10, 5,
        0, 0, 0, 0, 0, 0, 0, 0};

    constexpr std::array<int, SQUARE_NB> knightPST = {
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20, 0, 0, 0, 0, -20, -40,
        -30, 0, 10, 15, 15, 10, 0, -30,
        -30, 5, 15, 20, 20, 15, 5, -30,
        -30, 0, 15, 20, 20, 15, 0, -30,
        -30, 5, 10, 15, 15, 10, 5, -30,
        -40, -20, 0, 5, 5, 0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50};

    constexpr std::array<int, SQUARE_NB> bishopPST = {
        -20, -10, -10, -10, -10, -10, -10, -20,
        -10, 0, 0, 0, 0, 0, 0, -10,
        -10, 0, 5, 10, 10, 5, 0, -10,
        -10, 5, 5, 10, 10, 5, 5, -10,
        -10, 0, 10, 10, 10, 10, 0, -10,
        -10, 10, 10, 10, 10, 10, 10, -10,
        -10, 5, 0, 0, 0, 0, 5, -10,
        -20, -10, -10, -10, -10, -10, -10, -20};

    constexpr std::array<int, SQUARE_NB> rookPST = {
        0, 0, 0, 0, 0, 0, 0, 0,
        5, 10, 10, 10, 10, 10, 10, 5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        0, 0, 0, 5, 5, 0, 0, 0};

    constexpr std::array<int, SQUARE_NB> queenPST = {
        -20, -10, -10, -5, -5, -10, -10, -20,
        -10, 0, 0, 0, 0, 0, 0, -10,
        -10, 0, 5, 5, 5, 5, 0, -10,
        -5, 0, 5, 5, 5, 5, 0, -5,
        0, 0, 5, 5, 5, 5, 0, -5,
        -10, 5, 5, 5, 5, 5, 0, -10,
        -10, 0, 5, 0, 0, 0, 0, -10,
        -20, -10, -10, -5, -5, -10, -10, -20};

    constexpr std::array<int, SQUARE_NB> kingPSTMid = {
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -10, -20, -20, -20, -20, -20, -20, -10,
        20, 20, 0, 0, 0, 0, 20, 20,
        20, 30, 10, 0, 0, 10, 30, 20};

    constexpr std::array<int, SQUARE_NB> kingPSTEnd = {
        -50, -40, -30, -20, -20, -30, -40, -50,
        -30, -20, -10, 0, 0, -10, -20, -30,
        -30, -10, 20, 30, 30, 20, -10, -30,
        -30, -10, 30, 40, 40, 30, -10, -30,
        -30, -10, 30, 40, 40, 30, -10, -30,
        -30, -10, 20, 30, 30, 20, -10, -30,
        -30, -30, 0, 0, 0, 0, -30, -30,
        -50, -30, -30, -30, -30, -30, -30, -50};

    const std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> mgPst = {
        pawnPST, knightPST, bishopPST, rookPST, queenPST, kingPSTMid, {}, {}};
    const std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> egPst = {
        pawnPST, knightPST, bishopPST, rookPST, queenPST, kingPSTEnd, {}, {}};

    // --- Bit utilities and geometry ---
    constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
    constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;

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

    Square popLsb(Bitboard &b)
    {
        const Square s = static_cast<Square>(countrZero(b));
        b &= b - 1;
        return s;
    }

    Bitboard fileMask(int file)
    {
        return (file < 0 || file > 7) ? 0 : FILE_A_BB << file;
    }

    Bitboard rankMask(int rank)
    {
        return RANK_1_BB << (8 * rank);
    }

    int pstIndex(Square sq, Color c)
    {
        const int file = sq & 7;
        const int rank = sq >> 3;
        return c == WHITE ? (7 - rank) * 8 + file : rank * 8 + file;
    }

    int mobilityScore(const Position &pos, Color c, Bitboard occ)
    {
        struct Mobility
        {
            PieceType pt;
            int weight;
        };

        static constexpr Mobility mobilities[] = {
            {KNIGHT, 4}, {BISHOP, 3}, {ROOK, 2}, {QUEEN, 1}};

        const Bitboard notOwn = ~pos.byColor[c];
        int score = 0;
        for (const Mobility &m : mobilities)
        {
            Bitboard b = pos.byColor[c] & pos.byType[m.pt];
            while (b)
            {
                const Square sq = popLsb(b);
                score += m.weight * popCount(movegen::attacks(m.pt, sq, occ) & notOwn);
            }
        }
        return score;
    }

    int pawnStructureScore(const Position &pos, Color c)
    {
        const Color them = static_cast<Color>(c ^ 1);
        const Bitboard pawns = pos.byColor[c] & pos.byType[PAWN];
        const Bitboard enemyPawns = pos.byColor[them] & pos.byType[PAWN];

        static constexpr std::array<int, 8> passedBonus = {0, 5, 10, 20, 35, 60, 100, 0};

        int score = 0;
        Bitboard b = pawns;
        while (b)
        {
            const Square sq = popLsb(b);
            const int file = sq & 7;
            const int rank = sq >> 3;

            // Isolated pawn: no friendly pawn on adjacent files.
            if (!(pawns & (fileMask(file - 1) | fileMask(file + 1))))
                score -= 10;

            // Passed pawn: no enemy pawn on the same or adjacent files ahead.
            Bitboard ahead = 0;
            if (c == WHITE)
                for (int r = rank + 1; r < 8; ++r)
                    ahead |= rankMask(r);
            else
                for (int r = 0; r < rank; ++r)
                    ahead |= rankMask(r);

            const Bitboard passFiles = fileMask(file - 1) | fileMask(file) | fileMask(file + 1);
            if (!(enemyPawns & ahead & passFiles))
                score += passedBonus[c == WHITE ? rank : 7 - rank];
        }

        // Doubled pawns.
        for (int f = 0; f < 8; ++f)
        {
            const int count = popCount(pawns & fileMask(f));
            if (count > 1)
                score -= (count - 1) * 10;
        }

        return score;
    }
}

int evaluate::evaluate(const Position &pos)
{
    const Color us = pos.sideToMove;

    int mg[COLOR_NB] = {0, 0};
    int eg[COLOR_NB] = {0, 0};

    // Material and piece-square tables (tapered).
    for (int c = WHITE; c <= BLACK; ++c)
    {
        for (int pt = PAWN; pt <= KING; ++pt)
        {
            Bitboard b = pos.byColor[c] & pos.byType[pt];
            while (b)
            {
                const Square sq = popLsb(b);
                const int idx = pstIndex(sq, static_cast<Color>(c));
                mg[c] += materialValue[pt] + mgPst[pt][idx];
                eg[c] += materialValue[pt] + egPst[pt][idx];
            }
        }
    }

    // Mobility and pawn structure (phase-independent).
    const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
    for (int c = WHITE; c <= BLACK; ++c)
    {
        const Color color = static_cast<Color>(c);
        const int extra = mobilityScore(pos, color, occ) + pawnStructureScore(pos, color);
        mg[c] += extra;
        eg[c] += extra;
    }

    // Game phase: 0 (endgame) .. 24 (midgame).
    int phase = popCount(pos.byType[KNIGHT] | pos.byType[BISHOP]) + 2 * popCount(pos.byType[ROOK]) + 4 * popCount(pos.byType[QUEEN]);
    if (phase > 24)
        phase = 24;

    const int score = ((mg[WHITE] - mg[BLACK]) * phase + (eg[WHITE] - eg[BLACK]) * (24 - phase)) / 24;

    return us == WHITE ? score : -score;
}
