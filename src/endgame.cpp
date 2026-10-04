#include "endgame.h"

#include "movegen.h"
#include "position.h"

#include <algorithm>
#include <bit>
#include <bitset>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace
{
    constexpr int PAWN_VALUE = 176;
    constexpr int KNIGHT_VALUE = 720;
    constexpr int BISHOP_VALUE = 770;
    constexpr int ROOK_VALUE = 1170;
    constexpr int QUEEN_VALUE = 2270;
    constexpr int MATERIAL_CAP = 8000;
    constexpr int SCALE_NONE = -1;
    constexpr int SCALE_MAX = 2 * endgame::SCALE_NORMAL;
    constexpr int MAX_SCALED_PIECES = 4;

    constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
    constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;

    struct Material
    {
        int count[COLOR_NB][PIECE_TYPE_NB] = {};
        int npm[COLOR_NB] = {};
        int pieces[COLOR_NB] = {};
    };

    Material materialOf(const Position &pos)
    {
        Material m;
        for (int c = WHITE; c <= BLACK; ++c)
        {
            for (int pt = PAWN; pt <= QUEEN; ++pt)
                m.count[c][pt] = std::popcount(pos.byColor[c] & pos.byType[pt]);
            m.npm[c] = m.count[c][KNIGHT] * KNIGHT_VALUE + m.count[c][BISHOP] * BISHOP_VALUE
                     + m.count[c][ROOK] * ROOK_VALUE + m.count[c][QUEEN] * QUEEN_VALUE;
            m.pieces[c] = m.count[c][KNIGHT] + m.count[c][BISHOP] + m.count[c][ROOK] + m.count[c][QUEEN];
        }
        return m;
    }

    bool has(const Material &m, Color c, int pawns, int knights, int bishops, int rooks, int queens)
    {
        return m.count[c][PAWN] == pawns && m.count[c][KNIGHT] == knights && m.count[c][BISHOP] == bishops
               && m.count[c][ROOK] == rooks && m.count[c][QUEEN] == queens;
    }

    bool bare(const Material &m, Color c)
    {
        return m.pieces[c] == 0 && m.count[c][PAWN] == 0;
    }

    Bitboard bb(Square s) { return 1ULL << s; }
    Bitboard fileBB(int file) { return FILE_A_BB << file; }
    Square lsbSq(Bitboard b) { return static_cast<Square>(std::countr_zero(b)); }
    Square msbSq(Bitboard b) { return static_cast<Square>(63 - std::countl_zero(b)); }
    bool moreThanOne(Bitboard b) { return (b & (b - 1)) != 0; }

    Square popLsb(Bitboard &b)
    {
        const Square s = lsbSq(b);
        b &= b - 1;
        return s;
    }

    Square pieceSquare(const Position &pos, Color c, PieceType pt)
    {
        return lsbSq(pos.byColor[c] & pos.byType[pt]);
    }

    Bitboard piecesOf(const Position &pos, Color c, PieceType pt)
    {
        return pos.byColor[c] & pos.byType[pt];
    }

    Square shift(Square s, int delta) { return static_cast<Square>(static_cast<int>(s) + delta); }
    Square flipFile(Square s) { return static_cast<Square>(s ^ 7); }
    Square flipRank(Square s) { return static_cast<Square>(s ^ 56); }
    int pawnPush(Color c) { return c == WHITE ? 8 : -8; }
    int relativeRank(Color c, Square s) { return c == WHITE ? rankOf(s) : 7 - rankOf(s); }
    Square relativeSquare(Color c, Square s) { return c == WHITE ? s : flipRank(s); }

    int fileDistance(Square a, Square b) { return std::abs(fileOf(a) - fileOf(b)); }
    int rankDistance(Square a, Square b) { return std::abs(rankOf(a) - rankOf(b)); }
    int distance(Square a, Square b) { return std::max(fileDistance(a, b), rankDistance(a, b)); }

    bool oppositeColors(Square a, Square b)
    {
        const int s = static_cast<int>(a) ^ static_cast<int>(b);
        return ((s >> 3) ^ s) & 1;
    }

    int edgeDistance(int x) { return std::min(x, 7 - x); }

    int pushToEdge(Square s)
    {
        const int rd = edgeDistance(rankOf(s));
        const int fd = edgeDistance(fileOf(s));
        return 90 - (7 * fd * fd / 2 + 7 * rd * rd / 2);
    }

    int pushToCorner(Square s) { return std::abs(7 - rankOf(s) - fileOf(s)); }
    int pushClose(Square a, Square b) { return 140 - 20 * distance(a, b); }
    int pushAway(Square a, Square b) { return 120 - pushClose(a, b); }

    Bitboard forwardRanks(Color c, Square s)
    {
        const int r = rankOf(s);
        if (c == WHITE)
            return r == RANK_8 ? 0 : ~0ULL << (8 * (r + 1));
        return r == RANK_1 ? 0 : ~0ULL >> (8 * (8 - r));
    }

    Bitboard forwardFile(Color c, Square s) { return forwardRanks(c, s) & fileBB(fileOf(s)); }

    Bitboard passedSpan(Color c, Square s)
    {
        const int f = fileOf(s);
        const Bitboard files = fileBB(f) | (f > FILE_A ? fileBB(f - 1) : 0) | (f < FILE_H ? fileBB(f + 1) : 0);
        return forwardRanks(c, s) & files;
    }

    Bitboard pawnAttacks(Color c, Square s)
    {
        const Bitboard b = bb(s);
        if (c == WHITE)
            return ((b & ~FILE_A_BB) << 7) | ((b & ~FILE_H_BB) << 9);
        return ((b & ~FILE_A_BB) >> 9) | ((b & ~FILE_H_BB) >> 7);
    }

    Bitboard kingAttacks(Square s) { return movegen::attacks(KING, s, 0); }

    Bitboard occupied(const Position &pos) { return pos.byColor[WHITE] | pos.byColor[BLACK]; }

    bool passedPawn(const Position &pos, Color c, Square s)
    {
        return (piecesOf(pos, static_cast<Color>(c ^ 1), PAWN) & passedSpan(c, s)) == 0;
    }

    Square normalize(const Position &pos, Color strong, Square s)
    {
        if (fileOf(pieceSquare(pos, strong, PAWN)) >= FILE_E)
            s = flipFile(s);
        return strong == WHITE ? s : flipRank(s);
    }

    int fromStrong(const Position &pos, Color strong, int value)
    {
        return pos.sideToMove == strong ? value : -value;
    }

    constexpr unsigned KPK_SIZE = 2 * 24 * 64 * 64;
    constexpr uint8_t KPK_INVALID = 0;
    constexpr uint8_t KPK_UNKNOWN = 1;
    constexpr uint8_t KPK_DRAW = 2;
    constexpr uint8_t KPK_WIN = 4;

    std::bitset<KPK_SIZE> kpkWins;
    bool initialized = false;

    unsigned kpkIndex(Color stm, Square blackKing, Square whiteKing, Square pawn)
    {
        return static_cast<unsigned>(whiteKing) | (static_cast<unsigned>(blackKing) << 6)
               | (static_cast<unsigned>(stm) << 12) | (static_cast<unsigned>(fileOf(pawn)) << 13)
               | (static_cast<unsigned>(RANK_7 - rankOf(pawn)) << 15);
    }

    struct KpkEntry
    {
        Color stm = WHITE;
        Square king[COLOR_NB] = {SQ_A1, SQ_A1};
        Square pawn = SQ_A2;
        uint8_t result = KPK_INVALID;
    };

    KpkEntry kpkEntry(unsigned index)
    {
        KpkEntry e;
        e.king[WHITE] = static_cast<Square>(index & 63);
        e.king[BLACK] = static_cast<Square>((index >> 6) & 63);
        e.stm = static_cast<Color>((index >> 12) & 1);
        e.pawn = makeSquare(static_cast<File>((index >> 13) & 3), static_cast<Rank>(RANK_7 - ((index >> 15) & 7)));

        const Square wk = e.king[WHITE];
        const Square bk = e.king[BLACK];
        const Square push = shift(e.pawn, 8);

        if (distance(wk, bk) <= 1 || wk == e.pawn || bk == e.pawn
            || (e.stm == WHITE && (pawnAttacks(WHITE, e.pawn) & bb(bk))))
            e.result = KPK_INVALID;
        else if (e.stm == WHITE && rankOf(e.pawn) == RANK_7 && wk != push
                 && (distance(bk, push) > 1 || (kingAttacks(wk) & bb(push))))
            e.result = KPK_WIN;
        else if (e.stm == BLACK
                 && (!(kingAttacks(bk) & ~(kingAttacks(wk) | pawnAttacks(WHITE, e.pawn)))
                     || (kingAttacks(bk) & bb(e.pawn) & ~kingAttacks(wk))))
            e.result = KPK_DRAW;
        else
            e.result = KPK_UNKNOWN;
        return e;
    }

    uint8_t kpkClassify(const std::vector<KpkEntry> &db, KpkEntry &e)
    {
        const uint8_t good = e.stm == WHITE ? KPK_WIN : KPK_DRAW;
        const uint8_t bad = e.stm == WHITE ? KPK_DRAW : KPK_WIN;
        const Square wk = e.king[WHITE];
        const Square bk = e.king[BLACK];

        uint8_t r = KPK_INVALID;
        Bitboard moves = kingAttacks(e.king[e.stm]);
        while (moves)
        {
            const Square to = popLsb(moves);
            r |= e.stm == WHITE ? db[kpkIndex(BLACK, bk, to, e.pawn)].result
                                : db[kpkIndex(WHITE, to, wk, e.pawn)].result;
        }

        if (e.stm == WHITE)
        {
            if (rankOf(e.pawn) < RANK_7)
                r |= db[kpkIndex(BLACK, bk, wk, shift(e.pawn, 8))].result;
            if (rankOf(e.pawn) == RANK_2 && shift(e.pawn, 8) != wk && shift(e.pawn, 8) != bk)
                r |= db[kpkIndex(BLACK, bk, wk, shift(e.pawn, 16))].result;
        }

        e.result = (r & good) ? good : (r & KPK_UNKNOWN) ? KPK_UNKNOWN : bad;
        return e.result;
    }

    void buildKPK()
    {
        std::vector<KpkEntry> db(KPK_SIZE);
        for (unsigned i = 0; i < KPK_SIZE; ++i)
            db[i] = kpkEntry(i);

        bool repeat = true;
        while (repeat)
        {
            repeat = false;
            for (unsigned i = 0; i < KPK_SIZE; ++i)
                if (db[i].result == KPK_UNKNOWN && kpkClassify(db, db[i]) != KPK_UNKNOWN)
                    repeat = true;
        }

        kpkWins.reset();
        for (unsigned i = 0; i < KPK_SIZE; ++i)
            if (db[i].result == KPK_WIN)
                kpkWins.set(i);
    }

    bool hasLegalMove(const Position &pos)
    {
        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);
        for (int i = 0; i < list.size; ++i)
            if (movegen::is_legal(pos, list.moves[i]))
                return true;
        return false;
    }

    int evalKXK(const Position &pos, const Material &m, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        if (pos.sideToMove == weak && !hasLegalMove(pos))
            return 0;

        const Square strongKing = pos.kingSquare(strong);
        const Square weakKing = pos.kingSquare(weak);
        int result = std::min(m.npm[strong] + m.count[strong][PAWN] * PAWN_VALUE, MATERIAL_CAP)
                     + pushToEdge(weakKing) + pushClose(strongKing, weakKing);

        const Bitboard bishops = piecesOf(pos, strong, BISHOP);
        constexpr Bitboard DARK = 0xAA55AA55AA55AA55ULL;
        if (m.count[strong][QUEEN] || m.count[strong][ROOK]
            || (m.count[strong][BISHOP] && m.count[strong][KNIGHT])
            || ((bishops & DARK) && (bishops & ~DARK)))
            result += endgame::KNOWN_WIN;

        return fromStrong(pos, strong, result);
    }

    int evalKBNK(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongKing = pos.kingSquare(strong);
        const Square weakKing = pos.kingSquare(weak);
        const Square bishop = pieceSquare(pos, strong, BISHOP);
        const Square corner = oppositeColors(bishop, SQ_A1) ? flipFile(weakKing) : weakKing;
        const int result = endgame::KNOWN_WIN + 3520 + pushClose(strongKing, weakKing) + 420 * pushToCorner(corner);
        return fromStrong(pos, strong, result);
    }

    int evalKPK(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongKing = normalize(pos, strong, pos.kingSquare(strong));
        const Square strongPawn = normalize(pos, strong, pieceSquare(pos, strong, PAWN));
        const Square weakKing = normalize(pos, strong, pos.kingSquare(weak));
        const Color us = pos.sideToMove == strong ? WHITE : BLACK;

        if (!endgame::probeKPK(strongKing, strongPawn, weakKing, us))
            return 0;

        return fromStrong(pos, strong, endgame::KNOWN_WIN + PAWN_VALUE + rankOf(strongPawn));
    }

    int evalKRKP(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongKing = pos.kingSquare(strong);
        const Square weakKing = pos.kingSquare(weak);
        const Square strongRook = pieceSquare(pos, strong, ROOK);
        const Square weakPawn = pieceSquare(pos, weak, PAWN);
        const Square queening = makeSquare(fileOf(weakPawn), static_cast<Rank>(weak == WHITE ? RANK_8 : RANK_1));
        const Square pawnFront = shift(weakPawn, pawnPush(weak));

        int result;
        if (forwardFile(strong, strongKing) & bb(weakPawn))
            result = ROOK_VALUE - distance(strongKing, weakPawn);
        else if (distance(weakKing, weakPawn) >= 3 + (pos.sideToMove == weak) && distance(weakKing, strongRook) >= 3)
            result = ROOK_VALUE - distance(strongKing, weakPawn);
        else if (relativeRank(strong, weakKing) <= RANK_3 && distance(weakKing, weakPawn) == 1
                 && relativeRank(strong, strongKing) >= RANK_4
                 && distance(strongKing, weakPawn) > 2 + (pos.sideToMove == strong))
            result = 80 - 8 * distance(strongKing, weakPawn);
        else
            result = 200 - 8 * (distance(strongKing, pawnFront) - distance(weakKing, pawnFront)
                                - distance(weakPawn, queening));

        return fromStrong(pos, strong, result);
    }

    int evalKRKB(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        return fromStrong(pos, strong, pushToEdge(pos.kingSquare(weak)));
    }

    int evalKRKN(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square weakKing = pos.kingSquare(weak);
        const Square weakKnight = pieceSquare(pos, weak, KNIGHT);
        return fromStrong(pos, strong, pushToEdge(weakKing) + pushAway(weakKing, weakKnight));
    }

    int evalKQKP(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongKing = pos.kingSquare(strong);
        const Square weakKing = pos.kingSquare(weak);
        const Square weakPawn = pieceSquare(pos, weak, PAWN);
        constexpr Bitboard DRAWISH_FILES = FILE_A_BB | (FILE_A_BB << 2) | (FILE_A_BB << 5) | FILE_H_BB;

        int result = pushClose(strongKing, weakKing);
        if (relativeRank(weak, weakPawn) != RANK_7 || distance(weakKing, weakPawn) != 1
            || !(DRAWISH_FILES & bb(weakPawn)))
            result += QUEEN_VALUE - PAWN_VALUE;

        return fromStrong(pos, strong, result);
    }

    int evalKQKR(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongKing = pos.kingSquare(strong);
        const Square weakKing = pos.kingSquare(weak);
        const int result = QUEEN_VALUE - ROOK_VALUE + pushToEdge(weakKing) + pushClose(strongKing, weakKing);
        return fromStrong(pos, strong, result);
    }

    int evalKNNKP(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square weakKing = pos.kingSquare(weak);
        const Square weakPawn = pieceSquare(pos, weak, PAWN);
        const int result = PAWN_VALUE + 2 * pushToEdge(weakKing) - 10 * relativeRank(weak, weakPawn);
        return fromStrong(pos, strong, result);
    }

    bool evaluateFor(const Position &pos, const Material &m, Color strong, int &value)
    {
        const Color weak = static_cast<Color>(strong ^ 1);

        if (bare(m, weak))
        {
            if (has(m, strong, 0, 1, 1, 0, 0))
            {
                value = evalKBNK(pos, strong);
                return true;
            }
            if (has(m, strong, 0, 2, 0, 0, 0))
            {
                value = 0;
                return true;
            }
            if (m.npm[strong] >= ROOK_VALUE)
            {
                value = evalKXK(pos, m, strong);
                return true;
            }
            if (has(m, strong, 1, 0, 0, 0, 0))
            {
                value = evalKPK(pos, strong);
                return true;
            }
            return false;
        }

        if (has(m, strong, 0, 0, 0, 1, 0))
        {
            if (has(m, weak, 1, 0, 0, 0, 0))
                value = evalKRKP(pos, strong);
            else if (has(m, weak, 0, 0, 1, 0, 0))
                value = evalKRKB(pos, strong);
            else if (has(m, weak, 0, 1, 0, 0, 0))
                value = evalKRKN(pos, strong);
            else
                return false;
            return true;
        }

        if (has(m, strong, 0, 0, 0, 0, 1))
        {
            if (has(m, weak, 1, 0, 0, 0, 0))
                value = evalKQKP(pos, strong);
            else if (has(m, weak, 0, 0, 0, 1, 0))
                value = evalKQKR(pos, strong);
            else
                return false;
            return true;
        }

        if (has(m, strong, 0, 2, 0, 0, 0) && has(m, weak, 1, 0, 0, 0, 0))
        {
            value = evalKNNKP(pos, strong);
            return true;
        }

        return false;
    }

    int scaleKBPsK(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Bitboard strongPawns = piecesOf(pos, strong, PAWN);
        const Bitboard allPawns = pos.byType[PAWN];
        const Square strongBishop = pieceSquare(pos, strong, BISHOP);
        const Square weakKing = pos.kingSquare(weak);
        const Square strongKing = pos.kingSquare(strong);

        if (!(strongPawns & ~FILE_A_BB) || !(strongPawns & ~FILE_H_BB))
        {
            const Square queening = relativeSquare(strong, makeSquare(fileOf(lsbSq(strongPawns)), RANK_8));
            if (oppositeColors(queening, strongBishop) && distance(queening, weakKing) <= 1)
                return 0;
        }

        const Bitboard weakPawns = piecesOf(pos, weak, PAWN);
        if ((!(allPawns & ~fileBB(FILE_B)) || !(allPawns & ~fileBB(FILE_G)))
            && pos.byColor[weak] == (piecesOf(pos, weak, KING) | weakPawns) && weakPawns)
        {
            const Square weakPawn = strong == WHITE ? msbSq(weakPawns) : lsbSq(weakPawns);
            if (relativeRank(strong, weakPawn) == RANK_7
                && (strongPawns & bb(shift(weakPawn, pawnPush(weak))))
                && (oppositeColors(strongBishop, weakPawn) || !moreThanOne(strongPawns)))
            {
                const int strongKingDistance = distance(weakPawn, strongKing);
                const int weakKingDistance = distance(weakPawn, weakKing);
                if (relativeRank(strong, weakKing) >= RANK_7 && weakKingDistance <= 2
                    && weakKingDistance <= strongKingDistance)
                    return 0;
            }
        }

        return SCALE_NONE;
    }

    int scaleKQKRPs(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongKing = pos.kingSquare(strong);
        const Square weakKing = pos.kingSquare(weak);
        const Square weakRook = pieceSquare(pos, weak, ROOK);

        if (relativeRank(weak, weakKing) <= RANK_2 && relativeRank(weak, strongKing) >= RANK_4
            && relativeRank(weak, weakRook) == RANK_3
            && (piecesOf(pos, weak, PAWN) & kingAttacks(weakKing) & pawnAttacks(strong, weakRook)))
            return 0;

        return SCALE_NONE;
    }

    int scaleKRPKR(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongKing = normalize(pos, strong, pos.kingSquare(strong));
        const Square weakKing = normalize(pos, strong, pos.kingSquare(weak));
        const Square strongRook = normalize(pos, strong, pieceSquare(pos, strong, ROOK));
        const Square strongPawn = normalize(pos, strong, pieceSquare(pos, strong, PAWN));
        const Square weakRook = normalize(pos, strong, pieceSquare(pos, weak, ROOK));

        const int pawnFile = fileOf(strongPawn);
        const int pawnRank = rankOf(strongPawn);
        const Square queening = makeSquare(static_cast<File>(pawnFile), RANK_8);
        const Square pawnFront = shift(strongPawn, 8);
        const int tempo = pos.sideToMove == strong;

        if (pawnRank <= RANK_5 && distance(weakKing, queening) <= 1 && strongKing <= SQ_H5
            && (rankOf(weakRook) == RANK_6 || (pawnRank <= RANK_3 && rankOf(strongRook) != RANK_6)))
            return 0;

        if (pawnRank == RANK_6 && distance(weakKing, queening) <= 1 && rankOf(strongKing) + tempo <= RANK_6
            && (rankOf(weakRook) == RANK_1 || (!tempo && fileDistance(weakRook, strongPawn) >= 3)))
            return 0;

        if (pawnRank >= RANK_6 && weakKing == queening && rankOf(weakRook) == RANK_1
            && (!tempo || distance(strongKing, strongPawn) >= 2))
            return 0;

        if (strongPawn == SQ_A7 && strongRook == SQ_A8 && (weakKing == SQ_H7 || weakKing == SQ_G7)
            && fileOf(weakRook) == FILE_A
            && (rankOf(weakRook) <= RANK_3 || fileOf(strongKing) >= FILE_D || rankOf(strongKing) <= RANK_5))
            return 0;

        if (pawnRank <= RANK_5 && weakKing == pawnFront && distance(strongKing, strongPawn) - tempo >= 2
            && distance(strongKing, weakRook) - tempo >= 2)
            return 0;

        if (pawnRank == RANK_7 && pawnFile != FILE_A && fileOf(strongRook) == pawnFile && strongRook != queening
            && distance(strongKing, queening) < distance(weakKing, queening) - 2 + tempo
            && distance(strongKing, queening) < distance(weakKing, strongRook) + tempo)
            return SCALE_MAX - 2 * distance(strongKing, queening);

        if (pawnFile != FILE_A && fileOf(strongRook) == pawnFile && strongRook < strongPawn
            && distance(strongKing, queening) < distance(weakKing, queening) - 2 + tempo
            && distance(strongKing, pawnFront) < distance(weakKing, pawnFront) - 2 + tempo
            && (distance(weakKing, strongRook) + tempo >= 3
                || (distance(strongKing, queening) < distance(weakKing, strongRook) + tempo
                    && distance(strongKing, pawnFront) < distance(weakKing, strongPawn) + tempo)))
            return SCALE_MAX - 8 * distance(strongPawn, queening) - 2 * distance(strongKing, queening);

        if (pawnRank <= RANK_4 && weakKing > strongPawn)
        {
            if (fileOf(weakKing) == fileOf(strongPawn))
                return 10;
            if (fileDistance(weakKing, strongPawn) == 1 && distance(strongKing, weakKing) > 2)
                return 24 - 2 * distance(strongKing, weakKing);
        }

        return SCALE_NONE;
    }

    int scaleKRPKB(const Position &pos, Color strong)
    {
        if (!(pos.byType[PAWN] & (FILE_A_BB | FILE_H_BB)))
            return SCALE_NONE;

        const Color weak = static_cast<Color>(strong ^ 1);
        const Square weakKing = pos.kingSquare(weak);
        const Square weakBishop = pieceSquare(pos, weak, BISHOP);
        const Square strongKing = pos.kingSquare(strong);
        const Square strongPawn = pieceSquare(pos, strong, PAWN);
        const int pawnRank = relativeRank(strong, strongPawn);
        const int push = pawnPush(strong);

        if (pawnRank == RANK_5 && !oppositeColors(weakBishop, strongPawn))
        {
            const int d = distance(shift(strongPawn, 3 * push), weakKing);
            if (d <= 2 && !(d == 0 && weakKing == shift(strongKing, 2 * push)))
                return 24;
            return 48;
        }

        if (pawnRank == RANK_6 && distance(shift(strongPawn, 2 * push), weakKing) <= 1
            && (movegen::attacks(BISHOP, weakBishop, 0) & bb(shift(strongPawn, push)))
            && fileDistance(weakBishop, strongPawn) >= 2)
            return 8;

        return SCALE_NONE;
    }

    int scaleKRPPKRP(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Bitboard strongPawns = piecesOf(pos, strong, PAWN);
        const Square pawn1 = lsbSq(strongPawns);
        const Square pawn2 = msbSq(strongPawns);
        const Square weakKing = pos.kingSquare(weak);

        if (passedPawn(pos, strong, pawn1) || passedPawn(pos, strong, pawn2))
            return SCALE_NONE;

        const int pawnRank = std::max(relativeRank(strong, pawn1), relativeRank(strong, pawn2));
        if (fileDistance(weakKing, pawn1) <= 1 && fileDistance(weakKing, pawn2) <= 1
            && relativeRank(strong, weakKing) > pawnRank)
            return 7 * pawnRank;

        return SCALE_NONE;
    }

    int scaleKPsK(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square weakKing = pos.kingSquare(weak);
        const Bitboard strongPawns = piecesOf(pos, strong, PAWN);

        if ((!(strongPawns & ~FILE_A_BB) || !(strongPawns & ~FILE_H_BB))
            && !(strongPawns & ~passedSpan(weak, weakKing)))
            return 0;

        return SCALE_NONE;
    }

    int scaleKBPKB(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongPawn = pieceSquare(pos, strong, PAWN);
        const Square strongBishop = pieceSquare(pos, strong, BISHOP);
        const Square weakBishop = pieceSquare(pos, weak, BISHOP);
        const Square weakKing = pos.kingSquare(weak);

        if ((forwardFile(strong, strongPawn) & bb(weakKing))
            && (oppositeColors(weakKing, strongBishop) || relativeRank(strong, weakKing) <= RANK_6))
            return 0;

        if (oppositeColors(strongBishop, weakBishop))
            return 0;

        return SCALE_NONE;
    }

    int scaleKBPPKB(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongBishop = pieceSquare(pos, strong, BISHOP);
        const Square weakBishop = pieceSquare(pos, weak, BISHOP);
        if (!oppositeColors(strongBishop, weakBishop))
            return SCALE_NONE;

        const Square weakKing = pos.kingSquare(weak);
        const Bitboard strongPawns = piecesOf(pos, strong, PAWN);
        const Square pawn1 = lsbSq(strongPawns);
        const Square pawn2 = msbSq(strongPawns);

        Square block1;
        Square block2;
        if (relativeRank(strong, pawn1) > relativeRank(strong, pawn2))
        {
            block1 = shift(pawn1, pawnPush(strong));
            block2 = makeSquare(fileOf(pawn2), rankOf(pawn1));
        }
        else
        {
            block1 = shift(pawn2, pawnPush(strong));
            block2 = makeSquare(fileOf(pawn1), rankOf(pawn2));
        }

        const Bitboard weakBishops = piecesOf(pos, weak, BISHOP);
        switch (fileDistance(pawn1, pawn2))
        {
        case 0:
            if (fileOf(weakKing) == fileOf(block1) && relativeRank(strong, weakKing) >= relativeRank(strong, block1)
                && oppositeColors(weakKing, strongBishop))
                return 0;
            return SCALE_NONE;
        case 1:
            if (weakKing == block1 && oppositeColors(weakKing, strongBishop)
                && (weakBishop == block2 || (movegen::attacks(BISHOP, block2, occupied(pos)) & weakBishops)
                    || rankDistance(pawn1, pawn2) >= 2))
                return 0;
            if (weakKing == block2 && oppositeColors(weakKing, strongBishop)
                && (weakBishop == block1 || (movegen::attacks(BISHOP, block1, occupied(pos)) & weakBishops)))
                return 0;
            return SCALE_NONE;
        default:
            return SCALE_NONE;
        }
    }

    int scaleKBPKN(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongPawn = pieceSquare(pos, strong, PAWN);
        const Square strongBishop = pieceSquare(pos, strong, BISHOP);
        const Square weakKing = pos.kingSquare(weak);

        if (fileOf(weakKing) == fileOf(strongPawn) && relativeRank(strong, strongPawn) < relativeRank(strong, weakKing)
            && (oppositeColors(weakKing, strongBishop) || relativeRank(strong, weakKing) <= RANK_6))
            return 0;

        return SCALE_NONE;
    }

    int scaleKNPK(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongPawn = normalize(pos, strong, pieceSquare(pos, strong, PAWN));
        const Square weakKing = normalize(pos, strong, pos.kingSquare(weak));

        if (strongPawn == SQ_A7 && distance(SQ_A8, weakKing) <= 1)
            return 0;

        return SCALE_NONE;
    }

    int scaleKNPKB(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongPawn = pieceSquare(pos, strong, PAWN);
        const Square weakBishop = pieceSquare(pos, weak, BISHOP);
        const Square weakKing = pos.kingSquare(weak);

        if (forwardFile(strong, strongPawn) & movegen::attacks(BISHOP, weakBishop, occupied(pos)))
            return distance(weakKing, strongPawn);

        return SCALE_NONE;
    }

    int scaleKPKP(const Position &pos, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const Square strongKing = normalize(pos, strong, pos.kingSquare(strong));
        const Square weakKing = normalize(pos, strong, pos.kingSquare(weak));
        const Square strongPawn = normalize(pos, strong, pieceSquare(pos, strong, PAWN));
        const Color us = pos.sideToMove == strong ? WHITE : BLACK;

        if (rankOf(strongPawn) >= RANK_5 && fileOf(strongPawn) != FILE_A)
            return SCALE_NONE;

        return endgame::probeKPK(strongKing, strongPawn, weakKing, us) ? SCALE_NONE : 0;
    }

    int scaleFor(const Position &pos, const Material &m, Color strong)
    {
        const Color weak = static_cast<Color>(strong ^ 1);
        const int strongPawns = m.count[strong][PAWN];
        const int weakPawns = m.count[weak][PAWN];

        if (m.pieces[strong] == 1 && m.count[strong][BISHOP] == 1 && strongPawns >= 1)
        {
            const int sf = scaleKBPsK(pos, strong);
            if (sf != SCALE_NONE)
                return sf;
        }

        int sf = SCALE_NONE;
        if (has(m, strong, 0, 0, 0, 0, 1) && m.pieces[weak] == 1 && m.count[weak][ROOK] == 1 && weakPawns >= 1)
            sf = scaleKQKRPs(pos, strong);
        else if (has(m, strong, 1, 0, 0, 1, 0) && has(m, weak, 0, 0, 0, 1, 0))
            sf = scaleKRPKR(pos, strong);
        else if (has(m, strong, 1, 0, 0, 1, 0) && has(m, weak, 0, 0, 1, 0, 0))
            sf = scaleKRPKB(pos, strong);
        else if (has(m, strong, 2, 0, 0, 1, 0) && has(m, weak, 1, 0, 0, 1, 0))
            sf = scaleKRPPKRP(pos, strong);
        else if (has(m, strong, 1, 0, 1, 0, 0) && has(m, weak, 0, 0, 1, 0, 0))
            sf = scaleKBPKB(pos, strong);
        else if (has(m, strong, 2, 0, 1, 0, 0) && has(m, weak, 0, 0, 1, 0, 0))
            sf = scaleKBPPKB(pos, strong);
        else if (has(m, strong, 1, 0, 1, 0, 0) && has(m, weak, 0, 1, 0, 0, 0))
            sf = scaleKBPKN(pos, strong);
        else if (has(m, strong, 1, 1, 0, 0, 0) && bare(m, weak))
            sf = scaleKNPK(pos, strong);
        else if (has(m, strong, 1, 1, 0, 0, 0) && has(m, weak, 0, 0, 1, 0, 0))
            sf = scaleKNPKB(pos, strong);
        else if (m.pieces[strong] == 0 && strongPawns >= 2 && bare(m, weak))
            sf = scaleKPsK(pos, strong);
        else if (has(m, strong, 1, 0, 0, 0, 0) && has(m, weak, 1, 0, 0, 0, 0))
            sf = scaleKPKP(pos, strong);

        if (sf != SCALE_NONE)
            return sf;

        if (strongPawns == 0 && m.npm[strong] - m.npm[weak] <= BISHOP_VALUE)
            return m.npm[strong] < ROOK_VALUE ? 0 : m.npm[weak] <= BISHOP_VALUE ? 4 : 14;

        return endgame::SCALE_NORMAL;
    }
}

void endgame::init()
{
    if (initialized)
        return;
    buildKPK();
    initialized = true;
}

bool endgame::probeKPK(Square strongKing, Square strongPawn, Square weakKing, Color stm)
{
    return kpkWins[kpkIndex(stm, weakKing, strongKing, strongPawn)];
}

bool endgame::evaluate(const Position &pos, int &value)
{
    const Bitboard pieces = pos.byType[KNIGHT] | pos.byType[BISHOP] | pos.byType[ROOK] | pos.byType[QUEEN];
    const bool whiteBare = (pos.byColor[WHITE] & ~pos.byType[KING]) == 0;
    const bool blackBare = (pos.byColor[BLACK] & ~pos.byType[KING]) == 0;
    if (!whiteBare && !blackBare && std::popcount(pieces | pos.byType[PAWN]) > 3)
        return false;

    const Material m = materialOf(pos);
    return evaluateFor(pos, m, WHITE, value) || evaluateFor(pos, m, BLACK, value);
}

int endgame::scale(const Position &pos, Color strong)
{
    const Bitboard pieces = pos.byType[KNIGHT] | pos.byType[BISHOP] | pos.byType[ROOK] | pos.byType[QUEEN];
    if (std::popcount(pieces) > MAX_SCALED_PIECES)
        return SCALE_NORMAL;

    return std::clamp(scaleFor(pos, materialOf(pos), strong), 0, SCALE_NORMAL);
}
