#include "position.h"
#include "movegen.h"

#include <iostream>
#include <sstream>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
    constexpr const char *START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Piece charToPiece(char c)
    {
        switch (c)
        {
        case 'P':
            return W_PAWN;
        case 'N':
            return W_KNIGHT;
        case 'B':
            return W_BISHOP;
        case 'R':
            return W_ROOK;
        case 'Q':
            return W_QUEEN;
        case 'K':
            return W_KING;
        case 'p':
            return B_PAWN;
        case 'n':
            return B_KNIGHT;
        case 'b':
            return B_BISHOP;
        case 'r':
            return B_ROOK;
        case 'q':
            return B_QUEEN;
        case 'k':
            return B_KING;
        default:
            return NO_PIECE;
        }
    }

    char pieceToChar(Piece p)
    {
        static constexpr char chars[PIECE_NB] = {
            ' ', 'P', 'N', 'B', 'R', 'Q', 'K',
            ' ', ' ',
            'p', 'n', 'b', 'r', 'q', 'k', ' '};
        return (p >= NO_PIECE && p < PIECE_NB) ? chars[p] : ' ';
    }
}

namespace
{
    // --- Zobrist hashing ---
    uint64_t rngState = 0x9E3779B97F4A7C15ULL;

    uint64_t rand64()
    {
        rngState += 0x9E3779B97F4A7C15ULL;
        uint64_t z = rngState;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    std::array<std::array<uint64_t, SQUARE_NB>, PIECE_NB> pieceKeys{};
    uint64_t sideKey = 0;
    std::array<uint64_t, 16> castlingKeys{};
    std::array<uint64_t, FILE_NB> enPassantKeys{};
    bool zobristReady = false;

    void initZobrist()
    {
        if (zobristReady)
            return;
        for (int p = 0; p < PIECE_NB; ++p)
            for (int s = 0; s < SQUARE_NB; ++s)
                pieceKeys[p][s] = rand64();
        sideKey = rand64();
        for (auto &k : castlingKeys)
            k = rand64();
        for (auto &k : enPassantKeys)
            k = rand64();
        zobristReady = true;
    }

    uint64_t epKey(Square s)
    {
        return s == SQ_NONE ? 0 : enPassantKeys[s & 7];
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

    int countLeadingZeros(Bitboard b)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_clzll(b);
#elif defined(_MSC_VER)
        unsigned long index = 0;
        _BitScanReverse64(&index, b);
        return 63 - static_cast<int>(index);
#else
        int n = 0;
        while ((b & 0x8000000000000000ULL) == 0)
        {
            b <<= 1;
            ++n;
        }
        return n;
#endif
    }

    Square msb(Bitboard b)
    {
        return static_cast<Square>(63 - countLeadingZeros(b));
    }

    Bitboard backRankOf(Color c)
    {
        return 0xFFULL << (8 * static_cast<int>(c == WHITE ? RANK_1 : RANK_8));
    }
}

Position::Position()
{
    set_fen(START_FEN);
}

void Position::clear()
{
    byColor.fill(0);
    byType.fill(0);
    byPiece.fill(0);
    board.fill(NO_PIECE);
    sideToMove = WHITE;
    castlingRights = 0;
    enPassantSquare = SQ_NONE;
    halfmoveClock = 0;
    fullmoveNumber = 1;
    zobristKey = 0;
    undoCount = 0;
    historyCount = 0;
    historyStart = 0;

    castlingRookSquare[WHITE][KING_SIDE] = SQ_H1;
    castlingRookSquare[WHITE][QUEEN_SIDE] = SQ_A1;
    castlingRookSquare[BLACK][KING_SIDE] = SQ_H8;
    castlingRookSquare[BLACK][QUEEN_SIDE] = SQ_A8;
}

Square Position::kingSquare(Color c) const
{
    const Bitboard k = byColor[c] & byType[KING];
    return k ? lsb(k) : SQ_NONE;
}

std::string Position::castlingString() const
{
    if (castlingRights == 0)
        return "-";

    std::string out;
    const auto append = [&](Color c, CastlingSide s)
    {
        if (!(castlingRights & castlingRight(c, s)))
            return;

        const Square sq = castlingRookSquare[c][s];
        const Square ksq = kingSquare(c);
        const bool queenSide = ksq != SQ_NONE ? sq < ksq : s == QUEEN_SIDE;

        Bitboard others = byColor[c] & byType[ROOK] & backRankOf(c) & ~(1ULL << sq);
        bool outermost = true;
        for (; others; others &= others - 1)
        {
            const Square other = lsb(others);
            if ((fileOf(other) < fileOf(sq)) == queenSide)
            {
                outermost = false;
                break;
            }
        }

        char letter;
        if (!outermost)
            letter = static_cast<char>((c == WHITE ? 'A' : 'a') + fileOf(sq));
        else if (queenSide)
            letter = c == WHITE ? 'Q' : 'q';
        else
            letter = c == WHITE ? 'K' : 'k';

        out += letter;
    };

    append(WHITE, KING_SIDE);
    append(WHITE, QUEEN_SIDE);
    append(BLACK, KING_SIDE);
    append(BLACK, QUEEN_SIDE);

    return out.empty() ? "-" : out;
}

void Position::putPiece(Piece piece, Square square)
{
    const Bitboard bit = 1ULL << square;
    board[square] = piece;
    byPiece[piece] |= bit;
    byColor[colorOf(piece)] |= bit;
    byType[typeOf(piece)] |= bit;
    zobristKey ^= pieceKeys[piece][square];
}

void Position::removePiece(Square square)
{
    const Piece piece = board[square];
    if (piece == NO_PIECE)
        return;
    const Bitboard notBit = ~(1ULL << square);
    board[square] = NO_PIECE;
    byPiece[piece] &= notBit;
    byColor[colorOf(piece)] &= notBit;
    byType[typeOf(piece)] &= notBit;
    zobristKey ^= pieceKeys[piece][square];
}

bool Position::do_move(Move move)
{
    initZobrist();

    const Color us = sideToMove;
    const Color them = static_cast<Color>(us ^ 1);
    const Square from = move.from();
    const Square to = move.to();
    const Piece piece = board[from];
    const PieceType pt = typeOf(piece);
    const bool castling = move.isCastling();

    // Castling legality is verified here so
    // that callers which only generate pseudo-legal moves can rely on the
    // return value. In Chess960 the king and rook may start on any back-rank
    // file, so this can never be decided from the move alone.
    if (castling && !movegen::canCastle(*this, move))
        return false;

    // Save state for unmake.
    UndoInfo &undo = undoStack[undoCount];
    undoCount = (undoCount + 1) & (MAX_UNDO - 1);
    undo.movedPiece = piece;
    undo.capturedPiece = castling ? NO_PIECE : board[to];
    undo.prevCastlingRights = castlingRights;
    undo.prevEnPassantSquare = enPassantSquare;
    undo.prevHalfmoveClock = halfmoveClock;
    undo.prevHistoryCount = historyCount;
    undo.prevHistoryStart = historyStart;

    // Move the piece.
    removePiece(from);

    if (castling)
    {
        removePiece(to);
        putPiece(makePiece(us, KING), castlingKingTo(move));
        putPiece(makePiece(us, ROOK), castlingRookTo(move));
    }
    else
    {
        if (move.isEnPassant())
        {
            const Square capSq = static_cast<Square>(to + (us == WHITE ? -8 : 8));
            undo.capturedPiece = board[capSq];
            removePiece(capSq);
        }
        else if (board[to] != NO_PIECE)
        {
            removePiece(to);
        }

        putPiece(move.isPromotion() ? makePiece(us, move.promoType()) : piece, to);
    }


    if (pt == KING)
    {
        castlingRights = static_cast<uint8_t>(
            castlingRights & ~(castlingRight(us, KING_SIDE) | castlingRight(us, QUEEN_SIDE)));
    }
    else if (pt == ROOK)
    {
        for (int s = 0; s < CASTLING_SIDE_NB; ++s)
        {
            const CastlingSide side = static_cast<CastlingSide>(s);
            if (from == castlingRookSquare[us][side])
                castlingRights = static_cast<uint8_t>(castlingRights & ~castlingRight(us, side));
        }
    }

    if (undo.capturedPiece != NO_PIECE && typeOf(undo.capturedPiece) == ROOK)
    {
        for (int s = 0; s < CASTLING_SIDE_NB; ++s)
        {
            const CastlingSide side = static_cast<CastlingSide>(s);
            if (to == castlingRookSquare[them][side])
                castlingRights = static_cast<uint8_t>(castlingRights & ~castlingRight(them, side));
        }
    }

    // Update state.
    enPassantSquare = (pt == PAWN && (from ^ to) == 16) ? static_cast<Square>((from + to) / 2) : SQ_NONE;

    if (pt == PAWN || undo.capturedPiece != NO_PIECE)
        halfmoveClock = 0;
    else
        ++halfmoveClock;

    if (us == BLACK)
        ++fullmoveNumber;

    sideToMove = them;

    // Update hash (side, castling, en passant).
    zobristKey ^= sideKey;
    zobristKey ^= castlingKeys[undo.prevCastlingRights] ^ castlingKeys[castlingRights];
    zobristKey ^= epKey(undo.prevEnPassantSquare) ^ epKey(enPassantSquare);

    // Track position history for repetition detection.
    if (pt == PAWN || undo.capturedPiece != NO_PIECE)
        historyStart = historyCount; // irreversible move: nothing earlier can repeat
    historyKeys[historyCount & (MAX_HISTORY - 1)] = zobristKey;
    ++historyCount;

    Square ksq = castling ? castlingKingTo(move) : to;
    if (!castling && pt != KING)
    {
        const Bitboard k = byType[KING] & byColor[us];
        if (!k)
        {
            undo_move(move);
            return false;
        }
        ksq = lsb(k);
    }
    if (movegen::squareAttacked(*this, ksq, them))
    {
        undo_move(move);
        return false;
    }

    return true;
}

void Position::undo_move(Move move)
{
    const Color us = static_cast<Color>(sideToMove ^ 1); // side that moved
    const Square from = move.from();
    const Square to = move.to();
    undoCount = (undoCount - 1) & (MAX_UNDO - 1);
    UndoInfo &undo = undoStack[undoCount];

    // Revert hash for side, castling, and en passant.
    zobristKey ^= sideKey;
    zobristKey ^= castlingKeys[castlingRights] ^ castlingKeys[undo.prevCastlingRights];
    zobristKey ^= epKey(enPassantSquare) ^ epKey(undo.prevEnPassantSquare);

    // Revert state fields.
    castlingRights = undo.prevCastlingRights;
    enPassantSquare = undo.prevEnPassantSquare;
    halfmoveClock = undo.prevHalfmoveClock;
    historyCount = undo.prevHistoryCount;
    historyStart = undo.prevHistoryStart;
    if (us == BLACK)
        --fullmoveNumber;
    sideToMove = us;

    if (move.isCastling())
    {
        // Take king and rook off their castling squares before restoring them
        // (Chess960: the two may have swapped, or the king may not have moved).
        removePiece(castlingKingTo(move));
        removePiece(castlingRookTo(move));
        putPiece(makePiece(us, KING), from);
        putPiece(makePiece(us, ROOK), to);
        return;
    }

    // Remove moved piece from destination and restore any captured piece.
    removePiece(to);
    if (move.isEnPassant())
        putPiece(undo.capturedPiece, static_cast<Square>(to + (us == WHITE ? -8 : 8)));
    else if (undo.capturedPiece != NO_PIECE)
        putPiece(undo.capturedPiece, to);

    // Restore the moving piece at its origin.
    putPiece(undo.movedPiece, from);
}

void Position::do_null_move()
{
    initZobrist();

    // Save state for unmake.
    UndoInfo &undo = undoStack[undoCount];
    undoCount = (undoCount + 1) & (MAX_UNDO - 1);
    undo.prevEnPassantSquare = enPassantSquare;
    undo.prevHalfmoveClock = halfmoveClock;
    undo.prevHistoryCount = historyCount;
    undo.prevHistoryStart = historyStart;

    enPassantSquare = SQ_NONE;
    ++halfmoveClock;
    sideToMove = static_cast<Color>(sideToMove ^ 1);

    // Hash: flip side and clear the en passant square.
    zobristKey ^= sideKey ^ epKey(undo.prevEnPassantSquare);
}

void Position::undo_null_move()
{
    undoCount = (undoCount - 1) & (MAX_UNDO - 1);
    UndoInfo &undo = undoStack[undoCount];

    sideToMove = static_cast<Color>(sideToMove ^ 1);
    enPassantSquare = undo.prevEnPassantSquare;
    halfmoveClock = undo.prevHalfmoveClock;
    historyCount = undo.prevHistoryCount;
    historyStart = undo.prevHistoryStart;

    // Hash: flip side back and restore the en passant square.
    zobristKey ^= sideKey ^ epKey(enPassantSquare);
}

bool Position::isRepetition(int ply) const
{
    const uint64_t key = zobristKey;
    const int rootCount = historyCount - ply;
    const int oldest = historyCount - MAX_HISTORY;
    const int from = oldest > historyStart ? oldest : historyStart;
    int gameMatches = 0;

    // Same side to move recurs every two plies; the previous one is two
    // entries behind the current position (which sits at historyCount - 1).
    for (int i = historyCount - 3; i >= from; i -= 2)
    {
        if (historyKeys[i & (MAX_HISTORY - 1)] != key)
            continue;
        if (i >= rootCount)
            return true; // second occurrence inside the search
        if (++gameMatches >= 2)
            return true; // threefold against game history
    }

    return false;
}

bool Position::set_fen(const std::string &fen)
{
    std::istringstream ss(fen);
    std::string boardStr, sideStr, castlingStr, epStr, halfmoveStr, fullmoveStr;
    if (!(ss >> boardStr >> sideStr >> castlingStr >> epStr))
        return false;

    // The halfmove clock and the fullmove number are optional in UCI.
    if (!(ss >> halfmoveStr))
        halfmoveStr = "0";
    if (!(ss >> fullmoveStr))
        fullmoveStr = "1";

    initZobrist();
    clear();

    // 1. Piece placement, ranks 8 down to 1.
    int rank = RANK_8;
    int file = FILE_A;
    for (char c : boardStr)
    {
        if (c == '/')
        {
            if (file != FILE_NB)
                return false;
            --rank;
            file = FILE_A;
        }
        else if (c >= '1' && c <= '8')
        {
            file += c - '0';
        }
        else
        {
            const Piece p = charToPiece(c);
            if (p == NO_PIECE || file >= FILE_NB || rank < RANK_1)
                return false;
            putPiece(p, makeSquare(static_cast<File>(file), static_cast<Rank>(rank)));
            ++file;
        }
        if (file > FILE_NB)
            return false;
    }
    if (rank != RANK_1 || file != FILE_NB)
        return false;

    // 2. Side to move.
    if (sideStr == "w")
        sideToMove = WHITE;
    else if (sideStr == "b")
        sideToMove = BLACK;
    else
        return false;

    // 3. Castling rights. Both the classical letters and the Chess960 file
    //    letters are accepted: "KQkq" (rook on the outermost file of its side)
    //    and "HAha" or "GBgb" (Shredder-FEN, the rook's own file).
    if (castlingStr != "-")
    {
        for (char c : castlingStr)
        {
            Color color;
            Square rookSq;

            switch (c)
            {
            case 'K':
            case 'Q':
            case 'k':
            case 'q':
            {
                color = (c == 'K' || c == 'Q') ? WHITE : BLACK;
                const bool queenSide = (c == 'Q' || c == 'q');
                const Rank homeRank = color == WHITE ? RANK_1 : RANK_8;
                const Bitboard rooks = byColor[color] & byType[ROOK] & backRankOf(color);
                const Square ksq = kingSquare(color);

                const Square outer = queenSide ? (rooks ? lsb(rooks) : SQ_NONE)
                                               : (rooks ? msb(rooks) : SQ_NONE);
                const bool usable = outer != SQ_NONE && (ksq == SQ_NONE || (queenSide ? outer < ksq : outer > ksq));
                rookSq = usable ? outer : makeSquare(queenSide ? FILE_A : FILE_H, homeRank);
                break;
            }
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'G':
            case 'H':
                color = WHITE;
                rookSq = makeSquare(static_cast<File>(c - 'A'), RANK_1);
                break;
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'h':
                color = BLACK;
                rookSq = makeSquare(static_cast<File>(c - 'a'), RANK_8);
                break;
            default:
                return false;
            }

            // The rook castles towards the king side when it starts right of
            // the king; that - not the letter itself - picks the right.
            const Square ksq = kingSquare(color);
            const CastlingSide side = (ksq != SQ_NONE && rookSq < ksq) ? QUEEN_SIDE : KING_SIDE;

            castlingRights |= castlingRight(color, side);
            castlingRookSquare[color][side] = rookSq;
        }
    }

    // 4. En passant target square.
    if (epStr == "-")
    {
        enPassantSquare = SQ_NONE;
    }
    else
    {
        if (epStr.size() != 2)
            return false;
        const int f = epStr[0] - 'a';
        const int r = epStr[1] - '1';
        if (f < FILE_A || f >= FILE_NB || r < RANK_1 || r >= RANK_NB)
            return false;
        enPassantSquare = makeSquare(static_cast<File>(f), static_cast<Rank>(r));
    }

    // 5. Halfmove clock and fullmove number.
    try
    {
        halfmoveClock = std::stoi(halfmoveStr);
        fullmoveNumber = std::stoi(fullmoveStr);
    }
    catch (...)
    {
        return false;
    }
    if (halfmoveClock < 0 || fullmoveNumber < 1)
        return false;

    // Finalize the Zobrist key with side, castling, and en passant components.
    zobristKey ^= sideToMove == BLACK ? sideKey : 0;
    zobristKey ^= castlingKeys[castlingRights];
    zobristKey ^= epKey(enPassantSquare);

    // Reset repetition history to just this position.
    historyCount = 1;
    historyStart = 0;
    historyKeys[0] = zobristKey;

    return true;
}

std::string Position::fen() const
{
    std::string fen;

    for (int r = RANK_8; r >= RANK_1; --r)
    {
        int empty = 0;
        for (int f = FILE_A; f < FILE_NB; ++f)
        {
            const Piece p = board[makeSquare(static_cast<File>(f), static_cast<Rank>(r))];
            if (p == NO_PIECE)
                ++empty;
            else
            {
                if (empty)
                {
                    fen += static_cast<char>('0' + empty);
                    empty = 0;
                }
                fen += pieceToChar(p);
            }
        }
        if (empty)
            fen += static_cast<char>('0' + empty);
        if (r > RANK_1)
            fen += '/';
    }

    fen += sideToMove == WHITE ? " w " : " b ";
    fen += castlingString();

    if (enPassantSquare == SQ_NONE)
        fen += " - ";
    else
    {
        fen += ' ';
        fen += static_cast<char>('a' + (enPassantSquare % 8));
        fen += static_cast<char>('1' + (enPassantSquare / 8));
        fen += ' ';
    }

    fen += std::to_string(halfmoveClock);
    fen += ' ';
    fen += std::to_string(fullmoveNumber);

    return fen;
}

void Position::print_board() const
{
    std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    for (int r = RANK_8; r >= RANK_1; --r)
    {
        std::cout << (r + 1) << " |";
        for (int f = FILE_A; f < FILE_NB; ++f)
        {
            const Piece p = board[makeSquare(static_cast<File>(f), static_cast<Rank>(r))];
            std::cout << ' ' << pieceToChar(p) << " |";
        }
        std::cout << '\n';
        std::cout << "  +---+---+---+---+---+---+---+---+\n";
    }
    std::cout << "    a   b   c   d   e   f   g   h\n\n";

    std::cout << "Side to move: " << (sideToMove == WHITE ? "white" : "black") << '\n';
    std::cout << "Castling: " << castlingString() << '\n';

    if (enPassantSquare == SQ_NONE)
        std::cout << "En passant: -\n";
    else
        std::cout << "En passant: "
                  << static_cast<char>('a' + (enPassantSquare % 8))
                  << static_cast<char>('1' + (enPassantSquare / 8)) << '\n';

    std::cout << "Halfmove clock: " << halfmoveClock << '\n';
    std::cout << "Fullmove number: " << fullmoveNumber << '\n';
}