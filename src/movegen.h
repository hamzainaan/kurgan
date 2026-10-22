#pragma once

#include "position.h"
#include "types.h"

#include <array>
#include <cstdint>
#include <string>

// Move flags occupy the upper four bits of the move word.
enum MoveFlag : uint16_t
{
    NORMAL = 0,
    KNIGHT_PROMO = 0 << 12,
    BISHOP_PROMO = 1 << 12,
    ROOK_PROMO = 2 << 12,
    QUEEN_PROMO = 3 << 12,
    PROMOTION = 1 << 14,
    EN_PASSANT = 2 << 14,
    CASTLING = 3 << 14
};

// A move stores from and to in six bits each and flags in four bits.
struct Move
{
    uint16_t data;

    constexpr Move() = default;
    constexpr Move(uint16_t d) : data(d)
    {
    }
    constexpr Move(Square from, Square to, uint16_t flag = NORMAL)
        : data(static_cast<uint16_t>(static_cast<uint16_t>(from) | (static_cast<uint16_t>(to) << 6) | flag))
    {
    }

    constexpr Square from() const { return static_cast<Square>(data & 0x3F); }
    constexpr Square to() const { return static_cast<Square>((data >> 6) & 0x3F); }
    constexpr uint16_t type() const { return data & 0xC000; }
    constexpr PieceType promoType() const { return static_cast<PieceType>(((data >> 12) & 3) + KNIGHT); }
    constexpr bool isPromotion() const { return type() == PROMOTION; }
    constexpr bool isEnPassant() const { return type() == EN_PASSANT; }
    constexpr bool isCastling() const { return type() == CASTLING; }

    constexpr bool operator==(Move m) const { return data == m.data; }
    constexpr bool operator!=(Move m) const { return data != m.data; }
};

constexpr bool isKingSideCastling(Move m)
{
    return m.to() > m.from();
}

constexpr Square castlingKingTo(Move m)
{
    return makeSquare(isKingSideCastling(m) ? FILE_G : FILE_C, rankOf(m.from()));
}

constexpr Square castlingRookTo(Move m)
{
    return makeSquare(isKingSideCastling(m) ? FILE_F : FILE_D, rankOf(m.from()));
}

constexpr Square moveTarget(Move m)
{
    return m.isCastling() ? castlingKingTo(m) : m.to();
}

// Fixed-capacity stack buffer of generated moves.
struct MoveList
{
    static constexpr int MAX_MOVES = 512;

    std::array<Move, MAX_MOVES> moves;
    int size = 0;

    void add(Move m)
    {
        if (size < MAX_MOVES)
            moves[size++] = m;
    }
    void clear() { size = 0; }
    int count() const { return size; }
    Move &operator[](int i) { return moves[i]; }
    const Move &operator[](int i) const { return moves[i]; }
};

// Renders a move in UCI coordinate notation.
std::string moveToUci(Move m);

namespace movegen
{
    // Builds all attack tables and must run before move generation.
    void init();
    
    // Selects Chess960 castling notation for UCI input and output.
    void setChess960(bool enabled);
    bool chess960();

    // Checks whether a castling move is legal. The move stores the king origin
    // and the rook origin, which can differ from their final squares in Chess960.
    bool canCastle(const Position &pos, Move move);

    // Returns the attacks of a non-pawn piece under the given occupancy.
    Bitboard attacks(PieceType pt, Square sq, Bitboard occupied);

    Bitboard pawnAttacksFrom(Color c, Square sq);

    bool squareAttacked(const Position &pos, Square sq, Color by);

    // Checks whether a move is a capture, promotion or en passant.
    inline bool isTactical(const Position &pos, Move m)
    {
        return m.isPromotion() || m.isEnPassant()
               || (!m.isCastling() && pos.board[m.to()] != NO_PIECE);
    }

    void generate_pseudo_legal_moves(const Position &pos, MoveList &list);

    // Generates the tactical subset in the same relative order as the full generator.
    void generate_tactical_moves(const Position &pos, MoveList &list);

    // Checks whether a pseudo-legal move leaves the king out of check.
    bool is_legal(const Position &pos, Move move);
}
