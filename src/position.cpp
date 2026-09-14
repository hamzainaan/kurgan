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

    // Castling rights remaining when a piece moves from, or is captured on, a square.
    std::array<uint8_t, SQUARE_NB> makeCastlingMask()
    {
        std::array<uint8_t, SQUARE_NB> m{};
        constexpr uint8_t all = WHITE_OO | WHITE_OOO | BLACK_OO | BLACK_OOO;
        m.fill(all);
        m[SQ_E1] = static_cast<uint8_t>(all & ~(WHITE_OO | WHITE_OOO));
        m[SQ_H1] = static_cast<uint8_t>(all & ~WHITE_OO);
        m[SQ_A1] = static_cast<uint8_t>(all & ~WHITE_OOO);
        m[SQ_E8] = static_cast<uint8_t>(all & ~(BLACK_OO | BLACK_OOO));
        m[SQ_H8] = static_cast<uint8_t>(all & ~BLACK_OO);
        m[SQ_A8] = static_cast<uint8_t>(all & ~BLACK_OOO);
        return m;
    }

    const std::array<uint8_t, SQUARE_NB> castlingMask = makeCastlingMask();

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

    // Castling is illegal if the king is currently in check, passes through
    // an attacked square, or lands on an attacked square.
    if (move.isCastling())
    {
        const bool kingside = to == SQ_G1 || to == SQ_G8;
        const Square pass = us == WHITE ? (kingside ? SQ_F1 : SQ_D1)
                                        : (kingside ? SQ_F8 : SQ_D8);
        if (movegen::squareAttacked(*this, from, them) ||
            movegen::squareAttacked(*this, pass, them) ||
            movegen::squareAttacked(*this, to, them))
            return false;
    }

    // Save state for unmake.
    UndoInfo &undo = undoStack[undoCount];
    undoCount = (undoCount + 1) & (MAX_UNDO - 1);
    undo.movedPiece = piece;
    undo.capturedPiece = board[to];
    undo.prevCastlingRights = castlingRights;
    undo.prevEnPassantSquare = enPassantSquare;
    undo.prevHalfmoveClock = halfmoveClock;
    undo.prevHistoryCount = historyCount;
    undo.prevHistoryStart = historyStart;

    // Move the piece.
    removePiece(from);

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

    if (move.isCastling())
    {
        if (to == SQ_G1)
        {
            removePiece(SQ_H1);
            putPiece(W_ROOK, SQ_F1);
        }
        else if (to == SQ_C1)
        {
            removePiece(SQ_A1);
            putPiece(W_ROOK, SQ_D1);
        }
        else if (to == SQ_G8)
        {
            removePiece(SQ_H8);
            putPiece(B_ROOK, SQ_F8);
        }
        else if (to == SQ_C8)
        {
            removePiece(SQ_A8);
            putPiece(B_ROOK, SQ_D8);
        }
    }

    // Update state.
    castlingRights = static_cast<uint8_t>(castlingRights & castlingMask[from] & castlingMask[to]);
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

    // Legality: the mover's king must not be attacked after the move.
    Square ksq = to;
    if (pt != KING)
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

    // Revert castling rook move.
    if (move.isCastling())
    {
        if (to == SQ_G1)
        {
            removePiece(SQ_F1);
            putPiece(W_ROOK, SQ_H1);
        }
        else if (to == SQ_C1)
        {
            removePiece(SQ_D1);
            putPiece(W_ROOK, SQ_A1);
        }
        else if (to == SQ_G8)
        {
            removePiece(SQ_F8);
            putPiece(B_ROOK, SQ_H8);
        }
        else if (to == SQ_C8)
        {
            removePiece(SQ_D8);
            putPiece(B_ROOK, SQ_A8);
        }
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
    if (!(ss >> boardStr >> sideStr >> castlingStr >> epStr >> halfmoveStr >> fullmoveStr))
        return false;

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

    // 3. Castling rights.
    if (castlingStr != "-")
    {
        for (char c : castlingStr)
        {
            switch (c)
            {
            case 'K':
                castlingRights |= WHITE_OO;
                break;
            case 'Q':
                castlingRights |= WHITE_OOO;
                break;
            case 'k':
                castlingRights |= BLACK_OO;
                break;
            case 'q':
                castlingRights |= BLACK_OOO;
                break;
            default:
                return false;
            }
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

    if (castlingRights == 0)
        fen += '-';
    else
    {
        if (castlingRights & WHITE_OO)
            fen += 'K';
        if (castlingRights & WHITE_OOO)
            fen += 'Q';
        if (castlingRights & BLACK_OO)
            fen += 'k';
        if (castlingRights & BLACK_OOO)
            fen += 'q';
    }

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

    std::string castling;
    if (castlingRights & WHITE_OO)
        castling += 'K';
    if (castlingRights & WHITE_OOO)
        castling += 'Q';
    if (castlingRights & BLACK_OO)
        castling += 'k';
    if (castlingRights & BLACK_OOO)
        castling += 'q';
    std::cout << "Castling: " << (castling.empty() ? "-" : castling) << '\n';

    if (enPassantSquare == SQ_NONE)
        std::cout << "En passant: -\n";
    else
        std::cout << "En passant: "
                  << static_cast<char>('a' + (enPassantSquare % 8))
                  << static_cast<char>('1' + (enPassantSquare / 8)) << '\n';

    std::cout << "Halfmove clock: " << halfmoveClock << '\n';
    std::cout << "Fullmove number: " << fullmoveNumber << '\n';
}
