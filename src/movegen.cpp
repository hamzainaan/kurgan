#include "movegen.h"

#include <cassert>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
    // Portable bit utilities.
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

    // Board geometry masks.
    constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;
    constexpr Bitboard RANK_2_BB = RANK_1_BB << 8;
    constexpr Bitboard RANK_3_BB = RANK_1_BB << 16;
    constexpr Bitboard RANK_4_BB = RANK_1_BB << 24;
    constexpr Bitboard RANK_5_BB = RANK_1_BB << 32;
    constexpr Bitboard RANK_6_BB = RANK_1_BB << 40;
    constexpr Bitboard RANK_7_BB = RANK_1_BB << 48;
    constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;

    constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
    constexpr Bitboard FILE_B_BB = FILE_A_BB << 1;
    constexpr Bitboard FILE_C_BB = FILE_A_BB << 2;
    constexpr Bitboard FILE_D_BB = FILE_A_BB << 3;
    constexpr Bitboard FILE_E_BB = FILE_A_BB << 4;
    constexpr Bitboard FILE_F_BB = FILE_A_BB << 5;
    constexpr Bitboard FILE_G_BB = FILE_A_BB << 6;
    constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;

    // Pre-computed leaper and pawn attack tables.
    std::array<Bitboard, SQUARE_NB> knightAttacks;
    std::array<Bitboard, SQUARE_NB> kingAttacks;
    std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> pawnAttacks;

    // Magic bitboard tables (fancy, variable shift).
    struct Magic
    {
        Bitboard mask;
        Bitboard magic;
        Bitboard *attacks;
        int shift;
    };

    std::array<Magic, SQUARE_NB> rookMagic;
    std::array<Magic, SQUARE_NB> bishopMagic;

    std::array<Bitboard, 0x19000> rookTable;  // 102400 entries
    std::array<Bitboard, 0x1480> bishopTable; // 5248 entries

    constexpr uint64_t rookMagics[SQUARE_NB] = {
        0x0080001020400080ULL, 0x0040001000200040ULL, 0x0080081000200080ULL, 0x0080040800100080ULL,
        0x0080020400080080ULL, 0x0080010200040080ULL, 0x0080008001000200ULL, 0x0080002040800100ULL,
        0x0000800020400080ULL, 0x0000400020005000ULL, 0x0000801000200080ULL, 0x0000800800100080ULL,
        0x0000800400080080ULL, 0x0000800200040080ULL, 0x0000800100020080ULL, 0x0000800040800100ULL,
        0x0000208000400080ULL, 0x0000404000201000ULL, 0x0000808010002000ULL, 0x0000808008001000ULL,
        0x0000808004000800ULL, 0x0000808002000400ULL, 0x0000010100020004ULL, 0x0000020000408104ULL,
        0x0000208080004000ULL, 0x0000200040005000ULL, 0x0000100080200080ULL, 0x0000080080100080ULL,
        0x0000040080080080ULL, 0x0000020080040080ULL, 0x0000010080800200ULL, 0x0000800080004100ULL,
        0x0000204000800080ULL, 0x0000200040401000ULL, 0x0000100080802000ULL, 0x0000080080801000ULL,
        0x0000040080800800ULL, 0x0000020080800400ULL, 0x0000020001010004ULL, 0x0000800040800100ULL,
        0x0000204000808000ULL, 0x0000200040008080ULL, 0x0000100020008080ULL, 0x0000080010008080ULL,
        0x0000040008008080ULL, 0x0000020004008080ULL, 0x0000010002008080ULL, 0x0000004081020004ULL,
        0x0000204000800080ULL, 0x0000200040008080ULL, 0x0000100020008080ULL, 0x0000080010008080ULL,
        0x0000040008008080ULL, 0x0000020004008080ULL, 0x0000800100020080ULL, 0x0000800041000080ULL,
        0x00FFFCDDFCED714AULL, 0x007FFCDDFCED714AULL, 0x003FFFCDFFD88096ULL, 0x0000040810002101ULL,
        0x0001000204080011ULL, 0x0001000204000801ULL, 0x0001000082000401ULL, 0x0001FFFAABFAD1A2ULL};

    constexpr uint64_t bishopMagics[SQUARE_NB] = {
        0x0002020202020200ULL, 0x0002020202020000ULL, 0x0004010202000000ULL, 0x0004040080000000ULL,
        0x0001104000000000ULL, 0x0000821040000000ULL, 0x0000410410400000ULL, 0x0000104104104000ULL,
        0x0000040404040400ULL, 0x0000020202020200ULL, 0x0000040102020000ULL, 0x0000040400800000ULL,
        0x0000011040000000ULL, 0x0000008210400000ULL, 0x0000004104104000ULL, 0x0000002082082000ULL,
        0x0004000808080800ULL, 0x0002000404040400ULL, 0x0001000202020200ULL, 0x0000800802004000ULL,
        0x0000800400A00000ULL, 0x0000200100884000ULL, 0x0000400082082000ULL, 0x0000200041041000ULL,
        0x0002080010101000ULL, 0x0001040008080800ULL, 0x0000208004010400ULL, 0x0000404004010200ULL,
        0x0000840000802000ULL, 0x0000404002011000ULL, 0x0000808001041000ULL, 0x0000404000820800ULL,
        0x0001041000202000ULL, 0x0000820800101000ULL, 0x0000104400080800ULL, 0x0000020080080080ULL,
        0x0000404040040100ULL, 0x0000808100020100ULL, 0x0001010100020800ULL, 0x0000808080010400ULL,
        0x0000820820004000ULL, 0x0000410410002000ULL, 0x0000082088001000ULL, 0x0000002011000800ULL,
        0x0000080100400400ULL, 0x0001010101000200ULL, 0x0002020202000400ULL, 0x0001010101000200ULL,
        0x0000410410400000ULL, 0x0000208208200000ULL, 0x0000002084100000ULL, 0x0000000020888000ULL,
        0x0000001002020000ULL, 0x0000040408020000ULL, 0x0004040404040000ULL, 0x0002020202020000ULL,
        0x0000104104104000ULL, 0x0000002082082000ULL, 0x0000000020841000ULL, 0x0000000000208800ULL,
        0x0000000010020200ULL, 0x0000000404080200ULL, 0x0000040404040400ULL, 0x0002020202020200ULL};

    Square popLsb(Bitboard &b)
    {
        const Square s = static_cast<Square>(countrZero(b));
        b &= b - 1;
        return s;
    }

    Square lsb(Bitboard b)
    {
        return static_cast<Square>(countrZero(b));
    }

    Bitboard slidingAttack(int sq, Bitboard occ, const int *df, const int *dr)
    {
        Bitboard attacks = 0;
        const int f = sq & 7;
        const int r = sq >> 3;
        for (int d = 0; d < 4; ++d)
        {
            int nf = f + df[d];
            int nr = r + dr[d];
            while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
            {
                const Bitboard bit = 1ULL << (nr * 8 + nf);
                attacks |= bit;
                if (occ & bit)
                    break;
                nf += df[d];
                nr += dr[d];
            }
        }
        return attacks;
    }

    Bitboard rookSlow(Square sq, Bitboard occ)
    {
        static constexpr int df[4] = {1, -1, 0, 0};
        static constexpr int dr[4] = {0, 0, 1, -1};
        return slidingAttack(sq, occ, df, dr);
    }

    Bitboard bishopSlow(Square sq, Bitboard occ)
    {
        static constexpr int df[4] = {1, 1, -1, -1};
        static constexpr int dr[4] = {1, -1, 1, -1};
        return slidingAttack(sq, occ, df, dr);
    }

    void initLeapers()
    {
        for (int s = 0; s < SQUARE_NB; ++s)
        {
            const int f = s & 7;
            const int r = s >> 3;

            Bitboard k = 0;
            constexpr int ndf[8] = {1, 2, 2, 1, -1, -2, -2, -1};
            constexpr int ndr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
            for (int i = 0; i < 8; ++i)
            {
                const int nf = f + ndf[i];
                const int nr = r + ndr[i];
                if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
                    k |= 1ULL << (nr * 8 + nf);
            }
            knightAttacks[s] = k;

            Bitboard kg = 0;
            constexpr int kdf[8] = {1, 1, 1, 0, -1, -1, -1, 0};
            constexpr int kdr[8] = {1, 0, -1, -1, -1, 0, 1, 1};
            for (int i = 0; i < 8; ++i)
            {
                const int nf = f + kdf[i];
                const int nr = r + kdr[i];
                if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
                    kg |= 1ULL << (nr * 8 + nf);
            }
            kingAttacks[s] = kg;

            Bitboard pw = 0;
            if (r + 1 < 8)
            {
                if (f - 1 >= 0)
                    pw |= 1ULL << ((r + 1) * 8 + (f - 1));
                if (f + 1 < 8)
                    pw |= 1ULL << ((r + 1) * 8 + (f + 1));
            }
            pawnAttacks[WHITE][s] = pw;

            Bitboard pb = 0;
            if (r - 1 >= 0)
            {
                if (f - 1 >= 0)
                    pb |= 1ULL << ((r - 1) * 8 + (f - 1));
                if (f + 1 < 8)
                    pb |= 1ULL << ((r - 1) * 8 + (f + 1));
            }
            pawnAttacks[BLACK][s] = pb;
        }
    }

    void initMagics(Magic *magics, Bitboard *table, [[maybe_unused]] size_t tableSize, const uint64_t *magicNumbers, bool rook)
    {
        size_t offset = 0;
        for (int s = 0; s < SQUARE_NB; ++s)
        {
            const int f = s & 7;
            const int r = s >> 3;

            const Bitboard rankBB = RANK_1_BB << (8 * r);
            const Bitboard fileBB = FILE_A_BB << f;
            const Bitboard edges = ((RANK_1_BB | RANK_8_BB) & ~rankBB) | ((FILE_A_BB | FILE_H_BB) & ~fileBB);

            const Bitboard mask = (rook ? rookSlow(static_cast<Square>(s), 0) : bishopSlow(static_cast<Square>(s), 0)) & ~edges;
            const int bits = popCount(mask);

            magics[s].mask = mask;
            magics[s].magic = magicNumbers[s];
            magics[s].shift = 64 - bits;
            magics[s].attacks = table + offset;

            const uint64_t blockSize = 1ULL << bits;

            Bitboard b = 0;
            do
            {
                const uint64_t idx = (b * magicNumbers[s]) >> magics[s].shift;
                assert(idx < blockSize);
                magics[s].attacks[idx] = rook ? rookSlow(static_cast<Square>(s), b) : bishopSlow(static_cast<Square>(s), b);
                b = (b - mask) & mask;
            } while (b);

            offset += blockSize;
        }
        assert(offset == tableSize);
    }

    Bitboard bishopAttack(Square sq, Bitboard occ)
    {
        const Magic &m = bishopMagic[sq];
        return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
    }

    Bitboard rookAttack(Square sq, Bitboard occ)
    {
        const Magic &m = rookMagic[sq];
        return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
    }

    Bitboard pieceAttacks(PieceType pt, Square sq, Bitboard occ)
    {
        switch (pt)
        {
        case KNIGHT:
            return knightAttacks[sq];
        case KING:
            return kingAttacks[sq];
        case BISHOP:
            return bishopAttack(sq, occ);
        case ROOK:
            return rookAttack(sq, occ);
        case QUEEN:
            return bishopAttack(sq, occ) | rookAttack(sq, occ);
        default:
            return 0;
        }
    }

    void putPiece(Position &pos, Piece p, Square sq)
    {
        const Bitboard bit = 1ULL << sq;
        pos.board[sq] = p;
        pos.byPiece[p] |= bit;
        pos.byColor[colorOf(p)] |= bit;
        pos.byType[typeOf(p)] |= bit;
    }

    void removePiece(Position &pos, Square sq)
    {
        const Piece p = pos.board[sq];
        if (p == NO_PIECE)
            return;
        const Bitboard notBit = ~(1ULL << sq);
        pos.board[sq] = NO_PIECE;
        pos.byPiece[p] &= notBit;
        pos.byColor[colorOf(p)] &= notBit;
        pos.byType[typeOf(p)] &= notBit;
    }

    void makeMove(Position &pos, Move m)
    {
        const Color us = pos.sideToMove;
        const Color them = static_cast<Color>(us ^ 1);
        const Square from = m.from();
        const Square to = m.to();

        const Piece piece = pos.board[from];
        removePiece(pos, from);

        if (m.isEnPassant())
            removePiece(pos, static_cast<Square>(to + (us == WHITE ? -8 : 8)));
        else if (pos.board[to] != NO_PIECE)
            removePiece(pos, to);

        putPiece(pos, m.isPromotion() ? makePiece(us, m.promoType()) : piece, to);

        if (m.isCastling())
        {
            if (to == SQ_G1)
            {
                removePiece(pos, SQ_H1);
                putPiece(pos, W_ROOK, SQ_F1);
            }
            else if (to == SQ_C1)
            {
                removePiece(pos, SQ_A1);
                putPiece(pos, W_ROOK, SQ_D1);
            }
            else if (to == SQ_G8)
            {
                removePiece(pos, SQ_H8);
                putPiece(pos, B_ROOK, SQ_F8);
            }
            else if (to == SQ_C8)
            {
                removePiece(pos, SQ_A8);
                putPiece(pos, B_ROOK, SQ_D8);
            }
        }

        pos.sideToMove = them;
    }

    void generatePawnMoves(const Position &pos, Bitboard empty, MoveList &list)
    {
        const Color us = pos.sideToMove;
        const Color them = static_cast<Color>(us ^ 1);
        const int up = us == WHITE ? 8 : -8;
        const Bitboard promoRank = us == WHITE ? RANK_8_BB : RANK_1_BB;

        const Bitboard pawns = pos.byColor[us] & pos.byType[PAWN];
        const Bitboard single = (us == WHITE ? pawns << 8 : pawns >> 8) & empty;

        Bitboard b = single & promoRank;
        while (b)
        {
            const Square to = popLsb(b);
            const Square from = static_cast<Square>(to - up);
            list.add(Move(from, to, PROMOTION | QUEEN_PROMO));
            list.add(Move(from, to, PROMOTION | ROOK_PROMO));
            list.add(Move(from, to, PROMOTION | BISHOP_PROMO));
            list.add(Move(from, to, PROMOTION | KNIGHT_PROMO));
        }

        b = single & ~promoRank;
        while (b)
        {
            const Square to = popLsb(b);
            list.add(Move(static_cast<Square>(to - up), to));
        }

        b = (us == WHITE ? ((pawns & RANK_2_BB) << 16) : ((pawns & RANK_7_BB) >> 16)) & empty & (us == WHITE ? empty << 8 : empty >> 8);
        while (b)
        {
            const Square to = popLsb(b);
            list.add(Move(static_cast<Square>(to - 2 * up), to));
        }

        Bitboard p = pawns;
        while (p)
        {
            const Square from = popLsb(p);
            const Bitboard pawnAttacksFrom = pawnAttacks[us][from];

            Bitboard caps = pawnAttacksFrom & pos.byColor[them] & ~promoRank;
            while (caps)
                list.add(Move(from, popLsb(caps)));

            caps = pawnAttacksFrom & pos.byColor[them] & promoRank;
            while (caps)
            {
                const Square to = popLsb(caps);
                list.add(Move(from, to, PROMOTION | QUEEN_PROMO));
                list.add(Move(from, to, PROMOTION | ROOK_PROMO));
                list.add(Move(from, to, PROMOTION | BISHOP_PROMO));
                list.add(Move(from, to, PROMOTION | KNIGHT_PROMO));
            }

            if (pos.enPassantSquare != SQ_NONE && (pawnAttacksFrom & (1ULL << pos.enPassantSquare)))
                list.add(Move(from, pos.enPassantSquare, EN_PASSANT));
        }
    }

    void generatePieceMoves(const Position &pos, PieceType pt, MoveList &list)
    {
        const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
        const Bitboard notOurs = ~pos.byColor[pos.sideToMove];

        Bitboard pieces = pos.byColor[pos.sideToMove] & pos.byType[pt];
        while (pieces)
        {
            const Square from = popLsb(pieces);
            Bitboard targets = pieceAttacks(pt, from, occ) & notOurs;
            while (targets)
                list.add(Move(from, popLsb(targets)));
        }
    }

    void generateKingMoves(const Position &pos, MoveList &list)
    {
        const Bitboard k = pos.byColor[pos.sideToMove] & pos.byType[KING];
        if (!k)
            return;
        const Square ksq = lsb(k);
        Bitboard targets = kingAttacks[ksq] & ~pos.byColor[pos.sideToMove];
        while (targets)
            list.add(Move(ksq, popLsb(targets)));
    }

    void generateCastling(const Position &pos, MoveList &list)
    {
        const Color us = pos.sideToMove;
        const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];

        if (us == WHITE)
        {
            if ((pos.castlingRights & WHITE_OO) && pos.board[SQ_E1] == W_KING && pos.board[SQ_H1] == W_ROOK &&
                !(occ & ((1ULL << SQ_F1) | (1ULL << SQ_G1))))
                list.add(Move(SQ_E1, SQ_G1, CASTLING));
            if ((pos.castlingRights & WHITE_OOO) && pos.board[SQ_E1] == W_KING && pos.board[SQ_A1] == W_ROOK &&
                !(occ & ((1ULL << SQ_B1) | (1ULL << SQ_C1) | (1ULL << SQ_D1))))
                list.add(Move(SQ_E1, SQ_C1, CASTLING));
        }
        else
        {
            if ((pos.castlingRights & BLACK_OO) && pos.board[SQ_E8] == B_KING && pos.board[SQ_H8] == B_ROOK &&
                !(occ & ((1ULL << SQ_F8) | (1ULL << SQ_G8))))
                list.add(Move(SQ_E8, SQ_G8, CASTLING));
            if ((pos.castlingRights & BLACK_OOO) && pos.board[SQ_E8] == B_KING && pos.board[SQ_A8] == B_ROOK &&
                !(occ & ((1ULL << SQ_B8) | (1ULL << SQ_C8) | (1ULL << SQ_D8))))
                list.add(Move(SQ_E8, SQ_C8, CASTLING));
        }
    }

    bool castlingIsLegal(const Position &pos, Square to)
    {
        const Color us = pos.sideToMove;
        const Color them = static_cast<Color>(us ^ 1);
        const Square kFrom = us == WHITE ? SQ_E1 : SQ_E8;

        if (movegen::squareAttacked(pos, kFrom, them))
            return false;

        if (to == SQ_G1 || to == SQ_G8)
        {
            const Square f = us == WHITE ? SQ_F1 : SQ_F8;
            return !movegen::squareAttacked(pos, f, them) && !movegen::squareAttacked(pos, to, them);
        }

        const Square d = us == WHITE ? SQ_D1 : SQ_D8;
        const Square c = us == WHITE ? SQ_C1 : SQ_C8;
        return !movegen::squareAttacked(pos, d, them) && !movegen::squareAttacked(pos, c, them);
    }
}

void movegen::init()
{
    initLeapers();
    initMagics(rookMagic.data(), rookTable.data(), rookTable.size(), rookMagics, true);
    initMagics(bishopMagic.data(), bishopTable.data(), bishopTable.size(), bishopMagics, false);
}

Bitboard movegen::attacks(PieceType pt, Square sq, Bitboard occupied)
{
    return pieceAttacks(pt, sq, occupied);
}

bool movegen::squareAttacked(const Position &pos, Square sq, Color by)
{
    const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];

    if (pawnAttacks[static_cast<Color>(by ^ 1)][sq] & pos.byColor[by] & pos.byType[PAWN])
        return true;
    if (knightAttacks[sq] & pos.byColor[by] & pos.byType[KNIGHT])
        return true;
    if (kingAttacks[sq] & pos.byColor[by] & pos.byType[KING])
        return true;
    if (bishopAttack(sq, occ) & pos.byColor[by] & (pos.byType[BISHOP] | pos.byType[QUEEN]))
        return true;
    if (rookAttack(sq, occ) & pos.byColor[by] & (pos.byType[ROOK] | pos.byType[QUEEN]))
        return true;
    return false;
}

void movegen::generate_pseudo_legal_moves(const Position &pos, MoveList &list)
{
    list.clear();

    const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
    const Bitboard empty = ~occ;

    generatePawnMoves(pos, empty, list);
    generatePieceMoves(pos, KNIGHT, list);
    generatePieceMoves(pos, BISHOP, list);
    generatePieceMoves(pos, ROOK, list);
    generatePieceMoves(pos, QUEEN, list);
    generateKingMoves(pos, list);
    generateCastling(pos, list);
}

bool movegen::is_legal(const Position &pos, Move move)
{
    const Color us = pos.sideToMove;
    const Color them = static_cast<Color>(us ^ 1);
    const Square from = move.from();
    const Square to = move.to();

    if (pos.board[from] == NO_PIECE)
        return false;

    if (typeOf(pos.board[from]) == KING)
    {
        if (move.isCastling())
            return castlingIsLegal(pos, to);
        return !squareAttacked(pos, to, them);
    }

    Position copy = pos;
    makeMove(copy, move);
    const Square ksq = lsb(copy.byColor[us] & copy.byType[KING]);
    return !squareAttacked(copy, ksq, them);
}
