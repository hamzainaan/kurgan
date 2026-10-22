#pragma once

#include "types.h"

#include <array>
#include <cstdint>
#include <string>

inline constexpr const char *START_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

enum CastlingRight : uint8_t
{
    WHITE_OO = 1 << 0,
    WHITE_OOO = 1 << 1,
    BLACK_OO = 1 << 2,
    BLACK_OOO = 1 << 3
};

enum CastlingSide : int
{
    KING_SIDE,
    QUEEN_SIDE,
    CASTLING_SIDE_NB = 2
};

constexpr uint8_t castlingRight(Color c, CastlingSide s)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(c == WHITE ? WHITE_OO : BLACK_OO) << s);
}

constexpr Color colorOf(Piece p)
{
    return static_cast<Color>(p >> 3);
}

constexpr PieceType typeOf(Piece p)
{
    return static_cast<PieceType>((p - 1) & 7);
}

struct Move;

class Position
{
public:
    Position();

    // Parses a FEN string and returns false when it is malformed.
    bool set_fen(const std::string &fen);

    std::string fen() const;

    void print_board() const;

    // Applies a move and returns false without changing the position when it is illegal.
    bool do_move(Move move);

    // Undoes the last move applied with do_move.
    void undo_move(Move move);

    void do_null_move();
    void undo_null_move();

    // Checks whether the current position repeats on the search path. The ply
    // locates the root, so a second occurrence in search draws but game history
    // needs three.
    bool isRepetition(int ply) const;

    // Bitboards by color, piece type and piece, plus a square to piece map.
    std::array<Bitboard, COLOR_NB> byColor{};
    std::array<Bitboard, PIECE_TYPE_NB> byType{};
    std::array<Bitboard, PIECE_NB> byPiece{};
    std::array<Piece, SQUARE_NB> board{};

    Color sideToMove = WHITE;
    uint8_t castlingRights = 0;
    Square enPassantSquare = SQ_NONE;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    std::array<std::array<Square, CASTLING_SIDE_NB>, COLOR_NB> castlingRookSquare{};

    Square kingSquare(Color c) const;
    std::string castlingString() const;

    // Incrementally maintained Zobrist key.
    uint64_t zobristKey = 0;
    uint64_t pawnKey = 0;
    std::array<uint64_t, COLOR_NB> nonPawnKey{};

private:
    static constexpr int MAX_UNDO = 256;
    static constexpr int MAX_HISTORY = 1024;

    struct UndoInfo
    {
        Piece movedPiece = NO_PIECE;
        Piece capturedPiece = NO_PIECE;
        uint8_t prevCastlingRights = 0;
        Square prevEnPassantSquare = SQ_NONE;
        int prevHalfmoveClock = 0;
        int prevHistoryCount = 0;
        int prevHistoryStart = 0;
    };

    std::array<UndoInfo, MAX_UNDO> undoStack{};
    int undoCount = 0;

    // Keys since the last irreversible move, used for repetition detection.
    std::array<uint64_t, MAX_HISTORY> historyKeys{};
    int historyCount = 0;
    int historyStart = 0;

    void clear();
    void putPiece(Piece piece, Square square);
    void removePiece(Square square);
};
