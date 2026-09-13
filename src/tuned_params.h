#pragma once

#include <array>

namespace tuned
{

// Material, indexed by PieceType (PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING).
constexpr std::array<int, 6> MATERIAL = {
     108,  326,  339,  513,  905,    0,
};

// Piece-square tables, white perspective, index 0 = a8 ... 63 = h1.
constexpr std::array<std::array<int, 64>, 6> MG_PST = {
    {
        // PAWN
           0,    0,    0,    0,    0,    0,    0,    0,
          63,   65,   51,   56,   54,   57,   43,   43,
           5,   11,   29,   17,   40,   50,   27,    3,
         -15,   18,   10,   39,   43,   24,   25,  -19,
         -34,  -16,    6,   26,   31,   16,    2,  -26,
         -22,  -17,    2,   -1,   19,   19,   32,   -1,
         -32,   -4,  -30,   -4,    5,   36,   46,  -11,
           0,    0,    0,    0,    0,    0,    0,    0,
        // KNIGHT
         -65,  -41,  -29,  -29,  -22,  -36,  -41,  -56,
         -47,  -24,   26,    4,    3,   14,  -13,  -37,
         -34,   15,   18,   33,   37,   34,   27,  -16,
         -18,   19,    5,   46,   21,   47,   19,    6,
         -14,   -1,   16,    7,   30,   14,   15,   -6,
         -27,   -7,    9,   19,   27,   28,   25,  -12,
         -30,  -25,   -7,   20,   28,   18,  -13,  -11,
         -54,   -6,  -32,  -15,   -8,  -20,   -3,  -48,
        // BISHOP
         -16,  -11,  -17,  -13,  -11,  -11,  -10,  -16,
         -15,    9,   -2,   -4,    6,   15,   12,  -26,
         -11,   12,   20,   16,   14,   19,   14,    4,
          -4,    7,   10,   38,   29,   18,   15,    5,
          -1,   17,   13,   31,   32,    3,   10,    7,
          13,   27,   33,   13,   24,   47,   29,   15,
           0,   49,   22,   25,   37,   26,   70,    4,
         -16,    6,   26,    4,    7,   22,   -9,  -11,
        // ROOK
           7,    9,    3,   11,   12,    4,    4,    5,
          15,   22,   32,   33,   28,   30,   19,   19,
          -2,    8,    9,   12,    7,   11,   20,    3,
         -11,   -6,    6,   12,    5,   15,    1,   -2,
         -22,   -6,    2,    1,    9,   -2,    7,   -9,
         -28,   -9,   -1,    0,   11,    6,    4,  -16,
         -32,    1,    1,   12,   20,   19,    8,  -43,
          -2,    7,   28,   40,   43,   35,  -14,    0,
        // QUEEN
         -14,   -5,    0,   -1,   13,    6,    2,    8,
         -15,  -34,   -1,    3,    2,   20,   19,   17,
         -11,  -12,   -3,    4,   15,   26,   22,   31,
         -18,  -22,  -14,  -17,    2,    7,    9,    6,
         -13,  -19,   -8,  -12,   -3,    2,    6,    4,
         -15,    4,   -4,    2,    3,    7,   16,    4,
         -16,   -5,   18,   19,   31,   20,    2,   -1,
          -3,   -4,    8,   29,    4,   -9,   -7,  -24,
        // KING
         -31,  -38,  -38,  -49,  -50,  -39,  -38,  -29,
         -28,  -34,  -35,  -44,  -46,  -34,  -36,  -29,
         -24,  -33,  -33,  -49,  -48,  -29,  -25,  -25,
         -29,  -34,  -36,  -50,  -51,  -37,  -32,  -28,
         -23,  -25,  -35,  -51,  -51,  -39,  -33,  -29,
          -9,  -15,  -25,  -36,  -40,  -34,  -12,  -12,
          16,   26,   -9,  -52,  -44,  -13,   35,   41,
           3,   59,   36,  -44,   20,  -20,   71,   42,
    },
};

constexpr std::array<std::array<int, 64>, 6> EG_PST = {
    {
        // PAWN
           0,    0,    0,    0,    0,    0,    0,    0,
         127,  113,   89,   75,   73,   72,   87,  110,
          86,   81,   58,   28,   18,   36,   57,   65,
          45,   30,   17,   -7,   -3,   10,   22,   29,
          31,   24,    5,   -4,   -2,    1,   11,   12,
          15,   17,    2,    7,    7,    5,   -1,    0,
          26,   12,   23,    3,   12,   10,    1,    1,
           0,    0,    0,    0,    0,    0,    0,    0,
        // KNIGHT
         -56,  -37,  -20,  -27,  -19,  -34,  -44,  -58,
         -31,   -6,   -6,    7,   -2,   -7,  -16,  -40,
         -27,    2,   14,   15,    7,   11,   -2,  -26,
          -6,   13,   25,   27,   26,   17,   13,   -7,
          -7,    0,   19,   30,   18,   20,   12,  -14,
         -15,    6,   -6,   14,    9,  -11,  -13,  -17,
         -33,  -16,   -3,   -3,    1,   -7,  -14,  -35,
         -46,  -35,  -22,   -8,  -10,  -16,  -39,  -50,
        // BISHOP
         -13,  -10,   -8,   -5,   -2,   -4,   -9,  -16,
          -2,    3,    9,   -5,    4,    7,    5,  -11,
           7,    8,    4,    4,    4,    8,    8,    6,
           8,   13,   12,    7,    8,    8,    2,    6,
           5,    6,   15,   17,    2,    9,    0,    4,
           1,   14,   12,   14,   17,   -3,    8,    1,
          -2,   -7,    3,    4,    1,    6,   -8,  -11,
         -11,    6,    4,   11,    9,    4,   -2,  -10,
        // ROOK
          30,   22,   23,   22,   21,   23,   20,   20,
          38,   36,   34,   31,   23,   32,   32,   31,
          30,   30,   25,   26,   18,   22,   22,   22,
          31,   23,   30,   18,   18,   27,   18,   28,
          30,   25,   28,   20,   18,   16,   15,   14,
          22,   22,   16,   18,   11,   12,   14,   10,
          23,   16,   21,   24,   15,   16,    9,   16,
          19,   20,   14,    8,    5,    8,   15,   -1,
        // QUEEN
         -10,    5,    4,    6,   14,   10,    3,    9,
          -5,   -1,    2,    8,   12,   18,   10,   12,
          -6,   -7,  -11,    9,   12,   18,   17,   20,
           4,   -3,   -8,   -8,    4,   10,   21,   26,
           3,    1,   -6,    0,   -6,    8,   17,   18,
          -1,   -8,   -3,  -12,   -1,    9,   16,    5,
          -5,   -5,   -9,   -5,   -5,    5,   -3,   -6,
          -7,  -12,   -9,   -3,    5,   -5,   -6,  -21,
        // KING
         -56,  -39,  -29,  -20,  -19,  -22,  -31,  -47,
         -27,   -4,    1,   11,    9,   15,    4,  -18,
         -14,    9,   24,   21,   25,   46,   33,  -11,
         -28,   14,   29,   34,   31,   39,   22,  -15,
         -36,  -10,   22,   28,   31,   23,    2,  -32,
         -37,  -13,    5,   16,   20,   13,   -2,  -31,
         -49,  -32,   -8,    3,    6,   -5,  -26,  -50,
         -79,  -61,  -45,  -32,  -50,  -29,  -65,  -91,
    },
};

// Mobility: centipawns per attacked square, indexed KNIGHT, BISHOP, ROOK, QUEEN.
constexpr std::array<int, 4> MG_MOBILITY = {
      10,   11,    6,    1,
};
constexpr std::array<int, 4> EG_MOBILITY = {
       6,    6,   12,   25,
};

// Outpost bonus, indexed KNIGHT, BISHOP.
constexpr std::array<int, 2> MG_OUTPOST = {
       6,    3,
};
constexpr std::array<int, 2> EG_OUTPOST = {
       5,    4,
};

// Pawn structure (symmetric penalties, added to both phases separately).
constexpr int MG_ISOLATED = 15;
constexpr int EG_ISOLATED = 9;
constexpr int MG_DOUBLED = 8;
constexpr int EG_DOUBLED = 13;

// Passed pawn bonus, indexed by relative rank bucket 0..7.
constexpr std::array<int, 8> MG_PASSED = {
       0,    4,    4,   11,   29,   64,  104,    0,
};
constexpr std::array<int, 8> EG_PASSED = {
       0,    2,    8,   29,   57,   99,  129,    0,
};

// Side-to-move bonus (added from the side to move's point of view).
constexpr int TEMPO = 14;

// Reverse futility pruning.
inline int RFP_DEPTH = 7;
inline int RFP_MARGIN = 80;

// Child-node futility pruning.
inline int FUTILITY_DEPTH = 1;
inline int FUTILITY_MARGIN = 120;

// Move-level futility pruning.
inline int MOVE_FUTILITY_DEPTH = 4;
inline int MOVE_FUTILITY_MARGIN = 90;

// SEE-based quiet pruning.
inline int SEE_QUIET_DEPTH = 6;

// Null move pruning.
inline int NMP_MIN_DEPTH = 3;
inline int NMP_BASE = 3;
inline int NMP_DEPTH_DIV = 4;
inline int NMP_EVAL_DIV = 200;
inline int NMP_VERIFY_DEPTH = 12;

} // namespace tuned
