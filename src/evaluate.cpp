#include "evaluate.h"

#include "movegen.h"
#include "position.h"
#include "tuned_params.h"

#include <algorithm>
#include <array>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
    // --- Bit utilities and geometry ---
    constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
    constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;
    constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;
    constexpr Bitboard FILE_B_BB = FILE_A_BB << 1;
    constexpr Bitboard FILE_C_BB = FILE_A_BB << 2;
    constexpr Bitboard FILE_D_BB = FILE_A_BB << 3;
    constexpr Bitboard FILE_E_BB = FILE_A_BB << 4;
    constexpr Bitboard FILE_F_BB = FILE_A_BB << 5;
    constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;
    constexpr Bitboard NOT_FILE_A = ~FILE_A_BB;
    constexpr Bitboard NOT_FILE_H = ~FILE_H_BB;
    constexpr Bitboard QSIDE_BB = FILE_A_BB | FILE_B_BB | FILE_C_BB | FILE_D_BB;
    constexpr Bitboard KSIDE_BB = QSIDE_BB << 4;
    constexpr Bitboard CENTER_FILES_BB = FILE_C_BB | FILE_D_BB | FILE_E_BB | FILE_F_BB;
    constexpr Bitboard HALF_BB[COLOR_NB] = {0x00000000FFFFFFFFULL, 0xFFFFFFFF00000000ULL};

    // King safety zones, indexed by the colour that owns the king.
    constexpr Bitboard KING_ZONE_DEFENDER[COLOR_NB] = {
        HALF_BB[WHITE] | (RANK_1_BB << 32),
        (RANK_1_BB << 24) | HALF_BB[BLACK],
    };
    constexpr Bitboard KING_ZONE_FLANK[8] = {
        QSIDE_BB, QSIDE_BB, QSIDE_BB, CENTER_FILES_BB, CENTER_FILES_BB, KSIDE_BB, KSIDE_BB, KSIDE_BB};
    constexpr Bitboard KING_DEFENSE_ZONE[8] = {
        QSIDE_BB ^ FILE_D_BB, QSIDE_BB ^ FILE_D_BB, QSIDE_BB ^ FILE_D_BB, FILE_D_BB | FILE_E_BB,
        FILE_D_BB | FILE_E_BB, KSIDE_BB ^ FILE_E_BB, KSIDE_BB ^ FILE_E_BB, KSIDE_BB ^ FILE_E_BB};
    constexpr Bitboard QSIDE_DIAG_REGION[2] = {
        FILE_F_BB | ((FILE_E_BB | FILE_D_BB) & 0x0000FFFFFFFF0000ULL),
        FILE_F_BB | FILE_E_BB | FILE_D_BB};
    constexpr Bitboard KSIDE_DIAG_REGION[2] = {
        FILE_C_BB | ((FILE_D_BB | FILE_E_BB) & 0x0000FFFFFFFF0000ULL),
        FILE_C_BB | FILE_D_BB | FILE_E_BB};

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

    // Per-piece attack sets, plus the unions the king-safety term needs. Laser
    // iterates pieces (not piece types) so that two pieces of the same type both
    // count towards `kingAttackPieces` and `kingAttackPts`.
    constexpr int MAX_ATTACK_PIECES = 20;

    struct AttackInfo
    {
        struct Entry
        {
            PieceType type;
            Bitboard attacks;
        };

        Bitboard byType[COLOR_NB][PIECE_TYPE_NB] = {};
        Bitboard full[COLOR_NB] = {};
        Bitboard doubled[COLOR_NB] = {};
        Entry pieces[COLOR_NB][MAX_ATTACK_PIECES] = {};
        int count[COLOR_NB] = {};

        void addPieces(Color c, PieceType pt, Bitboard att)
        {
            byType[c][pt] |= att;
            full[c] |= att;
            if (count[c] < MAX_ATTACK_PIECES)
                pieces[c][count[c]++] = {pt, att};
        }
    };

    Bitboard pawnAttackMap(Color c, Bitboard pawns)
    {
        return (c == WHITE) ? (((pawns << 7) & NOT_FILE_H) | ((pawns << 9) & NOT_FILE_A))
                            : (((pawns >> 7) & NOT_FILE_A) | ((pawns >> 9) & NOT_FILE_H));
    }

    void mobilityScore(const Position &pos, Color c, Bitboard occ, int &mg, int &eg, AttackInfo &ai)
    {
        const Bitboard notOwn = ~pos.byColor[c];
        Bitboard seen = ai.byType[c][PAWN];

        for (int i = 0; i < 4; ++i)
        {
            int count = 0;
            Bitboard b = pos.byColor[c] & pos.byType[mobilityTypes[i]];
            while (b)
            {
                const Square sq = popLsb(b);
                const Bitboard att = movegen::attacks(mobilityTypes[i], sq, occ) & notOwn;
                ai.doubled[c] |= att & seen;
                seen |= att;
                ai.addPieces(c, mobilityTypes[i], att);
                count += popCount(att);
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

    void imbalanceScore(const int counts[COLOR_NB][PIECE_TYPE_NB], Color us, int &mg, int &eg)
    {
        const Color them = static_cast<Color>(us ^ 1);
        const int *ownCounts = counts[us];
        const int *theirCounts = counts[them];

        for (int own = KNIGHT; own <= QUEEN; ++own)
        {
            const int ownCount = ownCounts[own];
            if (ownCount == 0)
                continue;

            for (int opp = PAWN; opp < own; ++opp)
            {
                const int theirCount = theirCounts[opp];
                if (theirCount == 0)
                    continue;

                const int pairs = ownCount * theirCount;
                mg += pairs * tuned::MG_IMBALANCE[own][opp];
                eg += pairs * tuned::EG_IMBALANCE[own][opp];
            }
        }
    }

    int relativeRank(Color c, int rank)
    {
        return rank ^ (7 * c);
    }

    Square lsbSquare(Bitboard b)
    {
        return static_cast<Square>(countrZero(b));
    }

    Square msbSquare(Bitboard b)
    {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<Square>(63 - __builtin_clzll(b));
#elif defined(_MSC_VER)
        unsigned long index = 0;
        _BitScanReverse64(&index, b);
        return static_cast<Square>(index);
#else
        Square s = SQ_A1;
        while (b >>= 1)
            ++s;
        return s;
#endif
    }

    void kingPawnScore(const Position &pos, Color c, int &ks)
    {
        const Color them = static_cast<Color>(c ^ 1);
        const Bitboard ownPawns = pos.byColor[c] & pos.byType[PAWN];
        const Bitboard enemyPawns = pos.byColor[them] & pos.byType[PAWN];
        const Square kingSq = pos.kingSquare(c);
        const int kingFile = fileOf(kingSq);
        const int kingRank = rankOf(kingSq);
        const int fileRange = std::min(6, std::max(1, kingFile));

        for (int i = fileRange - 1; i <= fileRange + 1; ++i)
        {
            const int f = std::min(i, 7 - i);
            const Bitboard ownOnFile = ownPawns & fileMask(i);

            if (ownOnFile)
            {
                const Square pawnSq = (c == WHITE) ? lsbSquare(ownOnFile) : msbSquare(ownOnFile);
                ks += tuned::PAWN_SHIELD_VALUE[f][relativeRank(c, rankOf(pawnSq))];
            }
            else
                ks += tuned::PAWN_SHIELD_VALUE[f][0];

            const Bitboard enemyOnFile = enemyPawns & fileMask(i);
            if (!enemyOnFile)
            {
                ks -= tuned::PAWN_STORM_VALUE[0][f][0];
                continue;
            }

            const Square pawnSq = (c == WHITE) ? lsbSquare(enemyOnFile) : msbSquare(enemyOnFile);
            const int r = relativeRank(c, rankOf(pawnSq));
            const Square stopSq = static_cast<Square>(pawnSq + (c == WHITE ? -8 : 8));
            const int state = (ownOnFile == 0) ? 0 : ((ownOnFile & (1ULL << stopSq)) ? 1 : 2);
            ks -= tuned::PAWN_STORM_VALUE[state][f][r];

            if (f == 0 && (kingFile == 0 || kingFile == 7) && (r == 1 || r == 2)
                && relativeRank(c, kingRank) + 1 == r)
                ks -= tuned::PAWN_STORM_SHIELDING_KING;
        }
    }

    int attackerBishopFactor(const AttackInfo &ai, Color attacker, Bitboard zone, Bitboard diagonal)
    {
        const int c = popCount(diagonal);
        const int supported = (zone & ai.byType[attacker][BISHOP]) ? 1 : 0;
        return tuned::KS_BISHOP_PRESSURE * (c * (c + 1) / 2 + supported - 1);
    }

    int defenderBishopFactor(Bitboard diagonal)
    {
        const int c = popCount(diagonal);
        return tuned::KS_BISHOP_PRESSURE * (c * (c + 1) / 2 - 1);
    }

    // Danger `attacker`'s pieces pose around `defender`'s king.
    int kingAttackScore(const Position &pos, const AttackInfo &ai, Bitboard occ,
                        const int counts[COLOR_NB][PIECE_TYPE_NB], Color attacker, int pawnScore)
    {
        const Color defender = static_cast<Color>(attacker ^ 1);
        const Square defKingSq = pos.kingSquare(defender);
        const int kingFile = fileOf(defKingSq);
        const Bitboard kingSqs = movegen::attacks(KING, defKingSq, occ);

        Bitboard neighborhood = kingSqs;
        if (attacker == WHITE)
        {
            if (pos.byColor[BLACK] & pos.byType[KING] & RANK_8_BB)
                neighborhood |= neighborhood >> 8;
        }
        else if (pos.byColor[WHITE] & pos.byType[KING] & RANK_1_BB)
            neighborhood |= neighborhood << 8;

        if (kingFile == 7)
            neighborhood |= neighborhood >> 1;
        else if (kingFile == 0)
            neighborhood |= neighborhood << 1;

        const Bitboard weakMap = ~ai.doubled[defender]
            & ((~ai.byType[defender][PAWN] & ~ai.full[defender]) | ai.byType[defender][QUEEN]);
        const Bitboard kingDefenseless = kingSqs & weakMap;

        const Bitboard checkKnights = movegen::attacks(KNIGHT, defKingSq, occ);
        const Bitboard checkBishops = movegen::attacks(BISHOP, defKingSq, occ);
        const Bitboard checkRooks = movegen::attacks(ROOK, defKingSq, occ);
        const Bitboard checkQueens = movegen::attacks(QUEEN, defKingSq, occ);

        int pts = tuned::KS_BASE;
        int attackPts = 0;
        int attackPieces = popCount(ai.byType[attacker][PAWN] & neighborhood);

        for (int k = 0; k < ai.count[attacker]; ++k)
        {
            const AttackInfo::Entry &entry = ai.pieces[attacker][k];
            if (!(entry.attacks & neighborhood))
                continue;

            const int idx = static_cast<int>(entry.type) - 1;
            ++attackPieces;
            attackPts += tuned::KING_THREAT_MULTIPLIER[idx];
            pts += tuned::KING_THREAT_SQUARE[idx] * popCount(entry.attacks & kingSqs);
            pts += tuned::KING_DEFENSELESS_SQUARE * popCount(entry.attacks & kingDefenseless);
        }

        pts += tuned::SAFE_CHECK_BONUS[0]
            * ((ai.byType[attacker][KNIGHT] & checkKnights & ~kingSqs & weakMap) ? 1 : 0);
        pts += tuned::SAFE_CHECK_BONUS[1]
            * ((ai.byType[attacker][BISHOP] & checkBishops & ~kingSqs & weakMap) ? 1 : 0);
        pts += tuned::SAFE_CHECK_BONUS[2]
            * ((ai.byType[attacker][ROOK] & checkRooks & ~kingSqs & weakMap) ? 1 : 0);
        pts += tuned::SAFE_CHECK_BONUS[3]
            * ((ai.byType[attacker][QUEEN] & checkQueens & ~kingSqs & weakMap
                & ~ai.byType[defender][QUEEN]) ? 1 : 0);

        pts += attackPieces * attackPts;

        const Bitboard kingZone = KING_ZONE_DEFENDER[defender] & KING_ZONE_FLANK[kingFile];
        const int kingPressure = tuned::KING_PRESSURE
            * (popCount(ai.full[attacker] & kingZone)
               + popCount(ai.doubled[attacker] & ~ai.byType[defender][PAWN] & kingZone));

        pts += (-tuned::KS_PAWN_FACTOR * pawnScore + tuned::KS_KING_PRESSURE_FACTOR * kingPressure) / 32;

        const Bitboard kingDefenseZone = KING_DEFENSE_ZONE[kingFile] & HALF_BB[defender];
        pts += tuned::KS_NO_KNIGHT_DEFENDER
            * (((kingDefenseZone & ai.byType[defender][KNIGHT]) ? 0 : 1)
               + ((kingZone & ai.byType[defender][KNIGHT]) ? 0 : 1))
            * counts[defender][KNIGHT];
        pts += tuned::KS_NO_BISHOP_DEFENDER
            * (((kingDefenseZone & ai.byType[defender][BISHOP]) ? 0 : 1)
               + ((kingZone & ai.byType[defender][BISHOP]) ? 0 : 1))
            * counts[defender][BISHOP];

        const Bitboard attackerPawns = pos.byColor[attacker] & pos.byType[PAWN];
        const Bitboard defenderPawns = pos.byColor[defender] & pos.byType[PAWN];
        if (kingFile < 3)
        {
            const Bitboard atk = (attacker == WHITE) ? (((attackerPawns & QSIDE_DIAG_REGION[0]) << 7) & attackerPawns)
                                                     : (((attackerPawns & QSIDE_DIAG_REGION[0]) >> 9) & attackerPawns);
            const Bitboard def = (attacker == WHITE) ? (((defenderPawns & KSIDE_DIAG_REGION[1]) >> 7) & defenderPawns)
                                                     : (((defenderPawns & KSIDE_DIAG_REGION[1]) << 9) & defenderPawns);
            if (atk)
                pts += attackerBishopFactor(ai, attacker, kingDefenseZone, atk);
            if (def)
                pts += defenderBishopFactor(def);
        }
        else if (kingFile > 4)
        {
            const Bitboard atk = (attacker == WHITE) ? (((attackerPawns & KSIDE_DIAG_REGION[0]) << 9) & attackerPawns)
                                                     : (((attackerPawns & KSIDE_DIAG_REGION[0]) >> 7) & attackerPawns);
            const Bitboard def = (attacker == WHITE) ? (((defenderPawns & QSIDE_DIAG_REGION[1]) >> 9) & defenderPawns)
                                                     : (((defenderPawns & QSIDE_DIAG_REGION[1]) << 7) & defenderPawns);
            if (atk)
                pts += attackerBishopFactor(ai, attacker, kingDefenseZone, atk);
            if (def)
                pts += defenderBishopFactor(def);
        }

        if (counts[attacker][QUEEN] == 0)
            pts += tuned::KS_NO_QUEEN;

        if (pts < 0)
            pts = 0;

        const int quadratic = pts * pts / tuned::KS_ARRAY_FACTOR;
        return (quadratic > tuned::KS_MAX ? tuned::KS_MAX : quadratic) + kingPressure;
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

    // Piece counts, consumed only by the material-imbalance term.
    int counts[COLOR_NB][PIECE_TYPE_NB] = {};
    for (int c = WHITE; c <= BLACK; ++c)
        for (int pt = PAWN; pt <= QUEEN; ++pt)
            counts[c][pt] = popCount(pos.byColor[c] & pos.byType[pt]);

    AttackInfo ai;
    for (int c = WHITE; c <= BLACK; ++c)
        ai.byType[c][PAWN] = pawnAttackMap(static_cast<Color>(c), pos.byColor[c] & pos.byType[PAWN]);

    for (int c = WHITE; c <= BLACK; ++c)
    {
        const Color color = static_cast<Color>(c);
        mobilityScore(pos, color, occ, mg[c], eg[c], ai);
        outpostScore(pos, color, mg[c], eg[c]);
        pawnStructureScore(pos, color, mg[c], eg[c]);
        imbalanceScore(counts, color, mg[c], eg[c]);
    }

    for (int c = WHITE; c <= BLACK; ++c)
    {
        const Color color = static_cast<Color>(c);
        const uint8_t rights = static_cast<uint8_t>(color == WHITE ? (WHITE_OO | WHITE_OOO) : (BLACK_OO | BLACK_OOO));
        int ks = 0;
        kingPawnScore(pos, color, ks);
        ks -= kingAttackScore(pos, ai, occ, counts, static_cast<Color>(c ^ 1), ks);
        ks += tuned::CASTLING_RIGHTS_VALUE[popCount(static_cast<Bitboard>(pos.castlingRights & rights))];
        ks = ks * tuned::KS_SCALE / 100;
        mg[c] += ks;
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