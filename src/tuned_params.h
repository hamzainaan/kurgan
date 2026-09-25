#pragma once

#include <array>

namespace tuned
{

constexpr std::array<int, 6> MATERIAL = {
     176,  337,  348,  557,  909,    0,
};

constexpr std::array<std::array<int, 64>, 6> MG_PST = {
    {
           0,    0,    0,    0,    0,    0,    0,    0,
         227,  245,  208,  239,  202,  199,   42,   84,
         236,  248,  278,  268,  299,  355,  283,  235,
         225,  234,  231,  243,  259,  247,  244,  210,
         210,  210,  224,  221,  231,  218,  220,  199,
         201,  195,  205,  207,  221,  200,  206,  190,
         197,  203,  196,  209,  208,  235,  242,  216,
           0,    0,    0,    0,    0,    0,    0,    0,
        -212,  -72,  -89,  -48,    7, -115,  -53, -129,
         -52,  -55,   42,   11,   31,   54,  -26,  -21,
         -27,   11,   38,   50,   70,  116,   13,   38,
          18,   26,   54,   35,   53,   64,   37,   40,
           9,   21,   47,   38,   54,   48,   47,   26,
          -6,    9,   18,   30,   35,   27,   21,    5,
         -28,  -55,  -14,   -2,   -5,    3,  -13,    6,
        -100,  -37,  -58,  -37,  -17,  -19,  -30,  -48,
         -35,  -29, -105,  -83,  -55,  -75,   -3,  -11,
         -33,  -14,  -22,  -22,   -9,   57,  -11,  -46,
           4,   33,   32,   27,   34,   51,   24,   14,
          11,   37,   27,   60,   52,   45,   40,   16,
          15,   42,   52,   54,   60,   46,   37,   29,
          20,   49,   40,   38,   40,   42,   45,   35,
          29,   43,   42,   26,   29,   48,   56,   35,
          19,   24,   10,    5,   21,   16,   -3,   20,
          30,   40,   20,   50,   48,   26,   39,   34,
          14,   12,   44,   61,   64,  100,   25,   43,
           2,   46,   35,   51,   56,   83,  127,   41,
           6,   14,   31,   53,   36,   54,   39,   25,
         -22,  -16,   -9,    0,    8,   17,   41,    4,
         -30,   -4,  -12,   -2,   -4,    9,   28,   -3,
         -53,  -23,  -28,  -12,   -9,    3,   11,  -61,
         -10,   -6,    2,   11,    9,   15,   12,   -9,
         -24,  -25,   -4,  -19,   31,   25,   19,   47,
         -33,  -80,  -39,  -49,  -75,   18,  -28,   23,
         -36,  -31,  -15,  -27,    7,    5,    5,  -16,
         -43,  -17,  -29,  -28,   -9,    1,   10,    0,
          -2,  -10,   -1,   -6,    3,   16,    6,    9,
         -19,   12,    8,    0,    2,    8,   15,    9,
         -12,   10,   16,   14,   13,   34,   31,    8,
           8,   -1,    8,   15,   16,  -14,   -8,  -41,
         -49,  -18,   -6,  -36,  -47,  -43,  -22,  -24,
         -16,   14,  -10,    4,  -20,    4,  -18,  -44,
          -2,   34,   61,    3,   16,   62,   72,  -19,
         -11,   10,   31,  -21,  -16,   11,   21,  -82,
         -45,   34,   -4,  -57,  -58,  -30,  -25,  -98,
         -28,  -24,  -18,  -47,  -59,  -39,  -24,  -82,
         -21,  -15,  -28,  -98,  -71,  -70,  -24,  -14,
         -44,   17,  -10, -110,  -70, -100,  -13,   -6,
    },
};

constexpr std::array<std::array<int, 64>, 6> EG_PST = {
    {
           0,    0,    0,    0,    0,    0,    0,    0,
         108,   99,   98,   66,   84,   69,  126,  127,
          39,   35,   10,  -22,  -30,  -22,   12,   22,
          11,    3,   -7,  -23,  -19,  -17,   -3,   -2,
          -4,   -5,  -19,  -18,  -17,  -19,  -15,  -11,
         -15,  -17,  -18,  -12,  -11,  -12,  -24,  -21,
         -17,  -16,   -7,   -9,    8,   -9,  -26,  -37,
           0,    0,    0,    0,    0,    0,    0,    0,
         -84,  -36,   17,   -6,    6,    0,  -46, -102,
          -6,   15,    2,   56,   33,   17,    5,  -30,
          -5,   19,   56,   53,   42,   29,   20,  -18,
          15,   27,   54,   79,   72,   54,   35,   15,
          11,   29,   55,   66,   65,   61,   44,    5,
          -7,   27,   32,   54,   47,   30,   13,   -9,
         -24,   12,   17,   19,   24,    7,   -3,  -30,
         -33,  -42,   -6,    7,   -8,   -2,  -40,  -64,
           7,   16,   27,   34,   31,   32,   15,    4,
          20,   32,   38,   23,   36,   25,   35,   16,
          24,   19,   24,   21,   26,   30,   27,   25,
          16,   17,   28,   23,   34,   22,   25,   14,
           9,   14,   24,   26,   19,   27,    8,   10,
           8,    8,   19,   22,   24,   13,    9,   -5,
           4,  -14,   -4,    7,    2,   -9,  -15,  -31,
          -5,    6,  -10,    6,    6,    0,   12,  -11,
         121,  119,  129,  117,  117,  130,  124,  124,
          93,   99,   92,   92,   90,   82,  106,   92,
         109,   98,  105,   97,   97,   91,   74,   97,
         103,  103,  109,   97,  101,   98,   99,  104,
         109,  121,  114,  107,  100,  106,   94,   98,
          95,  105,   98,   90,   88,   81,   93,   82,
          72,   69,   78,   65,   60,   58,   54,   76,
          81,   94,   90,   81,   81,   86,   88,   74,
           4,   58,   31,   48,   43,   49,   37,   37,
          29,   60,   57,   74,  107,   50,   67,   28,
          20,   25,    4,   60,   55,   56,   62,   59,
          41,   36,   31,   66,   51,   59,   79,   48,
         -23,   33,   10,   37,   27,   37,   37,   42,
          -8,  -33,    2,  -19,  -13,    8,   12,    6,
         -21,  -36,  -71,  -35,  -42,  -72,  -82,  -31,
         -37,  -61,  -52,  -44,  -36,  -34,  -29,  -26,
        -117,  -37,  -23,  -15,   -9,    1,    2,  -53,
         -30,   22,   27,   29,   24,   36,   33,   -9,
           6,   21,   22,   23,   15,   32,   28,   -8,
         -16,   15,   26,   31,   28,   29,    6,   -7,
         -32,  -12,   19,   37,   38,   20,    1,  -26,
         -40,   -9,    2,   19,   16,    8,  -18,  -25,
         -50,  -31,  -18,   -1,   -6,   -7,  -32,  -59,
         -99,  -75,  -54,  -46,  -74,  -33,  -63, -125,
    },
};

constexpr std::array<int, 4> MG_MOBILITY = {
      -1,    3,    3,    1,
};
constexpr std::array<int, 4> EG_MOBILITY = {
      -1,    5,    4,    5,
};

constexpr std::array<int, 2> MG_OUTPOST = {
      40,   48,
};
constexpr std::array<int, 2> EG_OUTPOST = {
      23,   18,
};

constexpr std::array<int, 11> MG_MINOR = {
      20,  -20,  -10,   -9,  -14,  -20,  -24,  -31,  -35,  -63,  -25,
};
constexpr std::array<int, 11> EG_MINOR = {
      77,   21,    8,   -8,  -13,  -14,  -16,  -15,  -34,  -48,   -9,
};
constexpr std::array<int, 8> MG_ROOK = {
      53,   32,    5,    9,   15,   20,   -1,   -2,
};
constexpr std::array<int, 8> EG_ROOK = {
       0,  -11,   30,   17,   19,   47,   24,  -54,
};

constexpr int MG_ISOLATED = 6;
constexpr int EG_ISOLATED = 11;
constexpr int MG_DOUBLED = 7;
constexpr int EG_DOUBLED = 15;

constexpr std::array<int, 8> MG_PASSED = {
       0,   18,   15,   17,   44,   75,  188,    0,
};
constexpr std::array<int, 8> EG_PASSED = {
       0,  -37,  -33,   -1,   36,  112,  132,    0,
};

constexpr std::array<int, 16> MG_PAWN_STRUCT = {
     -16,   28,   19,  -12,   31,   12,   -7,   -7,
       4,   -2,   -4,   17,   -4,    0,   14,  -16,
};
constexpr std::array<int, 16> EG_PAWN_STRUCT = {
      -1,    5,   -8,    1,   13,   19,  -26,   15,
     -12,    3,    3,    5,    4,   -6,  -41,   37,
};

constexpr std::array<int, 22> MG_THREATS = {
      -6,   -5,   29,   74,   35,  -10,   20,    6,   60,   39,  -16,
      -3,    1,    6,   41,  -59,   69,   15,    1,   23,   21,   32,
};
constexpr std::array<int, 22> EG_THREATS = {
      12,    3,   42,   15,   -1,    0,   52,   21,   31,   38,   19,
      24,   32,   10,   17,   73,   48,   20,    9,  -12,   20,    2,
};

constexpr std::array<std::array<int, 5>, 5> MG_IMBALANCE = {
    {
        {   0,    0,    0,    0,    0},
        {  28,    0,    0,    0,    0},
        {  23,    7,    0,    0,    0},
        {  26,  -20,  -30,    0,    0},
        { 121,  -19,  -10, -105,    0},
    },
};
constexpr std::array<std::array<int, 5>, 5> EG_IMBALANCE = {
    {
        {   0,    0,    0,    0,    0},
        {  40,    0,    0,    0,    0},
        {  39,  -14,    0,    0,    0},
        {  61,   -9,   -6,    0,    0},
        { 110,   65,   75,  239,    0},
    },
};

constexpr int TEMPO = 20;

// --- KING SAFETY BLOCK (preserved verbatim by kurgan_tune.py) ---
constexpr std::array<std::array<int, 8>, 4> PAWN_SHIELD_VALUE = {
    {
        -23,  28,  38,  17,   15,   22, -6,   0,
        -22,  36,  45,  3,  -3,  12, -9,   0,
        -16,  45,   25,  9,  12,  1,  -4,   0,
         -8,  26,   17,   11,  7,  3,  -8,   0,
    },
};
constexpr std::array<std::array<std::array<int, 8>, 4>, 3> PAWN_STORM_VALUE = {
    {
         21,  -10,  -20,  18,  18,  3,   0,   0,
        26,  -27,  -3,  30,  29,  13,   11,   0,
        18,  -8,  -13,  17,  35,  17,   -10,   0,
         12,  -11,  -6,  12,  19,  -5,   0,   0,
         6,   3,   22,   -10,   0,  -1,   -1,   0,
        10,   5,   17,  16,  15,  13,   -5,   0,
        14,   7,  49,  9,  19,  33,   8,   0,
         8,   4,   20,   31,  15,  8,   -11,   0,
         6,  12,  18,  22,  17,  14,   -10,   0,
        10,  9,  19,  20,  22,  10,   -5,   0,
        14,  19,  19,  28,  21,  22,   -20,   0,
         8,  9,  22,  38,  11,  -10,   1,   0,
    },
};
constexpr int PAWN_STORM_SHIELDING_KING = -148;
constexpr std::array<int, 3> CASTLING_RIGHTS_VALUE = {0, 25, 61};
constexpr int KS_BASE = -18;
constexpr std::array<int, 4> KING_THREAT_MULTIPLIER = {7, 4, 5, 5};
constexpr std::array<int, 4> KING_THREAT_SQUARE = {8, 13, 11, 13};
constexpr int KING_DEFENSELESS_SQUARE = 22;
constexpr int KS_PAWN_FACTOR = 10;
constexpr int KING_PRESSURE = 2;
constexpr int KS_KING_PRESSURE_FACTOR = 10;
constexpr std::array<int, 4> SAFE_CHECK_BONUS = {78, 27, 47, 51};
constexpr int KS_NO_KNIGHT_DEFENDER = 15;
constexpr int KS_NO_BISHOP_DEFENDER = 15;
constexpr int KS_BISHOP_PRESSURE = 8;
constexpr int KS_NO_QUEEN = -40;
constexpr int KS_ARRAY_FACTOR = 128;
constexpr int KS_MAX = 600;
constexpr int KS_ATTACK_SCALE = 126;
constexpr int KS_SCALE = 69;
// --- END KING SAFETY BLOCK ---

inline int RFP_DEPTH = 7;
inline int RFP_MARGIN = 80;
inline int FUTILITY_DEPTH = 0;
inline int FUTILITY_MARGIN = 120;
inline int MOVE_FUTILITY_DEPTH = 4;
inline int MOVE_FUTILITY_MARGIN = 90;
inline int SEE_QUIET_DEPTH = 6;
inline int NMP_MIN_DEPTH = 3;
inline int NMP_BASE = 3;
inline int NMP_DEPTH_DIV = 4;
inline int NMP_EVAL_DIV = 200;
inline int NMP_VERIFY_DEPTH = 12;
inline int RAZOR_DEPTH = 3;
inline int RAZOR_MARGIN = 200;
inline int PROBCUT_DEPTH = 9;
inline int PROBCUT_MARGIN = 80;
inline int IID_DEPTH = 5;
inline int LMP_DEPTH = 10;
inline int MOVE_BUDGET = 800000;

} // namespace tuned
