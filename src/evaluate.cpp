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
    constexpr int PAWN_VALUE = 101;
    constexpr int KNIGHT_VALUE = 320;
    constexpr int BISHOP_VALUE = 330;
    constexpr int ROOK_VALUE = 501;
    constexpr int QUEEN_VALUE = 901;

    constexpr std::array<int, PIECE_TYPE_NB> materialValue = {
        PAWN_VALUE, KNIGHT_VALUE, BISHOP_VALUE, ROOK_VALUE, QUEEN_VALUE, 0, 0, 0};

    // --- Piece-square tables (white perspective, index 0 = a8 ... 63 = h1) ---
    constexpr std::array<int, SQUARE_NB> MG_PAWN_PST = {
        0,    0,    0,    0,    0,    0,    0,    0,
        53,   52,   50,   50,   51,   51,   50,   50,
        14,   14,   23,   29,   31,   23,   13,   13,
        8,    9,   10,   25,   23,   11,    7,    4,
        0,    2,    2,   18,   16,   -3,   -2,   -3,
        2,   -3,   -5,   -1,    0,   -8,    0,    4,
        1,    5,    5,  -19,  -16,   14,   10,    3,
        0,    0,    0,    0,    0,    0,    0,    0,
    };

    constexpr std::array<int, SQUARE_NB> MG_KNIGHT_PST = {
        -52,  -40,  -30,  -30,  -29,  -31,  -40,  -51,
        -41,  -21,    2,    0,    0,    1,  -19,  -40,
        -31,    1,   11,   17,   17,   13,    2,  -29,
        -29,    5,   15,   23,   21,   18,    6,  -27,
        -30,   -1,   14,   18,   21,   15,    1,  -28,
        -31,    3,    6,   14,   16,   12,    5,  -31,
        -40,  -21,   -1,    4,    3,    0,  -20,  -38,
        -51,  -36,  -31,  -31,  -30,  -30,  -39,  -50,
    };

    constexpr std::array<int, SQUARE_NB> MG_BISHOP_PST = {
        -20,  -10,  -11,  -10,  -10,  -10,  -10,  -20,
        -12,    0,   -1,   -1,    0,    1,    1,  -12,
        -11,    1,    6,   10,   10,    7,    2,  -10,
        -10,    3,    6,   12,   12,    7,    5,   -9,
        -10,    1,    9,   12,   12,    8,    0,   -9,
        -9,   10,   11,   10,    9,   11,    8,   -9,
        -10,    8,    2,    0,    3,    1,   10,  -10,
        -21,  -10,  -10,  -11,  -10,  -13,  -11,  -20,
    };

    constexpr std::array<int, SQUARE_NB> MG_ROOK_PST = {
        2,    1,    1,    2,    2,    0,    1,    1,
        7,   12,   13,   13,   11,   12,   11,    7,
        -3,    2,    2,    2,    1,    1,    2,   -4,
        -4,    0,    1,    2,    1,    2,    0,   -4,
        -6,   -1,    0,    0,    1,   -1,    0,   -5,
        -7,   -1,   -1,   -1,    0,   -1,   -1,   -7,
        -8,    0,   -1,    0,    0,    0,   -1,   -9,
        -3,    0,    3,    8,    8,    5,   -4,   -5,
    };

    constexpr std::array<int, SQUARE_NB> MG_QUEEN_PST = {
        -20,   -9,   -8,   -4,   -3,   -8,   -9,  -18,
        -12,   -3,    0,    1,    1,    3,    2,   -8,
        -12,   -1,    5,    6,    7,    8,    2,   -7,
        -6,   -3,    4,    4,    5,    6,    1,   -5,
        -3,   -2,    4,    2,    5,    5,    1,   -5,
        -11,    4,    4,    2,    3,    3,    1,   -9,
        -11,   -1,    7,   -1,    1,    0,   -1,  -10,
        -19,  -12,  -10,    0,   -6,  -12,  -11,  -21,
    };

    constexpr std::array<int, SQUARE_NB> MG_KING_PST = {
        -30,  -40,  -40,  -50,  -50,  -40,  -40,  -30,
        -30,  -39,  -39,  -49,  -49,  -39,  -39,  -30,
        -29,  -39,  -39,  -50,  -50,  -38,  -37,  -29,
        -30,  -39,  -40,  -50,  -50,  -39,  -38,  -29,
        -20,  -29,  -31,  -42,  -42,  -31,  -29,  -20,
        -10,  -19,  -21,  -23,  -23,  -22,  -18,   -9,
        19,   21,   -1,   -4,   -4,   -2,   24,   21,
        18,   28,   10,   -4,   -2,    6,   35,   18,
    };

    constexpr std::array<int, SQUARE_NB> EG_PAWN_PST = {
        0,    0,    0,    0,    0,    0,    0,    0,
        55,   54,   53,   51,   52,   51,   53,   54,
        15,   15,   24,   30,   29,   23,   15,   15,
        10,    9,   11,   21,   20,    9,    9,    9,
        4,    4,    0,   15,   15,   -2,    2,    2,
        1,   -2,   -7,    0,    3,   -6,   -2,    1,
        3,    6,    7,  -17,  -16,   12,    7,    2,
        0,    0,    0,    0,    0,    0,    0,    0,
    };

    constexpr std::array<int, SQUARE_NB> EG_KNIGHT_PST = {
        -51,  -40,  -29,  -30,  -29,  -31,  -41,  -51,
        -40,  -20,   -1,    0,   -1,   -2,  -20,  -41,
        -30,    0,   10,   15,   14,   10,    0,  -30,
        -28,    5,   15,   21,   20,   16,    5,  -28,
        -29,   -1,   14,   20,   19,   15,    1,  -29,
        -29,    4,    6,   13,   13,    7,    3,  -29,
        -40,  -20,   -2,    2,    2,   -2,  -20,  -40,
        -50,  -40,  -31,  -30,  -30,  -30,  -40,  -50,
    };

    constexpr std::array<int, SQUARE_NB> EG_BISHOP_PST = {
        -20,  -11,  -11,  -10,  -10,  -10,  -10,  -20,
        -11,    0,    0,   -2,   -1,    0,    0,  -11,
        -10,    1,    4,    9,    8,    5,    1,   -9,
        -9,    5,    5,    9,   10,    5,    4,   -9,
        -10,    0,   10,   11,    9,    8,   -1,   -9,
        -9,    9,   10,    9,   10,    8,    8,   -9,
        -10,    4,    0,    0,    2,    0,    7,  -11,
        -21,  -10,  -11,  -10,  -10,  -10,  -11,  -20,
    };

    constexpr std::array<int, SQUARE_NB> EG_ROOK_PST = {
        3,    3,    3,    3,    2,    1,    2,    2,
        9,   13,   13,   13,   11,   12,   12,    7,
        -2,    3,    3,    2,    1,    1,    2,   -3,
        -2,    1,    3,    2,    2,    2,    0,   -3,
        -3,    1,    2,    1,    1,    0,    0,   -5,
        -4,    0,    0,    0,   -1,   -1,    0,   -6,
        -6,    0,    1,    1,    0,    0,   -1,   -6,
        -2,    1,    3,    7,    5,    2,   -2,   -4,
    };

    constexpr std::array<int, SQUARE_NB> EG_QUEEN_PST = {
        -19,   -8,   -8,   -3,   -2,   -8,   -8,  -17,
        -11,   -1,    1,    1,    2,    2,    1,   -8,
        -11,    0,    5,    7,    7,    8,    2,   -7,
        -5,    0,    5,    6,    7,    7,    2,   -3,
        -1,    0,    5,    6,    6,    6,    2,   -3,
        -10,    4,    5,    5,    5,    5,    1,   -9,
        -11,   -1,    5,    0,    1,    0,   -1,  -10,
        -19,  -11,  -10,   -3,   -5,  -11,  -10,  -21,
    };

    constexpr std::array<int, SQUARE_NB> EG_KING_PST = {
        -50,  -40,  -29,  -20,  -20,  -29,  -39,  -50,
        -29,  -18,   -9,    1,    1,   -7,  -17,  -28,
        -28,   -7,   21,   29,   28,   23,   -6,  -27,
        -29,   -7,   30,   38,   39,   31,   -6,  -27,
        -30,   -9,   28,   37,   37,   29,   -7,  -29,
        -30,   -9,   18,   26,   27,   18,   -6,  -28,
        -32,  -29,   -1,   -2,    0,   -1,  -25,  -31,
        -53,  -33,  -31,  -32,  -32,  -32,  -30,  -54,
    };

    const std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> mgPst = {
        MG_PAWN_PST, MG_KNIGHT_PST, MG_BISHOP_PST, MG_ROOK_PST, MG_QUEEN_PST, MG_KING_PST, {}, {}};
    const std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> egPst = {
        EG_PAWN_PST, EG_KNIGHT_PST, EG_BISHOP_PST, EG_ROOK_PST, EG_QUEEN_PST, EG_KING_PST, {}, {}};

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
            {KNIGHT, 3}, {BISHOP, 5}, {ROOK, 5}, {QUEEN, 3}};

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

        static constexpr std::array<int, 8> passedBonus = {0, 5, 9, 20, 36, 63, 102, 0};

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
