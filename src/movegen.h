#pragma once

#include "position.h"
#include "types.h"

#include <array>
#include <cstdint>
#include <string>

// Flags packed into the upper four bits of a move.
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

// A 16-bit move: from (6 bits) | to (6 bits) | flags (4 bits).
struct Move
{
    uint16_t data = 0;

    constexpr Move() = default;
    constexpr Move(uint16_t d) : data(d)
    {
    }
    constexpr Move(Square from, Square to, uint16_t flag = NORMAL)
        : data(static_cast<uint16_t>(from) | (static_cast<uint16_t>(to) << 6) | flag)
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

    std::array<Move, MAX_MOVES> moves{};
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

// Render a move in coordinate notation ("e2e4", "e7e8q").
std::string moveToUci(Move m);

namespace movegen
{
    // Build all attack tables. Must be called once before any generation.
    void init();
    
    void setChess960(bool enabled);
    bool chess960();

    // True if 'move' (king-from -> rook-from) is a legal castling move in 'pos'.
    bool canCastle(const Position &pos, Move move);

    // Attack bitboard for a non-pawn piece type from a square, given occupancy.
    Bitboard attacks(PieceType pt, Square sq, Bitboard occupied);

    // Squares from which a pawn of color 'c' attacks 'sq'.
    Bitboard pawnAttacksFrom(Color c, Square sq);

    // True if 'sq' is attacked by color 'by'.
    bool squareAttacked(const Position &pos, Square sq, Color by);

    // Generate all pseudo-legal moves for the side to move.
    void generate_pseudo_legal_moves(const Position &pos, MoveList &list);

    // Same moves as above in the same relative order, but only captures,
    // promotions and en passant.
    void generate_tactical_moves(const Position &pos, MoveList &list);

    // True if the pseudo-legal move leaves the mover's king out of check.
    bool is_legal(const Position &pos, Move move);
}
