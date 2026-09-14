#pragma once

#include "types.h"

#include <array>
#include <cstdint>
#include <string>

// Castling rights as bit flags.
enum CastlingRight : uint8_t
{
    WHITE_OO = 1 << 0,
    WHITE_OOO = 1 << 1,
    BLACK_OO = 1 << 2,
    BLACK_OOO = 1 << 3
};

// Which side the king castles to
enum CastlingSide : int
{
    KING_SIDE,
    QUEEN_SIDE,
    CASTLING_SIDE_NB = 2
};

// Right flag that allows color 'c' to castle towards 's'.
constexpr uint8_t castlingRight(Color c, CastlingSide s)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(c == WHITE ? WHITE_OO : BLACK_OO) << s);
}

// Decompose a Piece into its Color and PieceType components.
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

    // Parse a FEN string into the position; returns false on malformed input.
    bool set_fen(const std::string &fen);

    // Serialize the current position to a FEN string.
    std::string fen() const;

    // Print an ASCII representation of the board to standard output.
    void print_board() const;

    // Make a move; returns false (without applying it) if the move is illegal.
    bool do_move(Move move);

    // Unmake the last move made by do_move.
    void undo_move(Move move);

    // Make/unmake a null move (pass the turn without moving a piece).
    void do_null_move();
    void undo_null_move();

    // True if the current position repeats on the path; 'ply' is the current
    // search ply (0 at the root) used to distinguish game and search history.
    bool isRepetition(int ply) const;

    // Bitboards keyed by color, piece type, and piece; plus a square->piece map.
    std::array<Bitboard, COLOR_NB> byColor{};
    std::array<Bitboard, PIECE_TYPE_NB> byType{};
    std::array<Bitboard, PIECE_NB> byPiece{};
    std::array<Piece, SQUARE_NB> board{};

    // Game state.
    Color sideToMove = WHITE;
    uint8_t castlingRights = 0;
    Square enPassantSquare = SQ_NONE;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    std::array<std::array<Square, CASTLING_SIDE_NB>, COLOR_NB> castlingRookSquare{};

    Square kingSquare(Color c) const;
    std::string castlingString() const;

    // Incrementally maintained Zobrist hash of the position.
    uint64_t zobristKey = 0;

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

    // Position keys since the last irreversible move (used for repetition).
    std::array<uint64_t, MAX_HISTORY> historyKeys{};
    int historyCount = 0;
    int historyStart = 0;

    void clear();
    void putPiece(Piece piece, Square square);
    void removePiece(Square square);
};
