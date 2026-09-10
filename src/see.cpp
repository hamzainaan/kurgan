#include "see.h"

#include <algorithm>
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
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

    // All squares whose piece attacks 'to', both colors, given occupancy.
    Bitboard attackersTo(const Position &pos, Square to, Bitboard occ)
    {
        const Bitboard b = 1ULL << to;
        constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
        constexpr Bitboard FILE_H_BB = 0x8080808080808080ULL;

        const Bitboard whitePawns = ((b >> 7) & ~FILE_A_BB) | ((b >> 9) & ~FILE_H_BB);
        const Bitboard blackPawns = ((b << 9) & ~FILE_A_BB) | ((b << 7) & ~FILE_H_BB);

        return (whitePawns & pos.byColor[WHITE] & pos.byType[PAWN]) |
               (blackPawns & pos.byColor[BLACK] & pos.byType[PAWN]) |
               (movegen::attacks(KNIGHT, to, occ) & pos.byType[KNIGHT]) |
               (movegen::attacks(KING, to, occ) & pos.byType[KING]) |
               (movegen::attacks(BISHOP, to, occ) & (pos.byType[BISHOP] | pos.byType[QUEEN])) |
               (movegen::attacks(ROOK, to, occ) & (pos.byType[ROOK] | pos.byType[QUEEN]));
    }
}

int see::evaluate(const Position &pos, Move m)
{
    if (m.isCastling())
        return 0;

    const Square from = m.from();
    const Square to = m.to();
    const Color us = pos.sideToMove;
    const Color them = static_cast<Color>(us ^ 1);

    // Value of the piece captured by the move (en passant captures a pawn).
    int swap[32] = {};
    swap[0] = m.isEnPassant() ? pieceValue(PAWN) : pieceValue(typeOf(pos.board[to]));

    // Piece now standing on 'to' (a promoting pawn becomes the promotion piece).
    PieceType onTo = m.isPromotion() ? m.promoType() : typeOf(pos.board[from]);
    if (m.isPromotion())
        swap[0] += pieceValue(m.promoType()) - pieceValue(PAWN);

    // Occupancy after the move: the mover leaves 'from' and lands on 'to';
    // the captured en passant pawn is removed.
    Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
    occ ^= 1ULL << from;
    occ |= 1ULL << to;
    if (m.isEnPassant())
        occ &= ~(1ULL << (to + (us == WHITE ? -8 : 8)));

    Bitboard attackers = attackersTo(pos, to, occ) & occ;

    Color side = them;
    int d = 0;

    while (d < 31)
    {
        const Bitboard sideAttackers = attackers & pos.byColor[side];
        if (!sideAttackers)
            break;

        // Least valuable attacker captures first (PAWN..KING).
        int pt = PAWN;
        for (int t = PAWN; t <= static_cast<int>(KING); ++t)
        {
            if (sideAttackers & pos.byType[t])
            {
                pt = t;
                break;
            }
        }

        const Square atkSq = lsb(sideAttackers & pos.byType[pt]);
        attackers &= ~(1ULL << atkSq);
        occ &= ~(1ULL << atkSq);

        // Removing the attacker may reveal an X-ray slider behind it.
        if (pt == PAWN || pt == BISHOP || pt == QUEEN)
            attackers |= movegen::attacks(BISHOP, to, occ) & occ &
                         (pos.byType[BISHOP] | pos.byType[QUEEN]);
        if (pt == ROOK || pt == QUEEN)
            attackers |= movegen::attacks(ROOK, to, occ) & occ &
                         (pos.byType[ROOK] | pos.byType[QUEEN]);

        ++d;
        swap[d] = pieceValue(onTo) - swap[d - 1];
        onTo = static_cast<PieceType>(pt);
        side = static_cast<Color>(side ^ 1);
    }

    // Negamax the swap list from the tail back to the root capture.
    while (--d >= 0)
        swap[d] = -std::max(-swap[d], swap[d + 1]);

    return swap[0];
}

bool see::ge(const Position &pos, Move m, int threshold)
{
    return evaluate(pos, m) >= threshold;
}

int see::sign(const Position &pos, Move m)
{
    const int v = evaluate(pos, m);
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}
