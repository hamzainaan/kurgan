#include "endgame.h"

#include "movegen.h"
#include "position.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace
{
    constexpr int SCALE_NORMAL = 64;
    constexpr int SCALE_MINOR_EDGE = 4;
    constexpr int SCALE_MINOR_UP = 14;
    constexpr int SCALE_PHILIDOR = 16;
    constexpr int SCALE_OCB_LONE = 4;
    constexpr int SCALE_OCB_EVEN = 16;
    constexpr int SCALE_OCB_UP = 32;
    constexpr int SCALE_QUEEN_VS_SEVENTH = 8;
    constexpr int KPK_WIN_BASE = 600;
    constexpr int KPK_WIN_RANK = 20;

    constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
    constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;
    constexpr Bitboard LIGHT_SQ_BB = 0x55AA55AA55AA55AAULL;

    constexpr int KPK_SIZE = 2 * 24 * 64 * 64;
    constexpr uint8_t KPK_INVALID = 0;
    constexpr uint8_t KPK_UNKNOWN = 1;
    constexpr uint8_t KPK_DRAW = 2;
    constexpr uint8_t KPK_WIN = 4;

    std::vector<uint8_t> kpk;

    int fileOf(int sq) { return sq & 7; }
    int rankOf(int sq) { return sq >> 3; }
    int relativeRank(Color c, int sq) { return c == WHITE ? rankOf(sq) : 7 - rankOf(sq); }
    int queeningSquare(Color c, int sq) { return c == WHITE ? 56 + fileOf(sq) : fileOf(sq); }
    bool lightSquare(int sq) { return ((LIGHT_SQ_BB >> sq) & 1) != 0; }
    Bitboard bb(int sq) { return 1ULL << sq; }

    int distance(int a, int b)
    {
        return std::max(std::abs(fileOf(a) - fileOf(b)), std::abs(rankOf(a) - rankOf(b)));
    }

    Bitboard kingAttacks(int sq)
    {
        return movegen::attacks(KING, static_cast<Square>(sq), 0);
    }

    Bitboard whitePawnAttacks(int sq)
    {
        const Bitboard b = bb(sq);
        return ((b & ~FILE_A_BB) << 7) | ((b & ~FILE_H_BB) << 9);
    }

    int kpkIndex(int stm, int wk, int bk, int psq)
    {
        return stm | (bk << 1) | (wk << 7) | (fileOf(psq) << 13) | ((6 - rankOf(psq)) << 15);
    }

    uint8_t kpkInitial(int stm, int wk, int bk, int psq)
    {
        if (distance(wk, bk) <= 1 || wk == psq || bk == psq || (stm == WHITE && (whitePawnAttacks(psq) & bb(bk))))
            return KPK_INVALID;

        const int promo = psq + 8;
        if (stm == WHITE && rankOf(psq) == 6 && wk != promo && bk != promo
            && (distance(bk, promo) > 1 || distance(wk, promo) == 1))
            return KPK_WIN;

        if (stm == BLACK)
        {
            const Bitboard escapes = kingAttacks(bk) & ~(kingAttacks(wk) | whitePawnAttacks(psq));
            if (escapes == 0 || (kingAttacks(bk) & bb(psq) & ~kingAttacks(wk)))
                return KPK_DRAW;
        }

        return KPK_UNKNOWN;
    }

    uint8_t kpkClassify(int stm, int wk, int bk, int psq)
    {
        const uint8_t good = stm == WHITE ? KPK_WIN : KPK_DRAW;
        const uint8_t bad = stm == WHITE ? KPK_DRAW : KPK_WIN;

        uint8_t r = KPK_INVALID;
        Bitboard moves = kingAttacks(stm == WHITE ? wk : bk);
        while (moves)
        {
            const int to = std::countr_zero(moves);
            moves &= moves - 1;
            r |= stm == WHITE ? kpk[static_cast<size_t>(kpkIndex(BLACK, to, bk, psq))]
                              : kpk[static_cast<size_t>(kpkIndex(WHITE, wk, to, psq))];
        }

        if (stm == WHITE)
        {
            if (rankOf(psq) < 6)
                r |= kpk[static_cast<size_t>(kpkIndex(BLACK, wk, bk, psq + 8))];
            if (rankOf(psq) == 1 && psq + 8 != wk && psq + 8 != bk)
                r |= kpk[static_cast<size_t>(kpkIndex(BLACK, wk, bk, psq + 16))];
        }

        return (r & good) ? good : (r & KPK_UNKNOWN) ? KPK_UNKNOWN : bad;
    }

    bool kpkWin(Color strong, int strongKing, int weakKing, int psq, Color stm)
    {
        int w = strongKing;
        int b = weakKing;
        int p = psq;
        if (strong == BLACK)
        {
            w ^= 56;
            b ^= 56;
            p ^= 56;
        }
        if (fileOf(p) > 3)
        {
            w ^= 7;
            b ^= 7;
            p ^= 7;
        }
        return kpk[static_cast<size_t>(kpkIndex(stm == strong ? WHITE : BLACK, w, b, p))] == KPK_WIN;
    }

    struct Material
    {
        int pawns[COLOR_NB];
        int knights[COLOR_NB];
        int bishops[COLOR_NB];
        int rooks[COLOR_NB];
        int queens[COLOR_NB];
        int npm[COLOR_NB];
    };

    int count(const Position &pos, Color c, PieceType pt)
    {
        return std::popcount(pos.byColor[c] & pos.byType[pt]);
    }

    int scaleFactor(const Position &pos, const Material &m, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const int strongKing = pos.kingSquare(strong);
        const int weakKing = pos.kingSquare(weak);
        const int strongPieces = m.knights[strong] + m.bishops[strong] + m.rooks[strong] + m.queens[strong];
        const int weakPieces = m.knights[weak] + m.bishops[weak] + m.rooks[weak] + m.queens[weak];

        if (m.pawns[strong] == 0)
        {
            if (m.knights[strong] == 2 && strongPieces == 2 && weakPieces == 0 && m.pawns[weak] == 0)
                return 0;

            const bool queenVsBishopKnight = m.queens[strong] == 1 && strongPieces == 1
                                             && m.bishops[weak] == 1 && m.knights[weak] == 1 && weakPieces == 2;

            if (m.npm[strong] - m.npm[weak] <= pieceValue(BISHOP) && !queenVsBishopKnight)
                return m.npm[strong] < pieceValue(ROOK) ? 0
                       : m.npm[weak] <= pieceValue(BISHOP) ? SCALE_MINOR_EDGE
                                                           : SCALE_MINOR_UP;

            if (m.queens[strong] == 1 && strongPieces == 1 && weakPieces == 0 && m.pawns[weak] == 1)
            {
                const int psq = std::countr_zero(pos.byColor[weak] & pos.byType[PAWN]);
                const int file = fileOf(psq);
                if (relativeRank(weak, psq) == 6 && (file == 0 || file == 2 || file == 5 || file == 7)
                    && distance(weakKing, psq) <= 1 && distance(strongKing, psq) > 3)
                    return SCALE_QUEEN_VS_SEVENTH;
            }

            return SCALE_NORMAL;
        }

        const Bitboard strongPawns = pos.byColor[strong] & pos.byType[PAWN];

        if (m.knights[strong] == 0 && m.rooks[strong] == 0 && m.queens[strong] == 0 && m.bishops[strong] <= 1
            && weakPieces == 0 && m.pawns[weak] == 0
            && ((strongPawns & ~FILE_A_BB) == 0 || (strongPawns & ~FILE_H_BB) == 0))
        {
            const int queening = queeningSquare(strong, std::countr_zero(strongPawns));
            const Bitboard bishop = pos.byColor[strong] & pos.byType[BISHOP];
            const bool rightBishop = bishop != 0 && lightSquare(std::countr_zero(bishop)) == lightSquare(queening);
            if (!rightBishop && distance(weakKing, queening) <= 1)
                return 0;
        }

        if (m.rooks[strong] == 1 && strongPieces == 1 && m.pawns[strong] == 1
            && m.rooks[weak] == 1 && weakPieces == 1 && m.pawns[weak] == 0)
        {
            const int psq = std::countr_zero(strongPawns);
            const int queening = queeningSquare(strong, psq);
            const bool rookPawn = fileOf(psq) == 0 || fileOf(psq) == 7;
            const bool kingInFront = std::abs(fileOf(weakKing) - fileOf(psq)) <= 1
                                     && relativeRank(strong, weakKing) > relativeRank(strong, psq);
            if (kingInFront && (relativeRank(strong, psq) <= 4 || (rookPawn && distance(weakKing, queening) <= 1)))
                return SCALE_PHILIDOR;
        }

        if (m.bishops[strong] == 1 && strongPieces == 1 && m.bishops[weak] == 1 && weakPieces == 1)
        {
            const int strongBishop = std::countr_zero(pos.byColor[strong] & pos.byType[BISHOP]);
            const int weakBishop = std::countr_zero(pos.byColor[weak] & pos.byType[BISHOP]);
            if (lightSquare(strongBishop) != lightSquare(weakBishop))
            {
                if (m.pawns[strong] - m.pawns[weak] <= 1)
                    return m.pawns[strong] <= 1 ? SCALE_OCB_LONE : SCALE_OCB_EVEN;
                return SCALE_OCB_UP;
            }
        }

        return SCALE_NORMAL;
    }
}

void endgame::init()
{
    if (!kpk.empty())
        return;

    kpk.assign(KPK_SIZE, KPK_INVALID);

    for (int psq = 8; psq < 56; ++psq)
    {
        if (fileOf(psq) > 3)
            continue;
        for (int wk = 0; wk < 64; ++wk)
            for (int bk = 0; bk < 64; ++bk)
                for (int stm = WHITE; stm <= BLACK; ++stm)
                    kpk[static_cast<size_t>(kpkIndex(stm, wk, bk, psq))] = kpkInitial(stm, wk, bk, psq);
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (int psq = 8; psq < 56; ++psq)
        {
            if (fileOf(psq) > 3)
                continue;
            for (int wk = 0; wk < 64; ++wk)
                for (int bk = 0; bk < 64; ++bk)
                    for (int stm = WHITE; stm <= BLACK; ++stm)
                    {
                        uint8_t &entry = kpk[static_cast<size_t>(kpkIndex(stm, wk, bk, psq))];
                        if (entry != KPK_UNKNOWN)
                            continue;
                        const uint8_t result = kpkClassify(stm, wk, bk, psq);
                        if (result != KPK_UNKNOWN)
                        {
                            entry = result;
                            changed = true;
                        }
                    }
        }
    }
}

int endgame::adjust(const Position &pos, int eval)
{
    const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
    if (std::popcount(occ) > 14 || kpk.empty())
        return eval;

    Material m{};
    for (int c = WHITE; c <= BLACK; ++c)
    {
        const Color color = static_cast<Color>(c);
        m.pawns[c] = count(pos, color, PAWN);
        m.knights[c] = count(pos, color, KNIGHT);
        m.bishops[c] = count(pos, color, BISHOP);
        m.rooks[c] = count(pos, color, ROOK);
        m.queens[c] = count(pos, color, QUEEN);
        m.npm[c] = m.knights[c] * pieceValue(KNIGHT) + m.bishops[c] * pieceValue(BISHOP)
                   + m.rooks[c] * pieceValue(ROOK) + m.queens[c] * pieceValue(QUEEN);
    }

    const Color stm = pos.sideToMove;

    if (m.npm[WHITE] == 0 && m.npm[BLACK] == 0 && m.pawns[WHITE] + m.pawns[BLACK] == 1)
    {
        const Color strong = m.pawns[WHITE] == 1 ? WHITE : BLACK;
        const int psq = std::countr_zero(pos.byType[PAWN]);
        if (!kpkWin(strong, pos.kingSquare(strong), pos.kingSquare(static_cast<Color>(strong ^ 1)), psq, stm))
            return 0;

        const int winning = KPK_WIN_BASE + KPK_WIN_RANK * relativeRank(strong, psq);
        return strong == stm ? std::max(eval, winning) : std::min(eval, -winning);
    }

    if (eval == 0)
        return 0;

    const Color strong = eval > 0 ? stm : static_cast<Color>(stm ^ 1);
    const int scale = scaleFactor(pos, m, strong);
    return scale == SCALE_NORMAL ? eval : eval * scale / SCALE_NORMAL;
}
