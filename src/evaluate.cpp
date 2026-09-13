#include "evaluate.h"

#include "movegen.h"
#include "position.h"
#include "tuned_params.h"

#include <array>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
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

    // Piece-type order used by the per-square evaluation terms.
    constexpr PieceType mobilityTypes[4] = {KNIGHT, BISHOP, ROOK, QUEEN};
    constexpr PieceType outpostTypes[2] = {KNIGHT, BISHOP};

    void mobilityScore(const Position &pos, Color c, Bitboard occ, int &mg, int &eg)
    {
        const Bitboard notOwn = ~pos.byColor[c];

        for (int i = 0; i < 4; ++i)
        {
            int count = 0;
            Bitboard b = pos.byColor[c] & pos.byType[mobilityTypes[i]];
            while (b)
            {
                const Square sq = popLsb(b);
                count += popCount(movegen::attacks(mobilityTypes[i], sq, occ) & notOwn);
            }
            mg += tuned::MG_MOBILITY[i] * count;
            eg += tuned::EG_MOBILITY[i] * count;
        }
    }

    void outpostScore(const Position &pos, Color c, int &mg, int &eg)
    {
        const Color them = static_cast<Color>(c ^ 1);
        const Bitboard ownPawns = pos.byColor[c] & pos.byType[PAWN];
        const Bitboard enemyPawns = pos.byColor[them] & pos.byType[PAWN];

        for (int i = 0; i < 2; ++i)
        {
            Bitboard b = pos.byColor[c] & pos.byType[outpostTypes[i]];
            while (b)
            {
                const Square sq = popLsb(b);
                const int file = sq & 7;
                const int rank = sq >> 3;
                const int relRank = (c == WHITE) ? rank : 7 - rank;

                // Only ranks 3..6 (own perspective) can hold an outpost.
                if (relRank < 3 || relRank > 6)
                    continue;

                // Must be defended by one of our own pawns.
                if (!(movegen::pawnAttacksFrom(them, sq) & ownPawns))
                    continue;

                // Reject if an enemy pawn on an adjacent file can still reach
                // a square from which it would attack this one.
                Bitboard ahead = 0;
                if (c == WHITE)
                    for (int r = rank + 1; r < 8; ++r)
                        ahead |= rankMask(r);
                else
                    for (int r = 0; r < rank; ++r)
                        ahead |= rankMask(r);

                if (enemyPawns & (fileMask(file - 1) | fileMask(file + 1)) & ahead)
                    continue;

                mg += tuned::MG_OUTPOST[i];
                eg += tuned::EG_OUTPOST[i];
            }
        }
    }

    void pawnStructureScore(const Position &pos, Color c, int &mg, int &eg)
    {
        const Color them = static_cast<Color>(c ^ 1);
        const Bitboard pawns = pos.byColor[c] & pos.byType[PAWN];
        const Bitboard enemyPawns = pos.byColor[them] & pos.byType[PAWN];

        int isolated = 0;
        int doubled = 0;

        Bitboard b = pawns;
        while (b)
        {
            const Square sq = popLsb(b);
            const int file = sq & 7;
            const int rank = sq >> 3;

            if (!(pawns & (fileMask(file - 1) | fileMask(file + 1))))
                ++isolated;

            Bitboard ahead = 0;
            if (c == WHITE)
                for (int r = rank + 1; r < 8; ++r)
                    ahead |= rankMask(r);
            else
                for (int r = 0; r < rank; ++r)
                    ahead |= rankMask(r);

            const Bitboard passFiles = fileMask(file - 1) | fileMask(file) | fileMask(file + 1);
            if (!(enemyPawns & ahead & passFiles))
            {
                const int bucket = (c == WHITE) ? rank : 7 - rank;
                mg += tuned::MG_PASSED[bucket];
                eg += tuned::EG_PASSED[bucket];
            }
        }

        for (int f = 0; f < 8; ++f)
        {
            const int count = popCount(pawns & fileMask(f));
            if (count > 1)
                doubled += count - 1;
        }

        mg -= tuned::MG_ISOLATED * isolated + tuned::MG_DOUBLED * doubled;
        eg -= tuned::EG_ISOLATED * isolated + tuned::EG_DOUBLED * doubled;
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
                mg[c] += tuned::MATERIAL[pt] + tuned::MG_PST[pt][idx];
                eg[c] += tuned::MATERIAL[pt] + tuned::EG_PST[pt][idx];
            }
        }
    }

    // Per-square terms, resolved separately for both phases.
    const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
    for (int c = WHITE; c <= BLACK; ++c)
    {
        const Color color = static_cast<Color>(c);
        mobilityScore(pos, color, occ, mg[c], eg[c]);
        outpostScore(pos, color, mg[c], eg[c]);
        pawnStructureScore(pos, color, mg[c], eg[c]);
    }

    // Game phase: 0 (endgame) .. 24 (midgame).
    int phase = popCount(pos.byType[KNIGHT] | pos.byType[BISHOP]) + 2 * popCount(pos.byType[ROOK]) + 4 * popCount(pos.byType[QUEEN]);
    if (phase > 24)
        phase = 24;

    const int mgDiff = mg[WHITE] - mg[BLACK];
    const int egDiff = eg[WHITE] - eg[BLACK];
    const int score = (mgDiff * phase + egDiff * (24 - phase)) / 24;

    const int stmScore = (us == WHITE) ? score : -score;
    return stmScore + tuned::TEMPO;
}