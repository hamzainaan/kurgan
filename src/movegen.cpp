#include "movegen.h"

#include <cassert>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
    // Chess960 (Fischer Random) mode: only affects how castling moves are
    // rendered in (and parsed from) coordinate notation.
    bool chess960Mode = false;

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
        0x0780001081400021ULL, 0x0140025004402000ULL, 0x1080200082081000ULL, 0x2580100018000480ULL,
        0x0600200822009410ULL, 0x0200100A00080104ULL, 0x04003A10180C0081ULL, 0x0300028022004500ULL,
        0x0008800088C00020ULL, 0x80C2402010004000ULL, 0x200880300086A000ULL, 0x8401002300081000ULL,
        0x0081005100080004ULL, 0x0420808004008200ULL, 0x0011001402002100ULL, 0x08020004018A0043ULL,
        0x808000400840A002ULL, 0x0002020041002080ULL, 0x05044100200B0010ULL, 0x4110808008061000ULL,
        0x0028004004004200ULL, 0xA2A1010024004208ULL, 0x0000840002103803ULL, 0x0000420000408401ULL,
        0x0840008480044420ULL, 0x0810004040002000ULL, 0x5210421100600100ULL, 0x2000A20200100840ULL,
        0x8484008080080004ULL, 0x08184400802E0080ULL, 0x2011009100020004ULL, 0x1400004200009504ULL,
        0xC000400060800081ULL, 0x52A1200080804000ULL, 0x0010006006801480ULL, 0x0000100080800800ULL,
        0x1408812802800400ULL, 0x0482005002000488ULL, 0x6080020104001008ULL, 0x0000801043801100ULL,
        0x0001C000A0818000ULL, 0x000842201000C000ULL, 0x0000220040860012ULL, 0x2040220050C20008ULL,
        0x8048000804008080ULL, 0x000A005008420014ULL, 0x0042281002440001ULL, 0x2020036085020014ULL,
        0x0010218002400080ULL, 0x0440200082400080ULL, 0x9A02004030A48200ULL, 0x088A090010012100ULL,
        0x400C810400280080ULL, 0x1001003208040100ULL, 0x20120A0950080C00ULL, 0x040A028101441200ULL,
        0x80210C1080006041ULL, 0x4080400023801901ULL, 0x1001024020009029ULL, 0x004A100020C90005ULL,
        0x048A004408102002ULL, 0x020D008C00080249ULL, 0x40A04904B0281204ULL, 0x0400A08400402102ULL};

    constexpr uint64_t bishopMagics[SQUARE_NB] = {
        0x0084102A10D40882ULL, 0x860A504216830A9BULL, 0x0924040C8A100040ULL, 0x8584040290948141ULL,
        0x920A02114A820414ULL, 0x90060246A1320406ULL, 0x32CE022514C03901ULL, 0x288300C1D0180880ULL,
        0x6010401124048088ULL, 0x148C821002060658ULL, 0xB0200C2102160840ULL, 0x1000110401800922ULL,
        0x5000191040800E64ULL, 0xB104320806088342ULL, 0x09C88A2814143C43ULL, 0x18C10AC308011001ULL,
        0x0821439908036801ULL, 0x894A00A00A440110ULL, 0x2064000801282200ULL, 0x71A8020404200821ULL,
        0x4004002A010C1004ULL, 0x048100860382050BULL, 0x56B1028C04176C21ULL, 0x04052482021A0218ULL,
        0x94D0100046041004ULL, 0x002420160C1802ACULL, 0x08190904100404A0ULL, 0xE9040802012200C0ULL,
        0xA265840022020208ULL, 0x02648D00A2024200ULL, 0xF00404400A881480ULL, 0x45EC8E8000220804ULL,
        0x2208244002100318ULL, 0x2004240344201A04ULL, 0x4620104802100684ULL, 0x169D840108040100ULL,
        0x22080B00C00400C2ULL, 0x00688D1200830088ULL, 0x020448088C014C01ULL, 0x000808C1402C8A0AULL,
        0x004C10180C010800ULL, 0x0024880410443F00ULL, 0x202012010B015010ULL, 0x1C3501EC98002404ULL,
        0x00116031A0804400ULL, 0x089C02CE82000302ULL, 0xA489011122000C08ULL, 0x8A342E8182141104ULL,
        0x03008804108C0001ULL, 0x6C45A90110704014ULL, 0x8813984544301222ULL, 0x0111022020882146ULL,
        0x844100C006820124ULL, 0x0591401294510447ULL, 0x4231102109440A00ULL, 0x423002083502A20CULL,
        0x680182A890100882ULL, 0x8E0B8081CC3E2025ULL, 0xD8000E0204420800ULL, 0x41104A0009420A0CULL,
        0x0021414A106A0208ULL, 0x0C44940821082180ULL, 0x4004E00875411C02ULL, 0xD088600C9A00C705ULL};

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

    // Same as movegen::squareAttacked, but sliding attacks are resolved with the
    // supplied occupancy. Castling has to test squares in positions where the
    // king and/or the castling rook are somewhere else, which no plain
    // occupancy query can express.
    bool squareAttackedWith(const Position &pos, Square sq, Color by, Bitboard occ)
    {
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

    // Squares strictly between 'a' and 'b'; both must share a rank (which is
    // always the case for the squares a castling king or rook travels over).
    Bitboard betweenOnRank(Square a, Square b)
    {
        const int lo = a < b ? a : b;
        const int hi = a < b ? b : a;
        Bitboard between = 0;
        for (int s = lo + 1; s < hi; ++s)
            between |= 1ULL << s;
        return between;
    }

    Square kingSquareOf(const Position &pos, Color c)
    {
        const Bitboard k = pos.byColor[c] & pos.byType[KING];
        return k ? lsb(k) : SQ_NONE;
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

        if (m.isCastling())
        {
            // King and rook are lifted before both are put back, so they may
            // swap squares (Chess960: rook on f1, king on g1, ...).
            removePiece(pos, to);
            putPiece(pos, makePiece(us, KING), castlingKingTo(m));
            putPiece(pos, makePiece(us, ROOK), castlingRookTo(m));
            pos.sideToMove = them;
            return;
        }

        if (m.isEnPassant())
            removePiece(pos, static_cast<Square>(to + (us == WHITE ? -8 : 8)));
        else if (pos.board[to] != NO_PIECE)
            removePiece(pos, to);

        putPiece(pos, m.isPromotion() ? makePiece(us, m.promoType()) : piece, to);

        pos.sideToMove = them;
    }

    void generatePawnMoves(const Position &pos, Bitboard empty, MoveList &list, bool tacticalOnly = false)
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
        while (!tacticalOnly && b)
        {
            const Square to = popLsb(b);
            list.add(Move(static_cast<Square>(to - up), to));
        }

        b = (us == WHITE ? ((pawns & RANK_2_BB) << 16) : ((pawns & RANK_7_BB) >> 16)) & empty & (us == WHITE ? empty << 8 : empty >> 8);
        while (!tacticalOnly && b)
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

    void generatePieceMoves(const Position &pos, PieceType pt, MoveList &list, bool tacticalOnly = false)
    {
        const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
        const Bitboard notOurs = ~pos.byColor[pos.sideToMove];

        Bitboard pieces = pos.byColor[pos.sideToMove] & pos.byType[pt];
        while (pieces)
        {
            const Square from = popLsb(pieces);
            Bitboard targets = pieceAttacks(pt, from, occ) & notOurs;
            if (tacticalOnly)
                targets &= occ;
            while (targets)
                list.add(Move(from, popLsb(targets)));
        }
    }

    void generateKingMoves(const Position &pos, MoveList &list, bool tacticalOnly = false)
    {
        const Bitboard k = pos.byColor[pos.sideToMove] & pos.byType[KING];
        if (!k)
            return;
        const Square ksq = lsb(k);
        Bitboard targets = kingAttacks[ksq] & ~pos.byColor[pos.sideToMove];
        if (tacticalOnly)
            targets &= pos.byColor[WHITE] | pos.byColor[BLACK];
        while (targets)
            list.add(Move(ksq, popLsb(targets)));
    }

    void generateCastling(const Position &pos, MoveList &list)
    {
        const Color us = pos.sideToMove;
        const Square ksq = kingSquareOf(pos, us);
        if (ksq == SQ_NONE)
            return;

        const Rank backRank = us == WHITE ? RANK_1 : RANK_8;

        for (int s = 0; s < CASTLING_SIDE_NB; ++s)
        {
            const CastlingSide side = static_cast<CastlingSide>(s);
            if (!(pos.castlingRights & castlingRight(us, side)))
                continue;

            const Square rsq = pos.castlingRookSquare[us][side];
            if (pos.board[rsq] != makePiece(us, ROOK))
                continue;
            if (rankOf(ksq) != backRank || rankOf(rsq) != backRank)
                continue;

            const Move m(ksq, rsq, CASTLING);
            if (movegen::canCastle(pos, m))
                list.add(m);
        }
    }
}

bool movegen::canCastle(const Position &pos, Move move)
{
    const Color us = pos.sideToMove;
    const Color them = static_cast<Color>(us ^ 1);
    const Square kFrom = move.from();
    const Square rFrom = move.to();

    if (pos.board[kFrom] != makePiece(us, KING) || pos.board[rFrom] != makePiece(us, ROOK))
        return false;

    const CastlingSide side = isKingSideCastling(move) ? KING_SIDE : QUEEN_SIDE;
    if (!(pos.castlingRights & castlingRight(us, side)) || pos.castlingRookSquare[us][side] != rFrom)
        return false;

    const Square kTo = castlingKingTo(move);
    const Square rTo = castlingRookTo(move);
    if (rankOf(kFrom) != rankOf(rFrom) || rankOf(kFrom) != rankOf(kTo) || rankOf(kFrom) != rankOf(rTo))
        return false;

    const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
    const Bitboard king = 1ULL << kFrom;
    const Bitboard rook = 1ULL << rFrom;

    if ((occ ^ king ^ rook) &
        (betweenOnRank(kFrom, kTo) | betweenOnRank(rFrom, rTo) | (1ULL << kTo) | (1ULL << rTo)))
        return false;

    const Bitboard kingPath = betweenOnRank(kFrom, kTo) | king;
    for (Bitboard b = kingPath; b;)
    {
        const Square s = popLsb(b);
        if (squareAttackedWith(pos, s, them, occ ^ king))
            return false;
    }

    const Bitboard afterKing = (occ & ~(king | rook)) | (1ULL << rTo);
    return !squareAttackedWith(pos, kTo, them, afterKing);
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

Bitboard movegen::pawnAttacksFrom(Color c, Square sq)
{
    return pawnAttacks[c][sq];
}

bool movegen::squareAttacked(const Position &pos, Square sq, Color by)
{
    return squareAttackedWith(pos, sq, by, pos.byColor[WHITE] | pos.byColor[BLACK]);
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

void movegen::generate_tactical_moves(const Position &pos, MoveList &list)
{
    list.clear();

    const Bitboard occ = pos.byColor[WHITE] | pos.byColor[BLACK];
    const Bitboard empty = ~occ;

    generatePawnMoves(pos, empty, list, true);
    generatePieceMoves(pos, KNIGHT, list, true);
    generatePieceMoves(pos, BISHOP, list, true);
    generatePieceMoves(pos, ROOK, list, true);
    generatePieceMoves(pos, QUEEN, list, true);
    generateKingMoves(pos, list, true);
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
            return canCastle(pos, move);
        return !squareAttacked(pos, to, them);
    }

    Position copy = pos;
    makeMove(copy, move);
    const Square ksq = lsb(copy.byColor[us] & copy.byType[KING]);
    return !squareAttacked(copy, ksq, them);
}

void movegen::setChess960(bool enabled)
{
    chess960Mode = enabled;
}

bool movegen::chess960()
{
    return chess960Mode;
}

std::string moveToUci(Move m)
{
    Square to = m.to();

    // Castling is stored as king -> rook; in normal chess the GUI expects
    // king -> king (e.g. e1g1).
    if (m.isCastling() && !movegen::chess960())
        to = castlingKingTo(m);

    std::string s;
    s += static_cast<char>('a' + fileOf(m.from()));
    s += static_cast<char>('1' + rankOf(m.from()));
    s += static_cast<char>('a' + fileOf(to));
    s += static_cast<char>('1' + rankOf(to));
    if (m.isPromotion())
        s += "nbrq"[m.promoType() - KNIGHT];
    return s;
}
